#pragma once
#include "bsp/porting/display.hpp"

/**
 * Lightweight boot splash whose progress bar tracks the real boot stages.
 *   begin()  — draw title/version + empty bar, fade in.
 *   step()   — advance the bar (0..100) as each boot stage completes.
 *   finish() — fill to 100 %, fade out to black for the launcher hand-off.
 */
struct SplashScreen {
    static void begin(LGFX_Class& lcd);
    static void step(LGFX_Class& lcd, int pct, const char* msg = nullptr);
    static void finish(LGFX_Class& lcd);
};
