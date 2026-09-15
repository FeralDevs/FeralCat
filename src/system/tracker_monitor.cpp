/**
 * @file  tracker_monitor.cpp
 * @brief Unwanted-tracker detector implementation (see tracker_monitor.h).
 */
#include "tracker_monitor.h"
#include <Arduino.h>
#include <cstring>
#include <BLEDevice.h>
#include "esp_gap_ble_api.h"

namespace {
constexpr int MAXTRK = TrackerMonitor::MAXTRK;
portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;

TrackerEntry  s_trk[MAXTRK];
int           s_trk_n = 0;

/* Record/refresh a tracker sighting (called from the BLE callback context). */
inline void trk_record(const uint8_t* mac, uint8_t type, int8_t rssi, uint32_t now)
{
    for (int i = 0; i < s_trk_n; i++) {
        if (memcmp(s_trk[i].mac, mac, 6) == 0) {
            if (s_trk[i].count < 0xFFFF) s_trk[i].count++;
            s_trk[i].rssi = rssi; s_trk[i].last_ms = now; s_trk[i].type = type;
            return;
        }
    }
    if (s_trk_n < MAXTRK) {
        TrackerEntry& e = s_trk[s_trk_n++];
        memcpy(e.mac, mac, 6);
        e.type = type; e.rssi = rssi; e.count = 1;
        e.first_ms = now; e.last_ms = now;
    }
}

/* Classify a BLE advert as a known tracker type, or return false. */
bool classify(uint8_t* adv, uint8_t* type_out)
{
    uint8_t mlen = 0;
    uint8_t* md = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mlen);
    if (md && mlen >= 3) {
        uint16_t cid = (uint16_t)md[0] | ((uint16_t)md[1] << 8);
        if (cid == 0x004C && md[2] == 0x12) { *type_out = TRK_APPLE;   return true; } /* Find My */
        if (cid == 0x0075)                  { *type_out = TRK_SAMSUNG; return true; } /* Samsung */
    }
    uint8_t slen = 0;
    uint8_t* sd = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_SERVICE_DATA, &slen);
    if (sd && slen >= 2) {
        uint16_t uuid = (uint16_t)sd[0] | ((uint16_t)sd[1] << 8);
        if (uuid == 0xFEED || uuid == 0xFEEC) { *type_out = TRK_TILE;    return true; } /* Tile */
        if (uuid == 0xFD5A || uuid == 0xFD59) { *type_out = TRK_SAMSUNG; return true; } /* SmartTag */
    }
    return false;
}

esp_ble_scan_params_t s_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x50,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
};

void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p)
{
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            esp_ble_gap_start_scanning(0);
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
            uint8_t type = 0;
            if (classify(p->scan_rst.ble_adv, &type)) {
                portENTER_CRITICAL_ISR(&s_mux);
                trk_record(p->scan_rst.bda, type, (int8_t)p->scan_rst.rssi, millis());
                portEXIT_CRITICAL_ISR(&s_mux);
            }
            break;
        }
        default: break;
    }
}
} // namespace

void TrackerMonitor::begin()
{
    _running   = true;
    _stats     = TrackerStats{};
    _accum_ms  = 0;
    _run_since = millis();
    _last_tick = millis();

    portENTER_CRITICAL(&s_mux);
    s_trk_n = 0;
    portEXIT_CRITICAL(&s_mux);

    BLEDevice::deinit(false);
    delay(50);
    BLEDevice::init("");
    _inited = true;
    esp_ble_gap_register_callback(&gap_cb);
    esp_ble_gap_set_scan_params(&s_scan_params);
}

void TrackerMonitor::stop()
{
    _running = false;
    if (_inited) {
        esp_ble_gap_stop_scanning();
        BLEDevice::deinit(false);
        _inited = false;
    }
}

void TrackerMonitor::pause()
{
    if (!_running) return;
    _accum_ms += millis() - _run_since;
    _running = false;
    esp_ble_gap_stop_scanning();
}

void TrackerMonitor::resume()
{
    if (_running) return;
    _run_since = millis();
    _last_tick = millis();
    _running = true;
    esp_ble_gap_start_scanning(0);
}

uint32_t TrackerMonitor::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

int TrackerMonitor::trackers(TrackerEntry* out, int max) const
{
    portENTER_CRITICAL(&s_mux);
    int n = s_trk_n < max ? s_trk_n : max;
    for (int i = 0; i < n; i++) out[i] = s_trk[i];
    portEXIT_CRITICAL(&s_mux);
    /* Closest first (strongest RSSI). */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[j].rssi > out[i].rssi) { TrackerEntry t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

void TrackerMonitor::loop()
{
    if (!_running) return;
    const uint32_t now = millis();
    if (now - _last_tick < 1000) return;
    _last_tick += 1000;

    uint16_t nearby = 0, persistent = 0;
    portENTER_CRITICAL(&s_mux);
    /* Drop stale entries (compact the array), then tally nearby/persistent. */
    int w = 0;
    for (int i = 0; i < s_trk_n; i++) {
        if (now - s_trk[i].last_ms > TrackerMonitor::STALE_MS) continue;
        if (w != i) s_trk[w] = s_trk[i];
        w++;
    }
    s_trk_n = w;
    for (int i = 0; i < s_trk_n; i++) {
        bool recent = (now - s_trk[i].last_ms) < TrackerMonitor::RECENT_MS;
        bool longlived = (s_trk[i].last_ms - s_trk[i].first_ms) > TrackerMonitor::PERSIST_MS;
        if (recent) nearby++;
        if (recent && longlived) persistent++;
    }
    portEXIT_CRITICAL(&s_mux);

    _stats.nearby     = nearby;
    _stats.persistent = persistent;
    _stats.alert      = (persistent > 0);
}
