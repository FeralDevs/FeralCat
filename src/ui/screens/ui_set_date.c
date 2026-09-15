/**
 * @file ui_set_date.c
 * @brief Set Date screen — three-column roller picker: M (Month) / D (Day) / Y (Year)
 *
 * Navigation:
 *   Enter  : from ui_manual clicking DATE button (FADE_ON) or A key when focused
 *   A save : write RTC → bottom-left hint → immediate FADE_ON to ui_manual
 *   B cancel: immediate FADE_ON to ui_manual (no Unsaved toast)
 *
 * Data flow:
 *   On LV_EVENT_SCREEN_LOADED → read RTC → pre-set rollers → store as clean state
 *   On save → read rollers → ui_rtc_bridge_set_date(Y, M, D) → update clean state
 */
#include "../ui.h"
#include "../ui_rtc_bridge.h"
#include <stdio.h>

/* Declare the picker-background image used by the original multi_column_picker */
LV_IMG_DECLARE(ui_img_788348331);

/* ── Screen objects ─────────────────────────────────── */
lv_obj_t * ui_date_picker       = NULL;
lv_obj_t * ui_date_Roller_M     = NULL;
lv_obj_t * ui_date_Roller_D     = NULL;
lv_obj_t * ui_date_Roller_Y     = NULL;

/* Private UI helpers */
static lv_obj_t * s_hdr_bg     = NULL;
static lv_obj_t * s_hdr_label  = NULL;
static lv_obj_t * s_kp_bg      = NULL;
static lv_obj_t * s_ka_bg      = NULL;
static lv_obj_t * s_kb_bg      = NULL;
static lv_obj_t * s_ka_lbl     = NULL;
static lv_obj_t * s_kb_lbl     = NULL;
static lv_obj_t * s_picker_bg  = NULL;

/* ── Dirty-tracking clean state ─────────────────────── */
static uint16_t s_init_M = 0, s_init_D = 0, s_init_Y = 2;
static bool     s_action_pending = false;  /* gate against double-trigger */

