# MeowKit-S3 Custom Firmware — "MeowGotchi" build

> **Legend:** throughout this wiki, 🐱 marks features **built for this fork** and 📦 marks features **from the original MeowKit firmware**.

Custom, community firmware for the **MeowKit-S3** (ESP32-S3) pocket multi-tool.
It fixes the boot bugs that stop the open-source firmware running on retail
hardware, repairs half-finished stock UI, fills the empty app slots with working
WiFi/BLE security tools, updates itself over WiFi or SD, and runs Berry scripts.

> ## ⚠️ Experimental / testing firmware — flash at your own risk
> Unofficial, experimental firmware, **as-is with no warranty**. You are solely
> responsible for your device. **Recovery:** the official installer at
> <https://meowkit.cc/pages/download> restores factory firmware.

> ## 🔐 Authorized use only
> The WiFi/BLE tools are for testing networks/devices **you own or are permitted
> to assess**, and for education. Detectors are passive; MeowGotchi's deauth and
> handshake capture are off by default and opt-in. Obey your local laws.

## Quick start
1. **First install (USB, once):** flash `meowgotchi-OTA-merged-0x0.bin` at `0x0` —
   see [[Flashing and Recovery]].
2. **After that:** update on-device via **[[Firmware Updates|Firmware → Update over
   WiFi]]** or from SD.
3. Explore the **[[Apps]]**, tweak Settings, try the **[[Script Runner]]**.

## Contents
- **[[Releases]]** — what changed in every version
- **[[Apps]]** — the full app roster (each app has its own page)
- **Features:** [[Firmware Updates]] · [[SD Settings Backup]] · [[WiFi Time Sync]] ·
  [[On-Device Screenshots]] · [[Settings Rail]] · [[Boot Splash]] · [[Script Runner]]
- **Reference:** [[Flashing and Recovery]] · [[Building from Source]] ·
  [[Hardware and Gotchas]]

## Based on
The open-source [`mingolucky/meowkit-s3-firmware`](https://github.com/mingolucky/meowkit-s3-firmware)
(links GPL-3.0 / LGPL-2.1 code — treat this fork under those terms). Built entirely
in Docker. LVGL desktop simulator adapted from upstream PR #31 (kdelfour).
