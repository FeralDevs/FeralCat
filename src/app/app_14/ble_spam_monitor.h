/**
 * @file  ble_spam_monitor.h
 * @brief Passive BLE-spam detector — listens (passive scan) for the flood of
 *        Apple Continuity / Google FastPair / Microsoft SwiftPair / Samsung
 *        EasySetup advertisements that BLE-spam tools blast out, and alerts on
 *        a burst. Listen-only; never advertises.
 */
#pragma once
#include <cstdint>

struct BleSpamStats {
    uint32_t total   = 0;   /* spam-pattern adverts seen since start */
    uint16_t rate    = 0;   /* spam adverts in the last second       */
    uint16_t peak    = 0;
    bool     alert   = false;
    uint16_t apple   = 0;   /* per-vector running totals             */
    uint16_t google  = 0;
    uint16_t ms      = 0;
    uint16_t samsung = 0;
};

class BleSpamMonitor {
public:
    static constexpr int HIST            = 30;
    static constexpr int ALERT_THRESHOLD = 12;   /* spam adverts/sec = flood */

    void begin();
    void stop();
    void loop();

    void pause();
    void resume();
    bool running() const { return _running; }

    const BleSpamStats& stats() const { return _stats; }
    void history(uint16_t* out) const;
    uint32_t uptime_s() const;

private:
    bool     _running     = false;
    bool     _inited      = false;
    uint32_t _last_tick   = 0;
    uint32_t _accum_ms    = 0;
    uint32_t _run_since   = 0;
    uint32_t _alert_until = 0;
    BleSpamStats _stats;

    uint16_t _hist[HIST] = {0};
    int      _hidx = 0;
};
