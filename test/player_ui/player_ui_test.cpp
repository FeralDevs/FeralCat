#include "player_ui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

static std::unordered_map<void*, size_t> allocations;
static size_t live = 0, peak = 0;
extern "C" void* ui_test_malloc(size_t size) {
    void* p = std::malloc(size);
    if (p) { allocations[p] = size; live += size; peak = (std::max)(peak, live); }
    return p;
}
extern "C" void ui_test_free(void* p) {
    if (p) { live -= allocations.at(p); allocations.erase(p); std::free(p); }
}
extern "C" void* ui_test_realloc(void* p, size_t size) {
    if (!size) { ui_test_free(p); return nullptr; }
    const size_t old = p ? allocations.at(p) : 0;
    void* next = std::realloc(p, size);
    if (next) { allocations.erase(p); allocations[next] = size; live = live - old + size; peak = (std::max)(peak, live); }
    return next;
}
using namespace meow::luaapps;
static unsigned checks = 0, actions = 0, unexpectedActions = 0;
static lv_color_t frame[320 * 240], draw[320 * 20];
static lv_point_t point{230, 94};
static bool pressed = false;
static char lastAction[32]{};
static void require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            if (x >= 0 && x < 320 && y >= 0 && y < 240)
                frame[y * 320 + x] = data[(y - area->y1) * (area->x2 - area->x1 + 1) + x - area->x1];
    lv_disp_flush_ready(driver);
}
static void pointer(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point = point;
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
static void pump() { lv_tick_inc(40); lv_timer_handler(); }
static void action(void*, const char* id) {
    ++actions;
    std::snprintf(lastAction, sizeof(lastAction), "%s", id);
    if (std::strcmp(id, "louder")) ++unexpectedActions;
}
static void image(const char* path) {
    lv_refr_now(nullptr);
    auto* f = std::fopen(path, "wb");
    require(f != nullptr, "preview file created");
    std::fprintf(f, "P6\n320 240\n255\n");
    for (auto c : frame) {
        lv_color32_t pixel; pixel.full = lv_color_to32(c);
        const unsigned char rgb[] = {pixel.ch.red, pixel.ch.green, pixel.ch.blue};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}
int main() {
    lv_init();
    lv_disp_draw_buf_t drawBuffer;
    lv_disp_draw_buf_init(&drawBuffer, draw, nullptr, 320 * 20);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240;
    display.draw_buf = &drawBuffer; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = pointer;
    lv_indev_drv_register(&input);
    auto* screen = lv_obj_create(nullptr);
    lv_scr_load(screen);
    PlayerUi ui;
    uint16_t cover[96 * 96]{};
    ui.create(screen, nullptr, action, cover);
    View v;
    v.kind = ViewKind::Menu; v.compact = true;
    std::strcpy(v.title, "Lautstärke");
    std::strcpy(v.body, "60 / 100");
    v.itemCount = 3;
    std::strcpy(v.items[0].id, "quieter"); std::strcpy(v.items[0].label, "Leiser −5");
    std::strcpy(v.items[1].id, "louder"); std::strcpy(v.items[1].label, "Lauter +5");
    std::strcpy(v.items[2].id, "back"); std::strcpy(v.items[2].label, "Zurück");
    ui.show(v); pump();
    lv_font_glyph_dsc_t glyph{};
    const auto* buttonFont = lv_obj_get_style_text_font(lv_obj_get_child(ui.button(0), 0), 0);
    require(lv_font_get_glyph_dsc(buttonFont, &glyph, 0x00fc, 0) && !glyph.is_placeholder,
            "German umlaut has a real rendered glyph");
    lv_area_t initial;
    lv_obj_get_coords(ui.button(1), &initial);
    require(initial.x1 == 163 && initial.y1 == 59 && initial.x2 == 307 && initial.y2 == 138, "large volume hit box has fixed geometry");
    for (unsigned i = 0; i < 500; ++i) {
        pressed = true; pump();
        std::snprintf(v.body, sizeof(v.body), "%u / 100", (i * 5) % 101);
        ui.show(v); pump(); // Status changes while the finger remains down.
        pressed = false; pump();
        lv_area_t area;
        lv_obj_get_coords(ui.button(1), &area);
        require(std::memcmp(&area, &initial, sizeof(area)) == 0, "status update cannot move volume controls");
        require(actions == i + 1 && unexpectedActions == 0 && !std::strcmp(lastAction, "louder"), "rapid volume tap has exactly its original action");
    }
    image("volume-preview.ppm");
    const size_t before = live;
    for (unsigned i = 0; i < 1000; ++i) { ui.show(v); pump(); }
    require(live == before, "repeated unchanged UI updates do not grow the LVGL heap");
    // A release from an old screen must not activate the new button at the
    // same coordinates (especially an exit confirmation or an error close).
    pressed = true; pump();
    std::strcpy(v.title, "App beenden?");
    std::strcpy(v.items[1].id, "exit");
    std::strcpy(v.items[1].label, "Beenden");
    ui.show(v); pump();
    pressed = false; pump();
    require(actions == 500 && unexpectedActions == 0, "release across page change cannot become Exit");
    pressed = true; pump(); ui.hide(); pressed = false; pump();
    require(actions == 500, "release across hidden UI cannot dismiss another view");
    View player;
    player.kind = ViewKind::Player;
    std::strcpy(player.player.title, "Body Electric");
    std::strcpy(player.player.subtitle, "Alle Titel · 1 / 12");
    std::strcpy(player.player.status, "Wiedergabe");
    player.player.playing = true;
    player.player.position = 41; player.player.duration = 3480; player.player.volume = 60;
    ui.show(player); pump();
    for (unsigned i = 0; i < 5; ++i) {
        require(lv_obj_get_height(ui.button(i)) >= 44, "every player touch target is at least 44 pixels high");
        lv_area_t a; lv_obj_get_coords(ui.button(i), &a);
        require(a.x1 >= 0 && a.y1 >= 0 && a.x2 < 320 && a.y2 < 240, "all player buttons fit on the physical screen");
    }
    image("player-preview.ppm");
    std::fill(std::begin(cover), std::end(cover), uint16_t(0xf800));
    ui.coverChanged(true); pump();
    require((lv_color_to32(frame[40 * 320 + 40]) & 0x00ffffff) == 0x00ff0000,
            "RGB565 cover uses correct colors and visible image geometry");
    ui.coverChanged(false); pump();
    require((lv_color_to32(frame[40 * 320 + 40]) & 0x00ffffff) != 0x00ff0000,
            "missing cover restores the placeholder");
    ui.coverChanged(false); ui.hide();
    lv_scr_load(lv_obj_create(nullptr));
    lv_obj_del(screen);
    std::printf("Player LVGL UI: %u checks passed, 500 rapid pointer taps, peak LVGL heap %zu bytes.\n", checks, peak);
}
