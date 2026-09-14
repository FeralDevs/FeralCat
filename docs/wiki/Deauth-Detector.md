# Deauth Detector

Passively watches for **deauthentication / disassociation** frames — the
signature of a WiFi "kick" attack — and helps you locate the attacker.

## Controls
- **A** — pause / resume
- **short B** — toggle graph ↔ Attacker Log
- **hold B** — exit

## Views
- **Graph:** a CLEAR/ALERT banner plus a 30-second bar graph of deauth/disassoc
  frames per second, with the current channel and totals.
- **Attacker Log:** each source MAC (usually the spoofed AP BSSID), how many
  frames it sent, and its **RSSI** — signal strength tells you how close the
  transmitter is, so you can walk toward it.

## How it works
Listen-only (promiscuous mode), never transmits. Counts management frames of
subtype deauth (12) and disassoc (10), buckets them per second, and alerts when
the rate crosses a threshold.
