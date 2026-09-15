/**
 * @file  meowplayer_ui.h
 * @brief MeowPlayer screens — pure rendering (device LCD + desktop TUI sim).
 *        Two views: Now-Playing (transport + equalizer) and Menu (song list +
 *        output option). MK_TUI / LovyanGFX primitives, no hardware.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cmath>
#include "../app_common/mk_tui.h"

namespace MeowPlayer {

/* ── Now-Playing view-model ── */
struct View {
    bool spkReady = true;
    bool ampOn    = true;   /* output: true = speaker, false = headphones/jack */
    bool playing  = true;   /* transport state                                 */
    const char* title = "";
    int  posSec   = 0;
    int  durSec   = 0;
    int  vol      = 10;
    int  volMax   = 21;
    const char* diag = nullptr;  /* temporary on-screen debug line             */
};

/* ── Menu view-model ── */
struct MenuView {
    const char* title = "Menu";
    const char* const* items = nullptr;
    int count = 0;
    int sel   = 0;
    int top   = 0;          /* first visible row (scrolling)                   */
};

/* ── Small output glyph (speaker / headphones), ~20px, centred on (cx,cy) ── */
template<typename LCD>
static inline void drawOutIcon(LCD& lcd, int cx, int cy, bool speaker, uint32_t col)
{
    if (speaker) {
        lcd.fillRect(cx - 9, cy - 3, 4, 6, col);
        lcd.fillTriangle(cx - 5, cy - 3, cx - 5, cy + 3, cx + 1, cy + 7, col);
        lcd.fillTriangle(cx - 5, cy - 3, cx + 1, cy - 7, cx + 1, cy + 7, col);
        for (int a = -50; a <= 50; a += 10) {
            float r = a * 3.14159f / 180.0f;
            lcd.fillCircle(cx + 3 + (int)(cosf(r) * 8), cy + (int)(sinf(r) * 8), 1, col);
        }
    } else {
        const int R = 10;
        for (int a = -80; a <= 80; a += 8) {
            float r = a * 3.14159f / 180.0f;
            lcd.fillCircle(cx + (int)(sinf(r) * R), cy - (int)(cosf(r) * R), 1, col);
        }
        lcd.fillRoundRect(cx - R - 2, cy - 1, 5, 9, 2, col);
        lcd.fillRoundRect(cx + R - 2, cy - 1, 5, 9, 2, col);
    }
}

static inline void fmtTime(char* b, size_t n, int sec)
{
    if (sec < 0) sec = 0;
    snprintf(b, n, "%d:%02d", sec / 60, sec % 60);
}

/* ── Now-Playing screen ── */
template<typename LCD>
static inline void drawNowPlaying(LCD& lcd, const View& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "MeowPlayer");

    if (!v.spkReady) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::ERR, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 90);
        lcd.printf("Audio init failed (ES8311)");
        MK_TUI::drawFooter(lcd, "", "Exit");
        return;
    }

    const uint32_t accent = (uint32_t)MK_PAL::ACCENT;

    /* output glyph, top-right */
    drawOutIcon(lcd, MK_LAYOUT::W - 20, 38, v.ampOn, (uint32_t)MK_PAL::TEXT_SEC);

    /* track title + output label */
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::WHITE, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 32);
    lcd.printf("%.26s", v.title && v.title[0] ? v.title : "(no track)");
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 52);
    lcd.printf("%s", v.ampOn ? "Speaker" : "Headphones (jack)");

    /* central play/pause disc — the focal point, cheap to draw */
    const int cx = MK_LAYOUT::W / 2, cy = 104, r = 26;
    lcd.drawCircle(cx, cy, r,     accent);
    lcd.drawCircle(cx, cy, r - 1, accent);
    if (v.playing) {                 /* pause = two bars */
        lcd.fillRect(cx - 8, cy - 11, 5, 22, accent);
        lcd.fillRect(cx + 3, cy - 11, 5, 22, accent);
    } else {                         /* play = triangle */
        lcd.fillTriangle(cx - 7, cy - 12, cx - 7, cy + 12, cx + 12, cy, accent);
    }

    /* progress bar + time */
    const int py = 150, bx = MK_LAYOUT::PAD, bw2 = MK_LAYOUT::W - bx - 16;
    lcd.fillRoundRect(bx, py - 3, bw2, 6, 3, (uint32_t)MK_PAL::ITEM_BG);
    int fw = (v.durSec > 0) ? (int)((long)bw2 * v.posSec / v.durSec) : 0;
    if (fw > 0) lcd.fillRoundRect(bx, py - 3, fw, 6, 3, accent);
    lcd.fillCircle(bx + fw, py, 5, (uint32_t)MK_PAL::WHITE);

    char t0[8], t1[8];
    fmtTime(t0, sizeof(t0), v.posSec);
    fmtTime(t1, sizeof(t1), v.durSec);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(bx, py + 10);
    lcd.printf("%s / %s", t0, t1);

    /* volume slider */
    const int vy = 178, vx = 74, vw = MK_LAYOUT::W - 74 - 40;
    drawOutIcon(lcd, MK_LAYOUT::PAD + 8, vy + 3, v.ampOn, (uint32_t)MK_PAL::TEXT_SEC);
    lcd.fillRoundRect(vx, vy, vw, 6, 3, (uint32_t)MK_PAL::ITEM_BG);
    int vf = v.volMax > 0 ? vw * v.vol / v.volMax : 0;
    if (vf > 0) lcd.fillRoundRect(vx, vy, vf, 6, 3, accent);
    lcd.fillCircle(vx + vf, vy + 3, 6, (uint32_t)MK_PAL::WHITE);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(vx + vw + 8, vy + 12);
    lcd.printf("%d", v.vol);

    if (v.diag) {
        lcd.setTextColor((uint32_t)MK_PAL::ACCENT_DIM, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 198);
        lcd.printf("%s", v.diag);
    }

    MK_TUI::drawFooter(lcd, v.playing ? "Pause" : "Play", "Menu");
}

/* ── Menu screen (song list + options) ── */
template<typename LCD>
static inline void drawMenu(LCD& lcd, const MenuView& m)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, m.title);

    lcd.setFont(&fonts::efontCN_16);
    const int rows = 8;
    int y = MK_LAYOUT::CONTENT_Y + 6;
    if (m.count == 0) {
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y + 20);
        lcd.printf("No music in /music (.mp3)");
    }
    for (int r = 0; r < rows; r++) {
        int i = m.top + r;
        if (i >= m.count) break;
        bool sel = (i == m.sel);
        if (sel) lcd.fillRoundRect(4, y - 2, MK_LAYOUT::W - 8, 20, 3, (uint32_t)MK_PAL::ITEM_BG);
        lcd.setTextColor(sel ? (uint32_t)MK_PAL::ACCENT : (uint32_t)MK_PAL::TEXT_PRI,
                         sel ? (uint32_t)MK_PAL::ITEM_BG : (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s %.34s", sel ? ">" : " ", m.items[i]);
        y += 22;
    }
    MK_TUI::drawFooter(lcd, "Select", "Back");
}

} /* namespace MeowPlayer */
