/**
 * @file screenshot.h
 * @brief On-device screenshots for the MK_TUI (LovyanGFX) app screens.
 *
 * The ST7789 panel has no read line, so the framebuffer can't be read back —
 * but the hacker apps double-buffer through an off-screen sprite that holds
 * exactly what's on screen. Holding the joystick Up+Down together saves that
 * sprite to /screenshots/shot_NNNN.bmp on the SD card.
 *
 * Usage: call once per frame from a MK_TUI app that owns a canvas sprite:
 *   if (_haveCanvas) screenshot_tui_tick(_device, _canvas);
 */
#pragma once
#include "../bsp/devices.h"

/**
 * Check the A+B screenshot chord and, if it just fired, save `sprite` to the SD
 * card. `sprite` is the app's lgfx::LGFX_Sprite* (taken as void* so this header
 * doesn't pull in LovyanGFX). Returns true when a screenshot was written.
 */
bool screenshot_tui_tick(DEVICES* dev, void* sprite);
