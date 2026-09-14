# MP3 Player 1.1.1 / firmware v0.8.1-lua.2

This extension combines an installable Lua MP3 app with native audio and UI
services. Firmware `v0.8.1-lua.2` is based on upstream `meowkit-mine` commit
`dc63a6c` (v0.8.1). The upstream WIP MeowPlayer (`app_19`) remains hidden by
default; the Lua package is a separate app. See [MP3-PLAYER.md](MP3-PLAYER.md)
for installation and controls and [LUA-APPS.md](LUA-APPS.md) for the package API.

## Input handling and runtime behavior

The app reuses menu tables and redraws only when state changes. Its main view
has five fixed controls; the volume page uses three large buttons. Exit follows
a separate confirmation flow. A short B press on the main page does not exit.

The native host records the action ID and page identity on press. Releasing
after a page change cannot activate a new action. Simultaneous B+A does not
activate an action from the previous page. Status updates retain the touch
geometry. Adjacent volume requests are coalesced within bounded limits while
other commands retain their order.

The Lua instruction limit is 50,000 per callback. Audio apps receive an 80 ms
wall-time budget, compared with 20 ms for other apps, to allow for scheduling,
PSRAM access, and garbage collection. A runtime error remains visible and
requires a fresh input after at least one second to dismiss. When the card is
available, the host writes the last error to `/lua-last-error.txt`. The Lua heap
value is captured before the VM is released; internal heap values in the log
are captured after audio shutdown.

## Audio output

When a full DMA buffer prevents an I2S write, the output path retains the
prepared PCM sample and any partial-write offset until I2S accepts it. A retry
does not filter or advance the same sample again. Neutral filters are bypassed.
The codec attenuation is fixed at **−12 dB**; the volume control is digital
`0..100`.

The Neutral, Voice, Warm, and Small speaker profiles use band attenuation,
bounded overall gain, and a short transition. Neutral leaves PCM values
unchanged in the new equalizer. EQ cannot give a small speaker deep bass;
subjective sound quality still requires an on-device listening comparison.
The ES8311 has a mono DAC, so the speaker and headphone outputs do not provide
independent stereo channels.

## Cover loading

Before starting a track, the audio worker tries `cover.jpg`, then `folder.jpg`,
then a supported ID3v2.3/v2.4 APIC JPEG. The loader accepts baseline JPEGs up to
256 KiB and 1,024 × 1,024 pixels. ID3 tags are limited to 512 KiB and 128 frames.
Progressive JPEGs and PNGs produce a placeholder.

Three bounded 96 × 96 RGB565 buffers require 54 KiB of PSRAM in total: two in the
service and one owned by the UI. The main view displays the source image at
80 × 80 pixels. Temporary JPEG and decoder buffers are separately bounded and
released after loading. Allocation failure allows music playback without cover
art. The 500 ms loading budget is checked between reads and decode steps; it
cannot guarantee a bound on blocking SD-driver calls.

The LGFX JPEG decoder has focused guards for the AC zigzag index, coefficient
overflow, and an entropy-buffer start exactly on a 512-byte boundary. The
targeted regressions can also be run with AddressSanitizer.

## Validation scope

The configuration under `test/lua_apps` contains **13 PC test suites** covering:

- Lua runtime, manifest validation, and SD app catalog limits.
- The shipped Lua player with a simulated audio service and the media catalog.
- The real Helix MP3 decoder, DSP, I2S backpressure, equalizer, DAC gain, and
  command ordering/coalescing.
- JPEG/APIC cover handling and the production LVGL player view.

The LVGL test injects 500 rapid pointer gestures, changes status while a pointer
is held, and checks page transitions and memory use. It saves previews directly
from the 320 × 240 framebuffer. The player test includes 2.5 hours of simulated
playback, a full 512-track queue, repeated starts/stops, busy states, and rapid
volume taps. Memory assertions check for growth during these simulated runs.

Build and test commands are documented in [LUA-BUILD.md](LUA-BUILD.md). PC tests
do not establish a hardware pass or cover all ESP32 memory and timing states.
Device validation remains necessary for rapid volume changes, cover display,
sound comparisons, pause/resume, track changes, and sustained playback. If an
app exits with an error, use the displayed message or `/lua-last-error.txt` to
investigate.
