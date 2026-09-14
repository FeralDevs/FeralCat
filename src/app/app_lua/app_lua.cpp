#include "app_lua.h"
#include "player_ui.h"
#include "../../system/usb_msc.h"
#include "../../system/media/audio_service.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <new>
#include <cstdio>
#include <cstring>

using namespace meow::luaapps;
namespace MOONCAKE::APPS {
struct AppLua::Session {
    enum class Mode { Manager, Running, Error } mode = Mode::Manager;
    struct Click { AppLua* owner; int index; uint32_t epoch = 0; bool pressed = false; } click[17]{};
    Runtime* runtime = nullptr;
    meow::media::AudioService* audio = nullptr;
    View view{};
    Manifest manifest{};
    char error[192]{};
    char pendingAction[32]{};
    PlayerUi playerUi;
    uint16_t* coverPixels = nullptr;
    uint32_t coverRevision = UINT32_MAX, coverAt = 0, viewEpoch = 1, faultAt = 0;
    bool modernReady = false, coverAvailable = false;
    bool dirty = true, scriptExit = false, resetScroll = false;
    bool backCaptured = false;
    int selected = 0, pending = -1, visible = 0;
    uint32_t lastTick = 0, revision = 0;
};

namespace {
void* psramResize(void*, void* ptr, size_t, size_t size) {
    if (!size) { heap_caps_free(ptr); return nullptr; }
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void styleText(lv_obj_t* object, uint32_t color) {
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_14, 0);
}
}

AppLua::AppLua(DEVICES* device) : device_(device) {
    setAppInfo().name = "App manager";
    usb_msc_set_before_enable([](void* context) {
        auto* app = static_cast<AppLua*>(context);
        // Join the audio worker and release every media file BEFORE unmounting.
        app->stopRuntime();
        app->exit_ = true; // Also safe if MSC activation fails after stopping Lua.
        app->catalog_.invalidate("USB storage active. Rescan after disconnecting.");
        ++app->revision_;
        app->requestExit();
    }, this);
}
uint32_t AppLua::uptimeMs() { return millis(); }
void AppLua::requestExit() { if (session_) session_->scriptExit = true; }
meow::media::Host* AppLua::audio() {
    return session_ && (session_->manifest.capabilities & CapAudio) ? session_->audio : nullptr;
}
void AppLua::captureBack(bool enabled) { if (session_) session_->backCaptured = enabled; }
void AppLua::show(const View& view) {
    if (!session_) return;
    const auto& previous = session_->view;
    bool changed = previous.kind != view.kind || strcmp(previous.title, view.title) || strcmp(previous.body, view.body) ||
                   previous.itemCount != view.itemCount || previous.compact != view.compact;
    if (view.kind == ViewKind::Player) {
        const auto& a = previous.player;
        const auto& b = view.player;
        changed = changed || strcmp(a.title, b.title) || strcmp(a.subtitle, b.subtitle) ||
            strcmp(a.status, b.status) || a.position != b.position || a.duration != b.duration ||
            a.volume != b.volume || a.playing != b.playing;
    }
    for (size_t i = 0; !changed && i < view.itemCount && i < 8; ++i)
        changed = strcmp(previous.items[i].id, view.items[i].id) || strcmp(previous.items[i].label, view.items[i].label);
    if (changed) {
        ++session_->viewEpoch;
        if (strcmp(previous.title, view.title) || previous.compact != view.compact) {
            session_->selected = 0;
            session_->resetScroll = true;
        }
        session_->view = view;
        session_->dirty = true;
    }
}
void AppLua::refreshCatalog() {
    catalog_.begin();
    if (!catalog_.scanning()) ++revision_;
}
void AppLua::scanStep() {
    if (!catalog_.scanning()) return;
    catalog_.step();
    if (!catalog_.scanning()) ++revision_;
}
void AppLua::selectPackage(const char* id) { snprintf(requestedId_, sizeof(requestedId_), "%s", id ? id : ""); }

void AppLua::createScreen() {
    previous_ = lv_scr_act();
    screen_ = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(0x10121A), 0);
    title_ = lv_label_create(screen_);
    styleText(title_, 0xBEE700);
    lv_obj_set_pos(title_, 10, 7);
    lv_obj_set_width(title_, 300);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    content_ = lv_obj_create(screen_);
    lv_obj_set_pos(content_, 0, 30);
    lv_obj_set_size(content_, 320, 185);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 8, 0);
    lv_obj_set_style_pad_row(content_, 7, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    body_ = lv_label_create(content_);
    styleText(body_, 0xC6CBD9);
    lv_obj_set_width(body_, lv_pct(100));
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    for (int i = 0; i < 17; ++i) {
        session_->click[i] = {this, i};
        buttons_[i] = lv_btn_create(content_);
        lv_obj_set_width(buttons_[i], lv_pct(100));
        lv_obj_set_height(buttons_[i], 44);
        lv_obj_set_style_bg_color(buttons_[i], lv_color_hex(0x263048), 0);
        lv_obj_set_style_shadow_width(buttons_[i], 0, 0);
        lv_obj_set_style_outline_color(buttons_[i], lv_color_hex(0xBEE700), 0);
        lv_obj_add_event_cb(buttons_[i], clicked, LV_EVENT_ALL, &session_->click[i]);
        labels_[i] = lv_label_create(buttons_[i]);
        styleText(labels_[i], 0xFFFFFF);
        lv_obj_set_width(labels_[i], lv_pct(100));
        lv_label_set_long_mode(labels_[i], LV_LABEL_LONG_DOT);
        lv_obj_center(labels_[i]);
        lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
    }
    footer_ = lv_label_create(screen_);
    styleText(footer_, 0x9CA6BD);
    lv_obj_set_pos(footer_, 8, 221);
    lv_obj_set_style_text_font(footer_, &lv_font_montserrat_12, 0);
    lv_disp_load_scr(screen_);
}

