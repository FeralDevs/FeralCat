/**
 * @file  deauth_monitor.h
 * @brief Passive deauth/disassoc detector — counts 802.11 deauthentication and
 *        disassociation management frames (the signature of a WiFi kick attack)
 *        via promiscuous mode, with a per-second rate and short history.
 *
 * Defensive/educational: it only listens, never transmits.
 */
#pragma once
#include <cstdint>

struct DeauthStats {
    uint32_t total   = 0;   /* deauth+disassoc frames seen since start */
    uint16_t rate    = 0;   /* frames in the last completed second     */
    uint16_t peak    = 0;   /* highest per-second rate seen            */
    uint8_t  channel = 1;
    bool     alert   = false; /* a burst was seen in the last few sec  */
};

class DeauthMonitor {
public:
    static constexpr int  HIST      = 30;   /* seconds shown in the graph */
    static constexpr int  ALERT_THRESHOLD = 6;   /* frames/sec = attack   */

    void begin();
    void stop();
    void loop();                 /* channel hop + 1 Hz bucket tick */

    void pause();
    void resume();
    bool running() const { return _running; }

    const DeauthStats& stats() const { return _stats; }
    /* Copy HIST per-second counts oldest..newest into out[HIST]. */
    void history(uint16_t* out) const;
    uint32_t uptime_s() const;

private:
    bool     _running     = false;
    uint32_t _last_hop    = 0;
    uint32_t _last_tick   = 0;
    uint32_t _accum_ms    = 0;
    uint32_t _run_since   = 0;
    uint32_t _alert_until = 0;
    DeauthStats _stats;

    uint16_t _hist[HIST] = {0};
    int      _hidx = 0;

    void _hop();
};
