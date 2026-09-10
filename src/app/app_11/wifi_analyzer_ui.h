/**
 * @file  wifi_analyzer_ui.h
 * @brief WiFi Analyzer — AP list + per-network detail, drawn with MK_TUI.
 *
 * Pure rendering (template<LCD> + plain view-models), shared by the device app
 * and the desktop TUI simulator.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cstring>
#include "../app_common/mk_tui.h"

namespace WifiAnalyzer {

struct Row {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    bool    encrypted;
    uint8_t auth;
    uint8_t bssid[6];
};

/* rssi(dBm) → 0..100 signal quality */
static inline int signalPct(int8_t rssi) {
    int p = 2 * (rssi + 100);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

static inline const char* authStr(uint8_t a) {
    switch (a) {
        case 0:  return "Open";      /* WIFI_AUTH_OPEN */
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA/2";
        case 5:  return "WPA2-EAP";
        case 6:  return "WPA3";
        case 7:  return "WPA2/3";
        default: return "WPA?";
    }
}

/* ── AP list ── */
template<typename LCD>
static inline void drawList(LCD& lcd, const Row* rows, int count,
                            int sel, int scroll, bool scanning)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "WiFi Analyzer");

    if (scanning) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("Scanning 2.4GHz...");
        MK_TUI::drawFooter(lcd, "", "");
        return;
    }
    if (count <= 0) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 110);
        lcd.printf("No networks found");
        MK_TUI::drawFooter(lcd, "Rescan", "Exit");
        return;
    }

    const int rows_vis = MK_LAYOUT::CONTENT_ROWS;
    for (int i = 0; i < rows_vis; i++) {
        int idx = scroll + i;
        if (idx >= count) break;
        const Row& r = rows[idx];
        char val[16];
        snprintf(val, sizeof(val), "C%-2d %d%%%s",
                 r.channel, signalPct(r.rssi), r.encrypted ? "*" : " ");
        const char* name = r.ssid[0] ? r.ssid : "<hidden>";
        MK_TUI::drawMenuItem(lcd, i, name, val, idx == sel);
    }
    MK_TUI::drawFooter(lcd, "Details", "Rescan");
}

/* ── Detail for one AP ── */
template<typename LCD>
static inline void drawDetail(LCD& lcd, const Row& r)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, r.ssid[0] ? r.ssid : "<hidden>");

    /* Left-aligned label/value columns (value at x=98) so a full MAC fits. */
    const int LX = MK_LAYOUT::PAD, VX = 98;
    auto field = [&](int yy, const char* lbl, const char* val, uint32_t vc) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(LX, yy); lcd.printf("%s", lbl);
        lcd.setTextColor(vc, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(VX, yy); lcd.printf("%s", val);
    };

    char buf[40];
    int y = MK_LAYOUT::CONTENT_Y + 10;
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             r.bssid[0], r.bssid[1], r.bssid[2], r.bssid[3], r.bssid[4], r.bssid[5]);
    field(y, "BSSID", buf, MK_PAL::TEXT_PRI);                          y += 28;
    snprintf(buf, sizeof(buf), "%d  (2.4 GHz)", r.channel);
    field(y, "Channel", buf, MK_PAL::TEXT_PRI);                        y += 28;
    int pct = signalPct(r.rssi);
    snprintf(buf, sizeof(buf), "%d dBm  %d%%", r.rssi, pct);
    uint32_t sc = pct >= 60 ? MK_PAL::OK : pct >= 35 ? MK_PAL::WARN : MK_PAL::ERR;
    field(y, "Signal", buf, sc);                                      y += 28;
    field(y, "Security", authStr(r.auth),
          r.auth == 0 ? MK_PAL::ERR : MK_PAL::ACCENT);                y += 28;

    MK_TUI::drawFooter(lcd, "", "Back");
}

} /* namespace WifiAnalyzer */
