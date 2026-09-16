/**
 * @file  tracker_monitor.cpp
 * @brief Unwanted-tracker detector implementation (see tracker_monitor.h).
 */
#include "tracker_monitor.h"
#include "tracker_store.h"
#include <Arduino.h>
#include <cstring>
#include <BLEDevice.h>
#include "esp_gap_ble_api.h"

namespace {
constexpr int MAXTRK = TrackerMonitor::MAXTRK;
portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;

TrackerStore s_store;
enum class ScanPhase { Off, Parameters, Ready, Starting, Running, Stopping, Failed };
ScanPhase s_phase = ScanPhase::Off;
bool s_enabled = false;
bool s_wanted = false;
uint32_t s_phase_since = 0;
const char* s_error = nullptr;
constexpr uint32_t SCAN_ACK_TIMEOUT_MS = 3000;

// All control fields and the store are protected by s_mux. Callbacks only
// acknowledge commands; only foreground code issues start/stop requests.
void fail_locked(const char* reason)
{
    s_error = reason;
    s_wanted = false;
    s_phase = ScanPhase::Failed;
}

void drive_scan(uint32_t now)
{
    enum class Action { None, Start, Stop } action = Action::None;
    portENTER_CRITICAL(&s_mux);
    if (s_enabled && !s_error) {
        if ((s_phase == ScanPhase::Parameters || s_phase == ScanPhase::Starting ||
             s_phase == ScanPhase::Stopping) && now - s_phase_since >= SCAN_ACK_TIMEOUT_MS) {
            fail_locked("BLE scan timed out");
        } else if (s_phase == ScanPhase::Ready && s_wanted) {
            s_phase = ScanPhase::Starting;
            s_phase_since = now;
            action = Action::Start;
        } else if (s_phase == ScanPhase::Running && !s_wanted) {
            s_phase = ScanPhase::Stopping;
            s_phase_since = now;
            action = Action::Stop;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    esp_err_t result = ESP_OK;
    if (action == Action::Start) result = esp_ble_gap_start_scanning(0);
    if (action == Action::Stop) result = esp_ble_gap_stop_scanning();
    if (result != ESP_OK) {
        portENTER_CRITICAL(&s_mux);
        if (s_enabled) fail_locked(action == Action::Start ? "BLE scan start failed" : "BLE scan stop failed");
        portEXIT_CRITICAL(&s_mux);
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

esp_ble_scan_params_t s_scan_params = [] {
    esp_ble_scan_params_t params{};
    params.scan_type = BLE_SCAN_TYPE_PASSIVE;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    params.scan_interval = 0x50;
    params.scan_window = 0x50;
    params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    return params;
}();

void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p)
{
    if (!p) return;
    portENTER_CRITICAL(&s_mux);
    if (!s_enabled) { portEXIT_CRITICAL(&s_mux); return; }
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            if (s_phase == ScanPhase::Parameters) {
                if (p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) s_phase = ScanPhase::Ready;
                else fail_locked("BLE scan setup failed");
            }
            break;
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            if (s_phase == ScanPhase::Starting) {
                if (p->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) s_phase = ScanPhase::Running;
                else fail_locked("BLE scan start failed");
            }
            break;
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (s_phase == ScanPhase::Stopping) {
                if (p->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) s_phase = ScanPhase::Ready;
                else fail_locked("BLE scan stop failed");
            }
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (!s_wanted || s_phase != ScanPhase::Running ||
                p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
            uint8_t type = 0;
            if (classify(p->scan_rst.ble_adv, &type)) {
                s_store.record(p->scan_rst.bda, static_cast<uint8_t>(p->scan_rst.ble_addr_type),
                               type, p->scan_rst.rssi, millis());
            }
            break;
        }
        default: break;
    }
    portEXIT_CRITICAL(&s_mux);
}
} // namespace

void TrackerMonitor::begin()
{
    stop();
    _running   = false;
    _starting  = true;
    _stats     = TrackerStats{};
    _accum_ms  = 0;
    _run_since = millis();
    _last_tick = millis();

    // This app retains the existing exclusive BLE-stack lifecycle. It does
    // not share the stack with advertising/HID apps; see their own lifecycle.
    BLEDevice::deinit(false);
    delay(50);
    BLEDevice::init("");
    _inited = BLEDevice::getInitialized();
    portENTER_CRITICAL(&s_mux);
    s_store.clear();
    s_error = nullptr;
    s_enabled = _inited;
    s_wanted = _inited;
    s_phase = ScanPhase::Parameters;
    s_phase_since = millis();
    if (!_inited) fail_locked("BLE initialization failed");
    portEXIT_CRITICAL(&s_mux);
    if (_inited) {
        esp_err_t result = esp_ble_gap_register_callback(&gap_cb);
        if (result == ESP_OK) result = esp_ble_gap_set_scan_params(&s_scan_params);
        if (result != ESP_OK) {
            portENTER_CRITICAL(&s_mux);
            fail_locked("BLE scan setup failed");
            portEXIT_CRITICAL(&s_mux);
        }
    }
    loop();
}

void TrackerMonitor::stop()
{
    if (_running) _accum_ms += millis() - _run_since;
    _running = false;
    _starting = false;
    // Gate callbacks before requesting asynchronous stop or deinitialization.
    portENTER_CRITICAL(&s_mux);
    s_enabled = false;
    s_wanted = false;
    s_store.invalidateScan();
    s_phase = ScanPhase::Off;
    portEXIT_CRITICAL(&s_mux);
    if (_inited) {
        esp_ble_gap_stop_scanning();
        BLEDevice::deinit(false);
        _inited = false;
    }
}

void TrackerMonitor::pause()
{
    if (!_inited) return;
    if (_running) _accum_ms += millis() - _run_since;
    _running = false;
    _starting = false;
    portENTER_CRITICAL(&s_mux);
    s_wanted = false;
    s_store.invalidateScan();
    portEXIT_CRITICAL(&s_mux);
    drive_scan(millis());
}

void TrackerMonitor::resume()
{
    if (!_inited || _running || _starting || error()) return;
    portENTER_CRITICAL(&s_mux);
    s_wanted = true;
    portEXIT_CRITICAL(&s_mux);
    _starting = true;
    loop();
}

const char* TrackerMonitor::error() const
{
    portENTER_CRITICAL(&s_mux);
    const char* result = s_error;
    portEXIT_CRITICAL(&s_mux);
    return result;
}

uint32_t TrackerMonitor::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

int TrackerMonitor::trackers(TrackerEntry* out, int max) const
{
    if (!out || max <= 0) return 0;
    TrackerEntry snapshot[MAXTRK];
    portENTER_CRITICAL(&s_mux);
    int n = s_store.snapshot(snapshot, MAXTRK);
    portEXIT_CRITICAL(&s_mux);
    /* Stable ties avoid shuffling equal-strength rows. */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (snapshot[j].filtered_rssi > snapshot[i].filtered_rssi ||
                (snapshot[j].filtered_rssi == snapshot[i].filtered_rssi && snapshot[j].id < snapshot[i].id)) {
                TrackerEntry t = snapshot[i]; snapshot[i] = snapshot[j]; snapshot[j] = t;
            }
    if (n > max) n = max;
    for (int i = 0; i < n; ++i) out[i] = snapshot[i];
    return n;
}

bool TrackerMonitor::tracker(uint32_t id, TrackerEntry& out) const
{
    portENTER_CRITICAL(&s_mux);
    const bool found = s_store.get(id, out);
    portEXIT_CRITICAL(&s_mux);
    return found;
}

bool TrackerMonitor::select(uint32_t id)
{
    portENTER_CRITICAL(&s_mux);
    const bool selected = s_store.select(id);
    portEXIT_CRITICAL(&s_mux);
    return selected;
}

void TrackerMonitor::loop()
{
    const uint32_t now = millis();
    drive_scan(now);
    portENTER_CRITICAL(&s_mux);
    const bool running = s_enabled && s_wanted && s_phase == ScanPhase::Running;
    const bool starting = s_enabled && s_wanted && !running && !s_error;
    const bool failed = s_error != nullptr;
    portEXIT_CRITICAL(&s_mux);
    if (_running && !running) _accum_ms += now - _run_since;
    if (!_running && running) _run_since = now;
    _running = running;
    _starting = starting;
    // A rejected command/ack timeout must not leave an unobserved radio scan
    // running. Keep the diagnostic available until the next begin().
    if (failed && _inited) stop();
    if (now - _last_tick < 1000) return;
    _last_tick = now;

    uint16_t nearby = 0, persistent = 0;
    TrackerEntry snapshot[MAXTRK];
    portENTER_CRITICAL(&s_mux);
    // Read time after taking the lock: a BLE callback may have stamped a new
    // sample after this loop began. An older `now` would wrap its age to ~49d.
    const uint32_t observed_now = millis();
    s_store.prune(observed_now);
    const int n = s_store.snapshot(snapshot, MAXTRK);
    const uint32_t evicted = s_store.evicted();
    portEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) {
        bool recent = (observed_now - snapshot[i].last_ms) < TrackerMonitor::RECENT_MS;
        bool longlived = (snapshot[i].last_ms - snapshot[i].first_ms) > TrackerMonitor::PERSIST_MS;
        if (recent) nearby++;
        if (recent && longlived) persistent++;
    }
    _stats.nearby     = nearby;
    _stats.persistent = persistent;
    _stats.alert      = (persistent > 0);
    _stats.evicted    = evicted;
}
