# Desktop UI simulators

Two small headless simulators that render the firmware's **own UI code** on a PC
and dump each screen to an image — so a UI change can be reviewed (and
screenshotted for the README) without flashing hardware. Everything runs in
Docker; nothing is installed on the host.

> These are **not** part of the official/stock MeowKit firmware — upstream ships
> no simulator. See [Attribution](#attribution).

## Why the renders are faithful (not mockups)

The apps draw through **pure `template<LCD>` functions** (see
[`../src/app/app_common/mk_tui.h`](../src/app/app_common/mk_tui.h) and each app's
UI header). The exact same function compiles two ways:

| Target | `LCD` is… |
|---|---|
| **On device** | the real ST7789 panel via LovyanGFX |
| **In the sim** | an off-screen sprite / SDL surface |

So the pixels the simulator produces come from the identical drawing code the
firmware runs. Only the *data* is illustrative — the scenes are fed sample values
(a fake SSID list, `47%`, `v0.3.0 → v0.4.0`); on device those are live.

## The two simulators

| | [`sim/`](.) — **LVGL sim** | [`sim/tui/`](tui) — **TUI sim** |
|---|---|---|
| Renders | LVGL **system** screens (home, settings, About, wifi list…) | LovyanGFX **MK_TUI app** screens (MeowGotchi, detectors, Firmware app) |
| Binary | `meowkit-sim` | `meowkit-tui` |
| Backend | SDL2 surface | off-screen `LGFX_Sprite` |
| Output | BMP, 320×240 | BMP, 320×240 |
| Select screen | `--screen <name>` | `--scene <name>` |
| Write image | `--shot <file.bmp>` | `--out <file.bmp>` |
| Origin | community PR #31, extended here | written for this fork |

Both draw at the device's native **320×240**. Renders land in `sim/shots/`;
the ones used in the README are copied into [`../docs/screenshots/`](../docs/screenshots).

## Build the container image (once)

The repo doesn't vendor the Dockerfiles, so create the tiny SDL image with this
one — it's just Debian + SDL2 + CMake:

```dockerfile
# Dockerfile.sim
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config libsdl2-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
CMD ["bash"]
```

```bash
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"   # macOS
docker build -t meowkit-sim:local -f Dockerfile.sim .
```

## Render a screen

Run everything against the repo root mounted at `/work`. `--shot`/headless mode
uses SDL's `dummy` video driver, so no display server is needed.

```bash
cd <repo root>                                   # …/firmware-upstream
IMG=meowkit-sim:local

# ── TUI sim (MeowGotchi / detectors / Firmware app) ───────────────────────
docker run --rm -v "$PWD":/work -w /work $IMG bash -c '
  cmake -S sim/tui -B sim/tui/build >/dev/null && cmake --build sim/tui/build
  SDL_VIDEODRIVER=dummy sim/tui/build/meowkit-tui \
      --scene wifi_download --out /work/sim/shots/wifi_download.bmp'

# ── LVGL sim (system screens) ─────────────────────────────────────────────
docker run --rm -v "$PWD":/work -w /work $IMG bash -c '
  cmake -S sim -B sim/build >/dev/null && cmake --build sim/build
  sim/build/meowkit-sim --screen settings --shot /work/sim/shots/settings.bmp'

# ── BMP → PNG (macOS) ─────────────────────────────────────────────────────
sips -s format png sim/shots/wifi_download.bmp --out sim/shots/wifi_download.png
```

Both binaries also accept `--list`. The LVGL sim can run **interactively**
(mouse = touch) if you drop the `--shot` flag and have a display.

## Scene / screen catalog

**TUI sim (`--scene`):**

| App | Scenes |
|---|---|
| MeowGotchi | `sleep` `hunt` `excited` `cool` `bored` `sad` `menu` |
| WiFi Analyzer | `wlist` `wdetail` |
| Deauth Detector | `deauth_clear` `deauth_alert` `deauth_log` |
| BLE Spam Detector | `ble_clear` `ble_alert` |
| Rogue Radar | `twins` `flood_clear` `flood_alert` |
| Firmware | `firmware_menu` `wifi_avail` `wifi_download` `sd_progress` |

**LVGL sim (`--screen`):** `home` `apps` `clock` `settings` `tabview` `wifi`
`files` `manual` `usb_msc` `update` `pcmon`, plus the `about` overlay.

## Adding a scene for a new screen

1. Add a branch to the sim's `main` that calls the **same** draw helpers the app
   uses, with sample data:
   - TUI: an `else if (!strcmp(scene, "my_scene"))` block in
     [`tui/main.cpp`](tui/main.cpp) calling `MK_TUI::drawHeader/drawMenuItem/…`.
   - LVGL: an entry in the `SCREENS[]` table (or a special overlay like `about`)
     in [`src/main.c`](src/main.c).
2. Rebuild + render as above; copy the PNG into `../docs/screenshots/` if it's
   for the README.

Keeping the on-device UI in shared `template<LCD>` helpers is what lets the same
code back both the firmware and these scenes — don't fork the drawing logic.

## Layout

```
sim/
├── CMakeLists.txt        LVGL sim build (SDL2 + vendored LVGL + real src/ui)
├── src/main.c            LVGL sim entry: --screen / --shot / --list, About overlay
├── src/stubs.c           desktop stand-ins for firmware entry points
├── compat/               desktop shims for ESP-only headers
│   ├── Arduino.h  esp_mac.h  esp_system.h  nvs.h
└── tui/
    ├── CMakeLists.txt    TUI sim build (SDL2 + LovyanGFX)
    └── main.cpp          TUI sim entry: --scene / --out / --list
```

The `compat/` shims let the LVGL sim link the real `src/ui` code on a desktop by
providing no-op stand-ins for ESP-IDF headers (`esp_read_mac`,
`esp_get_free_heap_size`, NVS, Arduino types) that don't exist off-device.

## Attribution

- **LVGL sim (`sim/`)** — adapted from upstream **PR #31 by kdelfour** (never
  merged into stock), then extended in this fork (About overlay + fork branding,
  the `compat/` ESP shims, extra screens).
- **TUI sim (`sim/tui/`)** — written for this fork. The MK_TUI apps draw with
  LovyanGFX rather than LVGL, so PR #31's LVGL sim can't render them; this is a
  purpose-built headless LovyanGFX renderer for those screens.

Neither exists in an official MeowKit build.
