/**
 * @file  meowgotchi_ui.h
 * @brief MeowGotchi — pwnagotchi-style WiFi-hunter face + stats, drawn with
 *        MK_TUI / LovyanGFX primitives.
 *
 * Pure rendering: every function takes an LCD reference and a plain view-model,
 * so the exact same code renders on the device (device->Lcd) and in the desktop
 * TUI simulator (an off-screen sprite). No hardware, no globals.
 */
#pragma once
#include <LovyanGFX.hpp>
#include <cstdio>
#include "../app_common/mk_tui.h"

namespace MeowGotchi {

/* Hunter mood — drives the cat's expression and default speech line. */
enum class Mood : uint8_t {
    Sleep,    /* radio idle / nothing around      */
    Bored,    /* traffic seen but nothing new     */
    Hunt,     /* actively finding new APs/clients  */
    Excited,  /* just captured a handshake!        */
    Cool,     /* aggressive (deauth) mode running  */
    Sad,      /* error, e.g. no SD card for pcaps  */
};

/* Everything the face screen needs to draw one frame. */
struct View {
    Mood        mood      = Mood::Sleep;
    uint8_t     channel   = 1;      /* 1..13                    */
    uint16_t    aps       = 0;      /* unique APs seen          */
    uint16_t    stas      = 0;      /* unique clients seen      */
    uint16_t    shakes    = 0;      /* handshakes captured      */
    uint16_t    deauths   = 0;      /* deauth frames sent/seen  */
    uint32_t    uptime_s  = 0;
    bool        aggressive= false;
    bool        blink     = false;  /* eye-blink animation frame*/
    const char* line      = nullptr;/* speech; null → mood default */
};

static inline const char* moodLine(Mood m) {
    switch (m) {
        case Mood::Sleep:   return "zzz... nothing here";
        case Mood::Bored:   return "meh, same old air";
        case Mood::Hunt:    return "ooh, who's this?";
        case Mood::Excited: return "got a handshake! >w<";
        case Mood::Cool:    return "deauth engaged >:3";
        case Mood::Sad:     return "no SD - can't save :(";
    }
    return "";
}

/* ── Cat face, centred on (cx,cy). Expression varies by mood. ── */
template<typename LCD>
static inline void drawCat(LCD& lcd, int cx, int cy, Mood mood, bool blink)
{
    const uint32_t body = MK_PAL::ACCENT;      /* MeowKit green   */
    const uint32_t dark = MK_PAL::ACCENT_DIM;
    const uint32_t blk  = MK_PAL::BLACK;
    const uint32_t wht  = MK_PAL::WHITE;

    /* Ears (triangles) */
    lcd.fillTriangle(cx-40, cy-18, cx-40, cy-52, cx-12, cy-30, body);
    lcd.fillTriangle(cx+40, cy-18, cx+40, cy-52, cx+12, cy-30, body);
    lcd.fillTriangle(cx-34, cy-24, cx-34, cy-44, cx-18, cy-31, dark);
    lcd.fillTriangle(cx+34, cy-24, cx+34, cy-44, cx+18, cy-31, dark);

    /* Head */
    lcd.fillRoundRect(cx-42, cy-24, 84, 66, 22, body);

    /* Whiskers */
    for (int i = -1; i <= 1; i++) {
        lcd.drawLine(cx-24, cy+8 + i*6, cx-56, cy+2 + i*10, dark);
        lcd.drawLine(cx+24, cy+8 + i*6, cx+56, cy+2 + i*10, dark);
    }

    /* Eyes — position */
    const int ex = 17, ey = -2;
    auto eyeL = [&](void){ };  /* placeholder to keep symmetry readable */
    (void)eyeL;

    if (blink && mood != Mood::Sleep) {
        lcd.drawFastHLine(cx-ex-8, cy+ey, 16, blk);
        lcd.drawFastHLine(cx+ex-8, cy+ey, 16, blk);
    } else switch (mood) {
        case Mood::Sleep:
            /* gentle closed arcs */
            lcd.drawLine(cx-ex-8, cy+ey, cx-ex, cy+ey+4, blk);
            lcd.drawLine(cx-ex, cy+ey+4, cx-ex+8, cy+ey, blk);
            lcd.drawLine(cx+ex-8, cy+ey, cx+ex, cy+ey+4, blk);
            lcd.drawLine(cx+ex, cy+ey+4, cx+ex+8, cy+ey, blk);
            break;
        case Mood::Excited:
            /* wide sparkly eyes */
            lcd.fillCircle(cx-ex, cy+ey, 8, wht);
            lcd.fillCircle(cx+ex, cy+ey, 8, wht);
            lcd.fillCircle(cx-ex, cy+ey, 4, blk);
            lcd.fillCircle(cx+ex, cy+ey, 4, blk);
            lcd.drawPixel(cx-ex+2, cy+ey-2, wht);
            lcd.drawPixel(cx+ex+2, cy+ey-2, wht);
            break;
        case Mood::Cool:
            /* sunglasses bar */
            lcd.fillRoundRect(cx-ex-11, cy+ey-6, 22, 12, 3, blk);
            lcd.fillRoundRect(cx+ex-11, cy+ey-6, 22, 12, 3, blk);
            lcd.drawFastHLine(cx-6, cy+ey, 12, blk);
            break;
        case Mood::Hunt:
            /* focused narrow eyes */
            lcd.fillRoundRect(cx-ex-7, cy+ey-2, 14, 6, 2, blk);
            lcd.fillRoundRect(cx+ex-7, cy+ey-2, 14, 6, 2, blk);
            break;
        case Mood::Sad:
            lcd.fillCircle(cx-ex, cy+ey+2, 5, blk);
            lcd.fillCircle(cx+ex, cy+ey+2, 5, blk);
            lcd.drawLine(cx-ex-8, cy+ey-6, cx-ex+2, cy+ey-3, dark);
            lcd.drawLine(cx+ex-2, cy+ey-3, cx+ex+8, cy+ey-6, dark);
            break;
        case Mood::Bored:
        default:
            lcd.fillCircle(cx-ex, cy+ey, 5, blk);
            lcd.fillCircle(cx+ex, cy+ey, 5, blk);
            lcd.drawFastHLine(cx-ex-7, cy+ey-6, 14, dark);
            lcd.drawFastHLine(cx+ex-7, cy+ey-6, 14, dark);
            break;
    }

    /* Nose */
    lcd.fillTriangle(cx-4, cy+12, cx+4, cy+12, cx, cy+17, dark);

    /* Mouth — cat "w" */
    if (mood == Mood::Excited) {
        lcd.drawLine(cx, cy+17, cx-8, cy+26, blk);
        lcd.drawLine(cx, cy+17, cx+8, cy+26, blk);
        lcd.drawFastHLine(cx-8, cy+26, 16, blk);   /* open happy mouth */
    } else if (mood == Mood::Sad) {
        lcd.drawLine(cx-7, cy+26, cx, cy+21, blk);
        lcd.drawLine(cx, cy+21, cx+7, cy+26, blk);
    } else {
        lcd.drawLine(cx, cy+17, cx-6, cy+23, blk);
        lcd.drawLine(cx, cy+17, cx+6, cy+23, blk);
    }
}

/* ── Small stat pill under the header ── */
template<typename LCD>
static inline void drawStatBar(LCD& lcd, const View& v)
{
    const int y = MK_LAYOUT::HDR_H + 6;
    lcd.setFont(&fonts::efontCN_16);
    char buf[64];
    snprintf(buf, sizeof(buf), "CH%-2d  AP:%-3d  STA:%-3d", v.channel, v.aps, v.stas);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, y);
    lcd.printf("%s", buf);

