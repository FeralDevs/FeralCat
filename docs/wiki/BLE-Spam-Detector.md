# BLE Spam Detector

Passively scans BLE for the **pop-up spam floods** that phones show (fake AirPods,
SwiftPair, Fast Pair, etc.) and alerts when one is happening near you.

## Controls
- **A** — pause / resume
- **hold B** — exit

## What it detects
Counts spam advertisement payloads from **Apple** (types 0x0F/0x07), **Microsoft**
SwiftPair (0x03), **Samsung** EasySetup (0x0075) and **Google** Fast Pair
(0xFE2C). To avoid false alarms from legitimate devices, it alerts on the number
of **distinct advertiser MACs per second** (spam uses many random MACs), with a
per-vector breakdown.
