# MeowGotchi

> 🐱 **Custom** — built for this fork (not in the original firmware).

A pwnagotchi-style WiFi hunter with a cat-face UI — the flagship app of this fork.
It passively watches 2.4 GHz WiFi, hops channels, discovers access points and
their clients, and captures WPA handshakes to the SD card. A mood face and live
stats react to what it finds.

## Controls
- **A** — Start / Pause hunting
- **short B** — menu (Mode, Handshakes list)
- **hold B** — exit
- **hold Up+Down** — [[On-Device Screenshots|screenshot]] to SD

## What it does
- **Promiscuous sniff + channel hop** across the 2.4 GHz band.
- **AP / client discovery** — tracks access points and associated stations.
- **WPA handshake (EAPOL) capture** → written as `.pcap` (LINKTYPE 105) to
  `/handshakes/` on the SD card, ready for offline analysis.
- **Opt-in deauth** — off by default; when enabled, sends broadcast deauth to
  encourage a client to reconnect so a handshake can be captured.
- **Mood face + stat strip** — the cat's expression and the on-screen counters
  reflect activity; the play-timer counts only while actively hunting.

## Notes
- **Passive by default.** Deauth and handshake capture are **opt-in** — only use
  them on networks you own or are authorised to test.
- The app's radio code only runs while the app is open; nothing transmits at boot.
- Roadmap: an **XP / leveling** system (persisted to SD) is planned so the cat
  levels up from captures — see [[Releases]].
