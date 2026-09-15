/**
 * @file ui_manual.c
 * @brief Clock Settings screen — choose DATE or TIME entry point.
 *
 * Navigation:
 *   Touch click DATE button   → Set Date (MOVE_LEFT)
 *   Touch click TIME button   → Set Time (MOVE_RIGHT)
 *   Joystick Left / Right     → shift focus between DATE and TIME (launcher)
 *   A button (physical)       → enter focused item (launcher → ui_manual_enter_focused)
 *   B button (physical)       → back to Clock (launcher)
 *   Swipe / gesture           → (none on this screen; A/B handle exit)
 *
 * Focus state resets to DATE (0) each time the screen loads.
 */
#include "../ui.h"

lv_obj_t * ui_manual;
lv_obj_t * ui_date_time_bg;
lv_obj_t * ui_date_time_header_bg;
lv_obj_t * ui_date_time_key_prompts_bg;
lv_obj_t * ui_date_time_key_a_bg;
lv_obj_t * ui_date_time_key_b_bg;
lv_obj_t * ui_date_button;
lv_obj_t * ui_time_button;
static lv_obj_t * ui_date_focus_frame = NULL;
static lv_obj_t * ui_time_focus_frame = NULL;
lv_obj_t * ui_date_time_header_label;
lv_obj_t * ui_date_time_key_a_enter;
lv_obj_t * ui_date_time_key_b_back;
lv_obj_t * ui_manual_date;
lv_obj_t * ui_manual_time;

/* ── Focus state (0 = DATE, 1 = TIME) ──────────────── */
static int s_focus = 0;

