/**
 * @file  probe_monitor.h
 * @brief Passive probe-request sniffer — logs the 802.11 probe-request frames
 *        that nearby phones/laptops broadcast while looking for known networks,
 *        capturing the source MAC, the requested SSID (when not a wildcard),
 *        signal, and channel.
 *
 * Defensive/educational: it only listens, never transmits. Useful to see which
 * devices are around and which network names they leak.
 */
#pragma once
#include <cstdint>

struct ProbeStats {
    uint32_t total   = 0;   /* probe-request frames seen since start */
    uint16_t rate    = 0;   /* frames in the last completed second   */
    uint16_t peak    = 0;   /* highest per-second rate seen          */
    uint16_t devices = 0;   /* distinct source MACs seen             */
    uint8_t  channel = 1;
};

/* One probing device. `ssid` holds the last non-empty network name it asked
 * for (empty string = only broadcast/wildcard probes seen). */
struct ProbeEntry {
    uint8_t  mac[6];
    char     ssid[33];
    uint16_t count;
    int8_t   rssi;      /* last seen signal (higher = closer) */
    uint8_t  channel;
};

class ProbeMonitor {
public:
    static constexpr int HIST   = 30;   /* seconds shown in the graph */
    static constexpr int MAXDEV = 24;   /* tracked distinct devices   */

    void begin();
    void stop();
    void loop();                 /* channel hop + 1 Hz bucket tick */

    void pause();
    void resume();
    bool running() const { return _running; }

    const ProbeStats& stats() const { return _stats; }
    /* Copy HIST per-second counts oldest..newest into out[HIST]. */
    void history(uint16_t* out) const;
    /* Copy up to max device rows (sorted by hit count desc) into out;
     * returns the number written. */
    int  devices(ProbeEntry* out, int max) const;
    uint32_t uptime_s() const;

private:
    bool     _running   = false;
    uint32_t _last_hop  = 0;
    uint32_t _last_tick = 0;
    uint32_t _accum_ms  = 0;
    uint32_t _run_since = 0;
    ProbeStats _stats;

    uint16_t _hist[HIST] = {0};
    int      _hidx = 0;

    void _hop();
};
