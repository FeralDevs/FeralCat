// ui_t9_keyboard.c — T9 keyboard (letter / number / symbol) for WiFi password entry
// Layout: 4-column × 3-row button matrix
//   Cols 1-3 : alphanumeric / symbol keys
//   Col  4   : Send (row1) · Shift/0/Page (row2) · Mode-switch (row3)
// Top: text input field + orange backspace key
// LVGL 8.3.11 / ESP32-S3

#include "../ui.h"
#include "../ui_wifi_bridge.h"

/* ── Screen-level object pointers ───────────────────────────── */
lv_obj_t * ui_t9_keyboard;
lv_obj_t * ui_t9_keyboard_bg;
lv_obj_t * ui_text_input;
lv_obj_t * ui_backspace__keybg;
lv_obj_t * ui_matrix_bg;
lv_obj_t * ui_matrix_1;
lv_obj_t * ui_matrix_2;
lv_obj_t * ui_matrix_3;
lv_obj_t * ui_matrix_4;
lv_obj_t * ui_matrix_5;
lv_obj_t * ui_matrix_6;
lv_obj_t * ui_matrix_7;
lv_obj_t * ui_matrix_8;
lv_obj_t * ui_matrix_9;
lv_obj_t * ui_control_1;
lv_obj_t * ui_control_2;
lv_obj_t * ui_control_3;
lv_obj_t * ui_capital;
lv_obj_t * ui_send;
lv_obj_t * ui_switch;
lv_obj_t * ui_input_textarea;

/* ── Keyboard state ─────────────────────────────────────────── */
typedef enum { MODE_LETTER, MODE_NUMBER, MODE_SYMBOL } kbd_mode_t;

static kbd_mode_t   s_mode;
static int          s_sym_page;
static int          s_last_key;
static int          s_tap_count;
static lv_timer_t * s_commit_timer;
static bool         s_upper;
static lv_obj_t   * s_key_labels[9];
static lv_obj_t   * s_lbl_ctrl2; /* "0" label on control_2 in number mode */
static lv_obj_t   * s_sym_icon;  /* ◄► label on control_2 in symbol mode */
static lv_obj_t   * s_space_bar; /* horizontal bar inside space key */

/* ── WiFi-mode state ────────────────────────────────────────── */
static char         s_wifi_ssid[33]   = "";
static char         s_prefill_buf[65] = "";
static bool         s_wifi_mode       = false;
static lv_obj_t   * s_overlay         = NULL;
static lv_timer_t * s_conn_timer      = NULL;
static lv_timer_t * s_nav_timer       = NULL;  /* tracked nav-back timer */
static uint32_t     s_conn_start_ms   = 0;
static lv_obj_t   * s_overlay_status  = NULL;  /* label inside overlay */
static lv_obj_t   * s_err_label       = NULL;  /* inline error below textarea */

static const uint32_t kConnTimeoutMs  = 15000;

/* ── T9 letter map ──────────────────────────────────────────── */
static const char * t9_lower[9][5] = {
    {"a","b","c",  NULL,NULL},   /* 0: abc  */
    {"d","e","f",  NULL,NULL},   /* 1: def  */
    {"g","h","i",  NULL,NULL},   /* 2: ghi  */
    {"j","k","l",  NULL,NULL},   /* 3: jkl  */
    {"m","n","o",  NULL,NULL},   /* 4: mno  */
    {"p","q","r","s",NULL},      /* 5: pqrs */
    {"t","u","v",  NULL,NULL},   /* 6: tuv  */
    {"w","x","y","z",NULL},      /* 7: wxyz */
    {" ", NULL,   NULL,NULL,NULL}/* 8: space*/
};

static const char * number_map[9]  = {"1","2","3","4","5","6","7","8","9"};

/* Symbol pages — all ASCII punctuation across 4 pages (◄► cycles them).
 * Empty "" slots on the last page are drawn blank and insert nothing. */
#define SYM_PAGES 4
static const char * sym_pages[SYM_PAGES][9] = {
    {".", "!", "@", "#", "$", "%", "&", "*", "?"},
    {"-", "_", "(", ")", "+", "=", "/", "\"", "'"},
    {";", ":", ",", "<", ">", "[", "]", "{", "}"},
    {"~", "^", "`", "\\", "|", "",  "",  "",  ""},
};