void AppLua::onOpen() {
    exit_ = false;
    direct_ = requestedId_[0] != '\0';
    void* memory = heap_caps_malloc(sizeof(Session), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) { exit_ = true; requestedId_[0] = 0; return; }
    session_ = new(memory) Session;
    session_->revision = revision_;
    createScreen();
    if (direct_) {
        bool found = false;
        for (size_t i = 0; i < catalog_.count(); ++i) {
            if (strcmp(catalog_.at(i).id, requestedId_) == 0) { launch(i); found = true; break; }
        }
        if (!found) fail("App is no longer available. Open App manager and select Rescan SD apps.");
    }
    requestedId_[0] = 0;
    render();
}

void AppLua::stopRuntime() {
    if (!session_) return;
    if (session_->runtime) {
        session_->runtime->stop();
        delete session_->runtime;
        session_->runtime = nullptr;
    }
    session_->backCaptured = false;
    if (session_->audio) {
        session_->audio->end();
        delete session_->audio;
        session_->audio = nullptr;
    }
}
void AppLua::fail(const char* message) {
    // Copy before deleting Runtime: message can point into its error buffer.
    snprintf(session_->error, sizeof(session_->error), "%s", message);
    const size_t luaBytes = session_->runtime ? session_->runtime->lastFaultMemoryBytes() : 0;
    stopRuntime();
    session_->pending = -1;
    session_->pendingAction[0] = 0;
    session_->faultAt = millis();
    ++session_->viewEpoch;
    if (session_->modernReady) session_->playerUi.hide();
    // Keep the last fault after leaving the app, so a subsequent quick tap can
    // never erase the only evidence. Audio has stopped before touching SD.
    char diagnostic[512];
    snprintf(diagnostic, sizeof(diagnostic),
        "app=%s uptime_ms=%lu lua_bytes=%u internal_free=%u largest_internal=%u\n%s\n",
        session_->manifest.id, static_cast<unsigned long>(millis()), unsigned(luaBytes),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), session_->error);
    Serial.printf("[Lua fault] %s", diagnostic);
    if (!usb_msc_is_active()) {
        auto log = SD_MMC.open("/lua-last-error.txt", FILE_WRITE);
        if (log) { log.print(diagnostic); log.close(); }
    }
    session_->mode = Session::Mode::Error;
    session_->selected = 0;
    session_->dirty = true;
}
void AppLua::launch(size_t index) {
    stopRuntime();
    char* source = nullptr;
    size_t length = 0;
    if (!catalog_.load(index, session_->manifest, source, length)) { fail(catalog_.status()); return; }
    if (session_->manifest.capabilities & CapAudio) {
        session_->audio = new(std::nothrow) meow::media::AudioService;
        if (!session_->audio || !session_->audio->begin(device_)) {
            heap_caps_free(source);
            fail("Audio could not start. Close other audio functions and try again.");
            return;
        }
    }
    Limits limits;
    limits.memoryBytes = session_->manifest.memoryBytes;
    // The wall-clock guard also includes scheduling/PSRAM/GC pauses. Keep the
    // 50k-instruction quota, but allow brief contention with the audio worker.
    if (session_->manifest.capabilities & CapAudio) limits.callbackMs = 80;
    Allocator allocator{nullptr, psramResize};
    session_->runtime = new(std::nothrow) Runtime(*this, limits, allocator);
    if (!session_->runtime) {
        heap_caps_free(source);
        fail("Not enough internal memory for the app host.");
        return;
    }
    session_->view = View{};
    snprintf(session_->view.title, sizeof(session_->view.title), "%s", session_->manifest.name);
    session_->scriptExit = false;
    session_->mode = Session::Mode::Running;
    session_->selected = 0;
    session_->pending = -1;
    session_->pendingAction[0] = 0;
    session_->lastTick = millis();
    const bool ok = session_->runtime->start(source, length);
    heap_caps_free(source);
    if (!ok) { fail(session_->runtime->error()); return; }
    session_->dirty = true;
}

