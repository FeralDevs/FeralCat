/** Bounded, allocation-free BLE observations. Caller supplies synchronization. */
#pragma once
#include "tracker_monitor.h"
#include <cstring>

class TrackerStore {
public:
    static constexpr uint32_t FILTER_RESET_MS = 5000;

    void clear() { *this = TrackerStore{}; }

    void invalidateScan()
    {
        for (auto& slot : _slots) {
            slot.entry.scan_fresh = false;
            slot.sample_count = slot.sample_next = 0;
        }
    }

    // GAP RSSI is -127..20 dBm; 127 denotes an unavailable reading.
    uint32_t record(const uint8_t* mac, uint8_t addr_type, uint8_t type,
                    int rssi, uint32_t now)
    {
        if (!mac || rssi < -127 || rssi > 20) return 0;
        Slot* target = nullptr;
        for (auto& slot : _slots) {
            if (slot.entry.id && slot.entry.addr_type == addr_type &&
                std::memcmp(slot.entry.mac, mac, 6) == 0) {
                target = &slot;
                break;
            }
        }
        if (!target) {
            for (auto& slot : _slots) {
                if (!slot.entry.id) { target = &slot; break; }
            }
            if (!target) {
                // Replace the oldest unselected observation, including stale
                // rows. The selected identity is never silently rebound.
                uint32_t oldest_age = 0;
                for (auto& slot : _slots) {
                    if (slot.entry.id == _selected) continue;
                    const uint32_t age = now - slot.entry.last_ms;
                    if (!target || age > oldest_age) {
                        target = &slot;
                        oldest_age = age;
                    }
                }
                if (!target) return 0;
                if (_evicted != UINT32_MAX) ++_evicted;
            }
            *target = Slot{};
            target->entry.id = allocateId();
            std::memcpy(target->entry.mac, mac, 6);
            target->entry.addr_type = addr_type;
            target->entry.first_ms = now;
        }
        TrackerEntry& entry = target->entry;
        if (!target->sample_count || !entry.sequence || now - entry.last_ms >= FILTER_RESET_MS) {
            target->sample_count = target->sample_next = 0;
            target->filtered_q8 = rssi * 256;
        }
        target->samples[target->sample_next] = static_cast<int8_t>(rssi);
        target->sample_next = (target->sample_next + 1) % 5;
        if (target->sample_count < 5) ++target->sample_count;
        int8_t sorted[5];
        for (uint8_t i = 0; i < target->sample_count; ++i) sorted[i] = target->samples[i];
        for (uint8_t i = 1; i < target->sample_count; ++i) {
            const int8_t value = sorted[i];
            uint8_t j = i;
            while (j && sorted[j - 1] > value) { sorted[j] = sorted[j - 1]; --j; }
            sorted[j] = value;
        }
        const int median_q8 = sorted[target->sample_count / 2] * 256;
        target->filtered_q8 += (median_q8 - target->filtered_q8) / 4;
        entry.filtered_rssi = static_cast<int16_t>(target->filtered_q8 / 256);
        entry.rssi = static_cast<int8_t>(rssi);
        entry.type = type;
        if (entry.count < UINT16_MAX) ++entry.count;
        if (++entry.sequence == 0) entry.sequence = 1;
        entry.last_ms = now;
        entry.scan_fresh = true;
        return entry.id;
    }

    bool get(uint32_t id, TrackerEntry& out) const
    {
        if (!id) return false;
        for (const auto& slot : _slots) {
            if (slot.entry.id == id) { out = slot.entry; return true; }
        }
        return false;
    }

    bool select(uint32_t id)
    {
        TrackerEntry unused{};
        if (id && !get(id, unused)) return false;
        _selected = id;
        return true;
    }

    void prune(uint32_t now)
    {
        for (auto& slot : _slots) {
            if (slot.entry.id && slot.entry.id != _selected &&
                now - slot.entry.last_ms > TrackerMonitor::STALE_MS) slot = Slot{};
        }
    }

    int snapshot(TrackerEntry* out, int max) const
    {
        if (!out || max <= 0) return 0;
        int count = 0;
        for (const auto& slot : _slots) {
            if (slot.entry.id && count < max) out[count++] = slot.entry;
        }
        return count;
    }

    uint32_t evicted() const { return _evicted; }

private:
    struct Slot {
        TrackerEntry entry{};
        int8_t samples[5]{};
        uint8_t sample_count = 0;
        uint8_t sample_next = 0;
        int32_t filtered_q8 = 0;
    };
    Slot _slots[TrackerMonitor::MAXTRK]{};
    uint32_t _selected = 0;
    uint32_t _next_id = 1;
    uint32_t _evicted = 0;

    uint32_t allocateId()
    {
        // Also handles the unlikely uint32 wrap without reusing a live id.
        TrackerEntry unused{};
        uint32_t id;
        do {
            id = _next_id++;
        } while (!id || get(id, unused));
        return id;
    }
};
