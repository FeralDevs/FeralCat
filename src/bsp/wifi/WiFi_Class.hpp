/**
 * @file WiFi_Class.hpp
 * @brief BSP WiFi manager — station mode, AP mode, scan.
 *
 * Usage:
 *   devices.wifi.begin();
 *   devices.wifi.connect("SSID", "pass");
 *   while (!devices.wifi.isConnected()) devices.wifi.update();
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>

struct WiFiAPInfo {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    bool    encrypted;
    uint8_t bssid[6];
    uint8_t auth;       /* wifi_auth_mode_t from the scan */
};

class WiFi_Class {
public:
    enum class State { IDLE, SCANNING, CONNECTING, CONNECTED, AP_MODE, FAILED };

    /* ── Lifecycle ───────────────────────────────────────────── */

    /** Initialise WiFi radio in STA mode. */
    void begin();

    /** Update state machine — call from main loop or 1 Hz ticker. */
    void update();

    /* ── Station mode ────────────────────────────────────────── */

    /** Non-blocking connect. Polls isConnected() or update() for result. */
    bool connect(const char* ssid, const char* pass, int timeout_ms = 15000);

    /** Reconnect to last-used credentials. */
    bool reconnect();

    void disconnect();

    bool   isConnected() const;
    State  getState()    const { return _state; }
    String getSSID()     const { return WiFi.SSID(); }
    String getIP()       const { return WiFi.localIP().toString(); }
    int    getRSSI()     const { return WiFi.RSSI(); }

    /* ── Scan (blocking, up to ~3 s) ────────────────────────── */

    /** Fill buf with up to max_count APs. Returns count found. */
    int scan(WiFiAPInfo* buf, int max_count);

    /* ── AP mode ─────────────────────────────────────────────── */

    /** Start soft-AP. pass = nullptr → open network. */
    bool startAP(const char* ssid, const char* pass = nullptr, uint8_t channel = 1);
    void stopAP();
    int  getClientCount() const;

    /* ── Credentials persistence (NVS) ──────────────────────── */
    void saveCredentials(const char* ssid, const char* pass);
    bool loadCredentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len);

private:
    State    _state      = State::IDLE;
    uint32_t _conn_start = 0;
    int      _timeout_ms = 15000;
    char     _last_ssid[33] = {};
    char     _last_pass[65] = {};

    void _setState(State s);
};
