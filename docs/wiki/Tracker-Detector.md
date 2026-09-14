# Tracker Detector

An **anti-stalking** BLE scanner: it looks for item trackers that follow you.

## Controls
- **A** — pause / resume
- **hold B** — exit

## What it detects
Classifies BLE advertisements from:
- **Apple Find My / AirTag** (manufacturer 0x004C, Find-My type 0x12)
- **Tile** (service UUID 0xFEED / 0xFEEC)
- **Samsung SmartTag** (manufacturer 0x0075 / service UUID 0xFD5A / 0xFD59)

Each tracker is listed with its type, proximity (RSSI) and **how long it's been
near you**. When one has been present longer than ~60 s and is still around, the
banner shows **`FOLLOWED!`**.

## Caveat
Find My trackers **rotate their MAC roughly every 15 minutes**, so a single
tracker can reappear as a "new" entry — dwell timing is best-effort, not forensic.
Listen-only.
