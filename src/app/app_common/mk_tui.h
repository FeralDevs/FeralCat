/**
 * @file mk_tui.h
 * @brief MeowKit unified TUI — LovyanGFX direct-draw component library.
 *
 * Two zones, one header:
 *
 *  MK_PAL   — product color palette constants (always compiled)
 *  MK_TUI   — app-mode stateless drawing primitives (always compiled)
 *  BBT block — board-test CRT UI, identical API to former bbt_ui.h
 *              (compiled only when MEOWKIT_HW_TEST_ENABLE is defined)
 *
 * App usage:
 *   #include "mk_tui.h"
 *   MK_TUI::clearScreen(lcd);
 *   MK_TUI::drawHeader(lcd, "VU Meter");
 *   MK_TUI::drawMenuItem(lcd, 0, "Channel", "L+R", true);
 *   MK_TUI::drawFooter(lcd, "Select", "Back");
 *
 * BBT usage (unchanged from former bbt_ui.h):
 *   bbt_drawOverview(lcd, results, true);
 *   bbt_beginContent(lcd, "01", "DISPLAY ST7789");
 *   bbt_drawResult(lcd,  "01", "DISPLAY ST7789", true, "320×240 OK");
 */
#pragma once
#include <LovyanGFX.hpp>

/* ════════════════════════════════════════════════════════════════
 *  MK_PAL — MeowKit product color palette
 * ════════════════════════════════════════════════════════════════ */
namespace MK_PAL {
    static constexpr uint32_t BLACK       = 0x000000;
    static constexpr uint32_t WHITE       = 0xFFFFFF;
    static constexpr uint32_t ACCENT      = 0xFF2A3D;  /* FeralCat red */
    static constexpr uint32_t ACCENT_DIM  = 0x8A0016;
    static constexpr uint32_t ACCENT_DARK = 0x2E0009;
    static constexpr uint32_t TEXT_PRI    = 0xFFFFFF;
    static constexpr uint32_t TEXT_SEC    = 0x888888;
    static constexpr uint32_t TEXT_TITLE  = 0xFF2A3D;
    static constexpr uint32_t ITEM_BG     = 0x1A1A1A;
    static constexpr uint32_t ITEM_BG_ALT = 0x222222;
    static constexpr uint32_t SEL_BG      = 0xFF2A3D;
    static constexpr uint32_t SEL_TEXT    = 0x000000;
    static constexpr uint32_t BORDER      = 0x333333;
    static constexpr uint32_t OK          = 0x00DD44;
    static constexpr uint32_t WARN        = 0xFFAA00;
    static constexpr uint32_t ERR         = 0xFF3333;
}

/* ════════════════════════════════════════════════════════════════
 *  Layout constants (320×240)
 * ════════════════════════════════════════════════════════════════ */
namespace MK_LAYOUT {
    static constexpr int W          = 320;
    static constexpr int H          = 240;
    static constexpr int HDR_H      = 24;
    static constexpr int FTR_H      = 20;
    static constexpr int ITEM_H     = 32;
    static constexpr int PAD        = 8;
    static constexpr int CONTENT_Y  = HDR_H + 1;
    static constexpr int CONTENT_H  = H - HDR_H - FTR_H - 2;
    static constexpr int CONTENT_ROWS = CONTENT_H / ITEM_H;   /* ~6 */
}

/* ════════════════════════════════════════════════════════════════
 *  MK_TUI — app-mode stateless drawing primitives
 *  All functions are header-only templates so any LGFX variant compiles.
 * ════════════════════════════════════════════════════════════════ */
namespace MK_TUI {

template<typename LCD>
static inline void clearScreen(LCD& lcd) {
    lcd.fillScreen((uint32_t)MK_PAL::BLACK);
}

/** Top header bar with accent-coloured title. */
template<typename LCD>
static inline void drawHeader(LCD& lcd, const char* title) {
    lcd.fillRect(0, 0, MK_LAYOUT::W, MK_LAYOUT::HDR_H,
                 (uint32_t)MK_PAL::ACCENT_DARK);
    lcd.drawFastHLine(0, MK_LAYOUT::HDR_H - 1, MK_LAYOUT::W,
                      (uint32_t)MK_PAL::ACCENT);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_TITLE,
                     (uint32_t)MK_PAL::ACCENT_DARK);
    lcd.setCursor(MK_LAYOUT::PAD, 4);
    lcd.printf("%s", title);
}

