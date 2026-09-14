# Apps

Open the apps grid from the home screen (joystick **Left**). Universal controls:
**A** = primary action, **short B** = secondary/toggle, **hold B** = exit. In
MeowGotchi and the detectors, **hold the joystick Up+Down** to save a screenshot
to `/screenshots/` on the SD card.

## Security / WiFi & BLE tools

| App | What it does |
|---|---|
| **MeowGotchi** | Pwnagotchi-style cat-face WiFi hunter: promiscuous sniff, channel hop, AP/client discovery, WPA-handshake (EAPOL) capture to `/handshakes/*.pcap` on SD, opt-in deauth. Mood face + live stats. `A` = start/pause · short `B` = menu · hold `B` = exit. |
| **WiFi Analyzer** | Scans 2.4 GHz, lists APs by signal (channel, quality, lock), per-AP detail (BSSID, dBm+%, security). |
| **Deauth Detector** | Passive deauth/disassoc monitor (CLEAR/ALERT + 30 s graph) with an **Attacker Log** (source MAC, count, RSSI to locate the attacker). |
| **Probe Sniffer** | Passive 802.11 probe-request capture — which devices are nearby and the SSIDs they leak (MAC, signal, requested network). |
| **Rogue Radar** | **Evil-Twin scan** (SSIDs advertised both open and secured) + **Beacon-Flood / Karma** monitor. |
| **Tracker Detector** | Passive BLE scan for **AirTag/Find My, Tile, Samsung SmartTag** item trackers; alerts (`FOLLOWED!`) when one stays near you. |
| **BLE Spam Detector** | Passive BLE scan for Apple/Google/MS/Samsung spam-popup floods; alerts on distinct advertiser MACs/sec. |
| **BLE Spam** | Sends BLE advertisement bursts (educational). |
| **Bad USB** | USB-HID keystroke-injection scripts (Ducky-style). |
| **Infrared** | IR remote — record/replay and a universal-remote database. |

## Tools

| App | What it does |
|---|---|
| **Firmware** | **Update over WiFi** (pulls the latest GitHub release to the SD), **Update from SD** (`/firmware.bin`), or **USB Download Mode**. See **[[Firmware Updates]]**. |
| **Script Runner** | Run **Berry** `.be` scripts from `/scripts` on the SD. See **[[Script Runner]]**. |

## Desktop gadgets

| App | What it does |
|---|---|
| **PC Monitor** | Shows PC stats (CPU/GPU/temps) streamed over USB serial. |
| **Air Mouse** | Move the host cursor with the IMU (BLE HID mouse). |
| **VU Meter** | Audio level meter from the mic. |
| **Retro TV** | Retro TV animation. |
| **Matrix Rain** | Digital-rain screensaver. |
| **Dino** | The offline dino jump game. |

*(Not carried over from stock: Music Player, Webserial, LED Matrix, AI Chat.)*
