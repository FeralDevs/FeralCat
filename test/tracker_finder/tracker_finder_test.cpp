#include "tracker_finder.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); std::exit(1); } } while (0)

static TrackerEntry sample(uint32_t id, uint32_t seq, uint32_t time, int16_t rssi) {
    TrackerEntry e{};
    e.id = id; e.sequence = seq; e.first_ms = 100; e.last_ms = time;
    e.rssi = static_cast<int8_t>(rssi); e.filtered_rssi = rssi;
    e.scan_fresh = true;
    return e;
}

int main() {
    using State = TrackerFinder::State;
    TrackerFinder f;
    auto e = sample(1, 1, 1000, -80);
    f.select(e, 1000, true);
    CHECK(f.hasTarget()); CHECK(f.strength() == 25);
    CHECK(f.state(3500, true) == State::Live);
    CHECK(f.state(3501, true) == State::Waiting);
    CHECK(f.state(11000, true) == State::Waiting);
    CHECK(f.state(11001, true) == State::Lost);
    CHECK(f.state(11001, false) == State::Paused);

    for (uint32_t now = 1000; now < 3000; now += 10) f.update(&e, now, true);
    CHECK(f.historyCount() == 1); // polling does not invent observations
    auto other = sample(2, 55, 3000, -35);
    f.update(&other, 3000, true);
    CHECK(f.target().id == 1); CHECK(f.target().filtered_rssi == -80);
    int direction = 99;
    CHECK(!f.trend(3000, true, direction)); CHECK(direction == 0);

    // A sustained measured increase becomes a trend only after enough history.
    for (uint32_t i = 2; i <= 17; ++i) {
        e = sample(1, i, 1000 + (i - 1) * 250, static_cast<int16_t>(-80 + int(i)));
        f.update(&e, e.last_ms, true);
    }
    CHECK(f.trend(e.last_ms, true, direction)); CHECK(direction == 1);
    CHECK(!f.trend(e.last_ms + 3000, true, direction));

    // Radio silence resets trend on return; a new address never takes the lock.
    e = sample(1, 18, e.last_ms + 5000, -45);
    f.update(&e, e.last_ms, true);
    CHECK(f.historyCount() == 1); CHECK(!f.trend(e.last_ms, true, direction));
    f.update(nullptr, e.last_ms + 12000, true);
    CHECK(f.state(e.last_ms + 12000, true) == State::Lost);
    CHECK(f.target().id == 1);

    // Pause/resume requires a post-resume sample, even for a very short pause.
    e.scan_fresh = false;
    f.update(&e, e.last_ms + 10, false);
    CHECK(f.historyCount() == 0);
    f.update(&e, e.last_ms + 20, true);
    CHECK(f.state(e.last_ms + 20, true) == State::Waiting);
    e.sequence++; e.last_ms += 30; e.scan_fresh = true;
    f.update(&e, e.last_ms, true);
    CHECK(f.state(e.last_ms, true) == State::Live);
    CHECK(f.historyCount() == 1);

    // Pause in List, resume, then open cached tag: never relight its old RSSI.
    e = sample(1, 20, 1000, -40); e.scan_fresh = false;
    f.select(e, 1250, true);
    CHECK(f.state(1250, true) == State::Waiting); CHECK(f.historyCount() == 0);
    e.sequence++; e.last_ms = 1300; e.scan_fresh = true; e.filtered_rssi = -90;
    f.update(&e, 1300, true);
    CHECK(f.state(1300, true) == State::Live); CHECK(f.historyCount() == 1);
    CHECK(f.target().filtered_rssi == -90);

    // First advertisement already in the first running snapshot is enough.
    e.scan_fresh = false; f.update(&e, 1320, false);
    e.sequence++; e.last_ms = 1350; e.scan_fresh = true;
    f.update(&e, 1350, true);
    CHECK(f.state(1350, true) == State::Live); CHECK(f.historyCount() == 1);

    // A pre-pause cached snapshot newer than the last draw cannot become live.
    e.sequence++; e.last_ms = 1360; e.scan_fresh = false;
    f.update(&e, 1370, false); f.update(&e, 1380, true);
    CHECK(f.state(1380, true) == State::Waiting);
    e.sequence++; e.last_ms = 1400; e.scan_fresh = true;
    f.update(&e, 1400, true); CHECK(f.state(1400, true) == State::Live);

    // Fixed memory, weaker/steady trends, and clamped relative-strength endpoints.
    for (uint32_t i = 0; i < 100; ++i) {
        e.sequence++; e.last_ms += 250; e.filtered_rssi = -60;
        f.update(&e, e.last_ms, true);
    }
    CHECK(f.historyCount() == TrackerFinder::HISTORY_SIZE);
    CHECK(f.trend(e.last_ms, true, direction)); CHECK(direction == 0);
    for (uint32_t i = 0; i < 12; ++i) {
        e.sequence++; e.last_ms += 250; e.filtered_rssi = static_cast<int16_t>(-60 - int(i) * 2);
        f.update(&e, e.last_ms, true);
    }
    CHECK(f.trend(e.last_ms, true, direction)); CHECK(direction == -1);
    e = sample(3, 1, 100, -127); f.select(e, 100, true); CHECK(f.strength() == 0);
    e = sample(3, 2, 200, 20); f.update(&e, 200, true); CHECK(f.strength() == 100);

    // Millisecond rollover is handled with unsigned elapsed-time arithmetic.
    e = sample(4, 1, UINT32_MAX - 500, -70); f.select(e, e.last_ms, true);
    e.sequence++; e.last_ms = 249; f.update(&e, 249, true);
    CHECK(f.historyCount() == 2); CHECK(f.age(349) == 100);
    CHECK(f.state(349, true) == State::Live);

    // Focus follows identity across sorting, all pages are reachable, disappearance safe.
    TrackerEntry rows[24]{};
    for (unsigned i = 0; i < 24; ++i) rows[i] = sample(i + 1, 1, 100, -70);
    TrackerSelection selection;
    selection.reconcile(rows, 24); CHECK(selection.id() == 1);
    for (int i = 0; i < 23; ++i) selection.move(rows, 24, 1);
    CHECK(selection.id() == 24); CHECK(selection.index() == 23);
    auto tmp = rows[0]; rows[0] = rows[23]; rows[23] = tmp;
    selection.reconcile(rows, 24); CHECK(selection.id() == 24); CHECK(selection.index() == 0);
    selection.move(rows, 24, -1); CHECK(selection.id() == 1);
    selection.reconcile(rows, 4); CHECK(selection.index() == 3); CHECK(selection.id() == 4);
    selection.reconcile(nullptr, 0); CHECK(selection.id() == 0);
    f.reset(); CHECK(!f.hasTarget()); CHECK(f.historyCount() == 0);
    std::printf("Tracker finder: %u checks passed\n", checks);
    return 0;
}