/** Bottom footer bar: [A] on left, [B] on right. */
template<typename LCD>
static inline void drawFooter(LCD& lcd,
                               const char* labelA,
                               const char* labelB) {
    int fy = MK_LAYOUT::H - MK_LAYOUT::FTR_H;
    lcd.fillRect(0, fy, MK_LAYOUT::W, MK_LAYOUT::FTR_H,
                 (uint32_t)MK_PAL::ACCENT_DARK);
    lcd.drawFastHLine(0, fy, MK_LAYOUT::W, (uint32_t)MK_PAL::ACCENT);
    lcd.setFont(&fonts::efontCN_16);
    if (labelA && labelA[0]) {
        lcd.setTextColor((uint32_t)MK_PAL::ACCENT,
                         (uint32_t)MK_PAL::ACCENT_DARK);
        lcd.setCursor(MK_LAYOUT::PAD, fy + 2);
        lcd.printf("[A] %s", labelA);
    }
    if (labelB && labelB[0]) {
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC,
                         (uint32_t)MK_PAL::ACCENT_DARK);
        lcd.setCursor(MK_LAYOUT::W - 96, fy + 2);
        lcd.printf("[B] %s", labelB);
    }
}

/**
 * Single menu row at zero-based `row` index in the content area.
 * `value` is optional right-aligned secondary text.
 */
template<typename LCD>
static inline void drawMenuItem(LCD& lcd, int row,
                                 const char* label,
                                 const char* value,
                                 bool selected) {
    int y = MK_LAYOUT::CONTENT_Y + row * MK_LAYOUT::ITEM_H;
    uint32_t bg   = selected ? MK_PAL::SEL_BG
                             : (row % 2 ? MK_PAL::ITEM_BG_ALT : MK_PAL::ITEM_BG);
    uint32_t fg   = selected ? MK_PAL::SEL_TEXT : MK_PAL::TEXT_PRI;
    uint32_t vfg  = selected ? MK_PAL::SEL_TEXT : MK_PAL::TEXT_SEC;
    lcd.fillRect(0, y, MK_LAYOUT::W, MK_LAYOUT::ITEM_H, bg);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(fg, bg);
    lcd.setCursor(MK_LAYOUT::PAD, y + 8);
    lcd.printf("%s", label);
    if (value && value[0]) {
        lcd.setTextColor(vfg, bg);
        lcd.setCursor(MK_LAYOUT::W - 110, y + 8);
        lcd.printf("%s", value);
    }
    /* Selection indicator stripe */
    if (selected)
        lcd.fillRect(0, y, 3, MK_LAYOUT::ITEM_H, (uint32_t)MK_PAL::ACCENT_DIM);
}

/** Horizontal progress bar. `y` is the top-left Y inside content area. */
template<typename LCD>
static inline void drawProgress(LCD& lcd, int y,
                                 int pct, const char* label) {
    if (label && label[0]) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s", label);
        y += 18;
    }
    int bx = MK_LAYOUT::PAD, bw = MK_LAYOUT::W - 16, bh = 10;
    lcd.fillRect(bx, y, bw, bh, (uint32_t)MK_PAL::ACCENT_DARK);
    int fill = pct * bw / 100;
    if (fill > 0) lcd.fillRect(bx, y, fill, bh, (uint32_t)MK_PAL::ACCENT);
    lcd.drawRect(bx - 1, y - 1, bw + 2, bh + 2, (uint32_t)MK_PAL::BORDER);
}

/** Centered modal dialog with optional A / B button labels. */
template<typename LCD>
static inline void drawDialog(LCD& lcd, const char* msg,
                               const char* btnA = nullptr,
                               const char* btnB = nullptr) {
    int bx = 20, by = 70, bw = 280, bh = 100;
    lcd.fillRect(bx, by, bw, bh, (uint32_t)MK_PAL::ITEM_BG);
    lcd.drawRect(bx,     by,     bw,     bh,     (uint32_t)MK_PAL::ACCENT);
    lcd.drawRect(bx + 2, by + 2, bw - 4, bh - 4, (uint32_t)MK_PAL::ACCENT_DIM);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::WHITE, (uint32_t)MK_PAL::ITEM_BG);
    lcd.setCursor(bx + 10, by + 22);
    lcd.printf("%s", msg);
    if (btnA) {
        lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::ITEM_BG);
        lcd.setCursor(bx + 10, by + 72);
        lcd.printf("[A] %s", btnA);
    }
    if (btnB) {
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::ITEM_BG);
        lcd.setCursor(bx + 160, by + 72);
        lcd.printf("[B] %s", btnB);
    }
}

