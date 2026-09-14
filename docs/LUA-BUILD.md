# Building and testing Lua apps and the MP3 player

This extension identifies itself as `v0.8.1-lua.1` and builds on the upstream
`meowkit-mine` branch at commit `dc63a6cdf9f15a1ef5f92d0a303cbe6af9fdf43e`.
The separately installed MP3 app is version 1.1.0. All commands below run from
the root of a checkout containing this extension.

## Prepare a checkout

Clone the branch or pull-request revision being tested, then initialize its
pinned submodules:

```sh
git submodule update --init --recursive
git submodule status --recursive
```

A source ZIP does not include the four submodule checkouts; prefer a Git clone.
Do not update the submodules to their latest branches. Their committed Git
revisions are part of the firmware build. Source URLs and archive hashes are
recorded in [build-dependencies.json](../tools/build-dependencies.json).

Lua 5.5.1 is vendored under `lib/lua-5.5.1`, so no separate Lua installation is
required. Its library configuration excludes the standalone `lua.c` and
`luac.c` programs and sets `LUAI_MAXCCALLS=32`. The earlier Lua 5.4.7 sources
remain in the repository and are excluded through PlatformIO's `lib_ignore`.

## Build the firmware

Requirements: Git, Python 3.11, and an Internet connection for the first
PlatformIO/toolchain installation. PlatformIO Core is pinned to 6.1.19 in the
commands below. A desktop C/C++ compiler is only needed for the native tests;
PlatformIO installs the ESP32 compiler itself.

On Windows, in PowerShell:

```powershell
py -3.11 -m venv .venv
& .\.venv\Scripts\python.exe -m pip install 'platformio==6.1.19'
& .\.venv\Scripts\python.exe -m platformio run -e esp32s3box
```

On Linux or macOS, with Python 3.11 available:

```sh
python3.11 -m venv .venv
.venv/bin/python -m pip install 'platformio==6.1.19'
.venv/bin/python -m platformio run -e esp32s3box
```

The application image is `.pio/build/esp32s3box/firmware.bin`. The build uses
`espressif32@6.0.1`, Arduino 2.0.6 / ESP-IDF 4.4.3, DIO flash, OPI PSRAM,
16 MiB flash, and the existing dual-OTA partition layout. Each application slot
holds 8,323,072 bytes. Build output does not replace the files in `releases/`.
Building the firmware does not upload or flash it.

### Optional PowerShell helper

After preparing `.venv`, the helper keeps toolchain downloads, dependencies,
build output, and logs together under `.build`:

```powershell
& .\tools\build-firmware.ps1 -BuildName mp3-player
```

Its image is `.build/build/mp3-player/esp32s3box/firmware.bin`; its log is
`.build/logs/mp3-player-build.log`. The helper uses `.venv/Scripts/python.exe`
on Windows or `.venv/bin/python` on other PowerShell hosts. It restores the
caller's PlatformIO environment variables after the build.

Optional parameters are `-ProjectPath`, `-BuildRoot`, `-BuildName`, and
`-PythonPath`. Relative build/interpreter paths are resolved against
`ProjectPath`, whose default is the repository containing the script. For an
existing toolchain, pass the full path to its Python executable and optionally
an existing cache/build directory. The helper does not create virtual
environments or install Python packages.

## Run the native tests

Requirements:

- CMake 3.20 or newer, including CTest.
- A C99/C11 and C++17 compiler, such as Visual Studio 2022 Build Tools, GCC,
  or Clang.
- Ninja on `PATH` for the generator used below.

For MSVC, run the commands in an **x64 Native Tools Command Prompt for VS 2022**
so CMake can find the compiler and Windows SDK. With GCC or Clang, use a shell
where the chosen compiler is available. The same commands work on both:

```sh
cmake -S test/lua_apps -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

For another compiler, select it at configuration time with
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`, for example, and use a
new build directory. For a multi-configuration generator instead of Ninja,
pass `--config Debug` to the build and `-C Debug` to CTest.

The entry point `test/lua_apps/CMakeLists.txt` includes **13 suites**:

