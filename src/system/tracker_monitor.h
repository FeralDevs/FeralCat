/**
 * @file  tracker_monitor.h
 * @brief Passive BLE tracker-candidate monitor and RSSI finder data.
 *
 * Addresses may rotate, so an entry is an observation identity within one
 * scan session, not proof of a particular physical device or of following.
 */
#pragma once
#include <cstdint>

enum TrackerType : uint8_t { TRK_APPLE = 0, TRK_TILE = 1, TRK_SAMSUNG = 2 };

struct TrackerStats {
    uint16_t nearby     = 0;   /* trackers seen recently          */
    uint16_t persistent = 0;   /* trackers present > PERSIST_S    */
    bool     alert      = false;
    uint32_t evicted    = 0;   /* older unselected observations replaced */
};

struct TrackerEntry {
    uint8_t  mac[6];
    uint8_t  type;        /* TrackerType */
    int8_t   rssi;        /* last signal (higher = closer)     */
    uint16_t count;
    uint32_t first_ms;    /* first seen                        */
    uint32_t last_ms;     /* last seen                         */
    // Keep the six original fields above in order for simulator aggregates.
    uint32_t id            = 0;      /* stable, nonzero within this session */
    uint32_t sequence      = 0;      /* changes only for a valid new sample */
    uint8_t  addr_type     = 0;
    int16_t  filtered_rssi = -127;   /* median + EWMA, in dBm */
    bool     scan_fresh    = false;  /* valid observation since last pause */
};

class TrackerMonitor {
public:
    static constexpr int MAXTRK    = 24;
    static constexpr uint32_t PERSIST_MS = 60000;   /* observed over > 60 s */
    static constexpr uint32_t RECENT_MS  = 30000;   /* seen in last 30 s = "nearby" */
    static constexpr uint32_t STALE_MS   = 300000;  /* drop after 5 min unseen      */

    void begin();
    void stop();
    void loop();

    void pause();
    void resume();
    bool running() const { return _running; }
    bool starting() const { return _starting; }
    const char* error() const;

    const TrackerStats& stats() const { return _stats; }
    /* Strongest filtered RSSI first. Signal strength is not measured range. */
    int  trackers(TrackerEntry* out, int max) const;
    bool tracker(uint32_t id, TrackerEntry& out) const;
    bool select(uint32_t id);   /* false if absent; 0 releases the selection */
    uint32_t uptime_s() const;

private:
    bool     _running   = false;
    bool     _starting  = false;
    uint32_t _accum_ms  = 0;
    uint32_t _run_since = 0;
    uint32_t _last_tick = 0;
    bool     _inited    = false;
    TrackerStats _stats;
};
