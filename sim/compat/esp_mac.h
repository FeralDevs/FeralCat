/* Desktop shim: esp_read_mac() used by the Settings About panel. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ESP_MAC_WIFI_STA = 0 } esp_mac_type_t;
static inline int esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
    (void)type;
    static const uint8_t m[6] = {0x3C, 0x84, 0x6A, 0x11, 0x22, 0x33};
    for (int i = 0; i < 6; i++) mac[i] = m[i];
    return 0;
}
#ifdef __cplusplus
}
#endif
