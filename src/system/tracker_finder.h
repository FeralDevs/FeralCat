/** Selected-target search state. Portable; no radio, display or allocation. */
#pragma once
#include "tracker_monitor.h"
#include <cstdint>

class TrackerFinder {
public:
    static constexpr unsigned HISTORY_SIZE = 48;
    static constexpr uint32_t FRESH_MS = 2500;
    static constexpr uint32_t LOST_MS = 10000;
    static constexpr uint32_t RESET_MS = 5000;
    enum class State { Live, Waiting, Lost, Paused };

    void reset() { *this = TrackerFinder{}; }
    void select(const TrackerEntry& entry, uint32_t now, bool running) {
        reset();
        target_ = entry;
        haveTarget_ = entry.id != 0;
        wasRunning_ = running;
        if (haveTarget_ && running && entry.scan_fresh && uint32_t(now - entry.last_ms) <= FRESH_MS)
            append(entry);
    }

    void update(const TrackerEntry* entry, uint32_t now, bool running) {
        if (!haveTarget_) return;
        const bool matches = entry && entry->id == target_.id;
        if (!running) {
            if (wasRunning_) clearHistory();
            if (matches) target_ = *entry;
            target_.scan_fresh = false;
            wasRunning_ = false;
            return;
        }
        if (!wasRunning_) {
            clearHistory();
        }
        wasRunning_ = true;
        if (!matches) return;
        // The store invalidates every entry at pause. Only an actual new
        // reception can make scan_fresh true, including when opened from List.
        const bool newSample = entry->sequence != target_.sequence ||
                               (!target_.scan_fresh && entry->scan_fresh);
        if (!newSample) { target_.scan_fresh = entry->scan_fresh; return; }
        if (uint32_t(entry->last_ms - target_.last_ms) >= RESET_MS)
            clearHistory();
        target_ = *entry;
        if (entry->scan_fresh && uint32_t(now - entry->last_ms) <= FRESH_MS) append(*entry);
    }

    bool hasTarget() const { return haveTarget_; }
    const TrackerEntry& target() const { return target_; }
    uint32_t age(uint32_t now) const { return uint32_t(now - target_.last_ms); }
    State state(uint32_t now, bool running) const {
        if (!running) return State::Paused;
        if (!haveTarget_ || age(now) > LOST_MS) return State::Lost;
        if (!target_.scan_fresh || age(now) > FRESH_MS) return State::Waiting;
        return State::Live;
    }
    int strength() const {
        // Relative received strength only: never a range or bearing estimate.
        const int value = (int(target_.filtered_rssi) + 95) * 100 / 60;
        return value < 0 ? 0 : value > 100 ? 100 : value;
    }
    unsigned historyCount() const { return count_; }
    int16_t history(unsigned index) const { return index < count_ ? values_[index] : 0; }

    bool trend(uint32_t now, bool running, int& direction) const {
        direction = 0;
        if (state(now, running) != State::Live || count_ < 4) return false;
        int total = 0;
        unsigned samples = 0;
        for (unsigned i = 0; i < count_; ++i) {
            const uint32_t ageMs = uint32_t(target_.last_ms - times_[i]);
            if (ageMs >= 1500 && ageMs <= 5000) {
                total += values_[i];
                ++samples;
            }
        }
        if (samples < 3) return false;
        const int change = int(target_.filtered_rssi) * int(samples) - total;
        if (change >= 4 * int(samples)) direction = 1;
        else if (change <= -4 * int(samples)) direction = -1;
        return true;
    }

private:
    TrackerEntry target_{};
    bool haveTarget_ = false;
    bool wasRunning_ = false;
    int16_t values_[HISTORY_SIZE]{};
    uint32_t times_[HISTORY_SIZE]{};
    unsigned count_ = 0;

    void clearHistory() { count_ = 0; }
    void append(const TrackerEntry& entry) {
        // Decimate new radio observations, never append repeated UI polls.
        if (count_ && uint32_t(entry.last_ms - times_[count_ - 1]) < 250) return;
        if (count_ == HISTORY_SIZE) {
            for (unsigned i = 1; i < count_; ++i) {
                values_[i - 1] = values_[i];
                times_[i - 1] = times_[i];
            }
            --count_;
        }
        values_[count_] = entry.filtered_rssi;
        times_[count_++] = entry.last_ms;
    }
};

/** Keep list focus on an identity, even while RSSI order changes. */
class TrackerSelection {
public:
    uint32_t id() const { return id_; }
    int index() const { return index_; }
    void reset() { id_ = 0; index_ = 0; }
    void reconcile(const TrackerEntry* rows, int count) {
        if (count <= 0) { reset(); return; }
        for (int i = 0; i < count; ++i) {
            if (rows[i].id == id_) { index_ = i; return; }
        }
        if (index_ >= count) index_ = count - 1;
        id_ = rows[index_].id;
    }
    void move(const TrackerEntry* rows, int count, int delta) {
        reconcile(rows, count);
        if (count <= 0) return;
        index_ = (index_ + (delta < 0 ? count - 1 : 1)) % count;
        id_ = rows[index_].id;
    }
private:
    uint32_t id_ = 0;
    int index_ = 0;
};
