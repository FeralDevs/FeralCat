# MeowKit-S3 Custom Firmware — "MeowGotchi" build

Custom, community firmware for the **MeowKit-S3** (ESP32-S3) pocket multi-tool.
It fixes the boot bugs that stop the open-source firmware from running on retail
hardware, repairs half-finished stock UI, fills the empty app slots with working
WiFi/BLE security tools, and can update itself over WiFi or SD.

> ## ⚠️ Experimental / testing firmware — flash at your own risk
> Unofficial, experimental firmware, provided **as-is with no warranty**. You are
> solely responsible for your device. **Recovery:** the official installer at
> <https://meowkit.cc/pages/download> restores the factory firmware.

> ## 🔐 Authorized use only
> The WiFi/BLE tools (sniffing, deauth, handshake/probe capture, BLE/AP scanning)
> are for testing networks and devices **you own or are permitted to assess**, and
> for education. Detectors are passive; MeowGotchi's deauth/handshake capture are
> off by default and opt-in. Obey the laws in your jurisdiction.

## Quick start

1. **First install (USB, once):** flash `meowgotchi-OTA-merged-0x0.bin` at `0x0`
   with a Web-Serial flasher — see **[[Flashing and Recovery]]**.
2. **After that:** update on-device via **Firmware → Update over WiFi** (pulls the
   latest [release](https://github.com/janud/MeowKitCustomFW/releases)) or from SD.
3. Explore the apps, tweak **Settings**, and try the **[[Script Runner]]**.

## Wiki pages

- **[[Releases]]** — what changed in every version (v0.1.0 → latest)
- **[[Apps]]** — the full app roster and controls
- **[[Firmware Updates]]** — WiFi/GitHub + SD self-update, dual-OTA layout
- **[[Settings and Backup]]** — settings, SD backup/restore, WiFi time sync
- **[[Script Runner]]** — run Berry scripts from the SD card
- **[[Flashing and Recovery]]** — Web-Serial flashing, download mode, recovery
- **[[Building from Source]]** — Docker build + the desktop simulators
- **[[Hardware and Gotchas]]** — pinout, boot fixes, and the hard-won lessons

## What this is based on

The open-source [`mingolucky/meowkit-s3-firmware`](https://github.com/mingolucky/meowkit-s3-firmware)
(v1.0.0), which links GPL-3.0 (`ESP32-audioI2S`) and LGPL-2.1 (`IRremoteESP8266`)
code — treat this fork under those terms. Built entirely in Docker (nothing on the
host). LVGL desktop simulator adapted from upstream PR #31 (kdelfour).
