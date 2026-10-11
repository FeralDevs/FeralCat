/**
 * @file ui_wifi_bridge.h
 * @brief C-ABI bridge: LVGL WiFi screen (C) ↔ ESP32 WiFi stack (C++).
 *
 * Called by:
 *   Launcher::buildUI()  → ui_wifi_bridge_init()
 *   ui_wifi.c events    → ui_wifi_bridge_scan / connect / disconnect
 *   ui_wifi.c update    → ui_wifi_bridge_get_scan_count / get_ssid / is_connected
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise WiFi in STA mode and attempt reconnect to saved credentials.
 *  Safe to call multiple times; subsequent calls are no-ops. */
void ui_wifi_bridge_init(void);

/* ── Scan ───────────────────────────────────────────────────── */
/** Start an async WiFi scan. Returns immediately. */
void ui_wifi_bridge_scan(void);

/** Number of APs found in the last scan (0 while scan is running). */
int  ui_wifi_bridge_get_scan_count(void);

/** Copy SSID of index idx into buf (null-terminated). */
void ui_wifi_bridge_get_ssid(int idx, char* buf, int len);

/** RSSI of index idx (negative dBm). */
int  ui_wifi_bridge_get_rssi(int idx);

/** true if the network at idx is password-protected. */
bool ui_wifi_bridge_is_encrypted(int idx);

/** Start an async scan (alias for ui_wifi_bridge_scan). */
void ui_wifi_bridge_start_scan(void);

/** Stop / clear async scan results. */
void ui_wifi_bridge_stop_scan(void);

/* ── Connect / Disconnect ────────────────────────────────────── */
/** Begin connecting to ssid with pass (non-blocking). */
void ui_wifi_bridge_connect(const char* ssid, const char* pass);

/** Connect to a network whose password is already saved in NVS. */
void ui_wifi_bridge_connect_saved(const char * ssid);

/** Store the SSID that the T9 keyboard is collecting a password for. */
void ui_wifi_bridge_set_pending_ssid(const char * ssid);

/** Connect the pending SSID with the password typed in T9 keyboard. */
void ui_wifi_bridge_connect_pending(const char * password);

/** Disconnect from current AP. */
void ui_wifi_bridge_disconnect(void);

/* ── State query ─────────────────────────────────────────────── */
typedef enum {
    WIFI_BRIDGE_IDLE       = 0,
    WIFI_BRIDGE_CONNECTING = 1,
    WIFI_BRIDGE_CONNECTED  = 2,
    WIFI_BRIDGE_FAILED     = 3,
} wifi_bridge_status_t;

/** Current connection state. */
wifi_bridge_status_t ui_wifi_bridge_get_status(void);

/** true while an async scan is still running (WiFi.scanComplete() == -1). */
bool ui_wifi_bridge_is_scanning(void);

/** True until all scan workers finish, including their deferred reconnect.
 * Use this before exclusive radio/sleep ownership, not the UI result flag. */
bool ui_wifi_bridge_scan_busy(void);

/** true when the last scan has finished and real results are available
 *  (WiFi.scanComplete() >= 0). Returns false while running (-1) or
 *  before a scan is triggered (-2). */
bool ui_wifi_bridge_scan_complete(void);

/**
 * Get details for AP at scan index i.
 * Returns false if i is out of range.
 */
bool ui_wifi_bridge_get_ap(int i, char * ssid, int ssid_len, int * pct_out, bool * saved_out);

/** Fill buf with a human-readable WiFi status string. */
void ui_wifi_bridge_get_status_text(char * buf, int len);

/** true once WL_CONNECTED is reached. */
bool ui_wifi_bridge_is_connected(void);

/** Copy current IP string into buf. */
void ui_wifi_bridge_get_ip(char* buf, int len);

/** Return pointer to current connected SSID (static buffer). */
const char *ui_wifi_bridge_get_current_ssid(void);

/** Remove saved credentials for ssid (no-op if ssid is not the saved network). */
void ui_wifi_bridge_forget(const char * ssid);

/** Copy the saved password into buf (for pre-filling the T9 keyboard on re-entry). */
void ui_wifi_bridge_get_saved_pass(char * buf, int len);

#ifdef __cplusplus
}
#endif