    /* handshake ("pwned") counter, right-aligned and bright */
    snprintf(buf, sizeof(buf), "HS:%d", v.shakes);
    lcd.setTextColor(v.shakes ? (uint32_t)MK_PAL::OK : (uint32_t)MK_PAL::TEXT_SEC,
                     (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::W - 64, y);
    lcd.printf("%s", buf);
}

/* ── Speech line in a rounded bubble ── */
template<typename LCD>
static inline void drawSpeech(LCD& lcd, const char* line)
{
    const int by = 150, bh = 24;
    lcd.fillRoundRect(16, by, MK_LAYOUT::W - 32, bh, 6, (uint32_t)MK_PAL::ITEM_BG);
    lcd.drawRoundRect(16, by, MK_LAYOUT::W - 32, bh, 6, (uint32_t)MK_PAL::ACCENT_DIM);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::WHITE, (uint32_t)MK_PAL::ITEM_BG);
    lcd.setCursor(26, by + 4);
    lcd.printf("%s", line ? line : "");
}

/* ── Full face screen ── */
template<typename LCD>
static inline void drawFace(LCD& lcd, const View& v)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, v.aggressive ? "MeowGotchi *" : "MeowGotchi");
    drawStatBar(lcd, v);
    drawCat(lcd, MK_LAYOUT::W / 2, 96, v.mood, v.blink);
    drawSpeech(lcd, v.line ? v.line : moodLine(v.mood));

    /* uptime + mode line */
    lcd.setFont(&fonts::efontCN_16);
    char up[24];
    snprintf(up, sizeof(up), "up %lum%02lus",
             (unsigned long)(v.uptime_s / 60), (unsigned long)(v.uptime_s % 60));
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 182);
    lcd.printf("%s", up);
    lcd.setTextColor(v.aggressive ? (uint32_t)MK_PAL::ERR : (uint32_t)MK_PAL::TEXT_SEC,
                     (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::W - 120, 182);
    lcd.printf("%s", v.aggressive ? "AGGRESSIVE" : "passive");

    MK_TUI::drawFooter(lcd, "Menu", "Exit");
}

} /* namespace MeowGotchi */
