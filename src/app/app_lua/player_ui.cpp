#include "player_ui.h"
#include "player_fonts.h"
#include <cstdio>
#include <cstring>

namespace meow::luaapps {
namespace {
constexpr uint32_t Background = 0x10151F, Card = 0x242F40, Ink = 0xF3F6FB;
constexpr uint32_t Muted = 0xA7B6CA, Accent = 0xB8EC83;
lv_font_t extendedFont(const lv_font_t& base, const lv_font_t& extra) {
    lv_font_t font = base;
    font.fallback = &extra;
    // Preserve all ascenders/descenders when Latin-1 needs more room.
    const unsigned ascent = base.line_height - base.base_line > extra.line_height - extra.base_line
        ? base.line_height - base.base_line : extra.line_height - extra.base_line;
    font.base_line = base.base_line > extra.base_line ? base.base_line : extra.base_line;
    font.line_height = ascent + font.base_line;
    return font;
}
const lv_font_t Font12 = extendedFont(lv_font_montserrat_12, player_font_12);
const lv_font_t Font18 = extendedFont(lv_font_montserrat_18, player_font_18);
const lv_font_t Font20 = extendedFont(lv_font_montserrat_20, player_font_20);
void plain(lv_obj_t* object, uint32_t color) {
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
}
void text(lv_obj_t* label, const char* value) {
    if (std::strcmp(lv_label_get_text(label), value)) lv_label_set_text(label, value);
}
lv_obj_t* label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font, uint32_t color) {
    auto* result = lv_label_create(parent);
    lv_label_set_text(result, "");
    lv_obj_set_pos(result, x, y);
    lv_obj_set_width(result, width);
    lv_obj_set_style_text_font(result, font, 0);
    lv_obj_set_style_text_color(result, lv_color_hex(color), 0);
    lv_label_set_long_mode(result, LV_LABEL_LONG_DOT);
    return result;
}
void clockText(char* target, size_t capacity, uint32_t seconds) {
    std::snprintf(target, capacity, "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
}
}

void PlayerUi::createButton(Button& button, lv_obj_t* parent) {
    button.owner = this;
    button.object = lv_btn_create(parent);
    plain(button.object, Card);
    lv_obj_set_style_radius(button.object, 12, 0);
    lv_obj_set_style_bg_color(button.object, lv_color_hex(0x40516A), LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(button.object, lv_color_hex(Accent), 0);
    lv_obj_set_style_outline_pad(button.object, 1, 0);
    lv_obj_add_event_cb(button.object, event, LV_EVENT_ALL, &button);
    button.label = label(button.object, 0, 0, 130, &Font18, Ink);
    lv_obj_set_style_text_align(button.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(button.label, lv_pct(94));
    lv_obj_center(button.label);
}

void PlayerUi::create(lv_obj_t* parent, void* context, Action action, uint16_t* coverPixels) {
    context_ = context;
    action_ = action;
    container_ = lv_obj_create(parent);
    plain(container_, Background);
    lv_obj_set_pos(container_, 0, 0);
    lv_obj_set_size(container_, 320, 240);
    player_ = lv_obj_create(container_);
    plain(player_, Background);
    lv_obj_set_size(player_, 320, 240);
    auto* artwork = lv_obj_create(player_);
    plain(artwork, 0x283A40);
    lv_obj_set_pos(artwork, 12, 12);
    lv_obj_set_size(artwork, 80, 80);
    lv_obj_set_style_radius(artwork, 12, 0);
    lv_obj_set_style_clip_corner(artwork, true, 0);
    placeholder_ = lv_obj_create(artwork);
    plain(placeholder_, 0x283A40);
    lv_obj_set_size(placeholder_, 80, 80);
    auto* disc = lv_obj_create(placeholder_);
    plain(disc, 0x152124);
    lv_obj_set_pos(disc, 10, 10);
    lv_obj_set_size(disc, 60, 60);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x526F61), 0);
    auto* note = label(disc, 0, 0, 50, &lv_font_montserrat_24, Accent);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(note);
    image_ = lv_img_create(artwork);
    imageDescriptor_.header.cf = LV_IMG_CF_TRUE_COLOR;
    imageDescriptor_.header.w = imageDescriptor_.header.h = 96;
    imageDescriptor_.data_size = 96 * 96 * 2;
    imageDescriptor_.data = reinterpret_cast<const uint8_t*>(coverPixels);
    if (coverPixels) lv_img_set_src(image_, &imageDescriptor_);
    lv_img_set_pivot(image_, 0, 0);
    lv_img_set_zoom(image_, 213); // 96 source pixels -> 80 display pixels.
    lv_obj_set_pos(image_, 0, 0);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    title_ = label(player_, 104, 12, 204, &Font20, Ink);
    lv_obj_set_height(title_, 50);
    subtitle_ = label(player_, 104, 65, 204, &Font12, Muted);
    status_ = label(player_, 104, 83, 204, &Font12, Accent);
    progress_ = lv_bar_create(player_);
    lv_obj_remove_style_all(progress_);
    lv_obj_set_pos(progress_, 12, 102);
    lv_obj_set_size(progress_, 296, 6);
    lv_obj_set_style_radius(progress_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(progress_, lv_color_hex(Card), 0);
    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress_, lv_color_hex(Accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_bar_set_range(progress_, 0, 1000);
    time_ = label(player_, 12, 113, 215, &Font12, Muted);
    volume_ = label(player_, 232, 113, 76, &Font12, Muted);
    lv_obj_set_style_text_align(volume_, LV_TEXT_ALIGN_RIGHT, 0);
    for (auto& button : playerButtons_) createButton(button, player_);
    const int x[] = {12, 96, 230, 12, 163};
    const int w[] = {78, 128, 78, 145, 145};
    const char* ids[] = {"previous", "play", "next", "library", "sound"};
    const char* labels[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT, "Library", "Sound"};
    for (unsigned i = 0; i < 5; ++i) {
        auto& button = playerButtons_[i];
        std::snprintf(button.id, sizeof(button.id), "%s", ids[i]);
        lv_obj_set_pos(button.object, x[i], i < 3 ? 132 : 192);
        lv_obj_set_size(button.object, w[i], i < 3 ? 54 : 44);
        text(button.label, labels[i]);
        if (i < 3) lv_obj_set_style_text_font(button.label, &lv_font_montserrat_24, 0);
        lv_obj_center(button.label);
    }
    lv_obj_set_style_bg_color(playerButtons_[1].object, lv_color_hex(Accent), 0);
    lv_obj_set_style_bg_color(playerButtons_[1].object, lv_color_hex(0x92C466), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(playerButtons_[1].label, lv_color_hex(0x142016), 0);
    menu_ = lv_obj_create(container_);
    plain(menu_, Background);
    lv_obj_set_size(menu_, 320, 240);
    menuTitle_ = label(menu_, 12, 8, 296, &Font20, Ink);
    menuBody_ = label(menu_, 12, 35, 296, &Font12, Muted);
    for (auto& button : menuButtons_) createButton(button, menu_);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

PlayerUi::Button* PlayerUi::activeButtons() { return kind_ == ViewKind::Player ? playerButtons_ : menuButtons_; }

void PlayerUi::show(const View& view) {
    if (!container_) return;
    const char* page = view.kind == ViewKind::Player ? view.player.title : view.title;
    bool newPage = !visible_ || kind_ != view.kind || std::strcmp(pageTitle_, page) ||
        (view.kind == ViewKind::Menu && (grid_ != view.compact || count_ != view.itemCount));
    if (view.kind == ViewKind::Menu)
        for (unsigned i = 0; i < view.itemCount && i < 4; ++i)
            if (std::strcmp(menuButtons_[i].id, view.items[i].id)) newPage = true;
    if (newPage) { ++epoch_; selected_ = view.kind == ViewKind::Player ? 1 : 0; }
    kind_ = view.kind;
    grid_ = view.compact;
    std::snprintf(pageTitle_, sizeof(pageTitle_), "%s", page);
    visible_ = true;
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    if (kind_ == ViewKind::Player) {
        count_ = 5;
        lv_obj_clear_flag(player_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
        text(title_, view.player.title);
        text(subtitle_, view.player.subtitle);
        text(status_, view.player.status);
        text(playerButtons_[1].label, view.player.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        char position[24], duration[24], value[64];
        clockText(position, sizeof(position), view.player.position);
        clockText(duration, sizeof(duration), view.player.duration);
        std::snprintf(value, sizeof(value), "%s  /  %s", position, view.player.duration ? duration : "--:--");
        text(time_, value);
        std::snprintf(value, sizeof(value), LV_SYMBOL_VOLUME_MAX " %u", view.player.volume);
        text(volume_, value);
        const uint32_t progress = view.player.duration ?
            static_cast<uint32_t>(static_cast<uint64_t>(view.player.position) * 1000 / view.player.duration) : 0;
        lv_bar_set_value(progress_, progress > 1000 ? 1000 : progress, LV_ANIM_OFF);
    } else {
        count_ = view.itemCount > 4 ? 4 : view.itemCount;
        lv_obj_add_flag(player_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(menu_, LV_OBJ_FLAG_HIDDEN);
        text(menuTitle_, view.title);
        text(menuBody_, view.body);
        for (unsigned i = 0; i < 4; ++i) {
            auto& button = menuButtons_[i];
            if (i >= count_) { lv_obj_add_flag(button.object, LV_OBJ_FLAG_HIDDEN); button.id[0] = 0; continue; }
            lv_obj_clear_flag(button.object, LV_OBJ_FLAG_HIDDEN);
            std::snprintf(button.id, sizeof(button.id), "%s", view.items[i].id);
            text(button.label, view.items[i].label);
            if (grid_) {
                lv_obj_set_pos(button.object, 12 + (i % 2) * 151, 59 + (i / 2) * 89);
                lv_obj_set_size(button.object, 145, 80);
                lv_label_set_long_mode(button.label, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(button.label, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_width(button.label, 129);
            } else {
                lv_obj_set_pos(button.object, 12, 53 + i * 46);
                lv_obj_set_size(button.object, 296, 44);
                lv_label_set_long_mode(button.label, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_align(button.label, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_set_width(button.label, 272);
            }
            lv_obj_center(button.label);
        }
    }
    highlight();
}

void PlayerUi::hide() {
    if (visible_) ++epoch_;
    visible_ = false;
    if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void PlayerUi::coverChanged(bool available) {
    if (!image_) return;
    lv_img_cache_invalidate_src(&imageDescriptor_);
    if (available && imageDescriptor_.data) {
        lv_img_set_src(image_, &imageDescriptor_);
        lv_obj_clear_flag(image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(image_);
    } else {
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PlayerUi::highlight() {
    if (!visible_) return;
    if (selected_ >= count_) selected_ = 0;
    auto* buttons = activeButtons();
    for (unsigned i = 0; i < count_; ++i)
        lv_obj_set_style_outline_width(buttons[i].object, i == selected_ ? 2 : 0, 0);
}
void PlayerUi::move(int delta) {
    if (!visible_ || !count_) return;
    selected_ = static_cast<unsigned>((static_cast<int>(selected_) + delta + count_) % count_);
    highlight();
}
void PlayerUi::activate() {
    if (visible_ && selected_ < count_ && action_)
        action_(context_, activeButtons()[selected_].id);
}
lv_obj_t* PlayerUi::button(unsigned index) const {
    if (index >= count_) return nullptr;
    return kind_ == ViewKind::Player ? playerButtons_[index].object : menuButtons_[index].object;
}
void PlayerUi::event(lv_event_t* event) {
    auto* button = static_cast<Button*>(lv_event_get_user_data(event));
    if (!button || !button->owner) return;
    auto& ui = *button->owner;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        button->pressed = ui.visible_;
        button->epoch = ui.epoch_;
        std::snprintf(button->pressedId, sizeof(button->pressedId), "%s", button->id);
    } else if (code == LV_EVENT_PRESS_LOST) button->pressed = false;
    else if (code == LV_EVENT_CLICKED) {
        const bool valid = button->pressed && ui.visible_ && button->epoch == ui.epoch_ &&
            !std::strcmp(button->pressedId, button->id);
        button->pressed = false;
        if (valid && button->id[0] && ui.action_) ui.action_(ui.context_, button->id);
    }
}
}
