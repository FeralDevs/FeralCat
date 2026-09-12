/**
 * @file  probe_monitor.cpp
 * @brief Probe-request sniffer implementation (see probe_monitor.h).
 */
#include "probe_monitor.h"
#include <Arduino.h>
#include <cstring>
#include "esp_wifi.h"

namespace {
portMUX_TYPE      s_mux    = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t s_cur    = 0;   /* frames in the current second */
volatile uint32_t s_total  = 0;

ProbeEntry        s_dev[ProbeMonitor::MAXDEV];
int               s_dev_n  = 0;

/* Record one probe request (called inside the critical section). */
inline void dev_record(const uint8_t* src, const char* ssid, int8_t rssi, uint8_t ch)
{
    for (int i = 0; i < s_dev_n; i++) {
        if (memcmp(s_dev[i].mac, src, 6) == 0) {
            if (s_dev[i].count < 0xFFFF) s_dev[i].count++;
            s_dev[i].rssi = rssi; s_dev[i].channel = ch;
            if (ssid[0]) strncpy(s_dev[i].ssid, ssid, sizeof(s_dev[i].ssid) - 1);
            return;
        }
    }
    if (s_dev_n < ProbeMonitor::MAXDEV) {
        ProbeEntry& e = s_dev[s_dev_n++];
        memcpy(e.mac, src, 6);
        strncpy(e.ssid, ssid, sizeof(e.ssid) - 1);
        e.ssid[sizeof(e.ssid) - 1] = '\0';
        e.count = 1; e.rssi = rssi; e.channel = ch;
    }
}

void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* fr = pkt->payload;
    const int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    const uint8_t ftype = (fr[0] >> 2) & 0x3;
    const uint8_t fsub  = (fr[0] >> 4) & 0xF;
    if (ftype != 0 || fsub != 4) return;    /* not a probe request */

    /* First tagged parameter (offset 24) is the SSID element: id 0, len, bytes. */
    char ssid[33] = {0};
    if (len >= 26 && fr[24] == 0) {
        int slen = fr[25];
        if (slen > 32) slen = 32;
        if (24 + 2 + slen <= len) {
            memcpy(ssid, fr + 26, slen);
            ssid[slen] = '\0';
        }
    }

    portENTER_CRITICAL_ISR(&s_mux);
    s_cur++;
    s_total++;
    dev_record(fr + 10 /*addr2 src*/, ssid,
               (int8_t)pkt->rx_ctrl.rssi, (uint8_t)pkt->rx_ctrl.channel);
    portEXIT_CRITICAL_ISR(&s_mux);
}
} // namespace

void ProbeMonitor::begin()
{
    _running   = true;
    _stats     = ProbeStats{};
    for (int i = 0; i < HIST; i++) _hist[i] = 0;
    _hidx      = 0;
    _accum_ms  = 0;
    _run_since = millis();
    _last_tick = millis();

    portENTER_CRITICAL(&s_mux);
    s_cur = 0; s_total = 0; s_dev_n = 0;
    portEXIT_CRITICAL(&s_mux);

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

void ProbeMonitor::stop()
{
    _running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

void ProbeMonitor::pause()
{
    if (!_running) return;
    _accum_ms += millis() - _run_since;
    _running = false;
    esp_wifi_set_promiscuous(false);
}

void ProbeMonitor::resume()
{
    if (_running) return;
    _run_since = millis();
    _last_tick = millis();
    _running = true;
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

uint32_t ProbeMonitor::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

void ProbeMonitor::history(uint16_t* out) const
{
    for (int i = 0; i < HIST; i++)
        out[i] = _hist[(_hidx + i) % HIST];
}

int ProbeMonitor::devices(ProbeEntry* out, int max) const
{
    portENTER_CRITICAL(&s_mux);
    int n = s_dev_n < max ? s_dev_n : max;
    for (int i = 0; i < n; i++) out[i] = s_dev[i];
    portEXIT_CRITICAL(&s_mux);
    /* Sort by hit count desc (small table). */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[j].count > out[i].count) { ProbeEntry t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

void ProbeMonitor::_hop()
{
    uint8_t ch = _stats.channel + 1;
    if (ch > 13) ch = 1;
    _stats.channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void ProbeMonitor::loop()
{
    if (!_running) return;
    const uint32_t now = millis();

    if (now - _last_hop >= 350) { _last_hop = now; _hop(); }

    if (now - _last_tick >= 1000) {
        _last_tick += 1000;
        uint16_t cur;
        uint32_t total;
        int devn;
        portENTER_CRITICAL(&s_mux);
        cur = s_cur; s_cur = 0; total = s_total; devn = s_dev_n;
        portEXIT_CRITICAL(&s_mux);

        _hist[_hidx] = cur;
        _hidx = (_hidx + 1) % HIST;

        _stats.rate    = cur;
        _stats.total   = total;
        _stats.devices = (uint16_t)devn;
        if (cur > _stats.peak) _stats.peak = cur;
    }
}
