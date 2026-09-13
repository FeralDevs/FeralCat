/**
 * @file splash_screen.cpp
 * @brief Lightweight boot splash — title + a progress bar tied to the real boot
 *        stages. Replaces the old 2 MB embedded GIF (and AnimatedGIF decoder).
 */
#include "splash_screen.h"
#include <LovyanGFX.hpp>
#include "bsp/config.h"       /* MEOWGOTCHI_FW_VERSION */

static constexpr int W = 320, H = 240;
static constexpr int BW = 220, BH = 12;
static constexpr int BX = (W - BW) / 2, BY = 176;

static uint32_t c_lime(LGFX_Class& l)  { return l.color888(0xC4, 0xEE, 0x1F); }
static uint32_t c_muted(LGFX_Class& l) { return l.color888(0x9A, 0xA3, 0x94); }

void SplashScreen::begin(LGFX_Class& lcd)
{
    lcd.setBrightness(0);
    lcd.fillScreen(0x0000);

    lcd.setFont(&fonts::efontCN_24);
    lcd.setTextColor(c_lime(lcd), 0x0000);
    int tw = lcd.textWidth("MeowGotchi");
    lcd.setCursor((W - tw) / 2, 86);
    lcd.print("MeowGotchi");

    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(lcd.color888(0xE9, 0xEF, 0xE2), 0x0000);
    tw = lcd.textWidth(MEOWGOTCHI_FW_VERSION);
    lcd.setCursor((W - tw) / 2, 118);
    lcd.print(MEOWGOTCHI_FW_VERSION);
    lcd.setTextColor(c_muted(lcd), 0x0000);
    tw = lcd.textWidth("Fork: Janud");
    lcd.setCursor((W - tw) / 2, 138);
    lcd.print("Fork: Janud");

    lcd.drawRoundRect(BX - 2, BY - 2, BW + 4, BH + 4, 4, lcd.color888(0x2A, 0x2D, 0x26));

    for (int b = 0; b <= 255; b += 16) { lcd.setBrightness((uint8_t)b); delay(6); }
    lcd.setBrightness(255);
}

void SplashScreen::step(LGFX_Class& lcd, int pct, const char* msg)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lcd.fillRoundRect(BX, BY, BW * pct / 100, BH, 3, c_lime(lcd));

    if (msg) {
        lcd.fillRect(0, BY + BH + 8, W, 20, 0x0000);   /* clear old label */
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor(c_muted(lcd), 0x0000);
        int tw = lcd.textWidth(msg);
        lcd.setCursor((W - tw) / 2, BY + BH + 10);
        lcd.print(msg);
    }
}

void SplashScreen::finish(LGFX_Class& lcd)
{
    lcd.fillRoundRect(BX, BY, BW, BH, 3, c_lime(lcd));
    delay(150);
    for (int b = 255; b >= 0; b -= 16) { lcd.setBrightness((uint8_t)b); delay(8); }
    lcd.setBrightness(0);
    lcd.fillScreen(0x0000);
}
