# Building from Source

Everything builds in **Docker** — nothing is installed on the host (PlatformIO,
the ESP toolchain, and the simulators all run in containers).

## Firmware

```bash
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"   # macOS

# 1. build the toolchain image once (from the parent dir …/MeowKit)
docker build -q -t meowkit-pio:local docker/

# 2. init the pinned submodules (a clean clone needs this — .gitmodules ships)
git submodule update --init

# 3. compile → .pio/build/esp32s3box/{bootloader,partitions,firmware}.bin
docker run --rm -v meowkit-pio:/root/.platformio \
  -v "$PWD/firmware-upstream":/work meowkit-pio:local run
```

Fuse the flash-at-`0x0` image (**DIO!**):

```bash
docker run --rm --entrypoint bash -v meowkit-pio:/root/.platformio \
  -v "$PWD/firmware-upstream":/work -w /work meowkit-pio:local -c '
  BA=$(find /root/.platformio -name boot_app0.bin | head -1)
  python -m esptool --chip esp32s3 merge-bin -o dist/meowgotchi-OTA-merged-0x0.bin \
    --flash-mode dio --flash-size 16MB \
    0x0 .pio/build/esp32s3box/bootloader.bin \
    0x8000 .pio/build/esp32s3box/partitions.bin \
    0xe000 "$BA" \
    0x10000 .pio/build/esp32s3box/firmware.bin'
```

## Simulators (review UI without hardware)

Two headless SDL simulators render the firmware's own drawing code to a PNG, so a
UI change can be checked before flashing. Built for this fork (stock has none):

- **LVGL sim** (`sim/`) — the LVGL system screens (home, settings, the scrollable
  rail, About…). `--screen <name> --shot out.bmp`.
- **TUI sim** (`sim/tui/`) — the LovyanGFX "MK_TUI" app screens (MeowGotchi, the
  detectors, Script Runner, the boot splash). `--scene <name> --out out.bmp`.

Both run headless (`SDL_VIDEODRIVER=dummy`) in the `meowkit-sim` container;
convert BMP→PNG with `sips`. Full details in `sim/README.md`.

## Adding a new app

Add an `installApp()` line and its icon in `src/app/app.h` (both tables, same
order), drop the app under `src/app/app_NN/`, and the scrollable apps menu picks
it up automatically. See `docs/CUSTOM-FIRMWARE.md`.
