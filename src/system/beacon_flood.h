/**
 * @file  beacon_flood.h
 * @brief Passive beacon/probe-response flood detector (Karma / Wi-Fi-Pineapple).
 *        Counts management beacon (subtype 8) and probe-response (subtype 5)
 *        frames and, crucially, the number of DISTINCT APs seen per second — a
 *        flood of fake APs (or an AP answering every probe) spikes this far
 *        above a normal environment. Listen-only.
 */
#pragma once
#include <cstdint>

struct FloodStats {
    uint32_t total     = 0;   /* beacon+probe-resp frames since start */
    uint16_t rate      = 0;   /* those frames in the last second      */
    uint16_t uniq      = 0;   /* distinct APs seen in the last second */
    uint16_t proberesp = 0;   /* probe-responses in the last second   */
    uint16_t peak_uniq = 0;
    bool     alert     = false;
    uint8_t  channel   = 1;
};

class BeaconFlood {
public:
    static constexpr int HIST      = 30;
    static constexpr int ALERT_UNIQ = 25;   /* distinct APs/sec = flood */

    void begin();
    void stop();
    void loop();
    void pause();
    void resume();
    bool running() const { return _running; }

    const FloodStats& stats() const { return _stats; }
    void history(uint16_t* out) const;   /* uniq-per-second, oldest..newest */
    uint32_t uptime_s() const;

private:
    bool     _running     = false;
    uint32_t _last_hop    = 0;
    uint32_t _last_tick   = 0;
    uint32_t _accum_ms    = 0;
    uint32_t _run_since   = 0;
    uint32_t _alert_until = 0;
    FloodStats _stats;
    uint16_t _hist[HIST] = {0};
    int      _hidx = 0;

    void _hop();
};
