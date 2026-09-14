/**
 * @file tracker_ui.h
 * @brief Tracker list and selected-tag signal finder, shared with the simulator.
 *        Concentric rings express relative signal strength, never a bearing.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cstring>
#include "../app_common/mk_tui.h"
#include "tracker_monitor.h"

namespace TrackerUI {

struct View {
    uint16_t nearby = 0;
    uint16_t persistent = 0;
    bool alert = false;
    bool running = true;
    uint32_t now_ms = 0;
    int selected = 0;
    int first = 0;
    const char* error = nullptr;
    bool starting = false;
};

enum class SignalState { Live, Waiting, Lost, Paused };

struct RadarView {
    TrackerEntry target{};
    bool running = true;
    bool hasTarget = false;
    uint32_t now_ms = 0;
    SignalState state = SignalState::Waiting;
    int strength = 0;
    int trend = 0;
    bool trend_ready = false;
    int16_t history[48]{};
    uint8_t history_count = 0;
    uint32_t age_ms = 0;
    const char* error = nullptr;
    bool starting = false;
};

static inline const char* type_name(uint8_t t)
{
    switch (t) {
        case TRK_APPLE: return "Find My candidate";
        case TRK_TILE: return "Tile candidate";
        case TRK_SAMSUNG: return "Samsung candidate";
        default: return "Tracker candidate";
    }
}

static inline int strength_for(int rssi)
{
    const int value = (rssi + 95) * 100 / 60;
    return value < 0 ? 0 : value > 100 ? 100 : value;
}

template<typename LCD>
static inline void controls(LCD& lcd, bool radar, bool scanning, bool error = false)
{
    const int y = MK_LAYOUT::H - MK_LAYOUT::FTR_H;
    lcd.fillRect(0, y, MK_LAYOUT::W, MK_LAYOUT::FTR_H, MK_PAL::ACCENT_DARK);
    lcd.drawFastHLine(0, y, MK_LAYOUT::W, MK_PAL::ACCENT);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextSize(1);
    lcd.setTextColor(MK_PAL::ACCENT, MK_PAL::ACCENT_DARK);
    lcd.setCursor(8, y + 2);
    lcd.printf("A %s", error ? "Retry" : radar ? (scanning ? "Pause" : "Resume") : "Find");
    lcd.setTextColor(MK_PAL::TEXT_PRI, MK_PAL::ACCENT_DARK);
    if (radar) {
        lcd.setCursor(124, y + 2);
        lcd.printf("B List");
    }
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::ACCENT_DARK);
    lcd.setCursor(216, y + 2);
    lcd.printf("Hold B Exit");
}

template<typename LCD>
static inline void drawList(LCD& lcd, const View& v, const TrackerEntry* rows, int n)
{
    MK_TUI::clearScreen(lcd);
    lcd.setTextSize(1);
    MK_TUI::drawHeader(lcd, "Tracker Detect");
    if (!rows || n < 0) n = 0;
    const int selected = v.selected < 0 ? 0 : v.selected >= n ? n - 1 : v.selected;
    int first = v.first < 0 ? 0 : v.first;
    if (n > 0 && (first > selected || first + 4 <= selected)) first = (selected / 4) * 4;
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::ACCENT_DARK);
    lcd.setCursor(256, 4);
    lcd.printf("%d/%d", n > 0 ? selected + 1 : 0, n);

    const bool error = v.error && v.error[0];
    const char* status = error ? "SCAN ERROR" : v.starting ? "STARTING" :
        !v.running ? "PAUSED" : v.persistent ? "PERSISTENT" : v.nearby ? "NEARBY" : "SCANNING";
    const uint32_t color = error ? MK_PAL::ERR : v.starting ? MK_PAL::WARN :
        !v.running ? MK_PAL::TEXT_SEC : v.persistent ? MK_PAL::WARN : MK_PAL::ACCENT;
    lcd.fillRoundRect(8, 30, 304, 26, 5, MK_PAL::ITEM_BG);
    lcd.setTextColor(color, MK_PAL::ITEM_BG);
    lcd.setCursor(16, 35);
    lcd.printf("%s", status);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::ITEM_BG);
    lcd.setCursor(168, 35);
    lcd.printf("%u near / %u pers", v.nearby, v.persistent);

    if (n == 0) {
        lcd.setTextColor(MK_PAL::TEXT_PRI, MK_PAL::BLACK);
        lcd.setCursor(24, 88);
        lcd.printf(error ? "Scan unavailable." : "No trackers detected.");
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(24, 113);
        lcd.printf("AirTag / Find My, Tile,");
        lcd.setCursor(24, 131);
        lcd.printf("and SmartTag appear here.");
        lcd.setCursor(24, 163);
        lcd.printf(v.running || v.starting ? "Listening for BLE signals..." : "Press Left/Right to resume.");
    }

    for (int slot = 0; slot < 4 && first + slot < n; ++slot) {
        const int index = first + slot;
        const auto& e = rows[index];
        const int y = 63 + slot * 35;
        const bool active = index == selected;
        const bool fresh = v.running && !error && !v.starting && e.scan_fresh &&
            uint32_t(v.now_ms - e.last_ms) <= 2500;
        const bool persistent = uint32_t(e.last_ms - e.first_ms) > TrackerMonitor::PERSIST_MS &&
            uint32_t(v.now_ms - e.last_ms) < TrackerMonitor::RECENT_MS;
        const uint32_t bg = active ? MK_PAL::ACCENT_DARK : MK_PAL::ITEM_BG;
        lcd.fillRoundRect(8, y, 304, 32, 4, bg);
        if (active) {
            lcd.drawRoundRect(8, y, 304, 32, 4, MK_PAL::ACCENT);
            lcd.fillRect(8, y + 5, 3, 22, MK_PAL::ACCENT);
        }
        lcd.setTextColor(active ? MK_PAL::ACCENT : MK_PAL::TEXT_PRI, bg);
        lcd.setCursor(17, y + 1);
        lcd.printf("%s", type_name(e.type));
        lcd.setTextColor(MK_PAL::TEXT_SEC, bg);
        lcd.setCursor(17, y + 16);
        lcd.printf("%02X:%02X:%02X #%lu %s", e.mac[3], e.mac[4], e.mac[5],
            static_cast<unsigned long>(e.id),
            v.starting ? "wait" : !v.running ? "off" : !fresh ? "old" : persistent ? "pers" : "live");
        lcd.setTextColor(fresh ? MK_PAL::ACCENT : MK_PAL::TEXT_SEC, bg);
        lcd.setCursor(242, y + 1);
        lcd.printf("%d dBm", int(e.filtered_rssi));
        lcd.fillRect(242, y + 23, 59, 4, MK_PAL::BORDER);
        if (fresh) {
            const int width = strength_for(e.filtered_rssi) * 59 / 100;
            if (width > 0) lcd.fillRect(242, y + 23, width, 4, MK_PAL::ACCENT);
        }
    }
    lcd.setTextColor(error ? MK_PAL::ERR : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(8, 202);
    if (error) lcd.printf("%.38s", v.error);
    else lcd.printf("Up/Down select   L/R %s", v.running || v.starting ? "Pause" : "Resume");
    controls(lcd, false, v.running || v.starting, error);
}

template<typename LCD>
static inline void drawRadar(LCD& lcd, const RadarView& v)
{
    MK_TUI::clearScreen(lcd);
    lcd.setTextSize(1);
    MK_TUI::drawHeader(lcd, "Proximity Radar");
    if (v.hasTarget) {
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::ACCENT_DARK);
        lcd.setCursor(208, 4);
        lcd.printf("#%lu", static_cast<unsigned long>(v.target.id));
    }
    const bool error = v.error && v.error[0];
    const bool live = !error && !v.starting && v.running && v.hasTarget && v.target.scan_fresh &&
        v.state == SignalState::Live;
    const bool paused = !v.running || v.state == SignalState::Paused;
    const char* status = error ? "SCAN ERROR" : v.starting ? "STARTING" : paused ? "PAUSED" :
        !v.hasTarget ? "WAITING" : v.state == SignalState::Lost ? "LOST" : live ? "LIVE" : "WAITING";
    const uint32_t color = error ? MK_PAL::ERR : live ? MK_PAL::ACCENT :
        !paused ? MK_PAL::WARN : MK_PAL::TEXT_SEC;

    lcd.setTextColor(MK_PAL::TEXT_PRI, MK_PAL::BLACK);
    lcd.setCursor(8, 29);
    lcd.printf("%s", v.hasTarget ? type_name(v.target.type) : "Selected tracker");
    if (v.hasTarget) {
        lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
        lcd.setCursor(208, 29);
        lcd.printf("%02X:%02X:%02X", v.target.mac[3], v.target.mac[4], v.target.mac[5]);
    }

    // Every ring is concentric. There is no angular position, sweep or compass.
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(36, 48);
    lcd.printf("SIGNAL /100");
    const int strength = v.strength < 0 ? 0 : v.strength > 100 ? 100 : v.strength;
    const int radii[] = {50, 40, 30};
    for (int i = 0; i < 3; ++i) {
        const bool lit = live && strength >= (i + 1) * 25;
        const uint32_t ringColor = lit ? MK_PAL::ACCENT : MK_PAL::BORDER;
        lcd.drawCircle(80, 116, radii[i], ringColor);
        lcd.drawCircle(80, 116, radii[i] - 1, lit ? MK_PAL::ACCENT_DIM : MK_PAL::ITEM_BG);
    }
    lcd.setTextColor(live ? MK_PAL::ACCENT : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setTextSize(2);
    const int digits = strength >= 100 ? 3 : strength >= 10 ? 2 : 1;
    lcd.setCursor(live ? 80 - digits * 8 : 64, 100);
    if (live) lcd.printf("%d", strength);
    else lcd.printf("--");
    lcd.setTextSize(1);
    lcd.fillRoundRect(148, 51, 164, 24, 5, MK_PAL::ITEM_BG);
    lcd.setTextColor(color, MK_PAL::ITEM_BG);
    lcd.setCursor(158, 55);
    lcd.printf("%s", status);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(148, 82);
    lcd.printf("FILTERED RSSI");
    lcd.setTextColor(live ? MK_PAL::TEXT_PRI : MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(148, 96);
    if (live) lcd.printf("%d", int(v.target.filtered_rssi));
    else lcd.printf("--");
    lcd.setTextSize(1);
    lcd.setCursor(216, 110);
    lcd.printf("dBm");
    lcd.setTextColor(color, MK_PAL::BLACK);
    lcd.setCursor(148, 135);
    if (error) lcd.printf("%.17s%s", v.error, std::strlen(v.error) > 17 ? "..." : "");
    else if (v.starting) lcd.printf("Starting BLE scan");
    else if (paused) lcd.printf("Scan is paused");
    else if (!live) lcd.printf("No fresh signal");
    else if (!v.trend_ready) lcd.printf("Sampling trend...");
    else lcd.printf("%s", v.trend > 0 ? "STRONGER" : v.trend < 0 ? "WEAKER" : "STEADY");
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(148, 153);
    if (!v.hasTarget) lcd.printf("Waiting for target");
    else if (v.age_ms < 1000) lcd.printf("Seen <1s ago");
    else if (v.age_ms < 60000) lcd.printf("Last %lus ago", (unsigned long)(v.age_ms / 1000));
    else lcd.printf("Last %lum ago", (unsigned long)(v.age_ms / 60000));

    // Fixed 48-sample window, right aligned. Blank space has no samples.
    // Old samples remain visible in gray while waiting, lost or paused.
    lcd.fillRoundRect(8, 175, 304, 25, 4, MK_PAL::ITEM_BG);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::ITEM_BG);
    lcd.setCursor(13, 180);
    lcd.printf("RSSI");
    lcd.drawFastHLine(50, 187, 255, MK_PAL::BORDER);
    const int count = v.history_count > 48 ? 48 : v.history_count;
    int previousX = 0, previousY = 0;
    for (int i = 0; i < count; ++i) {
        const int x = 50 + (48 - count + i) * 254 / 47;
        const int y = 196 - strength_for(v.history[i]) * 18 / 100;
        const uint32_t lineColor = live ? MK_PAL::ACCENT : MK_PAL::TEXT_SEC;
        if (i) lcd.drawLine(previousX, previousY, x, y, lineColor);
        else lcd.fillRect(x, y, 2, 2, lineColor);
        previousX = x;
        previousY = y;
    }
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(8, 202);
    lcd.printf("Signal only - no distance");
    controls(lcd, true, v.running || v.starting, error);
}

} // namespace TrackerUI