/** Status / info line printed at absolute `y` in the content area. */
template<typename LCD>
static inline void drawInfo(LCD& lcd, int y,
                             const char* label, const char* value,
                             uint32_t valueColor = MK_PAL::ACCENT) {
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", label);
    if (value && value[0]) {
        lcd.setTextColor(valueColor, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::W - 120, y);
        lcd.printf("%s", value);
    }
}

} /* namespace MK_TUI */

/* ════════════════════════════════════════════════════════════════
 *  BBT — Board-level hardware test TUI  (green-on-black CRT style)
 *  Compiled only when MEOWKIT_HW_TEST_ENABLE is defined.
 *  API is identical to the former bsp/bbt_ui.h — no call-sites change.
 * ════════════════════════════════════════════════════════════════ */
#if MEOWKIT_HW_TEST_ENABLE

#define BBT_ST_PENDING  0
#define BBT_ST_PASS     1
#define BBT_ST_FAIL     2
#define BBT_ST_SKIP     3

#define BBT_NUM_TESTS   13

#define BBT_IDX_DISPLAY   0
#define BBT_IDX_TOUCH     1
#define BBT_IDX_I2C       2
#define BBT_IDX_RTC       3
#define BBT_IDX_IMU       4
#define BBT_IDX_LED       5
#define BBT_IDX_BUTTONS   6
#define BBT_IDX_IR        7
#define BBT_IDX_MIC       8
#define BBT_IDX_SPEAKER   9
#define BBT_IDX_SD        10
#define BBT_IDX_PMU       11
#define BBT_IDX_GPIO      12

static const struct { const char* num; const char* name; } _bbt_reg[BBT_NUM_TESTS] = {
    { "01",  "DISPLAY ST7789"   },
    { "02",  "TOUCH FT6336"     },
    { "03",  "I2C BUS SCAN"     },
    { "04",  "RTC PCF8563"      },
    { "05",  "IMU BMI270"       },
    { "06",  "LED WS2812B"      },
    { "07",  "BUTTONS+JOY"      },
    { "08",  "IR TX/RX"         },
    { "09a", "MIC ES7210"       },
    { "09b", "SPEAKER ES8311"   },
    { "10",  "SD CARD"          },
    { "11",  "PMU AXP173"       },
    { "12",  "GPIO TEST"        },
};

static const uint16_t _BBT_BG      = TFT_BLACK;
static const uint16_t _BBT_BORDER  = TFT_GREEN;
static const uint16_t _BBT_CYAN    = TFT_CYAN;
static const uint16_t _BBT_WHITE   = TFT_WHITE;
static const uint16_t _BBT_PASS    = TFT_GREEN;
static const uint16_t _BBT_FAIL    = TFT_RED;
static const uint16_t _BBT_SKIP    = TFT_YELLOW;
static const uint16_t _BBT_PENDING = 0x4208;
static const uint16_t _BBT_DIM     = 0x4208;

#define _BBT_W          320
#define _BBT_H          240
#define _BBT_HDR_SEP    17
#define _BBT_CON_Y1     18
#define _BBT_CON_Y2     220
#define _BBT_FTR_SEP    221
#define _BBT_FTR_TXT    224
#define _BBT_ROWH       16
#define _BBT_PAD        6

