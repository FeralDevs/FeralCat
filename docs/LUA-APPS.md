# Installable Lua apps (v0.8.1-lua.1)

Lua apps appear as **individual tiles in the normal app menu**, alongside the
built-in apps. The firmware runs one Lua app at a time. The additional
**Apps verwalten** (app manager) tile rescans the SD card and reports the package
inventory. The existing 18 built-in apps remain available. The upstream WIP
MeowPlayer (`app_19`, controlled by `MEOWKIT_ENABLE_PLAYER`) remains hidden by
default; it is separate from the installable Lua MP3 Player.

This extension includes a Lua host, a bounded UI API, the **Hallo Meow** example,
and a native SD-MP3 service used by the separately installable
[MP3 Player 1.1.0](MP3-PLAYER.md). Firmware `v0.8.1-lua.1` is based on upstream
`meowkit-mine` commit `dc63a6c` (v0.8.1). Network access, internet radio, and
SoundCloud are outside this release's scope.

## Install, update, or remove an app

Install firmware containing this Lua extension once. Compatible Lua packages
can then be changed without rebuilding or flashing the firmware. The firmware
version is `v0.8.1-lua.1`. A subsequent firmware update must also include the Lua
extension to retain these capabilities; an unextended upstream image does not
provide the host required by these packages.

1. Turn off the device and open the SD card on a computer, or use the existing
   USB mass-storage mode.
2. Copy the complete app folder from the repository's `sd files/apps` into
   `apps` at the root of the card. The example must have this layout:

   ```text
   /apps/hello_meow/manifest.ini
   /apps/hello_meow/main.lua
   ```

3. Safely eject the card. Start the device or leave USB mass-storage mode.
   The app inventory is refreshed at startup and after leaving USB mass-storage
   mode. Alternatively, select **Apps verwalten → Rescan SD apps**.
4. Select **Hallo Meow** in the normal app menu to run the example.

To update an app, close it first, then replace its complete folder. To uninstall
it, remove its folder and rescan. Keep the same app ID across updates. This
version has no download store, package signatures, or automatic version selection.

Use touch or joystick up/down to select an action, and A to activate it.
Left/right are delivered to the app as events. Releasing B goes back; holding B
for at least one second always returns to the normal menu. The example keeps its
counter only for the current app session.

## Package format: API 1

Each app has its own `/apps/<id>` directory and two required files:

```ini
id=hello_meow
name=Hallo Meow
version=1.0.0
api=1
entry=main.lua
capabilities=ui,system
memory_kb=128
```

| Field | Rule |
|---|---|
| `id` | 1–31 ASCII characters; starts with `a`–`z`, followed by `a`–`z`, `0`–`9`, `_`, or `-`; must exactly match the directory name |
| `name` | 1–63 UTF-8 bytes; long names are shortened on the tile |
| `version` | 1–31 printable ASCII bytes; metadata only, with no version comparison |
| `api` | Exactly `1` |
| `entry` | Exactly `main.lua` |
| `capabilities` | `ui` and `system`, each exactly once, in either order; optionally also `audio` |
| `memory_kb` | Optional, 64–256; defaults to a 256 KiB Lua heap |
| `icon` | Optional, `app` or `music`; selects an existing firmware icon, not a file path |

The manifest is limited to 2,048 bytes. It accepts UTF-8 without a BOM, LF or
CRLF line endings, blank lines, and whole comment lines beginning with `#`.
Tabs, NUL, control characters, duplicate fields, and unknown fields are rejected.
`main.lua` must be text of at most 64 KiB. Bytecode, alternative entry paths, and
path traversal are not allowed. A scan accepts at most 16 valid packages and
examines at most 64 directory entries. Accepted apps are sorted by app ID.
Beyond these limits, which packages are selected depends on directory order;
keep the package count within the limit.

The manifest and script are read again when an app starts. A stale tile cannot
launch an app that has been removed or replaced by an incompatible package.
Startup failures appear on an error page. The manager counts rejected packages
and displays the first rejection reason. Use the host-side package checker for
more detail; see [building and testing](LUA-BUILD.md).

The tile identifier is `lua:<id>`, independent of its display name or position.
Native-app XP bits are not reused for SD packages. Lua app launches receive only
the existing time-limited opening bonus; there is no separate discovery bonus
for each package.

## Lua interface

The runtime uses **Lua 5.5.1**, with 64-bit integers and double-precision numbers.
The app API version and Lua language version are separate. App code should use
the documented API.

Optional global callbacks:

```lua
function on_start() end
function on_event(kind, value) end
function on_tick(dt_ms) end
function on_stop() end
```

The script is executed once, followed by `on_start`. `on_tick` runs up to 20 times
per second, with `dt_ms` capped at 250. This is not a precise audio or real-time
clock. Every callback must return. `on_stop` is for normal cleanup; after a
runtime error, no more app code is called. The host must also release native
resources independently of app cleanup.

```lua
meow.ui.show("Title", "Text", {
    { id = "play", label = "Play" },
    { id = "close", label = "Close" }
})
```