/* Small bottom-left save hint — visible briefly during FADE_ON transition */
static void make_save_hint(void)
{
    lv_obj_t * bg = lv_obj_create(ui_date_picker);
    lv_obj_set_size(bg, 110, 28);
    lv_obj_align(bg, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x1A2400), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bg, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bg, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(bg);
    lv_obj_set_align(lbl, LV_ALIGN_CENTER);
    lv_label_set_text(lbl, "Saved");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* ── Public actions ─────────────────────────────────── */
void ui_date_picker_action_save(void)
{
    if (s_action_pending) return;
    s_action_pending = true;

    uint16_t m_idx = lv_roller_get_selected(ui_date_Roller_M);         /* 0-11 */
    uint16_t d_raw = lv_roller_get_selected(ui_date_Roller_D);         /* 0-61 (2×31) */
    uint16_t y_idx = lv_roller_get_selected(ui_date_Roller_Y);         /* 0-11 */

    uint8_t  month = (uint8_t)(m_idx + 1);
    uint8_t  day   = (uint8_t)(d_raw % 31 + 1);                        /* wrap: 0-61→1-31 */
    uint16_t year  = (uint16_t)(2024 + y_idx);

    ui_rtc_bridge_set_date(year, month, day);
    s_init_M = m_idx; s_init_D = (uint16_t)(d_raw % 31); s_init_Y = y_idx;

    make_save_hint();  /* briefly visible during FADE_ON */
    _ui_screen_change(&ui_manual, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_manual_screen_init);
}

void ui_date_picker_action_cancel(void)
{
    if (s_action_pending) return;
    s_action_pending = true;
    _ui_screen_change(&ui_manual, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_manual_screen_init);
}

/* ── Screen-loaded: sync rollers to current RTC ──────── */
static void on_screen_loaded(lv_event_t * e)
{
    (void)e;
    uint16_t year; uint8_t month, day, weekday, hour, minute, second;
    if (!ui_rtc_bridge_get(&year, &month, &day, &weekday, &hour, &minute, &second)) return;

    uint16_t m_idx = (month >= 1 && month <= 12) ? (uint16_t)(month - 1) : 0;
    uint16_t d_idx = (day   >= 1 && day   <= 31) ? (uint16_t)(day   - 1) : 0;
    int y_raw = (int)year - 2024;
    uint16_t y_idx = (y_raw < 0) ? 0 : (y_raw > 11) ? 11 : (uint16_t)y_raw;

    lv_roller_set_selected(ui_date_Roller_M, m_idx, LV_ANIM_OFF);
    lv_roller_set_selected(ui_date_Roller_D, 31 + d_idx, LV_ANIM_OFF); /* start in 2nd copy */
    lv_roller_set_selected(ui_date_Roller_Y, y_idx, LV_ANIM_OFF);

    s_init_M = m_idx; s_init_D = d_idx; s_init_Y = y_idx;
    s_action_pending = false;
}

/* ── Screen init ────────────────────────────────────── */
void ui_date_picker_screen_init(void)
{
    ui_date_picker = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_date_picker, LV_OBJ_FLAG_SCROLLABLE);

    /* Background */
    lv_obj_t * bg = lv_img_create(ui_date_picker);
    lv_img_set_src(bg, &ui_img_background_v_png);
    lv_obj_set_width(bg, LV_SIZE_CONTENT);
    lv_obj_set_height(bg, LV_SIZE_CONTENT);
    lv_obj_set_x(bg, 0); lv_obj_set_y(bg, -1);
    lv_obj_set_align(bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Header bar */
    s_hdr_bg = lv_img_create(ui_date_picker);
    lv_img_set_src(s_hdr_bg, &ui_img_header_png);
    lv_obj_set_width(s_hdr_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(s_hdr_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(s_hdr_bg, 10); lv_obj_set_y(s_hdr_bg, 3);
    lv_obj_add_flag(s_hdr_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_hdr_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Key-prompts strip */
    s_kp_bg = lv_img_create(ui_date_picker);
    lv_img_set_src(s_kp_bg, &ui_img_key_prompts_bg_png);
    lv_obj_set_width(s_kp_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(s_kp_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(s_kp_bg, 168); lv_obj_set_y(s_kp_bg, 204);
    lv_obj_add_flag(s_kp_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_kp_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_ka_bg = lv_img_create(ui_date_picker);
    lv_img_set_src(s_ka_bg, &ui_img_key_a_png);
    lv_obj_set_x(s_ka_bg, 171); lv_obj_set_y(s_ka_bg, 207);
    lv_obj_add_flag(s_ka_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_ka_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_kb_bg = lv_img_create(ui_date_picker);
    lv_img_set_src(s_kb_bg, &ui_img_key_b_png);
    lv_obj_set_x(s_kb_bg, 241); lv_obj_set_y(s_kb_bg, 207);
    lv_obj_add_flag(s_kb_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_kb_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Header title */
    s_hdr_label = lv_label_create(ui_date_picker);
    lv_obj_set_x(s_hdr_label, 112); lv_obj_set_y(s_hdr_label, 14);
    lv_label_set_text(s_hdr_label, "Set Date");
    lv_obj_set_style_text_color(s_hdr_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_hdr_label, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Key labels */
    s_ka_lbl = lv_label_create(ui_date_picker);
    lv_obj_set_x(s_ka_lbl, 195); lv_obj_set_y(s_ka_lbl, 210);
    lv_label_set_text(s_ka_lbl, "Save");
    lv_obj_set_style_text_color(s_ka_lbl, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_ka_lbl, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_kb_lbl = lv_label_create(ui_date_picker);
    lv_obj_set_x(s_kb_lbl, 267); lv_obj_set_y(s_kb_lbl, 210);
    lv_label_set_text(s_kb_lbl, "Back");
    lv_obj_set_style_text_color(s_kb_lbl, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_kb_lbl, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Picker background panel */
    s_picker_bg = lv_img_create(ui_date_picker);
    lv_img_set_src(s_picker_bg, &ui_img_788348331);
    lv_obj_set_width(s_picker_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(s_picker_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(s_picker_bg, 10); lv_obj_set_y(s_picker_bg, 62);
    lv_obj_add_flag(s_picker_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_picker_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Month roller (M) */
    ui_date_Roller_M = lv_roller_create(ui_date_picker);
    lv_roller_set_options(ui_date_Roller_M,
        "JAN\nFEB\nMAR\nAPR\nMAY\nJUN\nJUL\nAUG\nSEP\nOCT\nNOV\nDEC",
        LV_ROLLER_MODE_INFINITE);
    lv_obj_set_height(ui_date_Roller_M, 120);
    lv_obj_set_width(ui_date_Roller_M, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_date_Roller_M, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_date_Roller_M, -100); lv_obj_set_y(ui_date_Roller_M, 6);
    lv_obj_set_style_text_color(ui_date_Roller_M, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_M, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_Roller_M, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_M, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_date_Roller_M, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_date_Roller_M, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_M, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_M, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Day roller (D) — written twice in NORMAL mode for seamless wrap without INFINITE bugs */
    ui_date_Roller_D = lv_roller_create(ui_date_picker);
    static const char kDayOpts[] =
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15"
        "\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31"
        "\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15"
        "\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31";
    lv_roller_set_options(ui_date_Roller_D, kDayOpts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_height(ui_date_Roller_D, 120);
    lv_obj_set_width(ui_date_Roller_D, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_date_Roller_D, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_date_Roller_D, 0); lv_obj_set_y(ui_date_Roller_D, 6);
    lv_obj_set_style_text_color(ui_date_Roller_D, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_D, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_Roller_D, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_D, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_date_Roller_D, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_date_Roller_D, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_D, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_D, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Year roller (Y) */
    ui_date_Roller_Y = lv_roller_create(ui_date_picker);
    lv_roller_set_options(ui_date_Roller_Y,
        "2024\n2025\n2026\n2027\n2028\n2029\n2030\n2031\n2032\n2033\n2034\n2035",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_date_Roller_Y, 2, LV_ANIM_OFF);  /* default 2026 */
    lv_obj_set_height(ui_date_Roller_Y, 120);
    lv_obj_set_width(ui_date_Roller_Y, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_date_Roller_Y, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_date_Roller_Y, 100); lv_obj_set_y(ui_date_Roller_Y, 6);
    lv_obj_set_style_text_color(ui_date_Roller_Y, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_Y, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date_Roller_Y, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_Y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_date_Roller_Y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_date_Roller_Y, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date_Roller_Y, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_date_Roller_Y, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Sync rollers to current RTC on each screen load */
    lv_obj_add_event_cb(ui_date_picker, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);
}

void ui_date_picker_screen_destroy(void)
{
    if (ui_date_picker) lv_obj_del(ui_date_picker);
    ui_date_picker   = NULL;
    ui_date_Roller_M = NULL;
    ui_date_Roller_D = NULL;
    ui_date_Roller_Y = NULL;
    s_hdr_bg   = NULL; s_hdr_label = NULL;
    s_kp_bg    = NULL; s_ka_bg     = NULL;
    s_kb_bg    = NULL; s_ka_lbl    = NULL;
    s_kb_lbl   = NULL; s_picker_bg = NULL;
}
