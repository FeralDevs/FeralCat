# On-Device Screenshots

> 🐱 **Custom** — built for this fork (not in the original firmware).

Capture what's on screen to the SD card — no computer.

## Use it
In **MeowGotchi** or any of the **detector** apps, **hold the joystick Up+Down**.
The current screen is saved to **`/screenshots/shot_NNNN.bmp`** on the SD.

## Why only those apps?
The ST7789 panel has **no read (MISO) line**, so the framebuffer can't be read
back. Those apps double-buffer through an off-screen **sprite** that holds exactly
what's on screen, so the screenshot copies that sprite. The LVGL *system* screens
(home, settings…) don't go through such a sprite and aren't captured on device —
but they can be rendered in the desktop simulator (see [[Building from Source]]).