`show` replaces the view. Limits: title 63 bytes, body text 1,023 bytes, at most
8 actions, action ID 1–31 ASCII letters/digits or `_`, `-`, `.`, and action label
63 bytes. Action IDs must be unique. The firmware copies the view; Lua receives
no LVGL objects or pointers. An action produces `on_event("action", "play")`.
Left/right produce `on_event("key", "left")` or `on_event("key", "right")`.
The host handles up/down and A/B. Text is UTF-8; device fonts do not cover every
writing system.

An optional fourth Boolean argument, `true`, selects a compact two-column grid.
Short labels and two short body lines suit this compact view. Without this
argument, `show` uses the list layout.

`meow.system.has("player_ui")` reports support for the native player UI:

```lua
meow.ui.player({title="My track", subtitle="1/12  All tracks",
    status="Playing", position=41, duration=180, playing=true, volume=60})
meow.ui.menu("Volume", "60 / 100", {
    {id="quieter", label="Quieter -5"}, {id="louder", label="Louder +5"},
    {id="back", label="Back"}
}, true)
```

`player` takes a title of at most 95 bytes, optional subtitle and status text of
at most 95 bytes each, integer times in `0..4294967295`, volume in `0..100`, and
a Boolean `playing` value. Its fixed actions are `previous`, `play`, `next`,
`library`, and `sound`. Cover art comes exclusively from the native audio
service; Lua supplies neither image data nor file paths. The player displays
the cover at 80 × 80 pixels from a 96 × 96 RGB565 source buffer.

`menu` supports at most four actions and a 127-byte description. Its other text
and ID limits match `show`. The fourth argument, `true`, selects a large 2×2 grid
with 80-pixel-high buttons; otherwise it uses four 44-pixel-high list rows. Player
transport buttons are 54 pixels high. UI objects persist across status updates,
so those updates do not move the controls. The player UI includes Latin-1 font
supplements for accented labels.

- `meow.api_version`: `1`.
- `meow.system.uptime_ms()`: milliseconds since device startup, using a 32-bit
  counter that wraps after approximately 49.7 days.
- `meow.system.has("ui")` / `has("system")`: `true`. `has("audio")` is true only
  when the manifest declares the audio capability and the host provides it.
  `network` and `storage` return `false`.
- `meow.app.exit()`: requests an orderly exit after the callback. Return from
  the callback after calling it.
- `meow.app.capture_back(true)`: delivers a short B press as
  `on_event("key", "back")`. Without it, normal host back behavior applies.
  A long B press still closes the app.

The audio interface is documented in [LUA-AUDIO-API.md](LUA-AUDIO-API.md).
Apps without `audio` in their manifest have no `meow.audio` table. A dedicated
worker executes audio commands outside the VM.

The standard library is deliberately limited. It excludes `io`, `os`, `package`,
`debug`, `coroutine`, `load`, `loadfile`, `dofile`, `require`, `pcall`, `xpcall`,
metatable access, and manual garbage collection. String pattern functions,
`dump`, `format`, `rep`, and `table.move` are also unavailable in API 1.
`tostring`, concatenation, and basic tables suffice for the example. Apps cannot
directly open files, GPIOs, network connections, or arbitrary native functions.

## Resources and limits

The Lua heap and loaded source use PSRAM. Allocation failure stops the app
instead of unexpectedly consuming internal RAM. The configured Lua heap limit
covers VM objects. Source text, limited to 64 KiB, host state, the app catalog,
and native LVGL widgets have separate bounds. Only one VM is active, and it is
closed on exit or error.

Each callback has a budget of 50,000 Lua instructions and a 20 ms wall-time limit
checked during VM execution. Audio apps receive 80 ms because measured wall
time also includes scheduling, PSRAM access, and garbage collection; their
instruction budget remains 50,000. Setup, compilation, and `on_start` receive
up to 250 ms with the instruction budget retained.

These limits are not hard real-time guarantees: native library calls, memory
management, and SD operations can take longer. The Lua host therefore exposes no
blocking audio or network calls. Expensive standard functions with known
unbounded C loops are excluded. This is a bounded app environment, not a proven
defense against every malicious input or hardware failure. Native Lua recursion
is additionally limited by `LUAI_MAXCCALLS=32`. Actual stack headroom on the
device still needs measurement.

Runtime errors remain visible on an error page. Dismissal requires at least one
second and a fresh input. After orderly audio shutdown, the host writes the last
error to `/lua-last-error.txt` when the SD card is available. It includes the
error text, app ID, runtime, and memory values to help reproduce device failures.

App SD scans read one entry per main-loop iteration and release open file
handles before USB mass-storage mode. Lua scripts retain no SD file handles;
their source is already in RAM. The audio worker keeps the MP3 file open during
playback and shuts down cooperatively before app exit or USB unmount. App
screens are deleted only after the previous LVGL screen is active again.

## Testing and future services

The native configuration under `test/lua_apps` includes 13 test suites covering
the runtime, package and media catalogs, MP3 app, audio components, cover art,
and player UI. These are PC tests; they do not establish a hardware pass. See
[LUA-BUILD.md](LUA-BUILD.md) for reproducible build and test instructions and
[MP3-PLAYER-1.1.md](MP3-PLAYER-1.1.md) for the player validation scope.

The native AudioService supports local MP3 files. SoundCloud would additionally
require a verified API/authentication route and suitable streams; Lua alone does
not supply those prerequisites. New native services require a firmware update.
Apps using available services can then be installed and updated separately.
