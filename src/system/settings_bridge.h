/**
 * @file settings_bridge.h
 * @brief Bridge between UI settings widgets, NVS persistence, and hardware.
 *
 * Two-tier API:
 *
 *   sys_apply_*(val)       — Apply value to hardware only (no NVS write).
 *                            Use at boot when loading from persist_get_int().
 *
 *   settings_set_*(val)    — Apply to hardware AND schedule an NVS write.
 *                            Use from UI event callbacks (slider, toggle, etc.)
 *
 * Write coalescing:
 *   settings_set_*() marks a dirty bit and records millis().
 *   settings_tick()  — call from main loop; flushes all dirty settings to NVS
 *                      after SETTINGS_FLUSH_DEBOUNCE_MS of inactivity.
 *   settings_flush() — immediate flush; call before power_shutdown / deep_sleep.
 *
 * Persisted NVS keys (namespace "mk_cfg", see persist.h PKEY_* constants):
 *   PKEY_BRIGHTNESS   int  0-100   LCD backlight %
 *   PKEY_DISP_TIMEOUT int  0-3600  auto-dim delay in seconds (0 = never)
 *   PKEY_VOLUME       int  0-100   speaker volume %
 *   PKEY_KEY_SOUND    int  0|1     key-click enable
 *   PKEY_LED_BRIGHT   int  0-100   WS2812B brightness %
 *   PKEY_LED_COLOR    u32  0xRRGGBB WS2812B color
 *   PKEY_LED_EFFECT   int  WS2812B::Effect enum value (0=OFF … 6=RAINBOW)
 *   PKEY_WIFI_EN      int  0|1     WiFi enabled
 *   PKEY_BLE_NAME     str  ≤20B    BLE advertisement name
 *   PKEY_BLE_EN       int  0|1     BLE auto-start on boot
 *
 * WiFi credentials (PKEY_WIFI_SSID / PKEY_WIFI_PASS) are managed by
 * ui_wifi_bridge — not duplicated here.
 *
 * Usage:
 *   // Once, after DEVICES is initialised:
 *   sys_settings_bridge_attach(&device);
 *
 *   // At boot — apply saved values (no NVS write):
 *   settings_load_all();
 *
 *   // In UI event callback — apply + schedule NVS write:
 *   settings_set_brightness(new_val);
 *
 *   // In main loop — debounced NVS flush:
 *   settings_tick();
 *
 *   // Before shutdown / sleep:
 *   settings_flush();
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#include "../bsp/devices.h"
/* C++ only: takes a C++ DEVICES* — invisible to C translation units.
 * Called exclusively from launcher.cpp; C files never need this symbol. */
void sys_settings_bridge_attach(DEVICES* dev);
extern "C" {
#endif

/* Milliseconds of inactivity after the last settings_set_*() call
 * before settings_tick() commits dirty values to NVS. */
#define SETTINGS_FLUSH_DEBOUNCE_MS  2000u

/* ── Lifecycle ───────────────────────────────────────────────── */

/** Initialise NVS and open the "mk_cfg" namespace.
 *  Wraps persist_init() so callers need not include persist.h directly.
 *  Call once at startup before settings_load_all(). */
void settings_init(void);

/** Load all settings from NVS and apply to hardware. Replaces the manual
 *  sequence of persist_get_int + sys_apply_* calls at boot. */
void settings_load_all(void);

/** Debounced NVS flush — call once per main-loop iteration.
 *  Writes dirty settings to NVS after SETTINGS_FLUSH_DEBOUNCE_MS of quiet. */
void settings_tick(void);

/** Immediate NVS flush — call before power_shutdown() or power_deep_sleep(). */
void settings_flush(void);

/* ── Display ─────────────────────────────────────────────────── */

/** Apply LCD backlight.  pct = 0..100 → driver PWM 0..255. (boot-time) */
void sys_apply_brightness(int pct);
/** Apply LCD backlight + schedule NVS write. (UI callback) */
void settings_set_brightness(int pct);
int  sys_get_brightness(void);

/** Set auto-dim timeout in seconds (0 = never). No immediate hardware effect;
 *  the main loop reads settings_get_disp_timeout() to drive the dimmer. */
void settings_set_disp_timeout(int secs);
int  settings_get_disp_timeout(void);

/** Sleep mode: when on, extended idle keeps the screen off (wake on any press)
 *  instead of powering off. The main loop reads settings_get_sleep_mode(). */
void settings_set_sleep_mode(int on);
int  settings_get_sleep_mode(void);

/* ── Audio ───────────────────────────────────────────────────── */

/** Apply speaker volume.  pct = 0..100 → Speaker_Class. (boot-time) */
void sys_apply_volume(int pct);
/** Apply speaker volume + schedule NVS write. (UI callback) */
void settings_set_volume(int pct);
int  sys_get_volume(void);

/** Apply key-click sound enable / disable. (boot-time, 1=on 0=off) */
void sys_apply_key_sound(int on);
/** Apply key-click + schedule NVS write. (UI callback) */
void settings_set_key_sound(int on);
bool sys_get_key_sound(void);

/* ── LED ─────────────────────────────────────────────────────── */

/** Apply WS2812B brightness.  pct = 0..100 → 0..255 raw. (boot-time) */
void sys_apply_led(int pct);
/** Apply WS2812B brightness + schedule NVS write. (UI callback) */
void settings_set_led_bright(int pct);
int  sys_get_led(void);

/** Apply WS2812B color (0xRRGGBB) + schedule NVS write. */
void     settings_set_led_color(uint32_t rgb);
uint32_t settings_get_led_color(void);

/** Apply WS2812B effect (WS2812B::Effect enum cast to int) + schedule write.
 *  Valid values: 0=OFF 1=SOLID 2=BREATHING 3=BLINK_SLOW 4=BLINK_FAST
 *                5=PULSE 6=RAINBOW                                          */
void settings_set_led_effect(int effect);
int  settings_get_led_effect(void);

/* ── WiFi ────────────────────────────────────────────────────── */

/** Enable or disable WiFi + schedule NVS write. */
void settings_set_wifi_en(int en);
bool settings_get_wifi_en(void);

/* ── BLE ─────────────────────────────────────────────────────── */

/** Set BLE advertisement name (max 20 chars) + schedule NVS write. */
void settings_set_ble_name(const char* name);
void settings_get_ble_name(char* buf, int len);

/** Enable or disable BLE auto-start + schedule NVS write. */
void settings_set_ble_en(int en);
bool settings_get_ble_en(void);

#ifdef __cplusplus
}
#endif
