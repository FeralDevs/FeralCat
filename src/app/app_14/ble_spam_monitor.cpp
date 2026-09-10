/**
 * @file  ble_spam_monitor.cpp
 * @brief BLE-spam detector implementation (see ble_spam_monitor.h).
 */
#include "ble_spam_monitor.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include "esp_gap_ble_api.h"

namespace {
portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t s_cur = 0;      /* spam adverts in the current second */
volatile uint32_t s_total = 0;
volatile uint16_t s_apple = 0, s_google = 0, s_ms = 0, s_samsung = 0;

/* Passive scan: listen only, don't send scan requests. */
esp_ble_scan_params_t s_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x50,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,   /* count every advert */
};

void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p)
{
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            esp_ble_gap_start_scanning(0);              /* 0 = until stopped */
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
            uint8_t* adv = p->scan_rst.ble_adv;
            bool is_spam = false;

            uint8_t mlen = 0;
            uint8_t* md = esp_ble_resolve_adv_data(
                adv, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mlen);
            if (md && mlen >= 2) {
                uint16_t cid = (uint16_t)md[0] | ((uint16_t)md[1] << 8);
                portENTER_CRITICAL_ISR(&s_mux);
                if      (cid == 0x004C) { s_apple++;   is_spam = true; }
                else if (cid == 0x0006) { s_ms++;      is_spam = true; }
                else if (cid == 0x0075) { s_samsung++; is_spam = true; }
                portEXIT_CRITICAL_ISR(&s_mux);
            }

            uint8_t slen = 0;
            uint8_t* sd = esp_ble_resolve_adv_data(
                adv, ESP_BLE_AD_TYPE_SERVICE_DATA, &slen);
            if (sd && slen >= 2) {
                uint16_t uuid = (uint16_t)sd[0] | ((uint16_t)sd[1] << 8);
                if (uuid == 0xFE2C) {                   /* Google FastPair */
                    portENTER_CRITICAL_ISR(&s_mux);
                    s_google++;
                    portEXIT_CRITICAL_ISR(&s_mux);
                    is_spam = true;
                }
            }

            if (is_spam) {
                portENTER_CRITICAL_ISR(&s_mux);
                s_cur++;
                s_total++;
                portEXIT_CRITICAL_ISR(&s_mux);
            }
            break;
        }
        default: break;
    }
}
} // namespace

void BleSpamMonitor::begin()
{
    _running = true;
    _stats   = BleSpamStats{};
    for (int i = 0; i < HIST; i++) _hist[i] = 0;
    _hidx = 0;
    _accum_ms = 0;
    _run_since = millis();
    _last_tick = millis();

    portENTER_CRITICAL(&s_mux);
    s_cur = s_apple = s_google = s_ms = s_samsung = 0;
    s_total = 0;
    portEXIT_CRITICAL(&s_mux);

    BLEDevice::deinit(false);
    delay(50);
    BLEDevice::init("");
    _inited = true;
    esp_ble_gap_register_callback(&gap_cb);
    esp_ble_gap_set_scan_params(&s_scan_params);        /* → starts on complete */
}

void BleSpamMonitor::stop()
{
    _running = false;
    if (_inited) {
        esp_ble_gap_stop_scanning();
        BLEDevice::deinit(false);
        _inited = false;
    }
}

void BleSpamMonitor::pause()
{
    if (!_running) return;
    _accum_ms += millis() - _run_since;
    _running = false;
    esp_ble_gap_stop_scanning();
}

void BleSpamMonitor::resume()
{
    if (_running) return;
    _run_since = millis();
    _last_tick = millis();
    _running = true;
    esp_ble_gap_start_scanning(0);
}

uint32_t BleSpamMonitor::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

void BleSpamMonitor::history(uint16_t* out) const
{
    for (int i = 0; i < HIST; i++) out[i] = _hist[(_hidx + i) % HIST];
}

void BleSpamMonitor::loop()
{
    if (!_running) return;
    const uint32_t now = millis();
    if (now - _last_tick >= 1000) {
        _last_tick += 1000;
        uint16_t cur;
        uint32_t total;
        portENTER_CRITICAL(&s_mux);
        cur = s_cur; s_cur = 0; total = s_total;
        _stats.apple = s_apple; _stats.google = s_google;
        _stats.ms = s_ms; _stats.samsung = s_samsung;
        portEXIT_CRITICAL(&s_mux);

        _hist[_hidx] = cur;
        _hidx = (_hidx + 1) % HIST;
        _stats.rate  = cur;
        _stats.total = total;
        if (cur > _stats.peak) _stats.peak = cur;
        if (cur >= ALERT_THRESHOLD) _alert_until = now + 4000;
    }
    _stats.alert = (now < _alert_until);
}
