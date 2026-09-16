#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_BT_STATUS_SUCCESS = 0;
constexpr int BLE_SCAN_TYPE_PASSIVE = 0;
constexpr int BLE_ADDR_TYPE_PUBLIC = 0;
constexpr int BLE_SCAN_FILTER_ALLOW_ALL = 0;
constexpr int BLE_SCAN_DUPLICATE_DISABLE = 0;
constexpr int ESP_GAP_SEARCH_INQ_RES_EVT = 0;
constexpr int ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE = 0xff;
constexpr int ESP_BLE_AD_TYPE_SERVICE_DATA = 0x16;
enum esp_gap_ble_cb_event_t {
    ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,
    ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,
    ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT,
    ESP_GAP_BLE_SCAN_RESULT_EVT
};
struct esp_ble_scan_params_t {
    int scan_type, own_addr_type, scan_filter_policy;
    uint16_t scan_interval, scan_window;
    int scan_duplicate;
};
struct esp_ble_gap_cb_param_t {
    struct { int status = 0; } scan_param_cmpl, scan_start_cmpl, scan_stop_cmpl;
    struct {
        int search_evt = 0;
        uint8_t ble_adv[62]{};
        uint8_t bda[6]{};
        int ble_addr_type = 0;
        int rssi = -70;
    } scan_rst;
};
using esp_gap_ble_cb_t = void (*)(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t*);
esp_err_t esp_ble_gap_start_scanning(uint32_t duration);
esp_err_t esp_ble_gap_stop_scanning();
esp_err_t esp_ble_gap_register_callback(esp_gap_ble_cb_t callback);
esp_err_t esp_ble_gap_set_scan_params(esp_ble_scan_params_t* params);
uint8_t* esp_ble_resolve_adv_data(uint8_t* advert, int type, uint8_t* length);