void AppLua::clicked(lv_event_t* event) {
    auto* click = static_cast<Session::Click*>(lv_event_get_user_data(event));
    if (!click || !click->owner->session_) return;
    auto& session = *click->owner->session_;
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        click->epoch = session.viewEpoch;
        click->pressed = session.mode != Session::Mode::Error || millis() - session.faultAt >= 1000;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) click->pressed = false;
    else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        const bool valid = click->pressed && click->epoch == session.viewEpoch;
        click->pressed = false;
        if (!valid || session.pending >= 0 || session.pendingAction[0]) return;
        if (session.mode == Session::Mode::Running && click->index < int(session.view.itemCount))
            snprintf(session.pendingAction, sizeof(session.pendingAction), "%s", session.view.items[click->index].id);
        else session.pending = click->index;
    }
}
void AppLua::activate(int index) {
    if (index < 0 || index >= session_->visible) return;
    if (session_->mode == Session::Mode::Running) {
        if (!session_->runtime || session_->scriptExit) return;
        char id[32];
        snprintf(id, sizeof(id), "%s", session_->view.items[index].id);
        if (!session_->runtime->event("action", id)) fail(session_->runtime->error());
    } else if (session_->mode == Session::Mode::Error) {
        if (millis() - session_->faultAt < 1000) return;
        if (direct_) exit_ = true;
        else { session_->mode = Session::Mode::Manager; session_->dirty = true; }
    } else if (!catalog_.scanning()) {
        if (index < int(catalog_.count())) launch(size_t(index));
        else { refreshCatalog(); session_->selected = 0; session_->dirty = true; }
    }
}

