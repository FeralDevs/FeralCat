/**
 * @file  app_store.h
 * @brief App Store — a SYSTEM MODULE (not an app).
 *
 * Fetches a curated catalog of signed native ELF apps over WiFi and installs
 * them to /apps, verifying each app's Ed25519 signature on-device before it is
 * kept. Reached from Settings ▸ System ▸ App Store: the settings button calls
 * app_store_request(); the launcher notices the pending request in its GUI loop
 * and runs the blocking full-screen takeover (same model as firmware_update).
 *
 * Trust model: single-key, curated. Every published app is signed with the
 * FeralCat release key and verified here against APP_SIGN_PUBKEY — a tampered
 * catalog can only offer apps that still fail verification.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Settings UI → request the App Store. Cheap; just sets a flag. */
void app_store_request(void);

/* Launcher → returns 1 (and clears the flag) if the store was requested. */
int app_store_take_pending(void);

#ifdef __cplusplus
}

class DEVICES;
/* Launcher → run the blocking App Store takeover (browse + install), then
 * return. Renders via LovyanGFX and reads the buttons directly, so LVGL should
 * be paused around the call (same as running a full-screen app). */
void app_store_run(DEVICES* dev);
#endif
