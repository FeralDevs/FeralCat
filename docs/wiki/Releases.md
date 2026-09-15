# Releases

Every tagged release, newest first. Downloads (firmware + merged image +
checksums) are on the [Releases page](https://github.com/FeralDevs/FeralCat/releases).
See **[[Flashing and Recovery]]** for how to apply them.

---

## v0.10.1 — FeralCat icons + fast boot
- **App icons** — new red FeralCat icons for the six native security apps and the
  MeowGotchi tile; each app now has its own icon.
- **Fast-boot fix** — the SD `/apps` scan no longer runs at boot (it stalled the
  splash for a few seconds once several apps were installed); it's done lazily the
  first time the Apps menu is opened.

## v0.10.0 — FeralCat: native signed apps + rebrand
- **Native signed apps** — load, verify and run **signed native ELF apps** from
  the SD card through a stable app SDK, each with its own launcher tile. **Ed25519**
  signatures verified on-device; unsigned apps are opt-in (Settings ▸ Features).
- Six security tools now ship as **signed SD apps**: [[WiFi Analyzer]], [[Deauth
  Detector]], [[Rogue Radar]], [[BLE Spam Detector]], [[Probe Sniffer]] and
  [[Tracker Detector]].
- **FeralCat rebrand + red theme** — new name, red UI, new mascot.
- **Firmware updater** moved into **Settings ▸ System ▸ Update** (a system module,
  no longer an app).

## v0.9.0 — MeowPlayer + Lua apps
- **[[MeowPlayer]]** — a real MP3 music player. Plays from `/music`, with
  play/pause, seek, volume, a Songs list and a Speaker/Jack output switch. Native
  player UI on a contributed audio engine.
- **[[Lua Apps]]** — an **opt-in** installable-app platform: drop Lua apps in
  `/apps` and they get launcher tiles, no rebuild needed. **Off by default** —
  enable it in **Settings ▸ Features ▸ Lua apps** (reboots to apply). Ships with a
  *Hello Meow* example.
- New **Settings ▸ Features** tab for opt-in/experimental toggles.
- **Credit:** the audio engine and the Lua runtime/app platform were contributed
  by **[Caliun](https://github.com/Caliun)** ([PR #2](https://github.com/FeralDevs/FeralCat/pull/2)). Thank you!

## v0.8.1 — Keyboard symbols
- **On-screen keyboard now types every ASCII symbol.** The password/text keyboard's
  symbol mode went from 2 pages to **4** (cycle with the **◄►** key), adding the
  previously-missing `;` `:` `,` `<` `>` `[` `]` `{` `}` `~` `^` `` ` `` `\` `|`.
  Fixes not being able to enter WiFi passwords that contain those characters.

## v0.8.0 — Meow XP (leveling)
- **[[Meow XP]]** — a device-wide experience/leveling system that rewards *using*
  the MeowKit. XP trickles in from active/idle time, opening apps (big bonus the
  first time you open each), running scripts, and capturing WiFi handshakes. Your
  **level + XP bar** show on the **Home screen** (by the nav cross), with a green
  **level-up banner**. Progress is saved to NVS and mirrored to `/system/xp.txt`
  on SD (**survives reflashes**, portable between devices, checksum-guarded against
  edits). **Backup XP / Restore XP** added to Settings ▸ Backup.
- **Fix:** a reboot when flicking the joystick the **opposite direction mid-slide**
  — a pre-existing crash from starting a new screen transition while one was still
  animating. Navigation now ignores the extra press until the slide finishes.

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
