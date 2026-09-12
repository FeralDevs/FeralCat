# MeowKit‑S3 Custom Firmware ("MeowGotchi" build)

A hardened, extended build of the open‑source MeowKit‑S3 firmware. It fixes the
show‑stopping boot bugs on retail hardware, repairs several half‑finished UI
elements, and fills every previously‑empty app slot with working WiFi/BLE
security tools — built entirely in Docker (nothing installed on the host).

> **Authorized use only.** The WiFi/BLE tools here are for testing networks and
> devices you own or are permitted to assess, and for education. The detectors
> are passive (listen‑only). MeowGotchi's deauth and handshake capture are
> off by default and opt‑in.

---

## 1. What was broken, and the fixes

### Boot (the big one — issue #27)
Retail units bootlooped on any firmware built from the public repo. Two stacked
causes, both fixed:

| Cause | Fix |
|---|---|
| **Wrong flash mode.** `platformio.ini` built **QIO**; the hardware runs **DIO** (verified by diffing the factory image's bootloader header: byte `@0x2` = `02`=DIO vs our `00`=QIO). QIO on DIO wiring boots once then crash‑loops. | `board_build.flash_mode = dio` + `board_build.arduino.memory_type = dio_opi`; merge images with `esptool --flash-mode dio`. |
| **Brownout reset.** A current spike during the AXP173/I²C power bring‑up trips the brownout detector → reset loop. | Disable brownout + quiet the watchdogs at the very first instruction in `setup()` (`src/main.cpp`). |

The device's USB serial is unusable while it crash‑loops (native‑USB CDC flaps),
so this was diagnosed by **downloading the factory image and diffing the
bootloader header** — no serial needed.

### UI / usability
- Clean‑clone build restored (`.gitmodules` was never committed → `mooncake.h` missing).
- WiFi long‑SSID rows truncate with `…` instead of wrapping out of the row.
- PC Monitor renders a real `°` (the temp font is ASCII‑only) and the mislabeled block.
- Settings **info button** was dead (no click handler) → now opens an **About** panel
  (firmware / SoC / MAC / free heap / SD) distinct from the gear/settings button.

---

## 2. App roster

Slots 10–15 shipped empty (or stubs); all are now real apps. Common controls:
**A** = primary action, **short B** = secondary/toggle, **hold B** = exit
(the firmware‑wide gesture the launcher handles).

| Slot | App | What it does | Controls |
|---|---|---|---|
| 10 | **MeowGotchi** | Pwnagotchi‑style cat‑face WiFi hunter: promiscuous sniff, channel hop, AP/client discovery, WPA‑handshake (EAPOL) capture to `/handshakes/*.pcap` on SD, opt‑in deauth. Mood face + live stats. | A = Start/Pause · short B = menu (Mode/Handshakes) · hold B = exit |
| 11 | **WiFi Analyzer** | Scans 2.4 GHz, lists APs by signal (channel, quality %, lock), per‑AP detail (BSSID, dBm+%, security). | A = detail · short B = rescan · hold B = exit |
| 12 | **Firmware** | **Update over WiFi** (reconnects to the saved WiFi, pulls this repo's latest GitHub release `firmware.bin` to the SD, then flashes it), **Update from SD** (dual‑OTA write of `/firmware.bin` to the spare slot, safe rollback), or **USB Download Mode** (`usb_persist_restart(RESTART_BOOTLOADER)` — reflash without the BOOT button). | A = select · hold B = exit |
| 13 | **Deauth Detector** | Passive deauth/disassoc counter with CLEAR/ALERT banner + 30 s graph. **Attacker Log** view lists source MAC, hit count and **RSSI** (proximity to locate the attacker). | A = pause/resume · short B = graph↔log · hold B = exit |
| 14 | **BLE Spam Detector** | Passive BLE scan for Apple/Google/MS/Samsung spam‑popup floods; alerts on **distinct advertiser MACs/sec** (not on legit devices), with a per‑vector breakdown. | A = pause/resume · hold B = exit |
| 15 | **Rogue Radar** | **Evil‑Twin scan** (flags SSIDs advertised as *both open and secured*) + **Beacon Flood / Karma** monitor (distinct APs/sec, promiscuous). | short B = twins↔flood · A = rescan/pause · hold B = exit |

All detectors draw into an off‑screen sprite and blit once (no flicker).

---

## 3. Building (Docker only — nothing on the host)

Two container images (defined in `../docker/`):
- `meowkit-pio` — PlatformIO + esptool, toolchain cached in the `meowkit-pio` volume.
- `meowkit-sim` — SDL2 + CMake for the two desktop UI simulators.

```bash
# from the repo parent dir (…/MeowKit)
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"

# 1. build the toolchain image once
docker build -q -t meowkit-pio:local docker/

# 2. compile the firmware  → .pio/build/esp32s3box/{bootloader,partitions,firmware}.bin
docker run --rm -v meowkit-pio:/root/.platformio \
  -v "$PWD/firmware-upstream":/work meowkit-pio:local run

# 3. fuse a single flash‑at‑0x0 image (DIO!)
docker run --rm --entrypoint bash -v meowkit-pio:/root/.platformio \
  -v "$PWD/firmware-upstream":/work -w /work meowkit-pio:local -c '
  BA=$(find /root/.platformio -name boot_app0.bin | head -1)
  python -m esptool --chip esp32s3 merge-bin -o dist/meowgotchi-OTA-merged-0x0.bin \
    --flash-mode dio --flash-size 16MB \
    0x0 .pio/build/esp32s3box/bootloader.bin \
    0x8000 .pio/build/esp32s3box/partitions.bin \
    0xe000 "$BA" \
    0x10000 .pio/build/esp32s3box/firmware.bin'
```

Result: **`dist/meowgotchi-OTA-merged-0x0.bin`**, flashed at offset `0x0`.

---

## 4. Flashing

Docker on macOS can't reach USB, so flash from a browser (Chrome/Edge, Web Serial):

1. **Enter download mode** — either open the **Firmware** app → **USB Download
   Mode** → **A**, or (fallback) power off, hold **BOOT**, plug USB, hold ~3 s,
   release.
2. In **esp.huhn.me** (or ESP‑Launchpad DIY): **Connect** → add
   `dist/meowgotchi-OTA-merged-0x0.bin` at **`0x0`** → **Program**.
3. Power‑cycle.

Because it's a complete image at `0x0`, an "Erase Flash" first is safe (and cures
any half‑written state). **Recovery:** the official installer at
meowkit.cc/pages/download writes the factory image.

> The merged image **must** be built/merged as **DIO**. A QIO image will boot
> once and then crash‑loop.

---

## 5. Simulators (see UI changes without hardware)

- **LVGL sim** (`sim/`) renders the LVGL system screens (home/wifi/settings/…).
  `tools/shoot.sh` builds + captures BMPs to `sim/shots/`.
- **TUI sim** (`sim/tui/`) renders the LovyanGFX "MK_TUI" app screens
  (MeowGotchi, the detectors, Rogue Radar) into an off‑screen sprite → BMP,
  via `meowkit-tui --scene <name> --out x.bmp` (headless, `SDL_VIDEODRIVER=dummy`).

Both run in the `meowkit-sim` container. Convert BMP→PNG with `sips` on macOS.

---

## 6. Layout of the additions

```
docker/                 Dockerfile (PlatformIO) + Dockerfile.sim (SDL sims)
tools/shoot.sh          LVGL screen capture helper
sim/                    LVGL desktop simulator (from PR #31)
sim/tui/                LovyanGFX headless TUI simulator (this fork)
src/main.cpp            boot hardening (brownout/watchdog)
src/app/app_10..app_15  the new apps (engine + shared template UI + app)
src/app/app_common/mk_tui.h   the shared TUI toolkit these apps draw with
dist/                   flashable merged image
```

Each detector app follows one shape: a hardware **engine** (promiscuous or scan),
a **pure `template<LCD>` UI** shared verbatim with the TUI simulator, and the
**app** wiring them with a lazily‑allocated double‑buffer sprite (allocating the
sprite as a value member hangs boot — allocate it in `onOpen`).

---

## 7. On‑device test checklist

- [x] Boots reliably (DIO + brownout).
- [x] MeowGotchi: steady face, A=start (stats climb), timer counts only while hunting, hold‑B exits.
- [x] Menu opens/holds; Settings About panel; info vs gear buttons differ.
- [ ] WiFi Analyzer lists real APs; A opens detail.
- [ ] Deauth Detector: log populates under attack; RSSI reads.
- [ ] BLE Spam Detector: **CLEAR** in a normal room; red only on a real flood.
- [ ] Rogue Radar: twins↔flood toggle stays stable (scan↔promiscuous switch).
- [ ] Flash Mode drops into download mode (screen dark, flashable port appears).

---

## 8. Credits & upstream

Base firmware: `mingolucky/meowkit-s3-firmware`. LVGL desktop simulator adapted
from PR #31 (kdelfour). The **DIO flash‑mode** and **brownout** boot fixes
resolve issue #27 and are worth contributing upstream (they affect every fork).
