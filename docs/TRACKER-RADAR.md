# TrackerDetect proximity radar

Selected-candidate search for the **TrackerDetect** native app. You pick a BLE
tracker candidate from the list and open a proximity radar that shows its
relative signal strength over time, so you can tell whether you are getting
closer to it.

Addresses rotate, so a row is an observation identity within one scan session —
**not** proof of a physical device or of being followed. The radar shows relative
received strength only: **no bearing, no distance, no compass**.

Original feature contributed by **Caliun** (PR #3), re-ported onto FeralCat's
ELF native-app architecture (the engine runs in firmware; the UI runs in a
signed ELF app over the app SDK).

## Architecture

The detector follows the FeralCat "engine in firmware, UI in ELF" pattern:

- **`src/system/tracker_store.h`** — bounded, allocation-free observation store.
  Fixed 24 slots, address+type identity, stable nonzero session IDs, valid-sample
  sequence numbers, selected-entry protection, oldest-unselected eviction, and a
  median-5 + EWMA RSSI filter. Each entry needs a fresh reception after pause
  (including a candidate selected from the list after resume). Pause resets the
  filter windows. No dynamic allocation, no SD I/O.
- **`src/system/tracker_monitor.{h,cpp}`** — passive BLE classification and scan
  settings, `s_mux`-synchronized snapshots, confirmed asynchronous scan-state
  transitions, error reporting, and foreground-only start/stop requests. Late
  GAP callbacks are gated on shutdown. Exposes `select(id)`, `tracker(id, out)`,
  `starting()` and `error()` on top of the original stats/list interface.
- **`src/system/tracker_finder.h`** — portable finder for one selected identity:
  48-sample real-reception history, freshness/loss state machine, pause/resume
  handling, a bounded relative strength scale and a stronger/weaker/steady trend.
  It computes no distance, bearing or physical identity.
- **App SDK (`src/system/mk_app_abi.h`, `app_sdk.cpp`)** — the finder and the
  selection live in firmware (`s_trkfinder`, `s_trk_sel`). Apps drive them via
  `mk_tracker_select(id)` and read a flattened snapshot with
  `mk_tracker_finder(&out)`; `mk_tracker_list` now carries each row's `id`,
  `filtered_rssi` and `scan_fresh`. A `mk_gfx_line()` primitive was added for the
  history sparkline.
- **`sd files/apps/trackerdetect/app_main.c`** — the signed ELF app. Stable list
  focus across RSSI reordering, list↔radar transitions, pause/resume, and the
  radar renderer drawn with `mk_gfx` primitives (concentric rings, filtered-RSSI
  panel, trend text, 48-sample sparkline). The launcher owns long-B exit.

## Scale and thresholds

The strength scale maps -95..-35 dBm onto 0..100 and clamps outside that range.
It is a visual received-strength scale with no model-specific calibration.
Signal state uses a 2.5 s freshness window and a 10 s loss window. The trend
needs at least three older samples 1.5..5 s behind the current reading and a
4 dB difference to call stronger/weaker rather than steady. These are starting
parameters for hardware trials, not measured accuracy claims.

## Controls

- **List:** Up/Down select · **A** Find (open radar) · Left/Right pause/resume ·
  hold **B** exit.
- **Radar:** **A** pause/resume · tap **B** back to list · hold **B** exit.

## Host tests

The portable suites compile the production store/finder/monitor code. The scanner
lifecycle test uses simulated BLE acknowledgments; it does not operate a radio.

```sh
cmake -S test/tracker_store  -B build/tracker-store  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tracker-store  && ctest --test-dir build/tracker-store  --output-on-failure
cmake -S test/tracker_finder -B build/tracker-finder -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tracker-finder && ctest --test-dir build/tracker-finder --output-on-failure
```

Use a configured C++17 compiler. A successful firmware build establishes
compilation and image size, not an on-device result.

## Device checks before accepting a build

- Use your own tags; verify two same-type tags stay distinguishable and the
  selected target stays selected when their received strengths cross.
- Walk toward/away in an open room, then repeat with walls, different antenna
  orientations, and the device partly blocked by a hand/body. Judge whether the
  smoothing and freshness thresholds fit each tag's actual broadcast cadence.
- Pause/resume, remove the target, return it, and leave it absent past ten
  seconds. Verify Waiting/Lost and that no live strength shows after stale
  reception.
- Exercise Up/Down paging, A selection, B return, long-B exit, and scan
  restart/error handling by reopening the app.
