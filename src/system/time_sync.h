/**
 * @file time_sync.h
 * @brief WiFi time sync — NTP (UTC) + IP-geolocation (timezone) → RTC + clock.
 *
 * Gets the correct *local* time with no manual timezone selection: NTP gives
 * precise UTC, a lightweight IP-geolocation call gives this connection's UTC
 * offset, and the sum is written to both the ESP system clock (so time() is
 * right now) and the PCF8563 RTC (so it survives a reboot).
 *
 * Wiring (launcher, once DEVICES is up):
 *   time_sync_attach(&device);          // give it the RTC + WiFi
 *   time_sync_restore_from_rtc();        // seed system clock from RTC at boot
 *
 * UI (C settings screen): time_sync_now(...) on a button press.
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
#include "../bsp/devices.h"
/* C++ only: takes a C++ DEVICES* — invisible to C translation units. */
void time_sync_attach(DEVICES* dev);
extern "C" {
#endif

/**
 * Sync the clock from WiFi. Reconnects to the WiFi saved in Settings if needed,
 * queries NTP for UTC and an IP-geolocation API for the local UTC offset, then
 * sets the system clock and the RTC to local time. Blocking (a few seconds).
 *
 * @param status      buffer for a short human-readable result (success or error)
 * @param status_len  size of that buffer
 * @return true on success.
 */
bool time_sync_now(char* status, int status_len);

/** Seed the ESP system clock from the RTC so time() is correct offline.
 *  Call once at boot. No-op if the RTC has never been set. */
void time_sync_restore_from_rtc(void);

#ifdef __cplusplus
}
#endif
