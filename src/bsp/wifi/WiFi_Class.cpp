/**
 * @file WiFi_Class.cpp
 * @brief BSP WiFi manager implementation.
 */
#include "WiFi_Class.hpp"
#include "../config.h"
#include <Arduino.h>

/* ── Lifecycle ────────────────────────────────────────────────── */

void WiFi_Class::begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   /* we manage reconnect manually */
    _setState(State::IDLE);
    Serial.println("[WiFi] BSP ready (STA mode)");
}

void WiFi_Class::update()
{
    switch (_state) {
        case State::CONNECTING: {
            wl_status_t s = WiFi.status();
            if (s == WL_CONNECTED) {
                _setState(State::CONNECTED);
                Serial.printf("[WiFi] Connected  SSID=%s  IP=%s\n",
                              WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL ||
                       (millis() - _conn_start) >= (uint32_t)_timeout_ms) {
                _setState(State::FAILED);
                Serial.printf("[WiFi] Connect failed (status=%d)\n", (int)s);
            }
            break;
        }
        case State::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Link lost");
                _setState(State::IDLE);
            }
            break;
        default:
            break;
    }
}

/* ── Station mode ─────────────────────────────────────────────── */

bool WiFi_Class::connect(const char* ssid, const char* pass, int timeout_ms)
{
    if (!ssid || ssid[0] == '\0') return false;
    strncpy(_last_ssid, ssid, sizeof(_last_ssid) - 1);
    strncpy(_last_pass, pass ? pass : "", sizeof(_last_pass) - 1);
    _timeout_ms = timeout_ms;
    _conn_start = millis();

    WiFi.disconnect(false);
    WiFi.begin(ssid, pass);
    _setState(State::CONNECTING);
    Serial.printf("[WiFi] Connecting to %s\n", ssid);
    return true;
}

bool WiFi_Class::reconnect()
{
    if (_last_ssid[0] == '\0') return false;
    return connect(_last_ssid, _last_pass, _timeout_ms);
}

void WiFi_Class::disconnect()
{
    WiFi.disconnect(false);
    _setState(State::IDLE);
}

bool WiFi_Class::isConnected() const
{
    return (WiFi.status() == WL_CONNECTED);
}

/* ── Scan (blocking) ──────────────────────────────────────────── */

int WiFi_Class::scan(WiFiAPInfo* buf, int max_count)
{
    if (!buf || max_count <= 0) return 0;

    /* Disconnect before scanning: scanNetworks() returns WIFI_SCAN_FAILED(-2)
     * if the driver is busy with a connect attempt.                           */
    bool was_connecting = (_state == State::CONNECTING);
    if (was_connecting) {
        WiFi.disconnect(false);
        delay(150);
    }

    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false,
                               /*passive=*/false, /*max_ms_per_chan=*/400);

    /* Restore connecting state if we interrupted a connection attempt */
    if (was_connecting && _last_ssid[0] != '\0') {
        WiFi.begin(_last_ssid, _last_pass);
        _setState(State::CONNECTING);
        _conn_start = millis();
    }

    if (n <= 0) return 0;

    int count = (n < max_count) ? n : max_count;
    for (int i = 0; i < count; i++) {
        strncpy(buf[i].ssid, WiFi.SSID(i).c_str(), sizeof(buf[i].ssid) - 1);
        buf[i].ssid[sizeof(buf[i].ssid) - 1] = '\0';
        buf[i].rssi      = (int8_t)WiFi.RSSI(i);
        buf[i].channel   = WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        buf[i].encrypted = (auth != WIFI_AUTH_OPEN);
        buf[i].auth      = (uint8_t)auth;
        const uint8_t* b = WiFi.BSSID(i);
        if (b) memcpy(buf[i].bssid, b, 6); else memset(buf[i].bssid, 0, 6);
    }
    WiFi.scanDelete();
    return count;
}

/* ── AP mode ──────────────────────────────────────────────────── */

bool WiFi_Class::startAP(const char* ssid, const char* pass, uint8_t channel)
{
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(ssid, pass, channel);
    if (ok) {
        _setState(State::AP_MODE);
        Serial.printf("[WiFi] AP started  SSID=%s  IP=%s\n",
                      ssid, WiFi.softAPIP().toString().c_str());
    }
    return ok;
}

void WiFi_Class::stopAP()
{
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _setState(State::IDLE);
}

int WiFi_Class::getClientCount() const
{
    return (int)WiFi.softAPgetStationNum();
}

/* ── Credentials persistence ──────────────────────────────────── */

void WiFi_Class::saveCredentials(const char* ssid, const char* pass)
{
    if (ssid) strncpy(_last_ssid, ssid, sizeof(_last_ssid) - 1);
    if (pass) strncpy(_last_pass, pass, sizeof(_last_pass) - 1);
    /* Caller may also persist_set_str() to NVS if desired */
}

bool WiFi_Class::loadCredentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len)
{
    if (ssid && ssid_len) {
        strncpy(ssid, _last_ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }
    if (pass && pass_len) {
        strncpy(pass, _last_pass, pass_len - 1);
        pass[pass_len - 1] = '\0';
    }
    return _last_ssid[0] != '\0';
}

/* ── Private ──────────────────────────────────────────────────── */

void WiFi_Class::_setState(State s)
{
    _state = s;
}
