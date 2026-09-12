/**
 * @file  tracker_ui.h
 * @brief Unwanted-tracker detector UI — status banner + tracker list (MK_TUI).
 *        Pure template render, shared by device and simulator.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"
#include "tracker_monitor.h"

namespace TrackerUI {

struct View {
    uint16_t nearby     = 0;
    uint16_t persistent = 0;
    bool     alert      = false;
    bool     running    = true;
    uint32_t now_ms     = 0;
};

static inline const char* type_name(uint8_t t)
{
    switch (t) {
        case TRK_APPLE:   return "AirTag / Find My";
        case TRK_TILE:    return "Tile";
        case TRK_SAMSUNG: return "Samsung SmartTag";
        default:          return "Tracker";
    }
}

template<typename LCD>
static inline void drawList(LCD& lcd, const View& v, const TrackerEntry* rows, int n)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Tracker Detector");

    /* ── Status banner ── */
    const int by = MK_LAYOUT::HDR_H + 6, bh = 40;
    uint32_t bg = v.alert ? MK_PAL::ERR : (v.running ? MK_PAL::ACCENT_DARK : MK_PAL::ITEM_BG);
    uint32_t fg = v.alert ? MK_PAL::WHITE : (v.running ? MK_PAL::OK : MK_PAL::TEXT_SEC);
    lcd.fillRoundRect(8, by, MK_LAYOUT::W - 16, bh, 6, bg);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(fg, bg);
    lcd.setTextSize(2);
    const char* msg = !v.running ? "PAUSED" : (v.alert ? "FOLLOWED!" : (v.nearby ? "NEARBY" : "CLEAR"));
    lcd.setCursor(20, by + 8);
    lcd.printf("%s", msg);
    lcd.setTextSize(1);
    if (v.running) {
        lcd.setTextColor(fg, bg);
        lcd.setCursor(180, by + 14);
        lcd.printf("%u seen  %u persist", v.nearby, v.persistent);
    }

    lcd.setFont(&fonts::efontCN_16);
    if (n <= 0) {
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("No trackers detected.");
        lcd.setCursor(MK_LAYOUT::PAD, 132);
        lcd.printf("AirTag / Tile / SmartTag show here.");
        MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Exit");
        return;
    }

    int y = by + bh + 10;
    const int rows_vis = 5;
    char buf[40];
    for (int i = 0; i < n && i < rows_vis; i++) {
        const TrackerEntry& e = rows[i];
        uint32_t seen_s   = (e.last_ms - e.first_ms) / 1000;
        bool persistent   = (e.last_ms - e.first_ms) > TrackerMonitor::PERSIST_MS &&
                            (v.now_ms - e.last_ms) < TrackerMonitor::RECENT_MS;

        lcd.setTextColor(persistent ? MK_PAL::ERR : MK_PAL::TEXT_PRI, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s", type_name(e.type));

        uint32_t rc = e.rssi > -55 ? MK_PAL::ERR : e.rssi > -75 ? MK_PAL::WARN : MK_PAL::TEXT_SEC;
        snprintf(buf, sizeof(buf), "%ddBm", e.rssi);
        lcd.setTextColor(rc, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::W - 56, y);
        lcd.printf("%s", buf);

        /* Duration seen (the stalking signal) + last MAC octet for reference. */
        if (seen_s >= 60) snprintf(buf, sizeof(buf), "seen %lum %lus", (unsigned long)(seen_s / 60), (unsigned long)(seen_s % 60));
        else              snprintf(buf, sizeof(buf), "seen %lus", (unsigned long)seen_s);
        lcd.setTextColor(persistent ? MK_PAL::ERR : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD + 8, y + 14);
        lcd.printf("%s%s", buf, persistent ? "  FOLLOWING" : "");
        y += 26;
    }

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Exit");
}

} /* namespace TrackerUI */
