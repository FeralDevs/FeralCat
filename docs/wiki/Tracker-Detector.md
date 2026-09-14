# Tracker Detector

Native passive BLE tracker-candidate scanner with a selected-target proximity
finder. The proximity extension runs inside **TrackerDetect**; it needs
neither Lua nor an SD application package.

## Find a tracker

1. Open **TrackerDetect** and wait for candidates to appear.
2. Move the joystick **Up/Down** to select a row. All 24 observation slots are
   reachable over pages. The address suffix and session ID distinguish similar
   devices. Selection follows the identity when signal sorting changes.
3. Press **A** to open the selected candidate's radar.
4. Move slowly through the room, keeping the MeowKit in a similar orientation.
   Use the relative strength rings, dBm reading and recent-sample graph to
   compare positions. **STRONGER** suggests trying that area; **WEAKER** suggests
   moving back. Walls, your body and reflections can change the signal.
5. Press **B** to return to the list; **hold B** to exit the app.

## Controls

| View | Input | Action |
|---|---|---|
| List | Up / Down | Select previous / next candidate; pages follow selection |
| List | A | Find the selected candidate |
| List or radar | Left / Right | Pause / resume scanning |
| Radar | A | Pause / resume scanning |
| Radar | B | Return to list |
| Either | Hold B | Exit to launcher |
| Error | A or Left / Right | Retry scanner; start a new observation session |
| Either | Up + Down together | Existing SD screenshot shortcut |

## What the radar measures

The radar displays **relative received signal strength**, not a measured
distance or bearing. Stronger signals light more concentric rings; there is no
direction arrow or meter estimate. RSSI is filtered from real advertisements
using a short median filter followed by an exponential average. Repainting the
screen does not create extra measurements.

The trend compares new filtered readings with recent history and needs enough
samples before appearing. The graph contains up to 48 actual sampled readings,
at most four per second; it is a sample history, not a fixed-duration chart.

- **Live:** last reception is at most 2.5 seconds old.
- **Waiting:** no recent reception; live strength is hidden.
- **Lost:** last reception is more than 10 seconds old; the selected identity
  remains selected and cannot silently switch to another candidate.
- **Paused / Starting / Scan error:** no live proximity indication.

After resume, a new reception is required. A gap of five seconds resets the
signal filter and trend history. These initial thresholds need practical
validation with the advertisement cadence of the tags being searched.

## Detection and identity limits

The existing signatures identify Apple Find My advertisements, Tile service
data and Samsung manufacturer/service data. They do **not** prove the exact
device model: in particular Samsung manufacturer ID `0x0075` is broad, and
Apple Find My data is not exclusive to AirTags. Results are **candidates**.

Entries are keyed by BLE address **and address type**, with a stable identifier
inside the current scan session. A rotating address appears as a new candidate;
the app does not assert it is the same physical tracker. A focused/selected
entry is protected when the 24-slot table is full; older unselected entries may
be replaced. The selected entry remains available during loss of reception.

The former `FOLLOWED!` wording has been replaced with a presence description.
Long presence in radio range alone does not establish that a device followed
you. This scanner listens only and does not connect to or ring the remote tag.

Implementation, tests and hardware validation scope: [Native radar notes](../TRACKER-RADAR.md).
