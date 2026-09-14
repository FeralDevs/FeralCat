# Native audio API for Lua apps

This document describes `v0.8.1-lua.1`, based on upstream `v0.8.1` on
`meowkit-mine`. The Lua API version remains `1`.

Declare the capability in the app manifest:

```ini
capabilities=ui,system,audio
icon=music
```

The host creates an exclusive audio session before starting the app.
Without the capability, `meow.audio` is absent. `meow.system.has("audio")`
reports that the interface is available; individual playback results must be
read from its status. The session ends when the app closes, a Lua fault occurs,
or the device enters USB storage mode.

The upstream native MeowPlayer (`app_19`) remains unchanged and disabled by
`MEOWKIT_ENABLE_PLAYER=0`. That switch controls the upstream work-in-progress
app, not this Lua audio service. Enabling both implementations requires a
separate review of their shared codec, I2S and decoder ownership.

## Commands

`meow.audio.command(action, value)` returns `true` when a command is accepted
into the bounded queue. Acceptance does not mean playback has succeeded.
`false` indicates a busy queue/snapshot or a rejected track selection. Lua
argument type and range violations raise a script error.

| Action | Value | Behavior |
|---|---|---|
| `scan` | Omit | Stop playback and rescan `/mp3` |
| `play` | Track ID 1–512 | Open a track from the current catalog generation |
| `pause` | Omit | Toggle playing/paused state when applicable |
| `stop` | Omit | Stop playback and close the file |
| `volume` | Integer 0–100 | Set digital volume independently of the fixed DAC attenuation |
| `eq` | Integer 0–3 | Select Neutral, Voice, Warm or Small Speaker |
| `seek` | Integer 0–65535 | Seek to an absolute time in seconds |

Seeking requires a playing or paused track with a known duration. A command
can be accepted and subsequently fail or have no effect if its required state
changes before execution; inspect the resulting status.

One worker owns the decoder and open audio files. Its queue holds eight
commands. Adjacent volume commands are collapsed to the latest value, consuming
at most eight entries per worker iteration. Play, pause, seek and EQ commands
remain ordering barriers. Lua never performs SD or I2S operations directly.

## Status

`meow.audio.status()` returns a table, or `nil` when the short snapshot lock is
busy. A `nil` result is temporary and does not mean playback stopped.

| Field | Meaning |
|---|---|
| `state` | `idle`, `scanning`, `loading`, `playing`, `paused`, `stopped`, `ended` or `error` |
| `title`, `error` | Current file title and bounded diagnostic text |
| `track` | Current track ID; zero means no selection |
| `position`, `duration` | Seconds; MP3 timing can be approximate and duration can initially be zero |
| `volume` | Digital volume, 0–100; default 35 |
| `equalizer` | Active preset, 0–3 |
| `tracks`, `playlists`, `skipped` | Catalog sizes and skipped entries |
| `generation` | Increments when a completed catalog scan is published |
| `playback` | Increments for each executed valid Play request, including replaying the same ID |
| `finished` | Increments once per natural track end; explicit stop and errors do not increment it |

The counters are unsigned 32-bit values and wrap. A matching title or track ID
alone cannot acknowledge a new Play request: repeated tracks and duplicate M3U
entries also need the `playback` sequence. The bundled player counts accepted
Play requests and associates delayed status snapshots with that sequence.
Discard old catalog IDs and playback queues after a `generation` change.

## Catalog pages

```lua
local page = meow.audio.tracks(0, 0, 8)
local playlists = meow.audio.playlists(0, 8)
-- Each result is nil while busy, or:
-- {total=N, items={{id=1, title="Track", count=0}, ...}}
```

Offsets start at zero; IDs start at one. Each query returns at most eight
entries. `tracks(playlist, offset, limit)` defaults to `(0, 0, 8)`; playlist
zero means all tracks and IDs 1–16 select an M3U list. Playlist order and
duplicates are preserved. `playlists(offset, limit)` defaults to `(0, 8)`;
its entry `count` is the playlist length. Pages are unavailable during a scan,
so an old catalog is not exposed as the newly scanned one.

The catalog indexes local MP3 files under `/mp3`, including subdirectories,
and local M3U lists. Bounds are 512 tracks, 16 playlists, 4096 inspected
directory entries, depth four, 191-byte canonical paths, 64 KiB per playlist
and 255 bytes per playlist line. Paths must remain inside `/mp3`.
See the [catalog limits](../src/system/media/media_catalog.h) and
[path validation](../src/system/media/media_paths.cpp).

## Output, EQ and artwork

The service uses I2S0, BCLK GPIO14, WS GPIO13 and DOUT GPIO16. Stereo PCM is
mixed to mono while I2S retains two 16-bit slots. Supported MP3 sample rates
are 22.05, 32, 44.1 and 48 kHz; other rates are rejected before PCM output.
The ES8311 has a mono DAC: this path does not provide independent stereo
channels through either the speaker or headphone jack.

DAC attenuation is fixed at **−12 dB**, register `0x32 = 0xA7`, with checked
I2C readback. Digital volume starts at 35/100. Existing speaker callers retain
their legacy volume API. See [DAC and PCM implementation notes](MP3-AUDIO-FIX.md).

EQ presets attenuate selected frequency bands and normalize their combined
gain; they do not raise DAC gain or apply loudness compensation. Neutral is
bit-exact after the bounded 256-frame preset transition. Voice and Small
Speaker reduce low bass; Warm reduces treble. These profiles cannot extend
the physical bass response of a small speaker. See the
[production equalizer](../src/system/media/audio_equalizer.h).

Artwork is a native UI feature, not a Lua image/file API. Before opening a
track, the worker tries `cover.jpg`, then `folder.jpg` in that directory,
then supported ID3v2.3/v2.4 APIC JPEG artwork. Images are limited to 256 KiB
and 1024×1024 pixels; ID3 tags to 512 KiB and 128 frames. The result is a
96×96 RGB565 thumbnail. Unsupported, missing or invalid artwork uses the
placeholder; music can continue without artwork when memory is unavailable.
The 500 ms loading budget is checked between reads and decode steps and
cannot interrupt a blocking SD driver. See the
[cover loader contract](../src/system/media/cover_loader.h).

## Ownership and validation

Microphone and legacy speaker drivers are stopped before the exclusive
session begins. Closing waits cooperatively until the worker closes files
and destroys its decoder/I2S driver; then the amplifier and codec are shut
down. A worker holding open files is not forcibly deleted. Snapshot getters
only copy bounded memory and use a nonblocking lock attempt.

The [native media contract](../src/system/media/media_api.h),
[Lua bindings](../src/system/lua_apps/lua_runtime.cpp) and
[audio service](../src/system/media/audio_service.cpp) define the implementation.
[Host tests](../test/media/CMakeLists.txt) cover real MP3 decoding, catalog and
app logic, output backpressure, EQ and artwork. They do not establish audible
output, ESP32 stack margin, electrical power, SD latency or a hard realtime
deadline. Device tests remain necessary after integration; build instructions
are in [LUA-BUILD.md](LUA-BUILD.md).

This service exposes local MP3/M3U playback. Network streaming and SoundCloud
are outside the API.
