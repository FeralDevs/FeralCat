/**
 * @file  ble_spam_ui.h
 * @brief BLE Spam Detector UI — status + rate + per-vector breakdown + graph.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"

namespace BleSpamUI {

struct View {
    uint32_t        total     = 0;
    uint16_t        rate      = 0;
    uint16_t        peak      = 0;
    bool            alert     = false;
    bool            running   = true;
    uint16_t        apple     = 0;
    uint16_t        google    = 0;
    uint16_t        ms        = 0;
    uint16_t        samsung   = 0;
    const uint16_t* hist      = nullptr;
    int             histLen   = 0;
    int             threshold = 12;
};

template<typename LCD>
static inline void draw(LCD& lcd, const View& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "BLE Spam Detect");

    const int by = MK_LAYOUT::HDR_H + 6, bh = 36;
    uint32_t bg = v.alert ? MK_PAL::ERR : (v.running ? MK_PAL::ACCENT_DARK : MK_PAL::ITEM_BG);
    uint32_t fg = v.alert ? MK_PAL::WHITE : (v.running ? MK_PAL::OK : MK_PAL::TEXT_SEC);
    lcd.fillRoundRect(8, by, MK_LAYOUT::W - 16, bh, 6, bg);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(fg, bg);
    lcd.setTextSize(2);
    lcd.setCursor(20, by + 6);
    lcd.printf("%s", !v.running ? "PAUSED" : (v.alert ? "ALERT!" : "CLEAR"));
    lcd.setTextSize(1);
    if (v.running && v.alert) {
        lcd.setTextColor(MK_PAL::WHITE, bg);
        lcd.setCursor(150, by + 12);
        lcd.printf("BLE spam flood!");
    }

    int y = by + bh + 6;
    char buf[56];
    snprintf(buf, sizeof(buf), "now %u/s  peak %u  total %lu",
             v.rate, v.peak, (unsigned long)v.total);
    lcd.setTextColor(v.rate >= (uint16_t)v.threshold ? MK_PAL::ERR : MK_PAL::ACCENT, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);
    y += 20;
    snprintf(buf, sizeof(buf), "Apple:%u Goog:%u MS:%u Sam:%u",
             v.apple, v.google, v.ms, v.samsung);
    lcd.setTextColor(MK_PAL::TEXT_SEC, MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);

    /* Graph */
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

    MK_TUI::drawFooter(lcd, v.running ? "Pause" : "Start", "Exit");
}

} /* namespace BleSpamUI */
