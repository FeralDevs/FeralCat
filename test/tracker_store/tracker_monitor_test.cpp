#include "tracker_monitor.h"
#include "Arduino.h"
#include "BLEDevice.h"
#include "esp_gap_ble_api.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; std::exit(1); } } while (false)
int tracker_test_lock_depth = 0;
void (*tracker_test_before_lock)() = nullptr;
static uint32_t clock_ms = 100;
static bool initialized = false;
static bool fail_init = false;
static esp_gap_ble_cb_t callback = nullptr;
static int starts = 0, stops = 0, deinitializations = 0;
static int start_result = 0, stop_result = 0, params_result = 0, register_result = 0;

uint32_t millis() { return clock_ms; }
void delay(uint32_t ms) { clock_ms += ms; }
void BLEDevice::init(const char*) { CHECK(tracker_test_lock_depth == 0); initialized = !fail_init; }
void BLEDevice::deinit(bool release_memory)
{
    CHECK(!release_memory && tracker_test_lock_depth == 0);
    initialized = false;
    ++deinitializations;
}
bool BLEDevice::getInitialized() { return initialized; }
esp_err_t esp_ble_gap_start_scanning(uint32_t duration)
{
    CHECK(duration == 0 && tracker_test_lock_depth == 0);
    ++starts;
    return start_result;
}
esp_err_t esp_ble_gap_stop_scanning()
{
    CHECK(tracker_test_lock_depth == 0);
    ++stops;
    return stop_result;
}
esp_err_t esp_ble_gap_register_callback(esp_gap_ble_cb_t handler)
{
    CHECK(tracker_test_lock_depth == 0);
    callback = handler;
    return register_result;
}
esp_err_t esp_ble_gap_set_scan_params(esp_ble_scan_params_t* params)
{
    CHECK(tracker_test_lock_depth == 0);
    CHECK(params->scan_type == BLE_SCAN_TYPE_PASSIVE);
    CHECK(params->scan_duplicate == BLE_SCAN_DUPLICATE_DISABLE);
    CHECK(params->scan_window == 0x50 && params->scan_interval == 0x50);
    return params_result;
}
uint8_t* esp_ble_resolve_adv_data(uint8_t* advert, int type, uint8_t* length)
{
    for (int offset = 0; offset < 31 && advert[offset]; offset += advert[offset] + 1) {
        if (offset + advert[offset] >= 31) break;
        if (advert[offset + 1] == type) {
            *length = advert[offset] - 1;
            return advert + offset + 2;
        }
    }
    *length = 0;
    return nullptr;
}

static void event(esp_gap_ble_cb_event_t kind, int status = ESP_BT_STATUS_SUCCESS)
{
    CHECK(callback);
    esp_ble_gap_cb_param_t p{};
    p.scan_param_cmpl.status = p.scan_start_cmpl.status = p.scan_stop_cmpl.status = status;
    callback(kind, &p);
    CHECK(tracker_test_lock_depth == 0);
}

static void advert(uint8_t address = 1, int rssi = -70, int address_type = 0)
{
    CHECK(callback);
    esp_ble_gap_cb_param_t p{};
    p.scan_rst.bda[0] = address;
    p.scan_rst.ble_addr_type = address_type;
    p.scan_rst.rssi = rssi;
    const uint8_t bytes[] = {4, 0xff, 0x4c, 0x00, 0x12, 0};
    std::memcpy(p.scan_rst.ble_adv, bytes, sizeof(bytes));
    callback(ESP_GAP_BLE_SCAN_RESULT_EVT, &p);
    CHECK(tracker_test_lock_depth == 0);
}

static void ready(TrackerMonitor& monitor)
{
    monitor.begin();
    CHECK(monitor.starting() && !monitor.running() && !monitor.error());
    event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    monitor.loop();
    CHECK(monitor.starting() && !monitor.running());
    event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    monitor.loop();
    CHECK(monitor.running() && !monitor.starting());
}

static void reset_fakes()
{
    tracker_test_before_lock = nullptr;
    start_result = stop_result = params_result = register_result = 0;
    fail_init = false;
    starts = stops = deinitializations = 0;
    clock_ms += 100;
}

static void newer_callback_timestamp_before_pruning()
{
    reset_fakes();
    TrackerMonitor monitor;
    ready(monitor);
    clock_ms += 1000; // housekeeping is due
    tracker_test_before_lock = [] {
        tracker_test_before_lock = nullptr;
        ++clock_ms;
        advert(7, -45); // delivered after loop's initial time read
    };
    monitor.loop();
    TrackerEntry rows[24]{};
    CHECK(monitor.trackers(rows, 24) == 1 && rows[0].mac[0] == 7);
    CHECK(monitor.stats().nearby == 1);
    monitor.stop();
}

static void pause_before_parameters_and_stop_ignores_callbacks()
{
    reset_fakes();
    TrackerMonitor monitor;
    monitor.begin();
    monitor.pause();
    event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    monitor.loop();
    CHECK(starts == 0 && !monitor.starting() && !monitor.running());
    monitor.resume();
    CHECK(starts == 1 && monitor.starting());
    monitor.stop();
    event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    advert();
    monitor.loop();
    TrackerEntry rows[24]{};
    CHECK(starts == 1 && monitor.trackers(rows, 24) == 0 && !monitor.running());
}

