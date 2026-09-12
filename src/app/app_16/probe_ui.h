/**
 * @file  probe_ui.h
 * @brief Probe-request sniffer UI — device list + probes/sec graph (MK_TUI).
 *        Pure template render, shared by device and simulator.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"
#include "probe_monitor.h"

namespace ProbeUI {

struct View {
    uint8_t         channel  = 1;
    uint32_t        total    = 0;
    uint16_t        rate     = 0;
    uint16_t        peak     = 0;
    uint16_t        devices  = 0;
    bool            running  = true;
    const uint16_t* hist     = nullptr;
    int             histLen  = 0;
};

/* Small stats strip under the header, shared by both views. */
template<typename LCD>
static inline int drawStats(LCD& lcd, const View& v)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "CH%-2d  %u dev  %lu probes",
             v.channel, v.devices, (unsigned long)v.total);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(v.running ? MK_PAL::ACCENT : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 6);
    lcd.printf("%s", buf);
    return MK_LAYOUT::CONTENT_Y + 28;
}

/* ── Main view: list of probing devices (MAC → requested SSID) ── */
template<typename LCD>
static inline void drawList(LCD& lcd, const View& v, const ProbeEntry* rows, int n)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Probe Sniffer");
    int y = drawStats(lcd, v);
    lcd.setFont(&fonts::efontCN_16);

    if (n <= 0) {
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("Listening for probe requests...");
        lcd.setCursor(MK_LAYOUT::PAD, 132);
        lcd.printf("Nearby devices will appear here.");
        MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Graph");
        return;
    }

    const int rows_vis = 7;
    char buf[40];
    for (int i = 0; i < n && i < rows_vis; i++) {
        const ProbeEntry& e = rows[i];
        /* Line 1 field: MAC + hit count + rssi color-coded by proximity. */
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
        lcd.setTextColor(MK_PAL::TEXT_PRI, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s", buf);

        uint32_t rc = e.rssi > -50 ? MK_PAL::OK : e.rssi > -70 ? MK_PAL::WARN : MK_PAL::TEXT_SEC;
        snprintf(buf, sizeof(buf), "x%u %ddBm", e.count, e.rssi);
        lcd.setTextColor(rc, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::W - 96, y);
        lcd.printf("%s", buf);

        /* Requested SSID (or a note when only wildcard probes were seen). */
        lcd.setTextColor(e.ssid[0] ? MK_PAL::ACCENT : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD + 8, y + 14);
        lcd.printf("%s", e.ssid[0] ? e.ssid : "(broadcast)");
        y += 26;
    }

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Graph");
}

/* ── Secondary view: probes/sec bar graph ── */
template<typename LCD>
static inline void drawGraph(LCD& lcd, const View& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Probe Sniffer");
    int y = drawStats(lcd, v);

    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(MK_PAL::ACCENT, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("now %u /s   peak %u /s", v.rate, v.peak);

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
            if (h > 0) lcd.fillRect(x, gy + gh - h, bw - 1, h, MK_PAL::ACCENT);
        }
    }
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(gx, gy + gh + 3);
    lcd.printf("last %ds", v.histLen);

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "List");
}

} /* namespace ProbeUI */
