/**
 * @file ui_wifi_bridge.cpp
 * @brief WiFi bridge — Arduino WiFi for LVGL screens (ESP32-S3).
 *
 * Root causes fixed vs original:
 *  1. WiFi.scanNetworks() fails when driver is busy connecting.
 *     Fix: disconnect before scan, save target and reconnect after.
 *  2. Polling WiFi.status() gives unreliable state.
 *     Fix: WiFi.onEvent() event handler updates s_status atomically.
 *  3. WiFi.scanDelete() race between LVGL task and scan task.
 *     Fix: only the scan task calls scanDelete() on its own results.
 *  4. Intentional pre-scan disconnect must not be reported as FAILED.
 *     Fix: s_scan_running flag suppresses disconnect→FAILED transition.
 */
#include "ui_wifi_bridge.h"
#include "../system/persist.h"
#include "../system/config_sd.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <atomic>

/* ── NVS keys ────────────────────────────────────────────────── */
static constexpr char kKeySsid[] = PKEY_WIFI_SSID;
static constexpr char kKeyPass[] = PKEY_WIFI_PASS;

/* ── Connection state  (written by event handler, read by LVGL) ─
 *  volatile: 32-bit aligned enum reads/writes are atomic on Xtensa */
static volatile wifi_bridge_status_t s_status = WIFI_BRIDGE_IDLE;

/* Current SSID / IP — updated from event handler; short strings,
 * single-writer, read races are benign for UI purposes.           */
static char s_current_ssid[33] = "";
static char s_current_ip[16]   = "";

/* ── Scan state ──────────────────────────────────────────────────
 *  -2 = idle / not started
 *  -1 = scan task running
 *  ≥0 = results ready (count of APs found)
 */
static volatile int s_scan_count = -2;
static volatile int s_scan_gen   = 0;

/* True while the scan task is active — suppresses WIFI_STA_DISCONNECTED
 * being treated as a connection failure during the pre-scan disconnect. */
static volatile bool s_scan_running = false;
// UI results can be ready before post-scan reconnect. Track actual worker
// ownership separately, also across stop_scan() and superseded generations.
static std::atomic<unsigned> s_scan_workers{0};

/* Reconnect after scan: saved here before we disconnect for the scan. */
static char  s_post_scan_ssid[33] = "";
static char  s_post_scan_pass[65] = "";
static bool  s_need_reconnect     = false;

/* Pending SSID from T9 keyboard screen */
static char s_pending_ssid[33] = "";

static bool s_inited = false;

/* ── WiFi event handler ──────────────────────────────────────────
 *  Runs on the WiFi event task, NOT the LVGL/Arduino-loop task.
 *  Only touches volatile primitives and short char arrays.
 */
static void _wifi_event(WiFiEvent_t event, WiFiEventInfo_t /*info*/)
{
    switch (event) {

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_status = WIFI_BRIDGE_CONNECTED;
            {
                String ssid = WiFi.SSID();
                String ip   = WiFi.localIP().toString();
                strncpy(s_current_ssid, ssid.c_str(), sizeof(s_current_ssid) - 1);
                s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
                strncpy(s_current_ip, ip.c_str(), sizeof(s_current_ip) - 1);
                s_current_ip[sizeof(s_current_ip) - 1] = '\0';
            }
            Serial.printf("[wifi] Connected  SSID=%s  IP=%s\n",
                          s_current_ssid, s_current_ip);
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            /* Ignore disconnect events that we ourselves trigger before a scan */
            if (s_scan_running) break;

            if (s_status == WIFI_BRIDGE_CONNECTING) {
                s_status = WIFI_BRIDGE_FAILED;
                Serial.println("[wifi] Connect failed");
            } else if (s_status == WIFI_BRIDGE_CONNECTED) {
                s_status = WIFI_BRIDGE_IDLE;
                s_current_ssid[0] = '\0';
                s_current_ip[0]   = '\0';
                Serial.println("[wifi] Link lost");
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            /* Authenticated with AP but no IP yet — keep CONNECTING */
            break;

        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            if (s_status == WIFI_BRIDGE_CONNECTED) {
                s_status = WIFI_BRIDGE_IDLE;
                s_current_ip[0] = '\0';
                Serial.println("[wifi] IP lost");
            }
            break;

        default:
            break;
    }
}

/* ── Scan FreeRTOS task ──────────────────────────────────────── */
static void _scan_task(void *arg)
{
    int my_gen = (int)(intptr_t)arg;

    /* Step 1 — ensure STA mode */
    WiFi.mode(WIFI_STA);

    /* Step 2 — disconnect if we were mid-connect so the driver is free */
    if (s_need_reconnect) {
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(200));   /* let driver settle */
    }

    /* Step 3 — scan (blocking, 400 ms/channel, 13 channels ≈ 5 s max) */
    int n = WiFi.scanNetworks(/*async=*/false,
                               /*show_hidden=*/false,
                               /*passive=*/false,
                               /*max_ms_per_chan=*/400);

    /* Step 4 — publish result or discard if superseded */
    if (my_gen == (int)s_scan_gen) {
        s_scan_count = (n >= 0) ? n : 0;   /* treat WIFI_SCAN_FAILED as 0 */
        Serial.printf("[wifi] scan done: %d AP(s)\n", (int)s_scan_count);
    } else {
        /* A newer scan or stop_scan() has superseded us */
        WiFi.scanDelete();
        Serial.printf("[wifi] scan discarded (gen %d < %d)\n", my_gen, (int)s_scan_gen);
    }

    /* Step 5 — allow disconnect events to propagate again */
    s_scan_running = false;

    /* Step 6 — reconnect to the network we interrupted (if any) */
    if (my_gen == (int)s_scan_gen && s_need_reconnect && s_post_scan_ssid[0] != '\0') {
        s_need_reconnect = false;
        s_status = WIFI_BRIDGE_CONNECTING;
        WiFi.begin(s_post_scan_ssid, s_post_scan_pass);
        Serial.printf("[wifi] post-scan reconnect → %s\n", s_post_scan_ssid);
    } else {
        s_need_reconnect = false;
    }

    s_scan_workers.fetch_sub(1, std::memory_order_release);
    vTaskDelete(NULL);
}

