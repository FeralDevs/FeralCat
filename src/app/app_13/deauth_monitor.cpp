/**
 * @file  deauth_monitor.cpp
 * @brief Deauth detector implementation (see deauth_monitor.h).
 */
#include "deauth_monitor.h"
#include <Arduino.h>
#include "esp_wifi.h"

namespace {
constexpr int     MAXATK   = 16;
portMUX_TYPE      s_mux    = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t s_cur    = 0;   /* frames in the current second */
volatile uint32_t s_total  = 0;

AttackerEntry     s_atk[MAXATK];
int               s_atk_n = 0;

/* Record one attacker frame (called inside the critical section). */
inline void atk_record(const uint8_t* src, const uint8_t* dst, int8_t rssi, uint8_t ch)
{
    for (int i = 0; i < s_atk_n; i++) {
        if (memcmp(s_atk[i].mac, src, 6) == 0) {
            if (s_atk[i].count < 0xFFFF) s_atk[i].count++;
            s_atk[i].rssi = rssi; s_atk[i].channel = ch;
            memcpy(s_atk[i].victim, dst, 6);
            return;
        }
    }
    if (s_atk_n < MAXATK) {
        AttackerEntry& e = s_atk[s_atk_n++];
        memcpy(e.mac, src, 6); memcpy(e.victim, dst, 6);
        e.count = 1; e.rssi = rssi; e.channel = ch;
    }
}

void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* fr = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) return;

    const uint8_t ftype = (fr[0] >> 2) & 0x3;
    const uint8_t fsub  = (fr[0] >> 4) & 0xF;
    if (ftype == 0 && (fsub == 12 /*deauth*/ || fsub == 10 /*disassoc*/)) {
        portENTER_CRITICAL_ISR(&s_mux);
        s_cur++;
        s_total++;
        atk_record(fr + 10 /*addr2 src*/, fr + 4 /*addr1 dst*/,
                   (int8_t)pkt->rx_ctrl.rssi, (uint8_t)pkt->rx_ctrl.channel);
        portEXIT_CRITICAL_ISR(&s_mux);
    }
}
} // namespace

void DeauthMonitor::begin()
{
    _running   = true;
    _stats     = DeauthStats{};
    for (int i = 0; i < HIST; i++) _hist[i] = 0;
    _hidx      = 0;
    _accum_ms  = 0;
    _run_since = millis();
    _last_tick = millis();

    portENTER_CRITICAL(&s_mux);
    s_cur = 0; s_total = 0; s_atk_n = 0;
    portEXIT_CRITICAL(&s_mux);

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

void DeauthMonitor::stop()
{
    _running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

void DeauthMonitor::pause()
{
    if (!_running) return;
    _accum_ms += millis() - _run_since;
    _running = false;
    esp_wifi_set_promiscuous(false);
}

void DeauthMonitor::resume()
{
    if (_running) return;
    _run_since = millis();
    _last_tick = millis();
    _running = true;
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

uint32_t DeauthMonitor::uptime_s() const
{
    uint32_t ms = _accum_ms + (_running ? millis() - _run_since : 0);
    return ms / 1000;
}

void DeauthMonitor::history(uint16_t* out) const
{
    /* Oldest..newest: ring starting at _hidx. */
    for (int i = 0; i < HIST; i++)
        out[i] = _hist[(_hidx + i) % HIST];
}

int DeauthMonitor::attackers(AttackerEntry* out, int max) const
{
    portENTER_CRITICAL(&s_mux);
    int n = s_atk_n < max ? s_atk_n : max;
    for (int i = 0; i < n; i++) out[i] = s_atk[i];
    portEXIT_CRITICAL(&s_mux);
    /* Sort by hit count desc (small table). */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[j].count > out[i].count) { AttackerEntry t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

void DeauthMonitor::_hop()
{
    uint8_t ch = _stats.channel + 1;
    if (ch > 13) ch = 1;
    _stats.channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void DeauthMonitor::loop()
{
    if (!_running) return;
    const uint32_t now = millis();

    /* Hop channels for broad coverage. */
    if (now - _last_hop >= 350) { _last_hop = now; _hop(); }

    /* 1 Hz: close the current second's bucket into history + stats. */
    if (now - _last_tick >= 1000) {
        _last_tick += 1000;
        uint16_t cur;
        uint32_t total;
        portENTER_CRITICAL(&s_mux);
        cur = s_cur; s_cur = 0; total = s_total;
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
