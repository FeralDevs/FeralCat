/**
 * @file ui_set_time.c
 * @brief Set Time screen — three-column roller picker: W (Weekday) / H (Hour) / M (Minute)
 *
 * Navigation:
 *   Enter  : from ui_manual clicking TIME button (FADE_ON) or A key when focused
 *   A save : write RTC → bottom-left hint → immediate FADE_ON to ui_manual
 *   B cancel: immediate FADE_ON to ui_manual (no Unsaved toast)
 *
 * Data flow:
 *   On LV_EVENT_SCREEN_LOADED → read RTC → pre-set rollers → store as clean state
 *   On save → ui_rtc_bridge_set_time(H, M, 0) + ui_rtc_bridge_set_weekday(W)
 */
#include "../ui.h"
#include "../ui_rtc_bridge.h"
#include <stdio.h>



/* ── Screen objects ─────────────────────────────────── */
lv_obj_t * ui_time_picker       = NULL;
lv_obj_t * ui_time_Roller_W     = NULL;
lv_obj_t * ui_time_Roller_H     = NULL;
lv_obj_t * ui_time_Roller_M     = NULL;

/* ── Dirty-tracking clean state ─────────────────────── */
static uint16_t s_init_W = 0, s_init_H = 0, s_init_Min = 0;
static bool     s_action_pending = false;

/* Small bottom-left save hint — visible briefly during FADE_ON transition */
static void make_save_hint(void)
{
    lv_obj_t * bg = lv_obj_create(ui_time_picker);
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
void ui_time_picker_action_save(void)
{
    if (s_action_pending) return;
    s_action_pending = true;

    uint8_t weekday = (uint8_t)lv_roller_get_selected(ui_time_Roller_W);           /* 0-6 */
    uint8_t hour    = (uint8_t)lv_roller_get_selected(ui_time_Roller_H);           /* 0-23 (INFINITE normalized) */
    uint8_t minute  = (uint8_t)(lv_roller_get_selected(ui_time_Roller_M) % 60);   /* wrap: 0-119→0-59 */

    ui_rtc_bridge_set_time(hour, minute, 0);
    ui_rtc_bridge_set_weekday(weekday);
    s_init_W = weekday; s_init_H = hour; s_init_Min = minute;

    make_save_hint();  /* briefly visible during FADE_ON */
    _ui_screen_change(&ui_manual, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_manual_screen_init);
}

void ui_time_picker_action_cancel(void)
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

    uint16_t w_idx = (weekday < 7) ? weekday : 0;
    uint16_t h_idx = (hour   < 24) ? hour    : 0;
    uint16_t m_idx = (minute < 60) ? minute  : 0;

    lv_roller_set_selected(ui_time_Roller_W, w_idx, LV_ANIM_OFF);
    lv_roller_set_selected(ui_time_Roller_H, h_idx, LV_ANIM_OFF);
    lv_roller_set_selected(ui_time_Roller_M, 60 + m_idx, LV_ANIM_OFF); /* start in 2nd copy */

    s_init_W = w_idx; s_init_H = h_idx; s_init_Min = m_idx;
    s_action_pending = false;
}

