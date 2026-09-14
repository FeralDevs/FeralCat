# Meow XP (leveling)

> 🐱 **Custom** — built for this fork (not in the original firmware).

A device-wide experience/leveling system. The whole point is to reward *using*
the MeowKit — XP trickles in just from carrying it and poking around, and "finds"
(WiFi handshakes, and more later) give a bonus on top. Your level shows on the
**Home screen**, and a banner pops when you level up.

## Where you see it

- **Home screen** — a thin XP progress bar along the bottom, next to the
  navigation cross, with a **"Lv N"** badge at its right end. It fills toward the
  next level and resets up a level when you get there.
- **Level-up banner** — a green **"Level N — Title!"** toast slides in over
  whatever you're doing when you cross a level.

## How you earn XP

Everything nudges the bar; finds spike it.

| Action | XP |
|---|---|
| Screen on / actively using it | **+1 per minute** |
| On but screen off (in your pocket) | **+1 per 5 minutes** |
| Open an app | **+2** (rate-limited to once / 2 min) |
| **First-ever** open of an app | **+20** (explore the whole toolset once) |
| Run a Berry script (Script Runner) | **+5** per successful run |
| **WiFi handshake captured** (MeowGotchi) | **+15** |

> Passive WiFi scan counts (APs/clients seen) deliberately award **nothing** —
> in a busy area that would flood the bar and defeat the "fidget with it" goal.
> More find bonuses (rogue AP / tracker / BLE-spam / deauth detectors, +3 each)
> are planned for a later release.

## Levels & titles

XP needed to *reach* level L is `25·(L-1)·L` → 0, 50, 150, 300, 500, 750, 1050…
so early levels come quickly and later ones take real playtime.

| Level | Title |
|---|---|
| 1 | Kitten |
| 2 | Script Kitty |
| 3 | Packet Prowler |
| 4 | Handshake Hunter |
| 5 | WiFi Wizard |
| 6–7 | Deauth Diva |
| 8+ | Legendary Meow |

## Where it's saved

Your progress **survives reboots and reflashes**, like the rest of the SD-backed
state:

- The live counters sit in RAM; every earn is written to **NVS** (internal flash)
  immediately, so a crash or sudden power-off loses nothing.
- Roughly **once a minute** (and on shutdown) the state is flushed to
  **`/system/xp.txt`** on the SD card — the durable, portable copy. Move the card
  to another MeowKit and your level comes with it.
- **On boot the SD copy wins.** After a reflash wipes NVS, your level is restored
  from the card. With no card, XP still earns into NVS and syncs once a card is in.

### Tamper check

`/system/xp.txt` carries a checksum, so hand-editing the file to inflate your
level makes it fail validation — it's ignored on load (your NVS value is kept) and
the serial log prints `[xp] backup checksum mismatch — ignored`. The checksum is
device-independent so the card stays portable; since the firmware is open-source
this only deters casual editing, by design.

## Backup / restore

**Settings ▸ Backup** has **Backup XP** (write the SD copy now) and **Restore XP**
(load level & lifetime stats from the card — applied live, no reboot). XP already
auto-syncs and auto-restores, so these are just manual parity with the WiFi /
settings backup entries. See **[[SD Settings Backup]]**.

## Under the hood

`src/system/meow_xp.{h,cpp}` — a small C module any code can call: `meow_xp_tick()`
(the time trickle, driven each frame by the launcher), `meow_xp_add()` /
`meow_xp_add_handshake()` / `meow_xp_app_open()` for the earners, and
`meow_xp_level()/pct()/title()` for the Home bar. State persists via
[`persist`](https://github.com/janud/MeowKitCustomFW/tree/main/src/system/persist.h)
(NVS) and `/system/xp.txt` (SD).
