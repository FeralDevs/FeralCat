/**
 * @file app.h
 * @brief Native app registry — install apps + icon table (co-located so they stay in sync).
 *
 * ADDING AN APP:
 *   1. Add installApp() call in registerAllApps() below.
 *   2. Add the matching icon pointer in APP_BUILTIN_ICONS[] immediately below.
 *   Both tables are in the same file — impossible to add one without seeing the other.
 */
#pragma once

/* ── BSP ── */
#include "../bsp/devices.h"

/* ── LVGL icon declarations ── */
#include "../ui/ui.h"

/* ── All native app headers (app_01 ~ app_15) ── */
#include "app_01/dino.h"        /* Dino         */
#include "app_02/matrix_rain.h" /* Matrix Rain  */
#include "app_03/vu_meter.h"    /* VU Meter     */
#include "app_04/retro_tv.h"    /* Retro TV     */
#include "app_05/pc_monitor.h"  /* PC Monitor   */
#include "app_06/air_mouse.h"   /* Air Mouse    */
#include "app_07/ble_spam.h"    /* BLE Spam     */
#include "app_08/badusb.h"      /* Bad USB      */
#include "app_09/infrared.h"    /* Infrared     */
#include "app_10/app_10.h"      /* MeowGotchi    */
#include "app_18/app_18.h"      /* Script Runner     */
#if MEOWKIT_ENABLE_PLAYER
#include "app_19/app_19.h"      /* MeowPlayer (WIP)  */
#endif
#include "app_lua/app_lua.h"    /* SD-installed Lua apps */

#include <mooncake.h>
#include <memory>
#include <cstring>

/* Menu-tile icons defined in src/ui/images/ but not declared in ui.h. */
extern "C" const lv_img_dsc_t ui_img_wifispam_png;
extern "C" const lv_img_dsc_t ui_img_usb_msc_png;

/* FeralCat app icons (docs/branding → src/ui/images/ui_img_ic_*.c). */
extern "C" const lv_img_dsc_t ui_img_ic_meowgotchi;
extern "C" const lv_img_dsc_t ui_img_ic_wifianalyzer;
extern "C" const lv_img_dsc_t ui_img_ic_deauth;
extern "C" const lv_img_dsc_t ui_img_ic_rogueradar;
extern "C" const lv_img_dsc_t ui_img_ic_blespam;
extern "C" const lv_img_dsc_t ui_img_ic_probe;
extern "C" const lv_img_dsc_t ui_img_ic_tracker;

/**
 * @brief Register active apps into Mooncake (order determines menu slot index).
 *        Keep in sync with APP_BUILTIN_ICONS below.
 */
inline MOONCAKE::APPS::AppLua* registerAllApps(mooncake::Mooncake& mc, DEVICES* dev,
                                               bool luaEnabled)
{
    /* Menu visual order: left → right, top → bottom (app_01 … app_15) */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App01>(dev));     /* app_01  Dino         */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App02>(dev));     /* app_02  Matrix Rain  */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App03>(dev));     /* app_03  VU Meter     */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App04>(dev));     /* app_04  Retro TV     */
    mc.installApp(std::make_unique<MOONCAKE::APPS::PCMonitor>(dev)); /* app_05  PC Monitor   */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App06>(dev));     /* app_06  Air Mouse    */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App07>(dev));     /* app_07  BLE Spam     */
    mc.installApp(std::make_unique<MOONCAKE::APPS::AppBadUSB>(dev)); /* app_08  Bad USB      */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App09>(dev));     /* app_09  Infrared     */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App10>(dev));     /* app_10  MeowGotchi   */
    mc.installApp(std::make_unique<MOONCAKE::APPS::App18>(dev));     /* app_18  Script Runner */
#if MEOWKIT_ENABLE_PLAYER
    mc.installApp(std::make_unique<MOONCAKE::APPS::App19>(dev));     /* app_19  MeowPlayer    */
#endif
    /* Lua installable-app platform — opt-in (Settings ▸ Features). When off it is
     * not installed at all: no App-manager tile, no /apps scan, no Lua behavior
     * (every launcher hook is gated on this returned pointer). Still compiled in. */
    if (!luaEnabled) return nullptr;
    auto luaHost = std::make_unique<MOONCAKE::APPS::AppLua>(dev);
    auto* luaHostPtr = luaHost.get();
    mc.installApp(std::move(luaHost));                            /* Lua app manager */
    return luaHostPtr;
}

/**
 * @brief App icons — one entry per installApp() call above, same order.
 *        Launcher reads this array by slot index to populate the apps menu grid.
 *
 * Rule: icon[N] must correspond to the Nth installApp() call in registerAllApps().
 */
static const void* const APP_BUILTIN_ICONS[] = {
    &ui_img_dino_png,        /* app_01  Dino        */
    &ui_img_matrix_rain_png, /* app_02  Matrix Rain */
    &ui_img_vu_meter_png,    /* app_03  VU Meter    */
    &ui_img_retro_tv_png,    /* app_04  Retro TV    */
    &ui_img_pc_montior_png,  /* app_05  PC Monitor  */
    &ui_img_air_mouse_png,   /* app_06  Air Mouse   */
    &ui_img_ble_spam_png,    /* app_07  BLE Spam    */
    &ui_img_badusb_png,      /* app_08  Bad USB     */
    &ui_img_infrared_png,    /* app_09  Infrared    */
    &ui_img_ic_meowgotchi,   /* app_10  MeowGotchi  */
    &ui_img_webserial_png,   /* app_18  Script Runner */
#if MEOWKIT_ENABLE_PLAYER
    &ui_img_vu_meter_png,    /* app_19  MeowPlayer (audio) */
#endif
    &ui_img_webserial_png,   /* Lua app manager */
};
static const int APP_BUILTIN_ICONS_COUNT =
    (int)(sizeof(APP_BUILTIN_ICONS) / sizeof(APP_BUILTIN_ICONS[0]));

/* Map a manifest icon= name to a built-in icon for native SD apps.
 * Unknown/empty names fall back to a generic icon. */
inline const void* native_icon_by_name(const char* name)
{
    if (name && name[0]) {
        /* FeralCat per-app icons */
        if (!strcmp(name, "wifianalyzer")) return &ui_img_ic_wifianalyzer;
        if (!strcmp(name, "deauth"))       return &ui_img_ic_deauth;
        if (!strcmp(name, "rogueradar"))   return &ui_img_ic_rogueradar;
        if (!strcmp(name, "blespam"))      return &ui_img_ic_blespam;
        if (!strcmp(name, "probe"))        return &ui_img_ic_probe;
        if (!strcmp(name, "tracker"))      return &ui_img_ic_tracker;
        /* generic families (fallbacks) */
        if (!strcmp(name, "wifi"))     return &ui_img_wifispam_png;
        if (!strcmp(name, "wifikill")) return &ui_img_wifi_killer_png;
        if (!strcmp(name, "ble"))      return &ui_img_ble_spam_png;
        if (!strcmp(name, "music"))    return &ui_img_music_png;
        if (!strcmp(name, "ir"))       return &ui_img_infrared_png;
        if (!strcmp(name, "usb"))      return &ui_img_badusb_png;
    }
    return &ui_img_webserial_png;   /* generic */
}