/* ── Init ────────────────────────────────────────────────────── */
void ui_wifi_bridge_init(void)
{
    if (s_inited) return;
    s_inited = true;

    WiFi.onEvent(_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   /* we manage reconnect explicitly */

    char ssid[33] = {};
    char pass[65] = {};
    persist_get_str(kKeySsid, ssid, sizeof(ssid), "");
    persist_get_str(kKeyPass, pass, sizeof(pass), "");
    if (ssid[0] != '\0') {
        Serial.printf("[wifi] Auto-connect → %s\n", ssid);
        s_status = WIFI_BRIDGE_CONNECTING;
        WiFi.begin(ssid, pass);
    }
}

/* ── Scan ────────────────────────────────────────────────────── */
void ui_wifi_bridge_start_scan(void)
{
    /* If we are currently connecting/connected, save the target so we
     * can reconnect after the scan finishes.                          */
    s_need_reconnect = (s_status == WIFI_BRIDGE_CONNECTING ||
                        s_status == WIFI_BRIDGE_CONNECTED);
    if (s_need_reconnect) {
        persist_get_str(kKeySsid, s_post_scan_ssid, sizeof(s_post_scan_ssid), "");
        persist_get_str(kKeyPass, s_post_scan_pass, sizeof(s_post_scan_pass), "");
        if (s_post_scan_ssid[0] == '\0') s_need_reconnect = false;
    }

    /* Advance generation (invalidates any still-running previous task) */
    s_scan_gen++;
    s_scan_count  = -1;       /* mark: scan in progress */
    s_scan_running = true;    /* suppress disconnect→FAILED during pre-scan disconnect */

    int gen = (int)s_scan_gen;
    s_scan_workers.fetch_add(1, std::memory_order_relaxed);
    BaseType_t rc = xTaskCreate(_scan_task, "wifi_scan",
                                 4096, (void *)(intptr_t)gen,
                                 3, NULL);
    if (rc != pdPASS) {
        s_scan_workers.fetch_sub(1, std::memory_order_release);
        s_scan_count  = 0;
        s_scan_running = false;
        Serial.println("[wifi] ERROR: scan task creation failed");
    } else {
        Serial.printf("[wifi] scan task started (gen=%d, reconnect=%d)\n",
                      gen, (int)s_need_reconnect);
    }
}

void ui_wifi_bridge_scan(void) { ui_wifi_bridge_start_scan(); }

void ui_wifi_bridge_stop_scan(void)
{
    s_scan_gen++;          /* invalidate running task's generation */
    s_scan_count  = -2;    /* idle */
    s_scan_running = false;
    /* Do NOT call WiFi.scanDelete() here — the scan task owns the buffer
     * and will call scanDelete() itself when it detects gen mismatch.   */
    Serial.println("[wifi] scan stopped");
}

int  ui_wifi_bridge_get_scan_count(void)
{
    int n = (int)s_scan_count;
    return (n < 0) ? 0 : n;
}

bool ui_wifi_bridge_is_scanning(void)   { return (s_scan_count == -1); }
bool ui_wifi_bridge_scan_busy(void)     { return s_scan_workers.load(std::memory_order_acquire) != 0; }
bool ui_wifi_bridge_scan_complete(void) { return (s_scan_count >= 0);  }

void ui_wifi_bridge_get_ssid(int idx, char *buf, int len)
{
    if (!buf || !len) return;
    String s = WiFi.SSID(idx);
    strncpy(buf, s.c_str(), len - 1);
    buf[len - 1] = '\0';
}

int  ui_wifi_bridge_get_rssi(int idx)     { return WiFi.RSSI(idx); }
bool ui_wifi_bridge_is_encrypted(int idx) { return (WiFi.encryptionType(idx) != WIFI_AUTH_OPEN); }

bool ui_wifi_bridge_get_ap(int i, char *ssid, int ssid_len, int *pct_out, bool *saved_out)
{
    int n = (int)s_scan_count;
    if (n < 0 || i < 0 || i >= n) return false;

    String s = WiFi.SSID(i);
    strncpy(ssid, s.c_str(), ssid_len - 1);
    ssid[ssid_len - 1] = '\0';

    if (pct_out) {
        int rssi = WiFi.RSSI(i);
        int pct  = (rssi < -100) ? 0 : (rssi > -50) ? 100 : 2 * (rssi + 100);
        *pct_out = pct;
    }

    if (saved_out) {
        char saved[33] = {};
        persist_get_str(kKeySsid, saved, sizeof(saved), "");
        *saved_out = (strcmp(ssid, saved) == 0);
    }

    return true;
}

/* ── Connect ─────────────────────────────────────────────────── */
void ui_wifi_bridge_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') return;

    /* Stop any in-progress scan before connecting */
    if (s_scan_count == -1) {
        s_scan_gen++;
        s_scan_count  = -2;
        s_scan_running = false;
    }

    Serial.printf("[wifi] Connecting → %s\n", ssid);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    s_status = WIFI_BRIDGE_CONNECTING;
    WiFi.begin(ssid, pass ? pass : "");

    persist_set_str(kKeySsid, ssid);
    persist_set_str(kKeyPass, pass ? pass : "");
    config_sd_backup();   /* mirror new WiFi creds to SD (survives a reflash) */
}

