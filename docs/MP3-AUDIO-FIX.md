# MP3 DAC and PCM implementation notes

These changes are part of `v0.8.1-lua.2`, based on upstream `v0.8.1` on
`meowkit-mine`. They make the native audio path suitable for the Lua MP3 app.
The [Lua audio API](LUA-AUDIO-API.md) documents its public contract.

## Explicit DAC attenuation

The inherited `es8311_voice_volume_set()` maps a nominal 0–100 value into a
0–255 register index. Passing `SPK_VOLUME_MAX=16` produces `0x27` (39).
That is not 16% PCM amplitude: ES8311 DAC register `0x32` uses a logarithmic
scale, with `0xBF` representing 0 dB and 0.5 dB per step. Consequently,
`0x27` means −76 dB and can make otherwise valid playback nearly inaudible.
The register scale is specified on page 25 of the
[ES8311 datasheet, revision 10.0](https://files.waveshare.com/wiki/common/ES8311.DS.pdf).
[Espressif's ES8311 implementation](https://github.com/espressif/esp-adf/blob/release/v2.x/components/esp_codec_dev/device/es8311/es8311.c)
also defines the register range as −95.5 to +32 dB.

The player uses an explicit **−12 dB** setting, `0x32 = 0xA7`, followed by a
checked register readback. An I2C write failure, read failure or mismatched
value aborts audio initialization. No positive DAC gain is exposed by this
API. This is an output policy, not a measured speaker-power or acoustic
certification.

The change is opt-in for the codec-only player session. Other apps keep the
legacy `Speaker_Class::setVolume()` behavior; the Lua app controls digital
volume from 0 to 100, initially 35. Relevant sources are the
[gain policy and checked bus operation](../src/bsp/audio/dac_gain.hpp),
[ES8311 adapter](../src/bsp/audio/ES8311_Class.cpp) and
[speaker session interface](../src/bsp/audio/Speaker_Class.cpp).

## PCM correctness under output backpressure

The vendored decoder uses nonblocking I2S writes. Previously, a full DMA
buffer left the current input sample pending but its filter history had
already advanced. Retrying that sample therefore ran recursive DSP again.

[PendingFrame](../lib/ESP32-audioI2S/src/audio_output.h) retains the processed
stereo word and the number of bytes already written. Retries send only the
remaining bytes and do not repeat DSP, gain or the PCM callback. Stop, pause
and seek discard a pending word. Signed PCM channels are packed with unsigned
shifts to avoid undefined behavior when the left sample is negative.

Filter history is initialized with the Audio object and reset for a new
source. Neutral tone bypasses the three legacy biquads, avoiding unnecessary
floating-point cancellation and per-sample work. The shared
[DSP helper](../lib/ESP32-audioI2S/src/audio_dsp.h) and
[Audio integration](../lib/ESP32-audioI2S/src/Audio.cpp) are exercised by tests
using the production code rather than a separate filter model.

The Lua player's four [EQ presets](../src/system/media/audio_equalizer.h) run
in its native PCM callback. They use band attenuation with a bounded mixing
gain and a 256-frame transition. Neutral preserves PCM values after that
transition. EQ does not increase the fixed DAC level.

## Resource ownership and supported hardware

One [AudioService](../src/system/media/audio_service.cpp) worker owns the
decoder, SD audio files and I2S0. Codec setup/teardown runs on the app owner,
outside Lua. The legacy speaker and microphone are stopped before the
session starts. Closing cooperatively waits for file and decoder cleanup
before releasing the codec and disabling the amplifier.

MP3 output supports 22.05, 32, 44.1 and 48 kHz with two 16-bit I2S slots.
Stereo input is mixed to mono. The ES8311 is a mono codec, as shown in the
[manufacturer's block diagram](https://files.waveshare.com/wiki/common/ES8311.DS.pdf#page=5);
its two analog output pins do not provide independent left/right channels.
No stereo-headphone capability is claimed.

The upstream `app_19` MeowPlayer remains unchanged and hidden with
`MEOWKIT_ENABLE_PLAYER=0`. Its decoder/task lifecycle is separate from this
service; this integration does not enable or validate simultaneous use.
The flag does not disable the decoder needed by Lua audio apps.

## Regression tests and remaining device checks

The [host media tests](../test/media/CMakeLists.txt) cover:

- Correct DAC register values and failed/mismatched I2C transactions through a bus adapter.
- Poisoned filter memory, source resets and exact neutral mono/stereo PCM values.
- Zero-progress and partial writes, including 100,000 output frames with no duplicates or gaps.
- Four sample rates, EQ frequency response, transitions and bounded stress input.
- Adjacent volume-command coalescing without crossing play/pause/EQ ordering barriers.
- The actual Helix MP3 decoder with a [locally generated sine-wave fixture](../test/media/fixtures/README.md), including allocation failure and bounded seek synchronization.

The cover loader additionally uses the existing LGFX JPEG decoder. Its
[targeted decoder changes](../lib/LovyanGFX/src/lgfx/utility/lgfx_tjpgd.c)
guard AC coefficient indices, coefficient arithmetic and aligned entropy
buffer starts. [Cover tests](../test/media/cover_test.cpp) include malformed
JPEG/APIC data and generated images; see [LUA-BUILD.md](LUA-BUILD.md) for
running the host suite.

Host tests do not execute the physical I2S/codec bus or the ESP32 FreeRTOS
worker. They cannot establish audible quality, stack/heap margin, amplifier
power or SD-card timing. After rebasing, check playback, volume bursts,
pause/resume, seeking, track changes and EOF, supported sample rates, artwork,
repeated open/close cycles, then another native app and USB storage mode.
Record the tested hardware and build separately from these implementation
claims.
