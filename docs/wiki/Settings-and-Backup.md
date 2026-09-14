# Settings and Backup

Open Settings from the home screen (joystick **Right**), then tap the **gear** for
the detailed settings — a **scrollable side rail** of tabs: Display, Sound,
Connect, Time, System, Backup.

## SD backup & restore (survive a reflash)

Settings normally live in NVS (internal flash), which a **full USB reflash
erases** — so you'd lose your WiFi and preferences. This firmware mirrors every
setting to **`/config/meowkit.cfg`** on the SD card on each change, and restores
it automatically when NVS comes up empty.

**Settings ▸ Backup** tab:
- **Backup now** — force-write the current settings to the SD.
- **Restore all** — settings + WiFi from the SD backup.
- **Restore WiFi only** — just the WiFi credentials.
- **Restore settings only** — everything except WiFi.

Each restore confirms, writes NVS, and **reboots** to apply cleanly. On boot, if
NVS has no saved WiFi (i.e. it was just erased), the backup is restored
automatically.

> ⚠️ The **WiFi password is stored in plain text** in `/config/meowkit.cfg` (by
> design, so the backup is portable and editable). Anyone with the SD card can
> read it.

## WiFi Time Sync

**Settings ▸ Time ▸ Sync over WiFi** — reconnects to the saved WiFi, gets precise
UTC from **NTP**, and this connection's timezone from **IP-geolocation**
(ip-api.com), then sets both the system clock and the PCF8563 RTC to local time —
**no manual timezone**. The clock is also restored from the RTC at boot, so it's
correct offline. (IP-geo gives the wrong zone behind a VPN.)

## Other settings

Display brightness / auto-dim, LED brightness/color/effect, speaker volume + key
tone, Bluetooth name/enable, WiFi connect, date/time pickers, battery/storage
info, Reboot, Factory Reset. All of these are included in the SD backup.

## Developer: Input Monitor

A hidden **Debug** tab (a live "last pressed" input monitor with GPIO) exists
behind a compile flag `MEOWKIT_DEBUG_TAB` in `src/bsp/config.h` (off in releases).
It was used to confirm the joystick centre isn't wired — see
**[[Hardware and Gotchas]]**.