template<typename LCD>
static inline void _bbt_chrome(LCD& lcd)
{
    lcd.fillRect(0, 0, _BBT_W, _BBT_HDR_SEP + 1, _BBT_BG);
    lcd.drawFastHLine(0, 0,            _BBT_W, _BBT_BORDER);
    lcd.drawFastHLine(0, _BBT_HDR_SEP, _BBT_W, _BBT_BORDER);
    lcd.drawFastVLine(0,          1, _BBT_HDR_SEP - 1, _BBT_BORDER);
    lcd.drawFastVLine(_BBT_W - 1, 1, _BBT_HDR_SEP - 1, _BBT_BORDER);
    lcd.drawFastVLine(0,          _BBT_CON_Y1, _BBT_CON_Y2 - _BBT_CON_Y1 + 1, _BBT_BORDER);
    lcd.drawFastVLine(_BBT_W - 1, _BBT_CON_Y1, _BBT_CON_Y2 - _BBT_CON_Y1 + 1, _BBT_BORDER);
    lcd.fillRect(0, _BBT_FTR_SEP, _BBT_W, _BBT_H - _BBT_FTR_SEP, _BBT_BG);
    lcd.drawFastHLine(0, _BBT_FTR_SEP, _BBT_W, _BBT_BORDER);
    lcd.drawFastHLine(0, _BBT_H - 1,   _BBT_W, _BBT_BORDER);
    lcd.drawFastVLine(0,          _BBT_FTR_SEP + 1, _BBT_H - _BBT_FTR_SEP - 2, _BBT_BORDER);
    lcd.drawFastVLine(_BBT_W - 1, _BBT_FTR_SEP + 1, _BBT_H - _BBT_FTR_SEP - 2, _BBT_BORDER);
}

template<typename LCD>
static inline void _bbt_header(LCD& lcd, const char* num, const char* title)
{
    lcd.fillRect(1, 1, _BBT_W - 2, _BBT_HDR_SEP - 1, _BBT_BG);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(_BBT_BORDER, _BBT_BG);
    lcd.setCursor(4, 2);
    lcd.printf("BBT %s/12", num);
    lcd.setTextColor(_BBT_DIM, _BBT_BG);
    lcd.setCursor(84, 2);
    lcd.printf(" -- ");
    lcd.setTextColor(_BBT_CYAN, _BBT_BG);
    lcd.setCursor(116, 2);
    char tb[25]; snprintf(tb, sizeof(tb), "%-24s", title);
    lcd.printf("%s", tb);
}

template<typename LCD>
static inline void _bbt_footer(LCD& lcd, const char* ca, const char* cb)
{
    lcd.fillRect(1, _BBT_FTR_SEP + 1, _BBT_W - 2, _BBT_H - _BBT_FTR_SEP - 2, _BBT_BG);
    lcd.setFont(&fonts::efontCN_16);
    if (ca && ca[0]) {
        lcd.setTextColor(_BBT_BORDER, _BBT_BG);
        lcd.setCursor(8, _BBT_FTR_TXT);
        lcd.printf("[A] %s", ca);
    }
    if (cb && cb[0]) {
        lcd.setTextColor(_BBT_DIM, _BBT_BG);
        lcd.setCursor(200, _BBT_FTR_TXT);
        lcd.printf("[B] %s", cb);
    }
}

template<typename LCD>
static inline void _bbt_clearContent(LCD& lcd)
{
    lcd.fillRect(1, _BBT_CON_Y1, _BBT_W - 2, _BBT_CON_Y2 - _BBT_CON_Y1 + 1, _BBT_BG);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(_BBT_WHITE, _BBT_BG);
    lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 4);
}

static inline uint16_t _bbt_stColor(int8_t st)
{
    switch (st) {
        case BBT_ST_PASS: return _BBT_PASS;
        case BBT_ST_FAIL: return _BBT_FAIL;
        case BBT_ST_SKIP: return _BBT_SKIP;
        default:          return _BBT_PENDING;
    }
}

static inline const char* _bbt_stMark(int8_t st)
{
    switch (st) {
        case BBT_ST_PASS: return " OK ";
        case BBT_ST_FAIL: return " !! ";
        case BBT_ST_SKIP: return " -- ";
        default:          return " .. ";
    }
}

