# WiFi Analyzer

> 🐱 **Custom** — built for this fork (not in the original firmware).

A 2.4 GHz WiFi scanner. Lists nearby access points ranked by signal, and drills
into per-AP detail.

## Controls
- **A** — open detail for the selected AP
- **short B** — rescan
- **hold B** — exit

## What it shows
- **List:** SSID, channel, signal quality (%), and a lock icon for encrypted
  networks.
- **Detail:** BSSID (MAC), signal in dBm and %, and the security type.

Built on the BSP WiFi scan, which captures SSID, BSSID, RSSI, channel and the
auth/encryption mode for each network.
