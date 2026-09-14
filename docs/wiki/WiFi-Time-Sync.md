# WiFi Time Sync

> 🐱 **Custom** — built for this fork (not in the original firmware).

Sets the clock correctly over WiFi with **no manual timezone**.

## Use it
**Settings ▸ Time ▸ Sync over WiFi.** (Connect to WiFi once in **Settings ▸
WiFi** first — the sync reuses the saved credentials.)

## How it works
1. Reconnects to the saved WiFi.
2. Gets precise **UTC from NTP** (`pool.ntp.org` + Google/Cloudflare fallbacks),
   then stops the SNTP updater so it can't later overwrite local time.
3. Gets this connection's **timezone offset from IP-geolocation** (ip-api.com,
   plain HTTP — no TLS needed).
4. Sets **both** the ESP32 system clock and the **PCF8563 RTC** to local time.

The system clock is also **restored from the RTC at boot**, so the time is correct
offline after a reboot (this was missing in stock — the clock read wrong until set).

## Caveats
- Behind a **VPN/proxy** you get the exit node's timezone, not yours.
- The API call reveals your IP to a third party (inherent to IP-geolocation).
- DST: the current offset is applied; re-sync after a DST change (or on next WiFi
  connect) keeps it right.
