/**
 * @file persist.h
 * @brief NVS Key-Value storage — thin C wrapper around ESP-IDF nvs_flash.
 *
 * Namespace: "mk_cfg"
 * Usage:
 *   persist_init();                                 // call once at startup
 *   int  v = persist_get_int(PKEY_BRIGHTNESS, 50);
 *   persist_set_int(PKEY_BRIGHTNESS, v);
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── NVS Schema version ──────────────────────────────────────────
 * Bump this whenever any PKEY_* constant is added, renamed, or removed.
 * v2 → v3: wifi_pass now stored as XOR-obfuscated blob (chip-unique key).
 *           Targeted migration: only wifi_pass is re-encoded; all other
 *           settings are kept.  Major-gap (>1) still does a full erase.  */
#define PERSIST_SCHEMA_VERSION  3

/* ── Key registry — ALL NVS keys defined in one place ───────────
 * Rules:
 *   • Max 15 characters (Arduino Preferences / ESP-IDF NVS limit)
 *   • Keep alphabetically grouped by subsystem
 *   • Document type, range, and boot default on the same line             */

/* Display */
#define PKEY_BRIGHTNESS     "bright"      /* int  0-100    LCD backlight %           */
#define PKEY_DISP_TIMEOUT   "disp_to"    /* int  0-3600   auto-dim delay s (0=never)*/

/* Audio */
#define PKEY_VOLUME         "vol"         /* int  0-100    speaker volume %           */
#define PKEY_KEY_SOUND      "key_snd"    /* int  0|1      key-click sound enable     */

/* LED */
#define PKEY_LED_BRIGHT     "led"         /* int  0-100    WS2812B brightness %       */
#define PKEY_LED_COLOR      "led_color"  /* u32  0xRRGGBB WS2812B color              */
#define PKEY_LED_EFFECT     "led_fx"     /* int  WS2812B::Effect (0=OFF…6=RAINBOW)   */

/* WiFi */
#define PKEY_WIFI_SSID      "wifi_ssid"  /* str  ≤32 B    last connected SSID        */
#define PKEY_WIFI_PASS      "wifi_pass"  /* str  ≤64 B    last connected password    */
#define PKEY_WIFI_EN        "wifi_en"    /* int  0|1      WiFi enabled               */

/* BLE */
#define PKEY_BLE_NAME       "ble_name"   /* str  ≤20 B    BLE advertisement name     */
#define PKEY_BLE_EN         "ble_en"     /* int  0|1      BLE auto-start on boot     */

/* XP / leveling — device-wide (see meow_xp.h). Additive keys: no schema bump,
 * so upgrading firmware never wipes existing settings; missing keys read as 0. */
#define PKEY_XP_TOTAL       "xp"          /* u32  total lifetime XP                   */
#define PKEY_XP_HS          "xp_hs"      /* u32  lifetime handshakes captured        */
#define PKEY_XP_PLAY        "xp_play"    /* u32  lifetime awake seconds              */
#define PKEY_XP_APPS        "xp_apps"    /* u32  bitmask of apps opened at least once*/

/* Features (Settings ▸ Features) — opt-in experimental subsystems, off by default. */
#define PKEY_LUA_EN         "lua_en"     /* int  0|1      Lua installable-app platform */
#define PKEY_ELF_UNSIGNED   "elf_unsig"  /* int  0|1      allow unsigned native ELF apps */

/* ELF-loader spike: crash breadcrumb (survives reboot to locate a hard fault). */
#define PKEY_ELF_STAGE      "elf_stg"    /* int  last ELF-load step reached           */

/* Internal — do not read/write from application code */
#define PKEY_NVS_VER        "nvs_ver"   /* int  schema version sentinel             */

/* ── Lifecycle ───────────────────────────────────────────────────
 * Initialise NVS flash and open the "mk_cfg" namespace.
 * Erases the namespace when the stored schema version ≠ PERSIST_SCHEMA_VERSION
 * so stale keys from older firmware do not interfere with new ones.       */
bool persist_init(void);

/* ── Integer KV ──────────────────────────────────────────────── */
int  persist_get_int(const char* key, int default_val);
bool persist_set_int(const char* key, int val);

/* ── Unsigned 32-bit KV (LED color 0xRRGGBB, packed RGB) ────── */
uint32_t persist_get_u32(const char* key, uint32_t default_val);
bool     persist_set_u32(const char* key, uint32_t val);

/* ── String KV ───────────────────────────────────────────────── */
bool persist_get_str(const char* key, char* buf, size_t len, const char* default_val);
bool persist_set_str(const char* key, const char* val);

/* ── Erase ───────────────────────────────────────────────────── */
bool persist_erase_key(const char* key);
bool persist_erase_all(void);

#ifdef __cplusplus
}
#endif