void ui_wifi_bridge_connect_saved(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return;
    char sv_ssid[33] = {};
    char sv_pass[65] = {};
    persist_get_str(kKeySsid, sv_ssid, sizeof(sv_ssid), "");
    persist_get_str(kKeyPass, sv_pass, sizeof(sv_pass), "");
    const char *pass = (strcmp(sv_ssid, ssid) == 0) ? sv_pass : "";
    ui_wifi_bridge_connect(ssid, pass);
}

void ui_wifi_bridge_set_pending_ssid(const char *ssid)
{
    if (!ssid) return;
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
}

void ui_wifi_bridge_connect_pending(const char *password)
{
    if (s_pending_ssid[0] == '\0') return;
    ui_wifi_bridge_connect(s_pending_ssid, password ? password : "");
    s_pending_ssid[0] = '\0';
}

void ui_wifi_bridge_disconnect(void)
{
    WiFi.disconnect(false);
    s_status = WIFI_BRIDGE_IDLE;
    s_current_ssid[0] = '\0';
    s_current_ip[0]   = '\0';
}

/* ── State ───────────────────────────────────────────────────── */
wifi_bridge_status_t ui_wifi_bridge_get_status(void)
{
    return s_status;
}

void ui_wifi_bridge_get_status_text(char *buf, int len)
{
    if (!buf || len <= 0) return;
    switch (s_status) {
        case WIFI_BRIDGE_CONNECTED:
            snprintf(buf, (size_t)len, "Connected: %s", s_current_ssid);
            break;
        case WIFI_BRIDGE_CONNECTING:
            snprintf(buf, (size_t)len, "Connecting...");
            break;
        case WIFI_BRIDGE_FAILED:
            snprintf(buf, (size_t)len, "Connection failed");
            break;
        default:
            snprintf(buf, (size_t)len, "Not connected");
            break;
    }
}

bool ui_wifi_bridge_is_connected(void) { return (s_status == WIFI_BRIDGE_CONNECTED); }

void ui_wifi_bridge_get_ip(char *buf, int len)
{
    if (!buf || !len) return;
    strncpy(buf, s_current_ip, len - 1);
    buf[len - 1] = '\0';
}

const char *ui_wifi_bridge_get_current_ssid(void)
{
    /* If connected, return event-cached SSID; fall back to driver query. */
    if (s_current_ssid[0] == '\0' && s_status == WIFI_BRIDGE_CONNECTED) {
        String s = WiFi.SSID();
        strncpy(s_current_ssid, s.c_str(), sizeof(s_current_ssid) - 1);
        s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
    }
    return s_current_ssid;
}

void ui_wifi_bridge_forget(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return;
    char saved[33] = {};
    persist_get_str(kKeySsid, saved, sizeof(saved), "");
    if (strcmp(ssid, saved) == 0) {
        persist_set_str(kKeySsid, "");
        persist_set_str(kKeyPass, "");
        config_sd_backup();   /* clear the SD backup too, so it won't resurrect */
        WiFi.disconnect(false);
        s_status = WIFI_BRIDGE_IDLE;
        s_current_ssid[0] = '\0';
        s_current_ip[0]   = '\0';
        Serial.printf("[wifi] Forgot: %s\n", ssid);
    }
}

void ui_wifi_bridge_get_saved_pass(char *buf, int len)
{
    if (!buf || len <= 0) return;
    persist_get_str(kKeyPass, buf, len, "");
}
