/**
 * @file config_sd.h
 * @brief Mirror device settings to the SD card so they survive a reflash.
 *
 * Settings normally live in NVS (internal flash), which a full USB reflash
 * erases — so WiFi credentials and preferences are lost. This keeps a copy on
 * the (non-volatile, removable) SD card and can restore it.
 *
 * NOTE: the WiFi password is stored in PLAINTEXT in /config/meowkit.cfg — by
 * design, so the backup is portable to another device / editable by hand.
 * Anyone with the SD card can read it.
 *
 * Boot:   config_sd_restore();      // auto-restore only if NVS was wiped
 * Change: config_sd_backup();       // after any settings write
 * UI:     config_sd_restore_all() / _wifi() / _settings()  then reboot
 */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Write all persisted settings (incl. plaintext WiFi password) to the SD card.
 *  No-op if no card is present. */
void config_sd_backup(void);

/** true if an SD backup file exists (for enabling the Restore buttons). */
bool config_sd_has_backup(void);

/** Auto-restore at boot: if NVS has no saved WiFi (freshly erased by a reflash)
 *  and a backup exists, restore everything into NVS. */
void config_sd_restore(void);

/* Forced restores from the Settings UI (write NVS; caller reboots to apply). */
void config_sd_restore_all(void);       /* WiFi + all settings   */
void config_sd_restore_wifi(void);      /* WiFi credentials only */
void config_sd_restore_settings(void);  /* everything except WiFi */

#ifdef __cplusplus
}
#endif
