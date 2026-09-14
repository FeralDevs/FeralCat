# Lua Apps (installable app platform)

> 🐱📦 **Contributed** — the Lua runtime + app platform were contributed by
> [Caliun](https://github.com/Caliun) ([PR #2](https://github.com/janud/MeowKitCustomFW/pull/2));
> integrated here as an **opt-in** feature.

> ⚠️ **Experimental — off by default.** You must enable it in Settings before it
> does anything (see below). It runs scripts from the SD card in a sandbox.

Install and run small **Lua** apps straight from the SD card — no firmware
rebuild, no toolchain. Great for community apps and quick tools.

## Enabling it
It ships **disabled**. To turn it on:

1. **Settings ▸ Features ▸ Lua apps** → toggle **on**.
2. Confirm **Reboot** when prompted (the change applies on boot).

When **off**, there is zero Lua activity — no App-manager tile, no SD scan,
nothing runs. When **on**, an **App manager** tile appears in the launcher along
with a tile for each installed package.

## Installing an app
Copy a package folder to **`/apps/<name>`** on the SD card (each has a
`manifest.ini` + `main.lua`). It appears in the launcher after the next scan
(or open **App manager**). A **Hello Meow** example ships in the repo under
[`sd files/apps/hello_meow`](https://github.com/janud/MeowKitCustomFW/tree/main/sd%20files/apps/hello_meow).

## What Lua apps can do
- Draw a simple button/label UI on screen.
- Bounded execution: memory / instruction / time quotas and capability checks, so
  a misbehaving app can't hang or crash the device.
- Access the media/audio service (the same engine [[MeowPlayer]] uses).

APIs and packaging: see the in-repo docs
[`LUA-APPS.md`](https://github.com/janud/MeowKitCustomFW/blob/main/docs/LUA-APPS.md),
[`LUA-AUDIO-API.md`](https://github.com/janud/MeowKitCustomFW/blob/main/docs/LUA-AUDIO-API.md),
and [`LUA-BUILD.md`](https://github.com/janud/MeowKitCustomFW/blob/main/docs/LUA-BUILD.md).

## Notes
- The Lua runtime is always compiled in (~3% flash) but dormant until enabled.
- Music does **not** depend on this — [[MeowPlayer]] is a native app and always
  works whether Lua is on or off.

## Credit
Lua 5.5.1 runtime, sandbox, app manager and the media/audio service by
**[Caliun](https://github.com/Caliun)** (PR #2). Thank you!
