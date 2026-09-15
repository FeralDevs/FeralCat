# Flashing and Recovery

## Which file do I need?

| Situation | File | How |
|---|---|---|
| **First install / coming from stock** | `feralcat-OTA-merged-0x0.bin` | USB flash at `0x0` (once) |
| **Already on this firmware** | *nothing* | **Firmware → Update over WiFi**, or SD |
| **Updating manually** | `firmware.bin` | SD → **Update from SD**, or USB at `0x10000` |

The **merged image** is the whole chip (bootloader + partition table + app). You
need it once because a plain app flash can't fix the bootloader (it must be
**DIO** or it bootloops) or install the dual-OTA partition table. After that,
updates are just `firmware.bin`. Downloads are on the
[Releases page](https://github.com/FeralDevs/FeralCat/releases);
verify against `SHA256SUMS.txt`.

## Entering download mode

- **From the firmware:** open **Firmware → USB Download Mode → A** (no BOOT button
  needed), or
- **Fallback:** power off, hold **BOOT**, plug USB, hold ~3 s, release.

## Flashing over USB (Web-Serial)

Docker on macOS can't pass through USB serial, so flash from a **Chrome/Edge
Web-Serial** flasher such as <https://esp.huhn.me> (or ESP-Launchpad):

1. Enter download mode (above).
2. **Connect**.
3. **First install:** *Erase Flash* (safe — it's a partition-layout change), then
   add `feralcat-OTA-merged-0x0.bin` at offset **`0x0`** → **Program**.
   **App-only update:** add `firmware.bin` at **`0x10000`** → **Program** (do
   **not** erase).
4. Power-cycle.

> The merged image **must** be DIO. A QIO image boots once then crash-loops.

## Recovery

- **Empty apps menu / stuck UI:** you can still reach **Settings ▸ System ▸
  Firmware ▸ Open** to reflash (added in v0.5.2). If even that fails, USB-flash the
  merged image at `0x0`.
- **Back to factory:** run the official installer at
  <https://meowkit.cc/pages/download> — it writes the stock image and restores
  original behaviour (single app slot).
- **Won't boot after a flash:** re-flash the merged image at `0x0` with *Erase
  Flash* first; that cures any half-written state.