| Suite | Production code and behavior covered |
| --- | --- |
| `runtime` | Real Lua VM, API bindings, execution/memory limits, errors, lifecycle, example app |
| `manifest` | Package schema, capabilities, UTF-8 and size/path limits |
| `catalog` | SD app discovery, revalidation, ordering, removal, short reads and handle cleanup |
| `player_app` | Shipped Lua player, asynchronous status, 512-track queues, M3U duplicates, shuffle/repeat, EQ, busy/error recovery and rapid volume input |
| `media_catalog` | MP3/M3U discovery, bounded paths, allocation/read failures and cancellation |
| `mp3_decode` | Vendored Helix decoder with synthetic audio, output bounds and bounded seek synchronization |
| `audio_dsp` | Filter initialization and source reset using the production DSP kernel |
| `dac_gain` | DAC decibel conversion and checked register access through a bus adapter |
| `audio_output` | Partial/stalled PCM output without repeated filtering |
| `audio_equalizer` | Bounded EQ presets and transitions |
| `audio_commands` | Bounded command coalescing and ordering |
| `cover` | JPEG/APIC parsing, decoder regression cases, limits and cleanup |
| `player_ui` | Production LVGL player/menu renderer, 500 rapid pointer gestures, stable action targets and heap cleanup |

The Lua player test includes 2.5 hours of simulated playback and repeated
start/stop cycles with allocation accounting. SD, PSRAM, codec-register and
audio-service adapters are explicitly simulated where hardware is required.
The LVGL test renders a real 320x240 framebuffer without SDL or a display
server. It writes `player-preview.ppm` and `volume-preview.ppm` under
`build/native/player_ui` when run through CTest.

### Check an individual Lua package

After the native build, on Windows PowerShell:

```powershell
& .\build\native\app_check.exe 'sd files/apps/hello_meow'
```

On Linux or macOS:

```sh
build/native/app_check 'sd files/apps/hello_meow'
```

For multi-configuration generators, the executable is normally in a `Debug`
subdirectory. This tool checks the manifest and executes startup/shutdown
under the declared Lua heap limit; it does not exercise input events. It has
no audio service, so use `player_app_test` for the MP3 package instead.

### Fixtures and generated fonts

Normal builds/tests use the committed files and require none of the following
generation tools:

- The MP3 fixture is a synthetic tone. Its FFmpeg command is recorded in
  [the fixture README](../test/media/fixtures/README.md).
- Synthetic JPEGs are generated by
  [generate-covers.py](../test/media/fixtures/generate-covers.py), using Python
  and Pillow. Regeneration is optional; no user artwork is required.
- Player font C files are already generated. Their headers record the command
  options for `lv_font_conv` 1.5.3, which requires Node.js when regenerating.
  The Montserrat source font, source hash and font license are referenced in
  [player_fonts.h](../src/app/app_lua/player_fonts.h).

### Reproduce a CI run

Use a fresh checkout of the exact revision, initialize the pinned submodules,
and run the native configure/build/CTest commands above in a new build
directory. Configure the CI runner with the compiler, CMake and Ninja first.
Do not reuse a CMake cache from another machine, checkout or compiler.

Preserve `build/native/Testing/Temporary/LastTest.log` and, when reviewing the
UI, the generated PPM previews as CI artifacts. Firmware validation is a
separate job using Python/PlatformIO as described above; preserve its build
log and report the final application size. Sanitizer builds with a supported
compiler are additional validation, not part of the default commands.

## Hardware validation boundary

Native tests and a successful firmware build do not establish physical audio
quality, ESP32 stack/PSRAM headroom, SD-card latency, power consumption, or a
hard real-time guarantee. Validate a candidate firmware on a device before
releasing it, including:

- Existing apps, saved XP/settings, and startup with and without an SD card.
- App installation/update/removal, rescans, malformed packages and error recovery.
- MP3 playback, rapid volume changes, pause/resume, seeking, EOF and playlist modes.
- Cover handling, EQ listening checks, repeated start/stop, long playback and
  return to other apps or USB storage mode.
- Free internal heap, largest free block and task stack reserve during use.

For an app failure, inspect the retained error screen and `/lua-last-error.txt`
on the SD card. Installation and the public app API are documented in
[LUA-APPS.md](LUA-APPS.md), [LUA-AUDIO-API.md](LUA-AUDIO-API.md) and
[MP3-PLAYER.md](MP3-PLAYER.md). Network streaming is not included in this change.
