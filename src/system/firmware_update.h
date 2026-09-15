/**
 * @file  firmware_update.h
 * @brief Firmware update — a SYSTEM MODULE (not an app).
 *
 * Provides WiFi-GitHub OTA, SD-card OTA (dual-OTA), and USB download mode.
 * Reached from Settings ▸ System ▸ Update, never from the app grid: the
 * settings button calls firmware_update_request(); the launcher notices the
 * pending request in its GUI loop and runs the blocking full-screen takeover.
 *
 * This deliberately does NOT depend on Mooncake / AppAbility — it is core
 * system functionality (and the recovery path when the app grid is unavailable).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Settings UI → request the updater. Cheap; just sets a flag. */
void firmware_update_request(void);

/* Launcher → returns 1 (and clears the flag) if an update was requested. */
int firmware_update_take_pending(void);

#ifdef __cplusplus
}

class DEVICES;
/* Launcher → run the blocking updater takeover (menu + WiFi/SD/USB flows), then
 * return. Renders via LovyanGFX and reads the buttons directly, so LVGL should
 * be paused around the call (same as running a full-screen app). */
void firmware_update_run(DEVICES* dev);
#endif