/* ═══════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════ */

/* ── Key-index from clicked object ─────────────────────────── */
static int _key_index(lv_obj_t * tgt)
{
    /* visual order: top-left→bottom-right across 3 columns */
    lv_obj_t * order[9] = {
        ui_matrix_4, ui_matrix_2, ui_matrix_3,
        ui_matrix_1, ui_matrix_5, ui_matrix_6,
        ui_matrix_7, ui_matrix_8, ui_matrix_9
    };
    for(int i = 0; i < 9; i++) if(order[i] == tgt) return i;
    return -1;
}

/* ── Refresh labels for current mode/page/case ─────────────── */
static void _refresh_labels(void)
{
    if(s_mode == MODE_LETTER) {
        static const char * lwr[9] = {"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"," "};
        static const char * upr[9] = {"ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ"," "};
        const char ** t = s_upper ? upr : lwr;
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], t[i]);
        /* Space key (index 8): white background */
        lv_obj_set_style_img_recolor(ui_matrix_9, lv_color_white(), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_COVER, 0);
        /* control_2: shift icon only, no text label */
        lv_img_set_src(ui_control_2, &ui_img_functional_control_keys_bg_png);
        if(s_lbl_ctrl2) lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        if(ui_capital)  lv_obj_clear_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_clear_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);

    } else if(s_mode == MODE_NUMBER) {
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], number_map[i]);
        /* Restore space-key to normal image (shows "9" in number mode) */
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_TRANSP, 0);
        /* control_2: alphanumeric bg (same as 1–9), "0" with black text */
        lv_img_set_src(ui_control_2, &ui_img_alphanumeric_keypad_bg_png);
        if(s_lbl_ctrl2) {
            lv_obj_set_style_text_color(s_lbl_ctrl2, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(s_lbl_ctrl2, &ui_font_name_24, 0);
            lv_obj_set_align(s_lbl_ctrl2, LV_ALIGN_CENTER);
            lv_obj_clear_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        }
        if(ui_capital)  lv_obj_add_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);

    } else { /* MODE_SYMBOL */
        const char ** sym = sym_pages[s_sym_page];
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], sym[i]);
        /* Restore space-key background */
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_TRANSP, 0);
        /* control_2: dark bg, switch icon for page toggle — no pg text */
        lv_img_set_src(ui_control_2, &ui_img_functional_control_keys_bg_png);
        if(s_lbl_ctrl2) lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        if(ui_capital)  lv_obj_add_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_clear_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── Clear inline error label ───────────────────────────────── */
static void _clear_err(void)
{
    if(s_err_label) { lv_obj_del(s_err_label); s_err_label = NULL; }
}

/* ── Show inline error below textarea ──────────────────────── */
static void _show_err(const char * msg)
{
    _clear_err();
    if(!ui_t9_keyboard) return;
    s_err_label = lv_label_create(ui_t9_keyboard);
    lv_label_set_text(s_err_label, msg);
    lv_obj_set_style_text_color(s_err_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(s_err_label, &ui_font_name_14, 0);
    lv_obj_set_pos(s_err_label, 16, 43);
    lv_obj_move_foreground(s_err_label);
}

/* ═══════════════════════════════════════════════════════════════
 *  Connecting overlay
 * ═══════════════════════════════════════════════════════════════ */

static void _overlay_hide(void)
{
    if(s_conn_timer)  { lv_timer_del(s_conn_timer);  s_conn_timer  = NULL; }
    if(s_overlay)     { lv_obj_del(s_overlay);        s_overlay     = NULL; }
    s_overlay_status = NULL;
}

static void _nav_back_to_wifi(lv_timer_t * t)
{
    (void)t;
    s_nav_timer = NULL;         /* already firing — prevent cleanup() double-del */
    ui_t9_keyboard_cleanup();   /* NULL all pointers before LVGL auto_del fires */
    _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, &ui_wifi_screen_init);
}

