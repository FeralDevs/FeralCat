# MP3 Player from the SD card

MP3 Player **1.1.0** appears as its own tile in the normal app menu. It requires
firmware with the native player UI, provided by **v0.8.1-lua.1**, based on upstream
`meowkit-mine` commit `dc63a6c` (v0.8.1). The package in
`sd files/apps/mp3_player` controls playlists and interaction; the firmware
provides the player view, audio service, SD catalog, cover loader, and MP3 decoder.
Compatible changes to `main.lua` do not require another firmware build.

This installable app is separate from upstream's WIP MeowPlayer (`app_19`), which
remains hidden by default. The current package uses German menu labels; this
guide includes those labels where needed to identify controls.

## Prepare files

Copy the complete repository folder `sd files/apps/mp3_player` to
`/apps/mp3_player` on the SD card. Put MP3 files and existing M3U playlists under
`/mp3`:

```text
/apps/mp3_player/manifest.ini
/apps/mp3_player/main.lua
/mp3/Favorites.m3u
/mp3/Album/Track 1.mp3
/mp3/Album/Track 2.mp3
```

After copying, leave USB mass-storage mode. The next app scan discovers the
player tile; use **Apps verwalten → Rescan SD apps** if needed. Inside the player,
**Bibliothek → Weitere Optionen → Musik neu einlesen** (Library → More options →
Rescan music) refreshes the music catalog. The firmware does not simultaneously
play from the card while USB mass-storage mode owns it.

Example `/mp3/Favorites.m3u`, saved as UTF-8 text:

```m3u
#EXTM3U
Album/Track 2.mp3
/mp3/Album/Track 1.mp3
Album/Track 2.mp3
```

Relative entries resolve against the M3U file's directory. Absolute entries start
with `/mp3/`. Entry order and duplicates are preserved. M3U and M3U8 are local
playlists here; web addresses and paths outside `/mp3` are skipped. The app does
not modify music files or playlists.

## Prepare cover art

For each track, the loader tries `cover.jpg` in the track's directory, then
`folder.jpg`, then a supported embedded ID3v2.3/v2.4 APIC JPEG. For example,
`/mp3/Album/cover.jpg` applies to the tracks in that album directory.

Baseline JPEGs up to 256 KiB and 1,024 × 1,024 pixels are supported. Progressive
JPEGs and PNGs are not displayed. A JPEG already resized to about 96 × 96 pixels
requires less loading work. The loader preserves aspect ratio and adds margins
when necessary. The source buffer is 96 × 96 RGB565; the main player view displays
it at **80 × 80 pixels**. Missing or unreadable artwork produces a placeholder
without preventing music playback.

## Controls

- **Play/pause:** the large center transport button starts the selected queue,
  pauses it, or resumes it. With no selection, it loads all tracks.
- **Previous/next:** the outer transport buttons move within the current queue.
  Joystick left/right perform the same action on the main page.
- **Library (`Bibliothek`):** offers all tracks or M3U playlists. Selecting a
  track starts the complete selected list at that position. Two tracks are shown
  per page. Use the page button or left/right to browse; the last page offers
  **Erste Seite** (First page).
- **Sound → Volume (`Klang → Lautstärke`):** large quieter/louder buttons change
  digital volume in steps of five, from 0 to 100. The native amplifier level
  remains bounded. Repeated taps at a limit do not enqueue another volume command.
- **Sound → Equalizer (`Klang → Equalizer`):** offers Neutral, Voice (`Stimme`),
  Warm, and Small speaker (`Kleine Box`) over two pages. Presets use fixed band
  attenuation; arbitrary bass/treble boosts are not provided.
- **Sound → Playback options (`Klang → Wiedergabeoptionen`):** offers shuffle
  and repeat. **Spulen / Stopp** (Seek / Stop) contains ten-second seeks and stop.
- **Back (`Zurück`) or short B:** returns from a subpage without restarting
  playback. On the main page, short B only shows a reminder to hold B to exit.
- **Long B:** exits the app and stops playback. For touch exit, use
  **Bibliothek → Weitere Optionen → Player beenden** (Library → More options →
  Exit player), then confirm. **Weiter Musik hören** (Keep listening) cancels exit.

The main page shows the title, queue, playback state, progress, and cover art,
with three large transport controls and two entries for Library and Sound.
Subpages have at most four large actions. The main page has no close button.

Shuffle mixes the current queue while retaining the current track. Each queue
entry is visited once per pass, including intentional duplicate M3U entries.
Turning shuffle off restores the original list order.

**Repeat off (`Aus`)** ends playback after the last entry. **All (`Alle`)**
continues from the start of the queue. **One (`Einer`)** repeats the current track.
Manual track changes remain available in all three modes. An explicit stop does
not trigger an automatic track change.

The audio service executes playback commands asynchronously. Loading or busy
status means the requested operation has not finished. If a file is damaged or
removed, the error remains visible: retry with Play, select another track, or
rescan the card. Errors do not automatically skip through the entire queue.

## Scope and limits

| Area | Limit or behavior |
|---|---|
| Music | MP3 under `/mp3`, including up to four subdirectory levels |
| Sample rates | 22.05 / 32 / 44.1 / 48 kHz; other rates are reported as unsupported |
| Output | Built-in speaker; stereo source audio is mixed to mono |
| Music catalog | Up to 512 MP3 files and 16 playlists |
| Queue | Up to 512 entries, loaded incrementally |
| Scan | Up to 4,096 directory entries examined |
| Paths | Up to 191 UTF-8 bytes, including `/mp3` |
| M3U file | Up to 64 KiB, with at most 255 bytes per line |
| Track titles | Derived from filenames; long text is shortened |
| Cover art | 80 × 80 display from a 96 × 96 source; sidecar JPEG or supported embedded JPEG |
| Duration/seeking | MP3 duration may be estimated; seeking requires a known duration |
| App memory | At most 256 KiB Lua heap; native audio buffers have separate bounds |

Volume, equalizer, shuffle, and repeat settings are not saved to the SD card in
this version. Publishing a new music catalog discards the old queue so changed
track IDs cannot accidentally select different files. Internet radio and
SoundCloud are not part of this MP3 app.

## PC tests and device validation

`test/media/player_app_test.cpp` loads the shipped Lua source into the actual
firmware runtime. A simulated audio service delivers delayed status and checks
complete queues, M3U ordering and duplicates, track changes, EOF, repeat,
shuffle, scan generations, busy/error cases, and UTF-8 bounds. It also exercises
rapid volume taps, equalizer selection, confirmed exit, and a simulated long run
with memory accounting. Unchanged status does not redraw the view.

The native configuration in `test/lua_apps/CMakeLists.txt` contains 13 test suites.
See [build instructions](LUA-BUILD.md) and [implementation notes](MP3-PLAYER-1.1.md).
These PC tests do not replace listening and interaction tests on the MeowKit.
Speaker output, supported files, SD performance, sustained playback, seeking,
cover art, and the button layout require device validation.
