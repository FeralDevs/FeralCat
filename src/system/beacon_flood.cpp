/**
 * @file  beacon_flood.cpp
 * @brief Beacon/probe-response flood detector implementation.
 */
#include "beacon_flood.h"
#include <Arduino.h>
#include <cstring>
#include "esp_wifi.h"

namespace {
constexpr int     MAXBSS = 128;
portMUX_TYPE      s_mux   = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t s_cur   = 0;   /* beacon+proberesp this second */
volatile uint16_t s_pr    = 0;   /* probe-responses this second  */
volatile uint32_t s_total = 0;
uint8_t           s_bss[MAXBSS][6];   /* distinct BSSIDs this second */
int               s_bss_n = 0;

inline void bss_add(const uint8_t* b) {
    for (int i = 0; i < s_bss_n; i++) if (memcmp(s_bss[i], b, 6) == 0) return;
    if (s_bss_n < MAXBSS) memcpy(s_bss[s_bss_n++], b, 6);
}

void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* fr = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) return;
    const uint8_t ftype = (fr[0] >> 2) & 0x3;
    const uint8_t fsub  = (fr[0] >> 4) & 0xF;
    if (ftype == 0 && (fsub == 8 /*beacon*/ || fsub == 5 /*probe-resp*/)) {
        portENTER_CRITICAL_ISR(&s_mux);
        s_cur++; s_total++;
        if (fsub == 5) s_pr++;
        bss_add(fr + 16 /*addr3 = bssid*/);
        portEXIT_CRITICAL_ISR(&s_mux);
    }
}
} // namespace

void BeaconFlood::begin()
{
    _running = true;
    _stats = FloodStats{};
    for (int i = 0; i < HIST; i++) _hist[i] = 0;
    _hidx = 0; _accum_ms = 0; _run_since = millis(); _last_tick = millis();

    portENTER_CRITICAL(&s_mux);
    s_cur = s_pr = 0; s_total = 0; s_bss_n = 0;
    portEXIT_CRITICAL(&s_mux);

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

void BeaconFlood::stop()
{
    _running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

void BeaconFlood::pause()
{
    if (!_running) return;
    _accum_ms += millis() - _run_since;
    _running = false;
    esp_wifi_set_promiscuous(false);
}

void BeaconFlood::resume()
{
    if (_running) return;
    _run_since = millis(); _last_tick = millis();
    _running = true;
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

uint32_t BeaconFlood::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

void BeaconFlood::history(uint16_t* out) const
{
    for (int i = 0; i < HIST; i++) out[i] = _hist[(_hidx + i) % HIST];
}

void BeaconFlood::_hop()
{
    uint8_t ch = _stats.channel + 1;
    if (ch > 13) ch = 1;
    _stats.channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void BeaconFlood::loop()
{
    if (!_running) return;
    const uint32_t now = millis();
    if (now - _last_hop >= 300) { _last_hop = now; _hop(); }
    if (now - _last_tick >= 1000) {
        _last_tick += 1000;
        uint16_t cur, pr, uniq;
        uint32_t total;
        portENTER_CRITICAL(&s_mux);
        cur = s_cur; pr = s_pr; uniq = (uint16_t)s_bss_n; total = s_total;
        s_cur = 0; s_pr = 0; s_bss_n = 0;
        portEXIT_CRITICAL(&s_mux);

        _hist[_hidx] = uniq; _hidx = (_hidx + 1) % HIST;
        _stats.rate = cur; _stats.proberesp = pr; _stats.uniq = uniq; _stats.total = total;
        if (uniq > _stats.peak_uniq) _stats.peak_uniq = uniq;
        if (uniq >= ALERT_UNIQ) _alert_until = now + 4000;
    }
    _stats.alert = (now < _alert_until);
}
