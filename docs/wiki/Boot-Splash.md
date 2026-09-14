# Boot Splash

The boot screen is a small **MeowGotchi + version** title with a **progress bar
that tracks the real boot stages** — SD card → Apps → Display → Interface → done —
then fades into the home screen.

## Why it changed (v0.7.0)
Stock baked a **~2 MB animated GIF** straight into the app image and played it
(with the AnimatedGIF decoder) for up to 15 s on every boot. Replacing it with a
LovyanGFX-drawn title + progress bar:
- **freed ~1.6 MB** of the app slot (88.6% → 70.6% flash), and
- **boots faster** — the bar fills as fast as the device actually initialises,
  with no artificial delay.

It also removed a fragile pre-build hook that embedded the GIF.