static void pause_during_start_then_resume_during_stop()
{
    reset_fakes();
    TrackerMonitor monitor;
    monitor.begin();
    event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    monitor.loop();
    CHECK(starts == 1);
    monitor.pause();
    CHECK(stops == 0);
    event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    advert(); // must not record a late packet while paused
    monitor.loop();
    CHECK(stops == 1 && !monitor.running());
    monitor.resume();
    CHECK(starts == 1 && monitor.starting());
    event(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT);
    monitor.loop();
    CHECK(starts == 2 && monitor.starting());
    event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    monitor.loop();
    TrackerEntry rows[24]{};
    CHECK(monitor.running() && monitor.trackers(rows, 24) == 0);
    advert();
    CHECK(monitor.trackers(rows, 24) == 1 && rows[0].sequence == 1);
    // Duplicate/out-of-order acknowledgements cannot start a second scanner.
    event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    event(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT);
    monitor.loop();
    CHECK(starts == 2 && monitor.running());
    monitor.stop();
}

static void repeated_samples_sorting_and_selected_identity()
{
    reset_fakes();
    TrackerMonitor monitor;
    ready(monitor);
    TrackerEntry rows[24]{}, target{};
    advert(1, -80);
    CHECK(monitor.trackers(rows, 24) == 1);
    const uint32_t selected = rows[0].id;
    CHECK(monitor.select(selected));
    ++clock_ms;
    advert(2, -40);
    CHECK(monitor.trackers(rows, 1) == 1 && rows[0].mac[0] == 2);
    CHECK(monitor.tracker(selected, target) && target.mac[0] == 1);
    for (uint8_t i = 3; i < 40; ++i) { ++clock_ms; advert(i); }
    CHECK(monitor.trackers(rows, 24) == 24);
    CHECK(monitor.tracker(selected, target));
    CHECK(!monitor.select(UINT32_MAX));
    ++clock_ms;
    advert(1, 127); // sentinel cannot count as a fresh sample
    CHECK(monitor.tracker(selected, target) && target.sequence == 1);
    advert(1, -80);
    CHECK(monitor.tracker(selected, target) && target.sequence == 2);
    clock_ms += 1000;
    monitor.loop();
    CHECK(monitor.stats().evicted == 15);
    CHECK(monitor.trackers(nullptr, 1) == 0 && monitor.trackers(rows, -1) == 0);
    clock_ms += TrackerMonitor::STALE_MS + 1;
    monitor.loop();
    CHECK(monitor.trackers(rows, 24) == 1 && rows[0].id == selected);
    CHECK(monitor.select(0));
    clock_ms += 1000;
    monitor.loop();
    CHECK(monitor.trackers(rows, 24) == 0);
    monitor.stop();
}

static void pause_invalidates_each_observation_until_its_next_packet()
{
    reset_fakes();
    TrackerMonitor monitor;
    ready(monitor);
    advert(1, -35);
    advert(2, -40);
    TrackerEntry rows[24]{};
    CHECK(monitor.trackers(rows, 24) == 2 && rows[0].scan_fresh && rows[1].scan_fresh);
    const uint32_t id = rows[0].id;
    monitor.pause();
    CHECK(monitor.trackers(rows, 24) == 2 && !rows[0].scan_fresh && !rows[1].scan_fresh);
    event(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT);
    monitor.resume();
    event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    monitor.loop();
    TrackerEntry first{};
    CHECK(monitor.running() && monitor.tracker(id, first) && !first.scan_fresh);
    advert(1, 127);
    CHECK(monitor.tracker(id, first) && !first.scan_fresh && first.sequence == 1);
    ++clock_ms;
    advert(1, -95);
    CHECK(monitor.tracker(id, first) && first.scan_fresh && first.sequence == 2 && first.filtered_rssi == -95);
    CHECK(monitor.trackers(rows, 24) == 2 && !rows[0].scan_fresh && rows[1].scan_fresh);
    monitor.stop();
    CHECK(monitor.tracker(id, first) && !first.scan_fresh);
}

static void errors_cleanup_and_reopen()
{
    for (int failure = 0; failure < 11; ++failure) {
        reset_fakes();
        TrackerMonitor monitor;
        if (failure == 0) fail_init = true;
        if (failure == 1) register_result = 1;
        if (failure == 2) params_result = 1;
        if (failure == 3) start_result = 1;
        monitor.begin();
        if (failure >= 3 && failure != 9) {
            event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, failure == 4 ? 1 : 0);
            monitor.loop();
        }
        if (failure == 5) event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT, 1);
        if (failure == 6) clock_ms += 3000; // missing start acknowledgement
        if (failure == 7 || failure == 8 || failure == 10) {
            event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
            monitor.loop();
            if (failure == 8) stop_result = 1;
            monitor.pause();
            if (failure == 7) event(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT, 1);
            if (failure == 10) clock_ms += 3000; // missing stop acknowledgement
        }
        if (failure == 9) clock_ms += 3000; // missing parameter acknowledgement
        monitor.loop();
        CHECK(monitor.error() && !monitor.running() && !monitor.starting());
        CHECK(!initialized);
        monitor.resume(); // explicit begin is required to retry after failure
        CHECK(!monitor.running() && !monitor.starting());
        reset_fakes();
        ready(monitor);
        CHECK(!monitor.error());
        monitor.stop();
    }
}

int main()
{
    pause_before_parameters_and_stop_ignores_callbacks();
    pause_during_start_then_resume_during_stop();
    repeated_samples_sorting_and_selected_identity();
    pause_invalidates_each_observation_until_its_next_packet();
    newer_callback_timestamp_before_pruning();
    errors_cleanup_and_reopen();
    std::cout << "tracker_monitor: async lifecycle, error cleanup, snapshots and target pin checks passed\n";
}
