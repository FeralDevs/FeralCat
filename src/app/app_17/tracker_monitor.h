/**
 * @file  tracker_monitor.h
 * @brief Unwanted-tracker detector — passively scans BLE for item trackers
 *        (Apple Find My / AirTag, Tile, Samsung SmartTag) and flags any that
 *        have stayed near you long enough to suggest you're being followed.
 *
 * Defensive/educational: listen-only. Note: Find My trackers rotate their MAC
 * roughly every 15 min, so a single tracker may reappear as a "new" entry —
 * persistence timing is best-effort, not forensic.
 */
#pragma once
#include <cstdint>

enum TrackerType : uint8_t { TRK_APPLE = 0, TRK_TILE = 1, TRK_SAMSUNG = 2 };

struct TrackerStats {
    uint16_t nearby     = 0;   /* trackers seen recently          */
    uint16_t persistent = 0;   /* trackers present > PERSIST_S    */
    bool     alert      = false;
};

struct TrackerEntry {
    uint8_t  mac[6];
    uint8_t  type;        /* TrackerType */
    int8_t   rssi;        /* last signal (higher = closer)     */
    uint16_t count;
    uint32_t first_ms;    /* first seen                        */
    uint32_t last_ms;     /* last seen                         */
};

class TrackerMonitor {
public:
    static constexpr int MAXTRK    = 24;
    static constexpr uint32_t PERSIST_MS = 60000;   /* present > 60 s = follows you */
    static constexpr uint32_t RECENT_MS  = 30000;   /* seen in last 30 s = "nearby" */
    static constexpr uint32_t STALE_MS   = 300000;  /* drop after 5 min unseen      */

    void begin();
    void stop();
    void loop();

    void pause();
    void resume();
    bool running() const { return _running; }

    const TrackerStats& stats() const { return _stats; }
    /* Copy up to max tracker rows (closest first) into out; returns count. */
    int  trackers(TrackerEntry* out, int max) const;
    uint32_t uptime_s() const;

private:
    bool     _running   = false;
    uint32_t _accum_ms  = 0;
    uint32_t _run_since = 0;
    uint32_t _last_tick = 0;
    bool     _inited    = false;
    TrackerStats _stats;
};