static void _conn_poll_cb(lv_timer_t * t)
{
    (void)t;
    wifi_bridge_status_t st  = ui_wifi_bridge_get_status();
    uint32_t elapsed = lv_tick_get() - s_conn_start_ms;

    if(st == WIFI_BRIDGE_CONNECTED) {
        if(s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
        if(s_overlay_status) lv_label_set_text(s_overlay_status, "Connected!");
        s_nav_timer = lv_timer_create(_nav_back_to_wifi, 800, NULL);
        lv_timer_set_repeat_count(s_nav_timer, 1);

    } else if(st == WIFI_BRIDGE_FAILED) {
        _overlay_hide();
        if(ui_input_textarea) lv_textarea_set_text(ui_input_textarea, "");
        _show_err(LV_SYMBOL_WARNING " Wrong password, try again");

    } else if(elapsed >= kConnTimeoutMs) {
        if(s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
        if(s_overlay_status) lv_label_set_text(s_overlay_status, "Timeout");
        s_nav_timer = lv_timer_create(_nav_back_to_wifi, 900, NULL);
        lv_timer_set_repeat_count(s_nav_timer, 1);

    } else {
        /* update countdown */
        if(s_overlay_status) {
            char buf[48];
            int rem = (int)((kConnTimeoutMs - elapsed) / 1000) + 1;
            lv_snprintf(buf, sizeof(buf), "Connecting... %ds", rem);
            lv_label_set_text(s_overlay_status, buf);
        }
    }
}

static void _overlay_show(void)
{
    _overlay_hide();
    if(!ui_t9_keyboard) return;

    /* ── Backdrop ──────────────────────────────────────────── */
    s_overlay = lv_obj_create(ui_t9_keyboard);
    lv_obj_set_size(s_overlay, 320, 240);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, 210, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_overlay);

    /* ── Card ──────────────────────────────────────────────── */
    lv_obj_t * card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, 240, 100);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(card, 255, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Spinner ───────────────────────────────────────────── */
    lv_obj_t * spin = lv_spinner_create(card, 1000, 60);
    lv_obj_set_size(spin, 36, 36);
    lv_obj_align(spin, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spin, lv_color_hex(0xFF2A3D), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 4, LV_PART_MAIN);

    /* ── SSID line ─────────────────────────────────────────── */
    lv_obj_t * ssid_lbl = lv_label_create(card);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssid_lbl, 210);
    {
        char buf[64];
        lv_snprintf(buf, sizeof(buf), "%s", s_wifi_ssid[0] ? s_wifi_ssid : "");
        lv_label_set_text(ssid_lbl, buf);
    }
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ssid_lbl, &ui_font_name_14, 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_CENTER, 0, 18);

    /* ── Status line ───────────────────────────────────────── */
    s_overlay_status = lv_label_create(card);
    lv_label_set_text(s_overlay_status, "Connecting...");
    lv_obj_set_style_text_color(s_overlay_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_overlay_status, &ui_font_name_14, 0);
    lv_obj_align(s_overlay_status, LV_ALIGN_CENTER, 0, 34);

    /* ── Poll timer ────────────────────────────────────────── */
    s_conn_start_ms = lv_tick_get();
    s_conn_timer = lv_timer_create(_conn_poll_cb, 500, NULL);
}

/* ═══════════════════════════════════════════════════════════════
 *  Send action (shared by button click and public API)
 * ═══════════════════════════════════════════════════════════════ */

