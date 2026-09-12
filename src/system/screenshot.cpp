/**
 * @file screenshot.cpp
 * @brief On-device screenshot implementation (see screenshot.h).
 */
#include "screenshot.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <LovyanGFX.hpp>
#include "../bsp/button/Button.hpp"   /* Button_Class::PRESSED */

static bool s_latch = false;   /* debounces the chord to one shot per hold */
static int  s_next  = 0;       /* rolling filename index                   */

/* Up+Down held together = capture. The detectors ignore the joystick, so this
 * chord never disturbs their state (A/B would toggle pause/view). */
static bool chord_down(DEVICES* dev)
{
    return dev->button.Up.state()   == Button_Class::PRESSED &&
           dev->button.Down.state() == Button_Class::PRESSED;
}

/* Write an RGB565 sprite buffer (LGFX stores it byte-swapped) as a 24-bit BMP. */
static bool write_bmp(File& f, const uint16_t* fb, int W, int H)
{
    const int row = W * 3;
    const int pad = (4 - (row % 4)) % 4;
    const int img = (row + pad) * H;
    const int off = 54;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t fsz = off + img;   memcpy(&hdr[2],  &fsz, 4);
    uint32_t o   = off;         memcpy(&hdr[10], &o,   4);
    uint32_t ih  = 40;          memcpy(&hdr[14], &ih,  4);
    int32_t  w = W, h = H;      memcpy(&hdr[18], &w, 4); memcpy(&hdr[22], &h, 4);
    uint16_t planes = 1, bpp = 24; memcpy(&hdr[26], &planes, 2); memcpy(&hdr[28], &bpp, 2);
    if (f.write(hdr, 54) != 54) return false;

    uint8_t* line = (uint8_t*)malloc(row + pad);
    if (!line) return false;
    memset(line, 0, row + pad);
    bool ok = true;
    for (int y = H - 1; y >= 0 && ok; y--) {           /* BMP is bottom-up */
        for (int x = 0; x < W; x++) {
            uint16_t p = fb[y * W + x];
            p = (uint16_t)((p >> 8) | (p << 8));        /* un-swap to RGB565 */
            line[x*3+0] = (uint8_t)(( p        & 0x1F) << 3);   /* B */
            line[x*3+1] = (uint8_t)(((p >> 5)  & 0x3F) << 2);   /* G */
            line[x*3+2] = (uint8_t)(((p >> 11) & 0x1F) << 3);   /* R */
        }
        if ((int)f.write(line, row + pad) != row + pad) ok = false;
    }
    free(line);
    return ok;
}

bool screenshot_tui_tick(DEVICES* dev, void* sprite)
{
    if (!chord_down(dev)) { s_latch = false; return false; }
    if (s_latch) return false;             /* already captured this hold */
    s_latch = true;

    lgfx::LGFX_Sprite* spr = (lgfx::LGFX_Sprite*)sprite;
    if (!spr) return false;
    const uint16_t* fb = (const uint16_t*)spr->getBuffer();
    int W = spr->width(), H = spr->height();
    if (!fb || W <= 0 || H <= 0) return false;

    if (!SD_MMC.exists("/screenshots")) SD_MMC.mkdir("/screenshots");

    char path[40];
    for (; s_next < 10000; s_next++) {
        snprintf(path, sizeof(path), "/screenshots/shot_%04d.bmp", s_next);
        if (!SD_MMC.exists(path)) break;
    }
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
    bool ok = write_bmp(f, fb, W, H);
    f.close();
    if (ok) s_next++; else SD_MMC.remove(path);
    return ok;
}
