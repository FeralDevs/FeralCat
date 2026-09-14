# Releases

Every tagged release, newest first. Downloads (firmware + merged image +
checksums) are on the [Releases page](https://github.com/janud/MeowKitCustomFW/releases).
See **[[Flashing and Recovery]]** for how to apply them.

---

## v0.7.0 — Script Runner (Berry)
- **[[Script Runner]]** — run small **Berry** scripts from `/scripts/*.be` on the
  SD card, with `print()` output on screen. Device API for scripts: `led(r,g,b)`
  (onboard WS2812), `pin_mode/write/read`, `button()`, `delay`, `millis`, `print`.
  A safety hook stops any script after ~20 s or when you hold **B** — an infinite
  loop can't hang the device. The Berry VM adds only ~120 KB.
- **Boot splash rewritten** — the old ~2 MB boot GIF is replaced by a title + a
  **progress bar tied to the real boot stages**. Frees ~1.6 MB (88.6% → 70.6%
  flash) and boots faster.

## v0.6.0 — Settings backup & restore
- **SD settings backup** — all settings (incl. **WiFi credentials**, plaintext by
  design) are mirrored to `/config/meowkit.cfg` on the SD on each change, and
  **auto-restored on boot** after a reflash wipes NVS. New **Settings ▸ Backup**
  tab: Backup now / Restore all / Restore WiFi / Restore settings (→ confirm →
  reboot). See **[[Settings and Backup]]**.
- **Scrollable settings rail** replacing LVGL's non-scrolling tab bar.
- **Fix:** unreadable gray-on-gray dialog text (backup/restore, Factory Reset,
  Time Sync).

## v0.5.2 — Firmware update in Settings
- **Settings ▸ System ▸ Firmware ▸ Open** launches the Firmware app (SD / GitHub /
  USB update) straight from Settings — reachable even if the apps grid is broken.

## v0.5.1 — Fix: empty apps menu
- The apps menu is built once at boot before the app list loads; v0.5.0's rebuild
  left it empty on device. Tiles are now populated when the list loads.

## v0.5.0 — Two detectors, screenshots, time sync
- **Tracker Detector** — BLE anti-stalking: AirTag/Find My, Tile, Samsung SmartTag
  → `FOLLOWED!` when one persists near you.
- **Probe Sniffer** — logs the probe-request frames nearby devices broadcast
  (MAC + signal + the SSIDs they leak).
- **On-device screenshots** — hold the joystick **Up+Down** in MeowGotchi/detectors
  to save the screen to `/screenshots/*.bmp` on the SD.
- **WiFi Time Sync** — NTP (UTC) + IP-geolocation (timezone) → sets the clock and
  RTC, no manual timezone; clock also restored from RTC at boot.

## v0.4.0 — Self-update over WiFi
- **Firmware ▸ Update over WiFi** — the device reads this repo's latest release
  from GitHub and, if newer, downloads `firmware.bin` to the SD and flashes it —
  all on-device. See **[[Firmware Updates]]**.

## v0.3.0 — SD-card firmware updates (dual-OTA)
- Repartitioned to **two ~7.94 MB app slots** so the device can update its own
  firmware from a `.bin` on the SD card (safe rollback) — no computer.

## v0.2.0 — More tools + fork branding
- Additional WiFi/BLE detector apps; Settings ▸ About shows the fork version +
  "Fork: Janud".

## v0.1.0 — Initial fork (makes it actually boot)
- **Boot fixes** (upstream issue #27): DIO flash mode + brownout/watchdog disable
  — retail units bootlooped on any build from the public repo. See
  **[[Hardware and Gotchas]]**.
- Clean-clone build restored (`.gitmodules` was never committed).
- UI fixes: WiFi long-SSID truncation, PC-Monitor `°C`, working Settings ▸ About.
- First new apps in the empty slots: MeowGotchi, WiFi Analyzer, the detectors.