static void _do_send(void)
{
    if(!ui_input_textarea) return;
    const char * txt = lv_textarea_get_text(ui_input_textarea);
    if(!txt) txt = "";

    /* Commit any pending T9 multi-tap */
    if(s_commit_timer) {
        lv_timer_del(s_commit_timer);
        s_commit_timer = NULL;
        s_last_key     = -1;
        s_tap_count    = 0;
    }
    _clear_err();

    if(s_wifi_mode) {
        int plen = (int)strlen(txt);
        if(plen == 0) {
            _show_err(LV_SYMBOL_WARNING " Enter a password");
            return;
        }
        if(plen < 8) {
            _show_err(LV_SYMBOL_WARNING " Too short (min 8 chars)");
            return;
        }
        ui_wifi_bridge_connect_pending(txt);
        _overlay_show();
    } else {
        /* Generic text-entry mode: just navigate back */
        _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, &ui_wifi_screen_init);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  LVGL event callbacks
 * ═══════════════════════════════════════════════════════════════ */

static void _commit_timer_cb(lv_timer_t * t)
{
    (void)t;
    s_last_key   = -1;
    s_tap_count  = 0;
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
}

static void _key_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = _key_index(lv_event_get_current_target(e));
    if(idx < 0 || !ui_input_textarea) return;
    _clear_err();

    if(s_mode == MODE_NUMBER) {
        lv_textarea_add_text(ui_input_textarea, number_map[idx]);
    } else if(s_mode == MODE_SYMBOL) {
        const char ** sym = sym_pages[s_sym_page];
        if(sym[idx] && sym[idx][0]) lv_textarea_add_text(ui_input_textarea, sym[idx]);
    } else {
        /* T9 multi-tap */
        if(idx == s_last_key) {
            lv_textarea_del_char(ui_input_textarea);
            s_tap_count++;
            int maxc = 0;
            for(int i = 0; i < 5 && t9_lower[idx][i]; i++) maxc++;
            if(s_tap_count >= maxc) s_tap_count = 0;
        } else {
            s_tap_count  = 0;
            s_last_key   = idx;
        }
        const char * ch = t9_lower[idx][s_tap_count];
        if(ch) {
            if(s_upper && ch[0] >= 'a' && ch[0] <= 'z') {
                char uc[2] = {(char)(ch[0] - 32), 0};
                lv_textarea_add_text(ui_input_textarea, uc);
            } else {
                lv_textarea_add_text(ui_input_textarea, ch);
            }
        }
        if(s_commit_timer) lv_timer_del(s_commit_timer);
        s_commit_timer = lv_timer_create(_commit_timer_cb, 1000, NULL);
        lv_timer_set_repeat_count(s_commit_timer, 1);
    }
}

/* Shift / 0 / page toggle */
static void _ctrl2_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(s_mode == MODE_NUMBER) {
        if(ui_input_textarea) lv_textarea_add_text(ui_input_textarea, "0");
    } else if(s_mode == MODE_SYMBOL) {
        s_sym_page = (s_sym_page + 1) % SYM_PAGES;
        _refresh_labels();
    } else {
        s_upper = !s_upper;
        _refresh_labels();
    }
}

/* Mode cycle: Letter → Number → Symbol → Letter */
static void _switch_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(s_mode == MODE_LETTER)        s_mode = MODE_NUMBER;
    else if(s_mode == MODE_NUMBER)  { s_mode = MODE_SYMBOL; s_sym_page = 0; }
    else                              s_mode = MODE_LETTER;
    _refresh_labels();
}

static void _send_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _do_send();
}

static void _backspace_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(ui_input_textarea) {
        lv_textarea_del_char(ui_input_textarea);
        _clear_err();
    }
    /* reset multi-tap on backspace */
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    s_last_key  = -1;
    s_tap_count = 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Helper: make an LVGL obj from an image asset, positioned
 * ═══════════════════════════════════════════════════════════════ */