template<typename LCD>
static void bbt_drawOverview(LCD& lcd, int8_t* results, bool showStart = true)
{
    lcd.fillScreen(_BBT_BG);
    lcd.drawRect(0, 0, _BBT_W, _BBT_H, _BBT_BORDER);
    lcd.drawFastHLine(0, 17, _BBT_W, _BBT_BORDER);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(_BBT_CYAN, _BBT_BG);
    lcd.setCursor(4, 2);
    lcd.printf("MEOWKIT HARDWARE BOARD TEST");
    lcd.setTextColor(_BBT_DIM, _BBT_BG);
    lcd.setCursor(256, 2);
    lcd.printf("12 ITEMS");

    for (int i = 0; i < BBT_NUM_TESTS; i++) {
        int y = 18 + i * 15;
        uint16_t col  = _bbt_stColor(results[i]);
        const char* mk = _bbt_stMark(results[i]);
        lcd.setTextColor(_BBT_BORDER, _BBT_BG);
        lcd.setCursor(4, y);
        lcd.printf("#%-3s", _bbt_reg[i].num);
        lcd.setTextColor(_BBT_WHITE, _BBT_BG);
        lcd.setCursor(40, y);
        char nb[20]; snprintf(nb, sizeof(nb), "%-18s", _bbt_reg[i].name);
        lcd.printf("%s", nb);
        lcd.setTextColor(col, _BBT_BG);
        lcd.setCursor(248, y);
        lcd.printf("[%s]", mk);
    }

    lcd.drawFastHLine(0, 214, _BBT_W, _BBT_BORDER);
    lcd.setFont(&fonts::efontCN_16);
    if (showStart) {
        lcd.setTextColor(_BBT_BORDER, _BBT_BG);
        lcd.setCursor(6, 217);
        lcd.printf("[A] START ALL TESTS");
        lcd.setTextColor(_BBT_DIM, _BBT_BG);
        lcd.setCursor(224, 217);
        lcd.printf("[B] EXIT");
    } else {
        int pass = 0, fail = 0, skip = 0;
        for (int i = 0; i < BBT_NUM_TESTS; i++) {
            if (results[i] == BBT_ST_PASS) pass++;
            if (results[i] == BBT_ST_FAIL) fail++;
            if (results[i] == BBT_ST_SKIP) skip++;
        }
        lcd.setTextColor(_BBT_PASS,   _BBT_BG); lcd.setCursor(6,   217); lcd.printf("PASS:%-2d", pass);
        lcd.setTextColor(_BBT_FAIL,   _BBT_BG); lcd.setCursor(88,  217); lcd.printf("FAIL:%-2d", fail);
        lcd.setTextColor(_BBT_SKIP,   _BBT_BG); lcd.setCursor(168, 217); lcd.printf("SKIP:%-2d", skip);
        lcd.setTextColor(_BBT_BORDER, _BBT_BG); lcd.setCursor(240, 217); lcd.printf("COMPLETE");
    }
}

template<typename LCD>
static void bbt_drawPrompt(LCD& lcd,
                            const char* num, const char* title,
                            const char* desc1, const char* desc2,
                            int8_t* results)
{
    lcd.fillScreen(_BBT_BG);
    _bbt_chrome(lcd);
    _bbt_header(lcd, num, title);
    _bbt_footer(lcd, "START", "SKIP");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(_BBT_WHITE, _BBT_BG);
    lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 8);
    lcd.printf("%s", desc1 ? desc1 : "");
    if (desc2 && desc2[0]) {
        lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 24);
        lcd.printf("%s", desc2);
    }
    lcd.drawFastHLine(1, _BBT_CON_Y1 + 44, _BBT_W - 2, 0x0300);
    lcd.setTextColor(_BBT_DIM, _BBT_BG);
    lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 52);
    lcd.printf("PROGRESS:");

    int done = 0, pass = 0, fail = 0, skip = 0;
    for (int i = 0; i < BBT_NUM_TESTS; i++) {
        if (results[i] != BBT_ST_PENDING) done++;
        if (results[i] == BBT_ST_PASS)    pass++;
        if (results[i] == BBT_ST_FAIL)    fail++;
        if (results[i] == BBT_ST_SKIP)    skip++;
    }

    int bx = _BBT_PAD, by = _BBT_CON_Y1 + 70;
    int bw = _BBT_W - 12, bh = 10;
    lcd.fillRect(bx, by, bw, bh, 0x0180);
    int filled = (done * bw) / BBT_NUM_TESTS;
    if (filled > 0) lcd.fillRect(bx, by, filled, bh, _BBT_PASS);
    lcd.drawRect(bx - 1, by - 1, bw + 2, bh + 2, _BBT_BORDER);

    lcd.setTextColor(_BBT_BORDER, _BBT_BG);
    lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 86);
    lcd.printf("DONE: %d/%d", done, BBT_NUM_TESTS);
    if (pass > 0) { lcd.setTextColor(_BBT_PASS, _BBT_BG); lcd.setCursor(104, _BBT_CON_Y1 + 86); lcd.printf("PASS:%d", pass); }
    if (fail > 0) { lcd.setTextColor(_BBT_FAIL, _BBT_BG); lcd.setCursor(176, _BBT_CON_Y1 + 86); lcd.printf("FAIL:%d", fail); }
    if (skip > 0) { lcd.setTextColor(_BBT_SKIP, _BBT_BG); lcd.setCursor(248, _BBT_CON_Y1 + 86); lcd.printf("SKIP:%d", skip); }

    lcd.drawFastHLine(1, _BBT_CON_Y1 + 108, _BBT_W - 2, 0x0300);
    int gx = _BBT_PAD, gy = _BBT_CON_Y1 + 116;
    for (int i = 0; i < BBT_NUM_TESTS; i++) {
        lcd.setTextColor(_bbt_stColor(results[i]), _BBT_BG);
        lcd.setCursor(gx, gy);
        lcd.printf("[%s]", _bbt_stMark(results[i]));
        gx += 48;
        if (gx > 248) { gx = _BBT_PAD; gy += 18; }
    }

    lcd.setTextColor(_BBT_CYAN, _BBT_BG);
    lcd.setCursor(_BBT_PAD, _BBT_CON_Y1 + 162);
    lcd.printf("> PRESS  [A]  TO  START  THIS  TEST");
}

