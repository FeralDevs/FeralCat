# MeowPlayer (MP3 player)

> 🐱 **Custom** — built for this fork. The player UI is custom; the audio engine
> underneath is contributed (see credit below).

A music player for MP3/WAV files on the SD card, with a proper transport UI and
speaker / 3.5mm-jack output.

## Using it
1. Put audio files in a **`/mp3`** folder on the SD card (`.mp3` or `.wav`).
2. Open **MeowPlayer**. It scans `/mp3` and starts the first track.

## Controls
| Key | Action |
|---|---|
| **A** | Play / Pause |
| **B** | Menu (Songs list · Output: Speaker/Jack) |
| **Up / Down** | Volume |
| **Left / Right** | Seek −5 s / +5 s |
| **hold B** | Exit |

Tracks auto-advance at the end. The **Output** menu switches between the built-in
speaker and the 3.5mm jack (the speaker amp is gated by `PA_EN`; volume to the
1 W speaker is capped for safety).

## Supported audio
MP3 and WAV at **22.05, 32, 44.1 or 48 kHz** (the rates that share this board's
ES8311 clock divider). Other rates are skipped with a notice.

## Under the hood
The UI (now-playing screen, Songs/Output menus, controls) is native to this fork.
Decoding, I2S and the ES8311 codec are handled by a dedicated worker on core 0 —
codec-only init, a large decode buffer, a fixed −12 dB DAC attenuation and clean
DMA flushing on pause/seek — which is what makes playback smooth.

## Credit
The audio engine (`meow::media` service + ESP32-audioI2S / ES8311 fixes) comes
from **[Caliun](https://github.com/Caliun)** via
[PR #2](https://github.com/janud/MeowKitCustomFW/pull/2). This fork pairs it with
a custom native player UI. Thank you!

See also **[[Lua Apps]]** (the optional app platform from the same PR).
