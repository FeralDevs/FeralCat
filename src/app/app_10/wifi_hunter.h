/**
 * @file  wifi_hunter.h
 * @brief MeowGotchi WiFi engine — promiscuous sniffer, channel hopping,
 *        AP/client discovery, WPA-handshake (EAPOL) capture to .pcap on SD,
 *        and an opt-in deauthentication mode.
 *
 * Authorized use only. Sniffing, deauth and handshake capture are for testing
 * networks you own or have explicit permission to assess, and for education.
 *
 * The ESP32 promiscuous RX callback runs in the WiFi task and must stay short:
 * it only updates counters and copies EAPOL frames into a small queue. The
 * heavy work (writing .pcap to SD) happens in loop() on the app task.
 */
#pragma once
#include <cstdint>

struct HunterStats {
    uint16_t aps      = 0;   /* distinct BSSIDs seen              */
    uint16_t stas     = 0;   /* distinct client stations seen     */
    uint16_t shakes   = 0;   /* WPA handshakes captured           */
    uint16_t deauths  = 0;   /* deauth frames sent (aggressive)   */
    uint16_t eapol    = 0;   /* EAPOL frames observed             */
    uint8_t  channel  = 1;   /* current hop channel               */
    uint32_t start_ms = 0;   /* millis() at begin()               */
};

class WifiHunter {
public:
    /** Enter promiscuous mode on the already-started STA interface. */
    void begin(bool sd_ready);

    /** Leave promiscuous mode and restore normal STA operation. */
    void stop();

    /** Suspend/resume sniffing without clearing the stats gathered so far. */
    void pause();
    void resume();
    bool running() const { return _running; }

    /** Channel-hop tick + drain the handshake queue to SD. Call frequently. */
    void loop();

    void setAggressive(bool on) { _aggressive = on; }
    bool aggressive() const     { return _aggressive; }
    bool sdReady()    const     { return _sd_ready; }

    const HunterStats& stats() const { return _stats; }

    /** Seconds spent actively hunting (excludes paused time). */
    uint32_t uptime_s() const;

private:
    bool          _aggressive = false;
    bool          _sd_ready   = false;
    bool          _running    = false;
    uint32_t      _last_hop   = 0;
    uint32_t      _last_deauth= 0;
    uint32_t      _accum_ms   = 0;   /* hunting time banked while paused   */
    uint32_t      _run_since  = 0;   /* millis() of the current run window */
    HunterStats   _stats;

    void _hop();
    void _drainHandshakes();
    void _sendDeauth();
};