template<typename LCD>
static void bbt_beginContent(LCD& lcd, const char* num, const char* title)
{
    lcd.fillScreen(_BBT_BG);
    _bbt_chrome(lcd);
    _bbt_header(lcd, num, title);
    _bbt_footer(lcd, "", "SKIP/EXIT");
    _bbt_clearContent(lcd);
}

template<typename LCD>
static void bbt_drawResult(LCD& lcd,
                            const char* num, const char* title,
                            bool pass,
                            const char* detail1 = nullptr,
                            const char* detail2 = nullptr)
{
    lcd.fillScreen(_BBT_BG);
    _bbt_chrome(lcd);
    _bbt_header(lcd, num, title);
    _bbt_footer(lcd, "", "CONTINUE");

    uint16_t rcol  = pass ? _BBT_PASS : _BBT_FAIL;
    const char* rs = pass ? "  PASS  " : "  FAIL  ";

    int bx = 60, by = _BBT_CON_Y1 + 32, bw = 200, bh = 52;
    lcd.fillRect(bx, by, bw, bh, _BBT_BG);
    lcd.drawRect(bx,     by,     bw,     bh,     rcol);
    lcd.drawRect(bx + 2, by + 2, bw - 4, bh - 4, rcol);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextSize(2);
    lcd.setTextColor(rcol, _BBT_BG);
    lcd.setCursor(bx + (bw - 128) / 2, by + (bh - 32) / 2);
    lcd.printf("%s", rs);
    lcd.setTextSize(1);

    int dy = by + bh + 12;
    if (detail1 && detail1[0]) {
        lcd.setTextColor(_BBT_WHITE, _BBT_BG);
        lcd.setCursor(_BBT_PAD, dy); lcd.printf("%s", detail1);
        dy += _BBT_ROWH + 2;
    }
    if (detail2 && detail2[0]) {
        lcd.setTextColor(_BBT_DIM, _BBT_BG);
        lcd.setCursor(_BBT_PAD, dy); lcd.printf("%s", detail2);
        dy += _BBT_ROWH + 2;
    }
    lcd.drawFastHLine(1, dy + 4, _BBT_W - 2, 0x0300);
    lcd.setTextColor(_BBT_BORDER, _BBT_BG);
    lcd.setCursor(_BBT_PAD, dy + 10);
    lcd.printf(pass ? "TEST PASSED -- PRESS [B] TO CONTINUE"
                    : "TEST FAILED -- PRESS [B] TO CONTINUE");
}

#endif /* MEOWKIT_HW_TEST_ENABLE */