static void apply_focus(void)
{
    if (!ui_date_focus_frame || !ui_time_focus_frame) return;
    /* 半透明填充 + 独立 frame overlay 显示/隐藏 */
    lv_obj_set_style_bg_opa(ui_date_button, s_focus == 0 ? 60 : 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    if (s_focus == 0) lv_obj_clear_flag(ui_date_focus_frame, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_add_flag  (ui_date_focus_frame, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_opa(ui_time_button, s_focus == 1 ? 60 : 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    if (s_focus == 1) lv_obj_clear_flag(ui_time_focus_frame, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_add_flag  (ui_time_focus_frame, LV_OBJ_FLAG_HIDDEN);
}

void ui_manual_set_focus(int idx)
{
    s_focus = (idx > 0) ? 1 : 0;
    apply_focus();
}

void ui_manual_enter_focused(void)
{
    if (s_focus == 0)
        _ui_screen_change(&ui_date_picker, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_date_picker_screen_init);
    else
        _ui_screen_change(&ui_time_picker, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_time_picker_screen_init);
}

/* ── Button click handlers ─────────────────────────── */
static void on_date_click(lv_event_t * e)
{
    (void)e;
    s_focus = 0;
    apply_focus();
    _ui_screen_change(&ui_date_picker, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_date_picker_screen_init);
}

static void on_time_click(lv_event_t * e)
{
    (void)e;
    s_focus = 1;
    apply_focus();
    _ui_screen_change(&ui_time_picker, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_time_picker_screen_init);
}

/* Reset focus to DATE and apply highlight on every screen load */
static void on_screen_loaded(lv_event_t * e)
{
    (void)e;
    s_focus = 0;
    apply_focus();
}

/* ── Screen init ────────────────────────────────────── */
void ui_manual_screen_init(void)
{
    ui_manual = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_manual, LV_OBJ_FLAG_SCROLLABLE);

    ui_date_time_bg = lv_img_create(ui_manual);
    lv_img_set_src(ui_date_time_bg, &ui_img_background_v_png);
    lv_obj_set_width(ui_date_time_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_bg, 0);
    lv_obj_set_y(ui_date_time_bg, -1);
    lv_obj_set_align(ui_date_time_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_date_time_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_date_time_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui_date_time_header_bg = lv_img_create(ui_manual);
    lv_img_set_src(ui_date_time_header_bg, &ui_img_header_png);
    lv_obj_set_width(ui_date_time_header_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_header_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_header_bg, 10);
    lv_obj_set_y(ui_date_time_header_bg, 3);
    lv_obj_add_flag(ui_date_time_header_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_date_time_header_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui_date_time_key_prompts_bg = lv_img_create(ui_manual);
    lv_img_set_src(ui_date_time_key_prompts_bg, &ui_img_key_prompts_bg_png);
    lv_obj_set_width(ui_date_time_key_prompts_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_key_prompts_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_key_prompts_bg, 168);
    lv_obj_set_y(ui_date_time_key_prompts_bg, 204);
    lv_obj_add_flag(ui_date_time_key_prompts_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_date_time_key_prompts_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui_date_time_key_a_bg = lv_img_create(ui_manual);
    lv_img_set_src(ui_date_time_key_a_bg, &ui_img_key_a_png);
    lv_obj_set_width(ui_date_time_key_a_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_key_a_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_key_a_bg, 171);
    lv_obj_set_y(ui_date_time_key_a_bg, 207);
    lv_obj_add_flag(ui_date_time_key_a_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_date_time_key_a_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui_date_time_key_b_bg = lv_img_create(ui_manual);
    lv_img_set_src(ui_date_time_key_b_bg, &ui_img_key_b_png);
    lv_obj_set_width(ui_date_time_key_b_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_key_b_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_key_b_bg, 241);
    lv_obj_set_y(ui_date_time_key_b_bg, 207);
    lv_obj_add_flag(ui_date_time_key_b_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_date_time_key_b_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* DATE button (left half, x=31) */
    ui_date_button = lv_btn_create(ui_manual);
    lv_obj_set_width(ui_date_button, 109);
    lv_obj_set_height(ui_date_button, 103);
    lv_obj_set_x(ui_date_button, 31);
    lv_obj_set_y(ui_date_button, 63);
    lv_obj_add_flag(ui_date_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_date_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_date_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_date_button, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_button, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_date_button, &ui_img_date_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_date_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_date_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_date_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(ui_date_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ui_date_button, on_date_click, LV_EVENT_CLICKED, NULL);

    /* TIME button (right half, x=181) */
    ui_time_button = lv_btn_create(ui_manual);
    lv_obj_set_width(ui_time_button, 109);
    lv_obj_set_height(ui_time_button, 103);
    lv_obj_set_x(ui_time_button, 181);
    lv_obj_set_y(ui_time_button, 63);
    lv_obj_add_flag(ui_time_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_time_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_time_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_time_button, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT); /* unfocused */
    lv_obj_set_style_bg_img_src(ui_time_button, &ui_img_time_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_time_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_time_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ui_time_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(ui_time_button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ui_time_button, on_time_click, LV_EVENT_CLICKED, NULL);

    /* Focus frame overlays — 109×103 / radius 16 / border 5px (#BBE700)
     * remove_style_all 必须在 set_size/set_pos 之前调用：LVGL v8 的
     * lv_obj_set_size/pos 写入本地样式，若先设尺寸/位置再 remove_style_all
     * 会把这些值一起清掉，导致焦点框错位到 (0,0)。                        */
    ui_date_focus_frame = lv_obj_create(ui_manual);
    lv_obj_remove_style_all(ui_date_focus_frame);
    lv_obj_set_size(ui_date_focus_frame, 109, 103);
    lv_obj_set_pos(ui_date_focus_frame, 31, 63);
    lv_obj_set_style_radius(ui_date_focus_frame, 16, 0);
    lv_obj_set_style_border_color(ui_date_focus_frame, lv_color_hex(0xFF2A3D), 0);
    lv_obj_set_style_border_width(ui_date_focus_frame, 5, 0);
    lv_obj_set_style_border_opa(ui_date_focus_frame, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_date_focus_frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ui_time_focus_frame = lv_obj_create(ui_manual);
    lv_obj_remove_style_all(ui_time_focus_frame);
    lv_obj_set_size(ui_time_focus_frame, 109, 103);
    lv_obj_set_pos(ui_time_focus_frame, 181, 63);
    lv_obj_set_style_radius(ui_time_focus_frame, 16, 0);
    lv_obj_set_style_border_color(ui_time_focus_frame, lv_color_hex(0xFF2A3D), 0);
    lv_obj_set_style_border_width(ui_time_focus_frame, 5, 0);
    lv_obj_set_style_border_opa(ui_time_focus_frame, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_time_focus_frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui_time_focus_frame, LV_OBJ_FLAG_HIDDEN);

    /* Header title */
    ui_date_time_header_label = lv_label_create(ui_manual);
    lv_obj_set_width(ui_date_time_header_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_header_label, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_header_label, 126);
    lv_obj_set_y(ui_date_time_header_label, 14);
    lv_label_set_text(ui_date_time_header_label, "Manual");
    lv_obj_set_style_text_color(ui_date_time_header_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_time_header_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_time_header_label, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Key prompt labels: A=Enter  B=Back */
    ui_date_time_key_a_enter = lv_label_create(ui_manual);
    lv_obj_set_width(ui_date_time_key_a_enter, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_key_a_enter, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_key_a_enter, 195);
    lv_obj_set_y(ui_date_time_key_a_enter, 210);
    lv_label_set_text(ui_date_time_key_a_enter, "Enter");
    lv_obj_set_style_text_color(ui_date_time_key_a_enter, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_time_key_a_enter, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_time_key_a_enter, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_date_time_key_b_back = lv_label_create(ui_manual);
    lv_obj_set_width(ui_date_time_key_b_back, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date_time_key_b_back, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date_time_key_b_back, 267);
    lv_obj_set_y(ui_date_time_key_b_back, 210);
    lv_label_set_text(ui_date_time_key_b_back, "Back");
    lv_obj_set_style_text_color(ui_date_time_key_b_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_time_key_b_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_time_key_b_back, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* "DATE" and "TIME" labels below each button */
    ui_manual_date = lv_label_create(ui_manual);
    lv_obj_set_width(ui_manual_date, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_manual_date, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_manual_date, 62);
    lv_obj_set_y(ui_manual_date, 168);
    lv_label_set_text(ui_manual_date, "DATE");
    lv_obj_set_style_text_color(ui_manual_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_manual_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_manual_date, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_manual_time = lv_label_create(ui_manual);
    lv_obj_set_width(ui_manual_time, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_manual_time, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_manual_time, 214);
    lv_obj_set_y(ui_manual_time, 168);
    lv_label_set_text(ui_manual_time, "TIME");
    lv_obj_set_style_text_color(ui_manual_time, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_manual_time, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_manual_time, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Reset focus highlight each time screen becomes visible */
    lv_obj_add_event_cb(ui_manual, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);
}

void ui_manual_screen_destroy(void)
{
    if (ui_manual) lv_obj_del(ui_manual);
    ui_manual                    = NULL;
    ui_date_time_bg              = NULL;
    ui_date_time_header_bg       = NULL;
    ui_date_time_key_prompts_bg  = NULL;
    ui_date_time_key_a_bg        = NULL;
    ui_date_time_key_b_bg        = NULL;
    ui_date_button               = NULL;
    ui_time_button               = NULL;
    ui_date_focus_frame          = NULL;
    ui_time_focus_frame          = NULL;
    ui_date_time_header_label    = NULL;
    ui_date_time_key_a_enter     = NULL;
    ui_date_time_key_b_back      = NULL;
    ui_manual_date               = NULL;
    ui_manual_time               = NULL;
}