static lv_obj_t * _img(lv_obj_t * parent, const void * src, int x, int y)
{
    lv_obj_t * o = lv_img_create(parent);
    lv_img_set_src(o, src);
    lv_obj_set_width(o,  LV_SIZE_CONTENT);
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    lv_obj_set_x(o, x);
    lv_obj_set_y(o, y);
    lv_obj_add_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Make an image obj clickable (clear ADV_HITTEST, add CLICKABLE) */
static void _make_clickable(lv_obj_t * o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

/* ═══════════════════════════════════════════════════════════════
 *  Screen init / destroy
 * ═══════════════════════════════════════════════════════════════ */

void ui_t9_keyboard_screen_init(void)
{
    /* ── Root screen ──────────────────────────────────────────── */
    ui_t9_keyboard = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_t9_keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_t9_keyboard, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_t9_keyboard, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Background image ─────────────────────────────────────── */
    ui_t9_keyboard_bg = lv_img_create(ui_t9_keyboard);
    lv_img_set_src(ui_t9_keyboard_bg, &ui_img_background_png);
    lv_obj_set_width(ui_t9_keyboard_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_t9_keyboard_bg, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_t9_keyboard_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_t9_keyboard_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_t9_keyboard_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Text input bar (decorative image) ────────────────────── */
    ui_text_input = _img(ui_t9_keyboard, &ui_img_text_input_png, 10, 2);

    /* ── Backspace button ─────────────────────────────────────── */
    ui_backspace__keybg = _img(ui_t9_keyboard, &ui_img_backspace_key_png, 265, 11);
    _make_clickable(ui_backspace__keybg);
    lv_obj_add_event_cb(ui_backspace__keybg, _backspace_cb, LV_EVENT_CLICKED, NULL);

    /* ── Key matrix background ────────────────────────────────── */
    ui_matrix_bg = _img(ui_t9_keyboard, &ui_img_t9_bg_png, 10, 61);

    /* ── 9 alphanumeric keys ──────────────────────────────────── */
    /*  Row 1 (y=66):  col1→(16)  col2→(90)  col3→(163)          */
    /*  Row 2 (y=121): col1→(16)  col2→(90)  col3→(163)          */
    /*  Row 3 (y=176): col1→(16)  col2→(90)  col3→(163)          */
    /*  Key names are historic; logical order used in _key_index() */
    ui_matrix_4 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 66);
    ui_matrix_2 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 66);
    ui_matrix_3 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 66);
    ui_matrix_1 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 121);
    ui_matrix_5 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 121);
    ui_matrix_6 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 121);
    ui_matrix_7 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 176);
    ui_matrix_8 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 176);
    ui_matrix_9 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 176);

    /* ── 3 control keys (col 4, dark background) ──────────────── */
    ui_control_1 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 66);
    ui_control_2 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 121);
    ui_control_3 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 176);

    /* ── Icon overlays on control keys ───────────────────────── */
    /* control_1: "Send" text label (yellow) */
    ui_send = lv_label_create(ui_control_1);
    lv_label_set_text(ui_send, "Send");
    lv_obj_set_style_text_color(ui_send, lv_color_hex(0xFF2A3D), 0);
    lv_obj_set_style_text_font(ui_send, &ui_font_name_24, 0);
    lv_obj_set_align(ui_send, LV_ALIGN_CENTER);

    /* control_2: shift icon (letter) / "0" key (number) / page icon (symbol) */
    ui_capital = _img(ui_t9_keyboard, &ui_img_capital_png, 254, 133);
    s_lbl_ctrl2 = lv_label_create(ui_control_2);
    lv_label_set_text(s_lbl_ctrl2, "0");
    lv_obj_set_style_text_color(s_lbl_ctrl2, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s_lbl_ctrl2, &ui_font_name_24, 0);
    lv_obj_set_align(s_lbl_ctrl2, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);

    /* control_3: mode-switch icon (image) */
    ui_switch = _img(ui_t9_keyboard, &ui_img_switch_png, 251, 185);

    /* ── Reset keyboard state ─────────────────────────────────── */
    s_mode         = MODE_LETTER;
    s_sym_page     = 0;
    s_last_key     = -1;
    s_tap_count    = 0;
    s_commit_timer = NULL;
    s_upper        = false;
    s_overlay      = NULL;
    s_conn_timer   = NULL;
    s_nav_timer    = NULL;
    s_overlay_status = NULL;
    s_err_label    = NULL;

    /* ── Text input textarea ──────────────────────────────────── */
    ui_input_textarea = lv_textarea_create(ui_t9_keyboard);
    lv_obj_set_width(ui_input_textarea, 240);
    lv_obj_set_height(ui_input_textarea, 44);
    lv_obj_set_pos(ui_input_textarea, 15, 8);
    lv_textarea_set_one_line(ui_input_textarea, true);
    lv_textarea_set_text(ui_input_textarea, "");

    lv_textarea_set_placeholder_text(ui_input_textarea, "");

    lv_obj_set_style_bg_opa(ui_input_textarea, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_input_textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_input_textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_input_textarea, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_input_textarea, &ui_font_name_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_input_textarea, lv_color_hex(0xFFFFFF),
                                 LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_left(ui_input_textarea, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui_input_textarea, 14, LV_PART_MAIN);   /* vertical centering */
    lv_obj_set_style_pad_bottom(ui_input_textarea, 0, LV_PART_MAIN);
    /* White cursor */
    lv_obj_set_style_bg_color(ui_input_textarea, lv_color_white(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ui_input_textarea, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ui_input_textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_add_state(ui_input_textarea, LV_STATE_FOCUSED);

    /* Apply pre-fill (used when retrying a saved-network password) */
    if(s_prefill_buf[0]) {
        lv_textarea_set_text(ui_input_textarea, s_prefill_buf);
        lv_textarea_set_cursor_pos(ui_input_textarea, LV_TEXTAREA_CURSOR_LAST);
        s_prefill_buf[0] = '\0';
    }

    /* ── Wire up alphanumeric key buttons ─────────────────────── */
    {
        lv_obj_t * mats[9] = {
            ui_matrix_4, ui_matrix_2, ui_matrix_3,
            ui_matrix_1, ui_matrix_5, ui_matrix_6,
            ui_matrix_7, ui_matrix_8, ui_matrix_9
        };
        /* Initial labels for letter mode */
        static const char * init_lbls[9] = {
            "abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"," "
        };
        for(int i = 0; i < 9; i++) {
            _make_clickable(mats[i]);
            s_key_labels[i] = lv_label_create(mats[i]);
            lv_label_set_text(s_key_labels[i], init_lbls[i]);
            lv_obj_set_align(s_key_labels[i], LV_ALIGN_CENTER);
            lv_obj_set_style_text_color(s_key_labels[i], lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(s_key_labels[i], &ui_font_name_24, 0);
            lv_obj_add_event_cb(mats[i], _key_cb, LV_EVENT_CLICKED, NULL);
        }
        /* Space key (index 8 = ui_matrix_9): start transparent, _refresh_labels sets it white */
        lv_obj_set_style_img_recolor(ui_matrix_9, lv_color_white(), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_TRANSP, 0);
        /* Space bar indicator: thin black horizontal bar, non-clickable (clicks fall through) */
        s_space_bar = lv_obj_create(ui_matrix_9);
        lv_obj_set_size(s_space_bar, 28, 4);
        lv_obj_align(s_space_bar, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_bg_color(s_space_bar, lv_color_hex(0x111111), 0);
        lv_obj_set_style_bg_opa(s_space_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_space_bar, 2, 0);
        lv_obj_set_style_border_width(s_space_bar, 0, 0);
        lv_obj_set_style_pad_all(s_space_bar, 0, 0);
        lv_obj_clear_flag(s_space_bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);  /* _refresh_labels shows it in LETTER mode */
    }

    /* ── Wire control_1 (Send) ────────────────────────────────── */
    _make_clickable(ui_control_1);
    lv_obj_add_event_cb(ui_control_1, _send_cb, LV_EVENT_CLICKED, NULL);

    /* ── Wire control_2 (Shift / 0 / Page) ───────────────────── */
    _make_clickable(ui_control_2);
    lv_obj_add_event_cb(ui_control_2, _ctrl2_cb, LV_EVENT_CLICKED, NULL);
    _make_clickable(ui_capital);
    lv_obj_add_event_cb(ui_capital, _ctrl2_cb, LV_EVENT_CLICKED, NULL);

    /* ── Symbol page-toggle icon: ◄► lime label, child of control_2 ── */
    /* Labels are not clickable by default — taps fall through to ui_control_2 → _ctrl2_cb */
    s_sym_icon = lv_label_create(ui_control_2);
    lv_label_set_text(s_sym_icon, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(s_sym_icon, lv_color_hex(0xFF2A3D), 0);
    lv_obj_set_align(s_sym_icon, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);

    /* ── Wire control_3 (Mode switch) ────────────────────────── */
    _make_clickable(ui_control_3);
    lv_obj_add_event_cb(ui_control_3, _switch_cb, LV_EVENT_CLICKED, NULL);
    _make_clickable(ui_switch);
    lv_obj_add_event_cb(ui_switch, _switch_cb, LV_EVENT_CLICKED, NULL);

    /* ── Apply initial label state ───────────────────────────── */
    _refresh_labels();
}

void ui_t9_keyboard_screen_destroy(void)
{
    _overlay_hide();
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    if(s_nav_timer)    { lv_timer_del(s_nav_timer);    s_nav_timer    = NULL; }
    if(ui_t9_keyboard) { lv_obj_del(ui_t9_keyboard);   ui_t9_keyboard = NULL; }

    ui_t9_keyboard_bg  = NULL;
    ui_text_input      = NULL;
    ui_backspace__keybg= NULL;
    ui_matrix_bg       = NULL;
    ui_matrix_1 = ui_matrix_2 = ui_matrix_3 = NULL;
    ui_matrix_4 = ui_matrix_5 = ui_matrix_6 = NULL;
    ui_matrix_7 = ui_matrix_8 = ui_matrix_9 = NULL;
    ui_control_1 = ui_control_2 = ui_control_3 = NULL;
    ui_capital     = NULL;
    ui_send        = NULL;
    ui_switch      = NULL;
    ui_input_textarea = NULL;
    s_overlay_status  = NULL;
    s_err_label       = NULL;
    s_lbl_ctrl2       = NULL;
    s_sym_icon        = NULL;
    s_space_bar       = NULL;
    for(int i = 0; i < 9; i++) s_key_labels[i] = NULL;

    /* Reset WiFi mode for next open */
    s_wifi_mode    = false;
}

/* ─────────────────────────────────────────────────────────────────
 * ui_t9_keyboard_cleanup — cancel timers + null all LVGL pointers
 * WITHOUT calling lv_obj_del on the screen.
 *
 * Use this instead of screen_destroy when the T9 screen is still
 * the active (visible) screen.  LVGL's lv_scr_load_anim(auto_del)
 * will delete the old screen after the animation; calling lv_obj_del
 * on the active screen first causes a use-after-free restart.
 * ─────────────────────────────────────────────────────────────────*/
void ui_t9_keyboard_cleanup(void)
{
    _overlay_hide();
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    if(s_nav_timer)    { lv_timer_del(s_nav_timer);    s_nav_timer    = NULL; }

    ui_t9_keyboard     = NULL;   /* screen object stays alive for LVGL auto_del */
    ui_t9_keyboard_bg  = NULL;
    ui_text_input      = NULL;
    ui_backspace__keybg= NULL;
    ui_matrix_bg       = NULL;
    ui_matrix_1 = ui_matrix_2 = ui_matrix_3 = NULL;
    ui_matrix_4 = ui_matrix_5 = ui_matrix_6 = NULL;
    ui_matrix_7 = ui_matrix_8 = ui_matrix_9 = NULL;
    ui_control_1 = ui_control_2 = ui_control_3 = NULL;
    ui_capital        = NULL;
    ui_send           = NULL;
    ui_switch         = NULL;
    ui_input_textarea = NULL;
    s_overlay_status  = NULL;
    s_err_label       = NULL;
    s_lbl_ctrl2       = NULL;
    s_sym_icon        = NULL;
    s_space_bar       = NULL;
    for(int i = 0; i < 9; i++) s_key_labels[i] = NULL;
    s_wifi_mode = false;
}

/* ═══════════════════════════════════════════════════════════════
 *  Public WiFi provisioning API
 * ═══════════════════════════════════════════════════════════════ */

void ui_t9_keyboard_prepare_for_wifi(const char * ssid)
{
    strncpy(s_wifi_ssid, ssid ? ssid : "", sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    s_wifi_mode = true;
}

void ui_t9_keyboard_set_prefill(const char * pass)
{
    if(!pass) return;
    strncpy(s_prefill_buf, pass, sizeof(s_prefill_buf) - 1);
    s_prefill_buf[sizeof(s_prefill_buf) - 1] = '\0';
    /* If screen already visible, apply directly */
    if(ui_input_textarea && s_prefill_buf[0]) {
        lv_textarea_set_text(ui_input_textarea, s_prefill_buf);
        lv_textarea_set_cursor_pos(ui_input_textarea, LV_TEXTAREA_CURSOR_LAST);
        s_prefill_buf[0] = '\0';
    }
}

void ui_t9_keyboard_send(void)
{
    _do_send();
}

bool ui_t9_keyboard_is_connecting(void)
{
    return (s_conn_timer != NULL);  /* true only while actively polling */
}

void ui_t9_keyboard_cancel_connect(void)
{
    _overlay_hide();
    ui_wifi_bridge_disconnect();
}