void AppLua::render() {
    if (!session_ || !session_->dirty) return;
    session_->dirty = false;
    if (session_->mode == Session::Mode::Running && session_->view.kind != ViewKind::List) {
        if (!session_->modernReady) {
            session_->coverPixels = static_cast<uint16_t*>(heap_caps_malloc(96 * 96 * 2,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            session_->playerUi.create(screen_, this, [](void* context, const char* id) {
                auto* self = static_cast<AppLua*>(context);
                auto* session = self->session_;
                if (session && session->mode == Session::Mode::Running && !session->scriptExit &&
                    !session->pendingAction[0] && session->pending < 0)
                    snprintf(session->pendingAction, sizeof(session->pendingAction), "%s", id);
            }, session_->coverPixels);
            session_->modernReady = true;
        }
        lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        session_->playerUi.show(session_->view);
        session_->visible = int(session_->view.itemCount);
        return;
    }
    if (session_->modernReady) session_->playerUi.hide();
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(title_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(footer_, LV_OBJ_FLAG_HIDDEN);
    int count = 0;
    if (session_->mode == Session::Mode::Running) {
        lv_label_set_text(title_, session_->view.title);
        lv_label_set_text(body_, session_->view.body);
        count = int(session_->view.itemCount);
        for (int i = 0; i < count; ++i) lv_label_set_text(labels_[i], session_->view.items[i].label);
    } else if (session_->mode == Session::Mode::Error) {
        lv_label_set_text(title_, "App stopped");
        lv_label_set_text(body_, session_->error);
        count = 1;
        lv_label_set_text(labels_[0], "Dismiss and go back");
    } else {
        lv_label_set_text(title_, "App manager");
        lv_label_set_text(body_, catalog_.scanning() ? "Reading apps from SD ..." : catalog_.status());
        if (!catalog_.scanning()) {
            count = int(catalog_.count());
            for (int i = 0; i < count; ++i) lv_label_set_text(labels_[i], catalog_.at(i).name);
            lv_label_set_text(labels_[count++], "Rescan SD apps");
        }
    }
    session_->visible = count;
    const bool compact = session_->mode == Session::Mode::Running && session_->view.compact;
    lv_obj_set_flex_flow(content_, compact ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content_, compact ? 4 : 7, 0);
    lv_obj_set_style_pad_column(content_, 6, 0);
    if (session_->selected >= count) session_->selected = 0;
    for (int i = 0; i < 17; ++i) {
        if (i < count) lv_obj_clear_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_outline_width(buttons_[i], i == session_->selected ? 2 : 0, 0);
        lv_obj_set_width(buttons_[i], lv_pct(compact ? 48 : 100));
        lv_obj_set_height(buttons_[i], compact ? 30 : 44);
        lv_obj_set_style_text_font(labels_[i], compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
    }
    lv_label_set_text(footer_, "A: Select  B: Back  Hold B: Menu");
    if (session_->resetScroll) {
        lv_obj_scroll_to_y(content_, 0, LV_ANIM_OFF);
        session_->resetScroll = false;
    }
}

void AppLua::onRunning() {
    if (!session_ || exit_) return;
    if (session_->mode == Session::Mode::Manager) {
        scanStep();
        if (session_->revision != revision_) {
            session_->revision = revision_;
            session_->dirty = true;
        }
    }
    if (session_->mode == Session::Mode::Running && usb_msc_is_active())
        fail("USB storage activated. Restart the app after disconnecting it.");
    const bool backHandled = device_->button.B.released();
    if (backHandled) {
        if (session_->mode == Session::Mode::Error && millis() - session_->faultAt < 1000) return;
        if (session_->mode == Session::Mode::Running && session_->backCaptured) {
            if (!session_->runtime->event("key", "back")) fail(session_->runtime->error());
            session_->pending = -1;
            session_->pendingAction[0] = 0;
        } else {
            if (direct_ || session_->mode == Session::Mode::Manager) { exit_ = true; return; }
            stopRuntime();
            session_->mode = Session::Mode::Manager;
            session_->selected = 0;
            session_->pending = -1;
            session_->dirty = true;
        }
    }
    int movement = 0;
    if (!backHandled && device_->button.Up.pressed()) movement = -1;
    if (!backHandled && device_->button.Down.pressed()) movement = 1;
    if (movement && session_->visible) {
        if (session_->modernReady && session_->mode == Session::Mode::Running && session_->view.kind != ViewKind::List)
            session_->playerUi.move(movement);
        else {
            session_->selected = (session_->selected + movement + session_->visible) % session_->visible;
            session_->dirty = true;
            lv_obj_scroll_to_view(buttons_[session_->selected], LV_ANIM_OFF);
        }
    }
    // Back can change Lua's page before render updates the native controls.
    // Do not activate an action from that previous page in the same frame.
    if (!backHandled && device_->button.A.pressed() && session_->pending < 0) {
        if (session_->modernReady && session_->mode == Session::Mode::Running && session_->view.kind != ViewKind::List)
            session_->playerUi.activate();
        else session_->pending = session_->selected;
    }
    const int pending = session_->pending;
    session_->pending = -1;
    if (pending >= 0) activate(pending);
    if (session_->pendingAction[0]) {
        char action[32];
        snprintf(action, sizeof(action), "%s", session_->pendingAction);
        session_->pendingAction[0] = 0;
        if (session_->mode == Session::Mode::Running && session_->runtime && !session_->scriptExit &&
            !session_->runtime->event("action", action)) fail(session_->runtime->error());
    }
    if (session_->mode == Session::Mode::Running && !session_->scriptExit) {
        const char* key = nullptr;
        if (!backHandled && device_->button.Left.pressed()) key = "left";
        if (!backHandled && device_->button.Right.pressed()) key = "right";
        if (key && !session_->runtime->event("key", key)) fail(session_->runtime->error());
        const uint32_t now = millis();
        if (session_->mode == Session::Mode::Running && !session_->scriptExit &&
            uint32_t(now - session_->lastTick) >= 50) {
            const uint32_t elapsed = now - session_->lastTick;
            session_->lastTick = now;
            if (!session_->runtime->tick(elapsed > 250 ? 250 : elapsed)) fail(session_->runtime->error());
        }
    }
    if (session_->scriptExit && session_->mode == Session::Mode::Running) {
        stopRuntime();
        if (direct_) { exit_ = true; return; }
        session_->mode = Session::Mode::Manager;
        session_->dirty = true;
    }
    render();
    if (session_->modernReady && session_->coverPixels && session_->audio && millis() - session_->coverAt >= 200) {
        session_->coverAt = millis();
        const auto before = session_->coverRevision;
        if (session_->audio->copyCover(session_->coverRevision, session_->coverAvailable, session_->coverPixels, 96 * 96) &&
            before != session_->coverRevision) session_->playerUi.coverChanged(session_->coverAvailable);
    }
    lv_timer_handler(); // Lua never calls LVGL directly or re-enters event delivery.
}

void AppLua::onClose() {
    if (catalog_.scanning()) {
        catalog_.invalidate("Scan interrupted. Choose Rescan to read SD apps.");
        ++revision_;
    }
    catalog_.close();
    stopRuntime();
    // The active screen must be replaced BEFORE deleting it.
    if (screen_) {
        if (session_ && session_->modernReady) session_->playerUi.coverChanged(false);
        if (previous_ && lv_obj_is_valid(previous_)) lv_disp_load_scr(previous_);
        lv_obj_del(screen_);
    }
    screen_ = previous_ = title_ = content_ = body_ = footer_ = nullptr;
    memset(buttons_, 0, sizeof(buttons_));
    memset(labels_, 0, sizeof(labels_));
    if (session_) {
        heap_caps_free(session_->coverPixels);
        session_->~Session(); heap_caps_free(session_); session_ = nullptr;
    }
    requestedId_[0] = 0;
    exit_ = false;
}
} // namespace MOONCAKE::APPS
