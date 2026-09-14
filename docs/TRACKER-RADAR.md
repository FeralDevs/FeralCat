# Native TrackerDetect proximity radar

This extension adds selected-candidate search to native app_17 on
MeowKitCustomFW v0.9.0. Lua and the SD app runtime are unchanged.

The user-facing workflow and controls are in [Tracker Detector](wiki/Tracker-Detector.md).

## Implementation

- `tracker_store.h`: fixed 24-slot observations, address/type identity, stable
  session IDs, valid-sample sequence numbers, selected-entry protection,
  eviction and median-5/EWMA RSSI filtering. Each entry requires a new reception
  after pause, including if selected from the list after resume. Pause also
  resets filter windows. No dynamic allocations or SD I/O.
- `tracker_monitor.cpp`: existing passive BLE classification/scan settings,
  synchronized snapshots, confirmed asynchronous scan states, error reporting
  and foreground start/stop requests. Late callbacks are gated on shutdown.
- `tracker_finder.h`: one selected identity, real-sample history (48 entries),
  signal freshness, pause/resume handling, trend and a bounded relative
  strength scale. It does not calculate distance, bearing or physical identity.
- `app_17.cpp`: stable list focus, navigation through all rows, target locking,
  list/radar transitions and 200 ms display refresh. The launcher still owns
  the long-B exit. The existing screenshot shortcut remains available.
- `tracker_ui.h`: production 320×240 list/radar renderer shared by the device
  and desktop simulator. Device UI language remains English.

The strength scale maps -95..-35 dBm onto 0..100 and clamps outside that range.
It is a visual received-strength scale, with no model-specific calibration.
Signal state uses a 2.5 s freshness window and 10 s loss window. The trend
requires at least three older samples 1.5..5 s behind the current reading and
uses a 4 dB difference to distinguish stronger/weaker from steady. These values
are starting parameters for hardware trials, not measured accuracy claims.

## Host checks

The portable tests compile production observation and finder code. The scanner
lifecycle tests use simulated BLE acknowledgments; they do not operate a radio.
All three tracker suites are also included in the existing combined native-test
entry point (`test/lua_apps`), which now runs 15 applicable suites on Windows and Linux CI.

```sh
cmake -S test/tracker_store -B build/tracker-store -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tracker-store
ctest --test-dir build/tracker-store --output-on-failure
cmake -S test/tracker_finder -B build/tracker-finder -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tracker-finder
ctest --test-dir build/tracker-finder --output-on-failure
```

Use a configured C++17 compiler; on Windows use the VS 2022 x64 Native Tools
environment. The TUI simulator renders the production drawing code with
synthetic observations; its previews demonstrate layout, not RF performance.

On Windows the sprite-only platform avoids an SDL installation:

```powershell
cmake -S sim/tui -B build/tracker-ui -G "Visual Studio 17 2022" -A x64 -DMEOWKIT_HEADLESS_PLATFORM=ON
cmake --build build/tracker-ui --config Release --parallel 4
.\sim\tui\render-trackers.ps1 -Renderer .\build\tracker-ui\Release\meowkit-tui.exe -OutputDirectory .\build\tracker-previews
```

The Windows adapter supplies host timing and opaque unused SDL types. Two
unused gradient helpers require allocation substitutions in a generated
build-directory source copy for MSVC; vendored firmware sources are unchanged.

Firmware builds use the pinned PlatformIO environment in `platformio.ini`.
Each OTA application slot is 8,323,072 bytes. A successful build establishes
compilation and image size, not an on-device test result.

## Device checks before accepting a build

- Use known own tags; verify two same-type tags stay distinguishable and the
  selected target remains selected when their received strengths cross.
- Walk toward/away in an open room, then repeat with walls, different antenna
  orientations and the device partly blocked by a hand/body. Assess whether
  smoothing and freshness thresholds fit each tag's actual broadcast cadence.
- Pause/resume, remove the target, return it, and leave it absent past ten
  seconds. Verify Waiting/Lost and no live strength after stale reception.
- Verify Up/Down paging, A selection, B return, long-B exit and the screenshot
  chord. Exercise scan restart/error handling and reopening the app.
- Exercise BLE app transitions, especially BadUSB BLE and BLE Spam Detector.
  The firmware's existing global BLE-stack ownership is retained; the prior
  cross-app HID lifecycle risk has not been resolved by this extension.
- Check internal heap/stack reserve and responsiveness during a prolonged
  scan and during screenshot writes. No background tracking or SD logging
  was introduced.

No device flashing or physical proximity validation is implied by the source,
host tests, simulator previews or build artifacts.