/* ── Screen init ────────────────────────────────────── */
void ui_time_picker_screen_init(void)
{
    ui_time_picker = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_time_picker, LV_OBJ_FLAG_SCROLLABLE);

    /* Background */
    lv_obj_t * bg = lv_img_create(ui_time_picker);
    lv_img_set_src(bg, &ui_img_background_v_png);
    lv_obj_set_width(bg, LV_SIZE_CONTENT);
    lv_obj_set_height(bg, LV_SIZE_CONTENT);
    lv_obj_set_x(bg, 0); lv_obj_set_y(bg, -1);
    lv_obj_set_align(bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Header bar */
    lv_obj_t * hdr = lv_img_create(ui_time_picker);
    lv_img_set_src(hdr, &ui_img_header_png);
    lv_obj_set_width(hdr, LV_SIZE_CONTENT);
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_x(hdr, 10); lv_obj_set_y(hdr, 3);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* Key-prompts strip */
    lv_obj_t * kp = lv_img_create(ui_time_picker);
    lv_img_set_src(kp, &ui_img_key_prompts_bg_png);
    lv_obj_set_x(kp, 168); lv_obj_set_y(kp, 204);
    lv_obj_add_flag(kp, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(kp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * ka = lv_img_create(ui_time_picker);
    lv_img_set_src(ka, &ui_img_key_a_png);
    lv_obj_set_x(ka, 171); lv_obj_set_y(ka, 207);
    lv_obj_add_flag(ka, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ka, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * kb = lv_img_create(ui_time_picker);
    lv_img_set_src(kb, &ui_img_key_b_png);
    lv_obj_set_x(kb, 241); lv_obj_set_y(kb, 207);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_SCROLLABLE);

    /* Header title */
    lv_obj_t * title = lv_label_create(ui_time_picker);
    lv_obj_set_x(title, 112); lv_obj_set_y(title, 14);
    lv_label_set_text(title, "Set Time");
    lv_obj_set_style_text_color(title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Key labels */
    lv_obj_t * lbl_a = lv_label_create(ui_time_picker);
    lv_obj_set_x(lbl_a, 195); lv_obj_set_y(lbl_a, 210);
    lv_label_set_text(lbl_a, "Save");
    lv_obj_set_style_text_color(lbl_a, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_a, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_b = lv_label_create(ui_time_picker);
    lv_obj_set_x(lbl_b, 267); lv_obj_set_y(lbl_b, 210);
    lv_label_set_text(lbl_b, "Back");
    lv_obj_set_style_text_color(lbl_b, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_b, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Picker background panel */
    lv_obj_t * pbg = lv_img_create(ui_time_picker);
    lv_img_set_src(pbg, &ui_img_picker_time_bg_png);
    lv_obj_set_width(pbg, LV_SIZE_CONTENT);
    lv_obj_set_height(pbg, LV_SIZE_CONTENT);
    lv_obj_set_x(pbg, 10); lv_obj_set_y(pbg, 62);
    lv_obj_add_flag(pbg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(pbg, LV_OBJ_FLAG_SCROLLABLE);

    /* Weekday roller (W) */
    ui_time_Roller_W = lv_roller_create(ui_time_picker);
    lv_roller_set_options(ui_time_Roller_W,
        "SUN\nMON\nTUE\nWED\nTHU\nFRI\nSAT",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_height(ui_time_Roller_W, 120);
    lv_obj_set_width(ui_time_Roller_W, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_time_Roller_W, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_time_Roller_W, -100); lv_obj_set_y(ui_time_Roller_W, 6);
    lv_obj_set_style_text_color(ui_time_Roller_W, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_W, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_time_Roller_W, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_W, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_time_Roller_W, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_time_Roller_W, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_W, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_W, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Hour roller (H) */
    ui_time_Roller_H = lv_roller_create(ui_time_picker);
    lv_roller_set_options(ui_time_Roller_H,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12"
        "\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_INFINITE);
    lv_obj_set_height(ui_time_Roller_H, 120);
    lv_obj_set_width(ui_time_Roller_H, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_time_Roller_H, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_time_Roller_H, 0); lv_obj_set_y(ui_time_Roller_H, 6);
    lv_obj_set_style_text_color(ui_time_Roller_H, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_H, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_time_Roller_H, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_H, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_time_Roller_H, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_time_Roller_H, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_H, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_H, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Minute roller (M) — written twice in NORMAL mode for seamless wrap without INFINITE bugs */
    ui_time_Roller_M = lv_roller_create(ui_time_picker);
    static const char kMinOpts[] =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19"
        "\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39"
        "\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59"
        "\n00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19"
        "\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39"
        "\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
    lv_roller_set_options(ui_time_Roller_M, kMinOpts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_height(ui_time_Roller_M, 120);
    lv_obj_set_width(ui_time_Roller_M, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_time_Roller_M, LV_ALIGN_CENTER);
    lv_obj_set_x(ui_time_Roller_M, 100); lv_obj_set_y(ui_time_Roller_M, 6);
    lv_obj_set_style_text_color(ui_time_Roller_M, lv_color_hex(0xFF2A3D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_M, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_time_Roller_M, &ui_font_name_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_M, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_time_Roller_M, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_time_Roller_M, lv_color_hex(0xFF2A3D), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time_Roller_M, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_time_Roller_M, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);

    /* Sync rollers to current RTC on each screen load */
    lv_obj_add_event_cb(ui_time_picker, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);
}

void ui_time_picker_screen_destroy(void)
{
    if (ui_time_picker) lv_obj_del(ui_time_picker);
    ui_time_picker   = NULL;
    ui_time_Roller_W = NULL;
    ui_time_Roller_H = NULL;
    ui_time_Roller_M = NULL;
}
