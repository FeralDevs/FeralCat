/**
 * @file  deauth_ui.h
 * @brief Deauth Detector UI — status banner + rate + 30 s bar graph (MK_TUI).
 *        Pure template render, shared by device and simulator.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"

namespace DeauthUI {

struct View {
    uint8_t         channel   = 1;
    uint32_t        total     = 0;
    uint16_t        rate      = 0;
    uint16_t        peak      = 0;
    bool            alert     = false;
    bool            running   = true;
    uint32_t        uptime_s  = 0;
    const uint16_t* hist      = nullptr;
    int             histLen   = 0;
    int             threshold = 6;
};

template<typename LCD>
static inline void draw(LCD& lcd, const View& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Deauth Detector");

    /* ── Status banner ── */
    const int by = MK_LAYOUT::HDR_H + 6, bh = 40;
    uint32_t bg  = v.alert ? MK_PAL::ERR : (v.running ? MK_PAL::ACCENT_DARK : MK_PAL::ITEM_BG);
    uint32_t fg  = v.alert ? MK_PAL::WHITE : (v.running ? MK_PAL::OK : MK_PAL::TEXT_SEC);
    lcd.fillRoundRect(8, by, MK_LAYOUT::W - 16, bh, 6, bg);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(fg, bg);
    lcd.setTextSize(2);
    const char* msg = !v.running ? "PAUSED" : (v.alert ? "ALERT!" : "CLEAR");
    lcd.setCursor(20, by + 8);
    lcd.printf("%s", msg);
    lcd.setTextSize(1);
    if (v.running && v.alert) {
        lcd.setTextColor(MK_PAL::WHITE, bg);
        lcd.setCursor(150, by + 14);
        lcd.printf("deauth burst!");
    }

    /* ── Stats line ── */
    int y = by + bh + 8;
    char buf[48];
    snprintf(buf, sizeof(buf), "CH%-2d  total %lu  peak %u/s",
             v.channel, (unsigned long)v.total, v.peak);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);
    y += 20;
    snprintf(buf, sizeof(buf), "now %u /s", v.rate);
    lcd.setTextColor(v.rate >= (uint16_t)v.threshold ? MK_PAL::ERR : MK_PAL::ACCENT, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);

    /* ── History bar graph ── */
    const int gx = 10, gw = MK_LAYOUT::W - 20;
    const int gy = 150, gh = 60;
    lcd.drawRect(gx - 1, gy - 1, gw + 2, gh + 2, MK_PAL::BORDER);
    if (v.hist && v.histLen > 0) {
        uint16_t mx = 1;
        for (int i = 0; i < v.histLen; i++) if (v.hist[i] > mx) mx = v.hist[i];
        int bw = gw / v.histLen;
        if (bw < 1) bw = 1;
        for (int i = 0; i < v.histLen; i++) {
            int h = (int)((uint32_t)v.hist[i] * gh / mx);
            if (v.hist[i] > 0 && h < 2) h = 2;
            int x = gx + i * bw;
            uint32_t c = v.hist[i] >= (uint16_t)v.threshold ? MK_PAL::ERR : MK_PAL::ACCENT;
            if (h > 0) lcd.fillRect(x, gy + gh - h, bw - 1, h, c);
        }
    }
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(gx, gy + gh + 3);
    lcd.printf("last %ds", v.histLen);

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Log");
}

/* ── Attacker log view (who is sending the deauths) ── */
template<typename LCD>
static inline void drawLog(LCD& lcd, const AttackerEntry* rows, int n, bool running)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Attacker Log");
    lcd.setFont(&fonts::efontCN_16);

    if (n <= 0) {
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 100);
        lcd.printf("No deauth sources seen yet.");
        lcd.setCursor(MK_LAYOUT::PAD, 122);
        lcd.printf("RSSI shows attacker proximity.");
        MK_TUI::drawFooter(lcd, running ? "Pause" : "Start", "Graph");
        return;
    }

    int y = MK_LAYOUT::CONTENT_Y + 6;
    const int rows_vis = 6;
    char buf[40];
    for (int i = 0; i < n && i < rows_vis; i++) {
        const AttackerEntry& e = rows[i];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
        lcd.setTextColor(MK_PAL::TEXT_PRI, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s", buf);
        /* count + rssi (closer = brighter) */
        uint32_t rc = e.rssi > -50 ? MK_PAL::ERR : e.rssi > -70 ? MK_PAL::WARN : MK_PAL::TEXT_SEC;
        snprintf(buf, sizeof(buf), "x%u", e.count);
        lcd.setTextColor(MK_PAL::ACCENT, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::W - 96, y);
        lcd.printf("%s", buf);
        snprintf(buf, sizeof(buf), "%ddBm", e.rssi);
        lcd.setTextColor(rc, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::W - 56, y);
        lcd.printf("%s", buf);
        y += 20;
    }

    MK_TUI::drawFooter(lcd, running ? "Pause" : "Start", "Graph");
}

} /* namespace DeauthUI */
