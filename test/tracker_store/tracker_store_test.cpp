#include "tracker_store.h"
#include <cstdlib>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; std::exit(1); } } while (false)

static const uint8_t tag_a[6] = {1, 2, 3, 4, 5, 6};

static void identity_and_validity()
{
    TrackerEntry legacy{{1, 2, 3, 4, 5, 6}, TRK_TILE, -55, 2, 10, 20};
    CHECK(legacy.id == 0 && legacy.sequence == 0 && legacy.addr_type == 0 && legacy.filtered_rssi == -127);
    CHECK(!legacy.scan_fresh);
    TrackerStore store;
    TrackerEntry e{};
    const uint32_t a = store.record(tag_a, 0, TRK_APPLE, -70, 100);
    CHECK(a != 0);
    CHECK(store.get(a, e) && e.id == a && e.sequence == 1 && e.count == 1);
    CHECK(e.scan_fresh);
    CHECK(e.first_ms == 100 && e.last_ms == 100 && e.filtered_rssi == -70);
    CHECK(store.record(tag_a, 0, TRK_APPLE, -70, 110) == a);
    CHECK(store.get(a, e) && e.count == 2 && e.sequence == 2 && e.last_ms == 110);
    const uint32_t random_a = store.record(tag_a, 1, TRK_APPLE, -45, 111);
    CHECK(random_a && random_a != a);
    for (int invalid : {-300, -128, 21, 127, 256}) {
        CHECK(store.record(tag_a, 0, TRK_APPLE, invalid, 200) == 0);
        CHECK(store.get(a, e) && e.sequence == 2 && e.last_ms == 110);
    }
    CHECK(store.record(nullptr, 0, TRK_APPLE, -70, 200) == 0);
    CHECK(store.record(tag_a, 0, TRK_APPLE, -127, 201) == a);
    CHECK(store.record(tag_a, 0, TRK_APPLE, 20, 202) == a);
    CHECK(store.snapshot(nullptr, 1) == 0);
    CHECK(store.snapshot(&e, -1) == 0);
    CHECK(!store.get(0, e));
    CHECK(store.get(random_a, e) && e.addr_type == 1 && e.rssi == -45);
}

static void filter_only_new_samples()
{
    TrackerStore store;
    TrackerEntry e{};
    const uint32_t id = store.record(tag_a, 0, TRK_TILE, -70, 100);
    for (uint32_t i = 1; i < 5; ++i) store.record(tag_a, 0, TRK_TILE, -70, 100 + i * 10);
    CHECK(store.record(tag_a, 0, TRK_TILE, -20, 160) == id);
    CHECK(store.get(id, e) && e.rssi == -20 && e.filtered_rssi == -70);
    const uint32_t sequence = e.sequence;
    for (int i = 0; i < 20; ++i) {
        CHECK(store.get(id, e) && e.filtered_rssi == -70 && e.sequence == sequence);
    }
    // Sustained improvement gets through the median and converges smoothly.
    for (uint32_t i = 0; i < 20; ++i) store.record(tag_a, 0, TRK_TILE, -40, 170 + i * 10);
    CHECK(store.get(id, e) && e.filtered_rssi >= -42 && e.filtered_rssi <= -40);
    // A long gap starts a new filter window, retaining only observation identity.
    const uint32_t next_time = e.last_ms + TrackerStore::FILTER_RESET_MS;
    store.record(tag_a, 0, TRK_TILE, -95, next_time);
    CHECK(store.get(id, e) && e.filtered_rssi == -95 && e.first_ms == 100);
    const uint32_t old_sequence = e.sequence;
    store.invalidateScan();
    CHECK(store.get(id, e) && !e.scan_fresh && e.sequence == old_sequence);
    // A short pause also reseeds smoothing; the first new valid packet counts.
    store.record(tag_a, 0, TRK_TILE, 127, next_time + 10);
    CHECK(store.get(id, e) && !e.scan_fresh && e.sequence == old_sequence);
    store.record(tag_a, 0, TRK_TILE, -35, next_time + 20);
    CHECK(store.get(id, e) && e.scan_fresh && e.filtered_rssi == -35 && e.sequence == old_sequence + 1);
}

static void bounded_eviction_and_pin()
{
    TrackerStore store;
    TrackerEntry e{};
    uint32_t ids[TrackerMonitor::MAXTRK]{};
    for (uint8_t i = 0; i < TrackerMonitor::MAXTRK; ++i) {
        const uint8_t mac[6] = {i, 1, 2, 3, 4, 5};
        ids[i] = store.record(mac, 0, TRK_APPLE, -70, 100 + i);
        CHECK(ids[i]);
    }
    CHECK(store.select(ids[0]));
    CHECK(!store.select(UINT32_MAX)); // failed selection retains existing pin
    const uint8_t replacement[6] = {99, 1, 2, 3, 4, 5};
    const uint32_t fresh = store.record(replacement, 0, TRK_SAMSUNG, -30, 200);
    CHECK(fresh && fresh != ids[1]);
    CHECK(store.get(ids[0], e) && !store.get(ids[1], e));
    CHECK(store.get(fresh, e) && e.count == 1 && e.sequence == 1 && e.filtered_rssi == -30);
    CHECK(store.evicted() == 1);
    TrackerEntry rows[TrackerMonitor::MAXTRK]{};
    CHECK(store.snapshot(rows, TrackerMonitor::MAXTRK) == TrackerMonitor::MAXTRK);
    store.prune(TrackerMonitor::STALE_MS + 201);
    CHECK(store.snapshot(rows, TrackerMonitor::MAXTRK) == 1);
    CHECK(store.get(ids[0], e) && e.id == ids[0]);
    // Reappearance of the exact same address/type retains the selected id.
    const uint8_t original[6] = {0, 1, 2, 3, 4, 5};
    CHECK(store.record(original, 0, TRK_APPLE, -80, TrackerMonitor::STALE_MS + 250) == ids[0]);
    CHECK(store.get(ids[0], e) && e.filtered_rssi == -80);
    CHECK(store.select(0));
    store.prune(2 * TrackerMonitor::STALE_MS + 251);
    CHECK(!store.get(ids[0], e));
    const uint32_t reborn = store.record(original, 0, TRK_APPLE, -55, 2 * TrackerMonitor::STALE_MS + 300);
    CHECK(reborn && reborn != ids[0]);
}

static void clock_wrap_and_count_saturation()
{
    TrackerStore store;
    TrackerEntry e{};
    const uint32_t start = UINT32_MAX - 2000;
    const uint32_t id = store.record(tag_a, 0, TRK_APPLE, -50, start);
    store.record(tag_a, 0, TRK_APPLE, -90, 2999); // exactly 5000 ms over wrap
    CHECK(store.get(id, e) && e.filtered_rssi == -90);
    for (uint32_t i = 0; i < 66000; ++i) store.record(tag_a, 0, TRK_APPLE, -80, 3000 + i);
    CHECK(store.get(id, e) && e.count == UINT16_MAX && e.sequence == 66002);
    CHECK(e.first_ms == start && e.last_ms == 68999);
    store.prune(e.last_ms + TrackerMonitor::STALE_MS);
    CHECK(store.get(id, e));
    store.prune(e.last_ms + TrackerMonitor::STALE_MS + 1);
    CHECK(!store.get(id, e));
}

int main()
{
    identity_and_validity();
    filter_only_new_samples();
    bounded_eviction_and_pin();
    clock_wrap_and_count_saturation();
    std::cout << "tracker_store: identity, filtering, capacity, target retention and wrap checks passed\n";
}
