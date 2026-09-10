/**
 * @file  rogue_ui.h
 * @brief Rogue Radar UI — Evil-Twin scan view + beacon-flood monitor view.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"
#include "beacon_flood.h"

namespace RogueUI {

/* One SSID summarised across the scan (for evil-twin detection). */
struct TwinRow {
    char    ssid[33];
    uint8_t bssids;    /* distinct BSSIDs advertising this SSID */
    bool    open;      /* seen as an open network              */
    bool    secure;    /* seen as a secured network            */
};
static inline bool isTwin(const TwinRow& r) { return r.open && r.secure; }

/* ── Evil-Twin scan view ── */
template<typename LCD>
static inline void drawTwins(LCD& lcd, const TwinRow* rows, int n, int sel, int scroll,
                             bool scanning, int flagged)
{
    MK_TUI::clearScreen(lcd);
    char hdr[28];
    snprintf(hdr, sizeof(hdr), flagged > 0 ? "Evil-Twin: %d!" : "Evil-Twin Scan", flagged);
    MK_TUI::drawHeader(lcd, hdr);

    if (scanning) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor(MK_PAL::ACCENT, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("Scanning...");
        MK_TUI::drawFooter(lcd, "Rescan", "Flood");
        return;
    }
    if (n <= 0) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("No networks found");
        MK_TUI::drawFooter(lcd, "Rescan", "Flood");
        return;
    }

    const int rows_vis = MK_LAYOUT::CONTENT_ROWS;
    for (int i = 0; i < rows_vis; i++) {
        int idx = scroll + i;
        if (idx >= n) break;
        const TwinRow& r = rows[idx];
        const char* tag = isTwin(r) ? "TWIN!" : (r.bssids >= 3 ? "multi" : "");
        char val[16];
        snprintf(val, sizeof(val), "x%u %s", r.bssids, tag);
        const char* name = r.ssid[0] ? r.ssid : "<hidden>";
        /* Reuse the menu-row look; twins get an accent selection stripe. */
        MK_TUI::drawMenuItem(lcd, i, name, val, idx == sel);
        if (isTwin(r) && idx != sel) {
            int y = MK_LAYOUT::CONTENT_Y + i * MK_LAYOUT::ITEM_H;
            lcd.fillRect(0, y, 3, MK_LAYOUT::ITEM_H, (uint32_t)MK_PAL::ERR);
        }
    }
    MK_TUI::drawFooter(lcd, "Rescan", "Flood");
}

/* ── Beacon-flood (Karma) monitor view ── */
struct FloodView {
    FloodStats      s;
    bool            running = true;
    const uint16_t* hist = nullptr;
    int             histLen = 0;
    int             threshold = BeaconFlood::ALERT_UNIQ;
};

template<typename LCD>
static inline void drawFlood(LCD& lcd, const FloodView& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Beacon Flood");

    const int by = MK_LAYOUT::HDR_H + 6, bh = 36;
    uint32_t bg = v.s.alert ? MK_PAL::ERR : (v.running ? MK_PAL::ACCENT_DARK : MK_PAL::ITEM_BG);
    uint32_t fg = v.s.alert ? MK_PAL::WHITE : (v.running ? MK_PAL::OK : MK_PAL::TEXT_SEC);
    lcd.fillRoundRect(8, by, MK_LAYOUT::W - 16, bh, 6, bg);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(fg, bg);
    lcd.setTextSize(2);
    lcd.setCursor(20, by + 6);
    lcd.printf("%s", !v.running ? "PAUSED" : (v.s.alert ? "FLOOD!" : "CLEAR"));
    lcd.setTextSize(1);
    if (v.running && v.s.alert) {
        lcd.setTextColor(MK_PAL::WHITE, bg);
        lcd.setCursor(150, by + 12);
        lcd.printf("rogue AP flood");
    }

    int y = by + bh + 6;
    char buf[56];
    snprintf(buf, sizeof(buf), "APs/s %u  peak %u  probe %u",
             v.s.uniq, v.s.peak_uniq, v.s.proberesp);
    lcd.setTextColor(v.s.uniq >= (uint16_t)v.threshold ? MK_PAL::ERR : MK_PAL::ACCENT, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);
    y += 20;
    snprintf(buf, sizeof(buf), "CH%-2d  frames/s %u", v.s.channel, v.s.rate);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);

    const int gx = 10, gw = MK_LAYOUT::W - 20, gy = 152, gh = 54;
    lcd.drawRect(gx - 1, gy - 1, gw + 2, gh + 2, MK_PAL::BORDER);
    if (v.hist && v.histLen > 0) {
        uint16_t mx = 1;
        for (int i = 0; i < v.histLen; i++) if (v.hist[i] > mx) mx = v.hist[i];
        int bw = gw / v.histLen; if (bw < 1) bw = 1;
        for (int i = 0; i < v.histLen; i++) {
            int h = (int)((uint32_t)v.hist[i] * gh / mx);
            if (v.hist[i] > 0 && h < 2) h = 2;
            uint32_t c = v.hist[i] >= (uint16_t)v.threshold ? MK_PAL::ERR : MK_PAL::ACCENT;
            if (h > 0) lcd.fillRect(gx + i * bw, gy + gh - h, bw - 1, h, c);
        }
    }

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Twins");
}

} /* namespace RogueUI */
