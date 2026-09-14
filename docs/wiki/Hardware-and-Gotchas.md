# Hardware and Gotchas

## The board

**ESP32-S3-WROOM-1-N16R8** — dual-core LX7 @240 MHz, **16 MB flash**, **8 MB PSRAM**
(OPI). Peripherals:

- 2.0" **320×240 IPS ST7789** (SPI) + **FT6336** capacitive touch
- A/B buttons + 5-way joystick (directions only — see below)
- WiFi + **BLE 5**
- **IR** TX (GPIO7) / RX (GPIO5)
- **WS2812** single RGB LED (GPIO38)
- IMU **BMI270** (+BMM150), audio **ES8311** DAC / **ES7210** ADC / mic / NS4150B amp
- **microSD** (SDMMC 1-bit), **PCF8563** RTC, **AXP173** PMU
- native USB (CDC/HID/MSC); shared I²C SDA=1/SCL=2; display CS/RST + amp-enable via a
  **PCA9557** IO expander (0x19). Full pin map in `src/bsp/config.h`.

## The big one — boot fixes (upstream issue #27)

Retail units **bootlooped** on any firmware built from the public repo. Two stacked
causes, both fixed here:

1. **Wrong flash mode.** The repo built **QIO**; the hardware runs **DIO** (verified
   by diffing the factory image's bootloader header: byte `@0x2` = `02`=DIO vs the
   repo build's `00`=QIO). QIO boots once, then crash-loops. → `board_build.flash_mode
   = dio` + `memory_type = dio_opi`; merge images with `--flash-mode dio`.
2. **Brownout reset.** A current spike during the AXP173/I²C power bring-up trips the
   brownout detector → reset loop. → brownout + watchdogs disabled at the very first
   instruction in `setup()`.

The native-USB serial is unusable while it crash-loops, so this was diagnosed by
**downloading the factory image and diffing the bootloader header** — no serial
needed.

## Other lessons baked into the firmware

- **No screenshot readback.** The ST7789 has **no MISO** (`LCD_MISO = -1`), so the
  panel can't be read back. On-device screenshots capture the app's off-screen
  **sprite** instead (that's why only the LovyanGFX app screens can be grabbed).
- **Joystick centre isn't wired.** The 5-way is labelled "…/Press", but the centre
  switch is **not routed to the ESP32** on this board — none of the free GPIOs
  (0/44/45/46) change when you press it (confirmed with the Debug ▸ Input Monitor).
  So "press" does nothing and the feature was dropped.
- **Flash budget.** The dual-OTA layout gives the app **~7.94 MB** (half the chip;
  the other half is the safe-update spare). The old 2 MB boot GIF was ~28% of the
  app until v0.7.0 replaced it with a progress bar. The firmware also drops the
  Chinese/Japanese/Korean font tables it doesn't need.
- **Don't allocate a LovyanGFX sprite as a value member of an app** — every app is
  constructed at boot, and building a sprite that early hangs startup. Allocate it
  lazily in `onOpen`.
- **Clean-clone build:** `.gitmodules` was never committed upstream; it's restored
  here with pinned submodule URLs. Run `git submodule update --init`.

## Recovery anytime

The official installer at <https://meowkit.cc/pages/download> writes the factory
image. See **[[Flashing and Recovery]]**.
