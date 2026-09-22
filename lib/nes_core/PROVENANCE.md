# MeowKit NES core provenance

This is a local prototype adaptation of the Nofrendo component from
[ducalex/retro-go](https://github.com/ducalex/retro-go/tree/4ced120669750ca7228fd0414211430c1d923166/retro-core/components/nofrendo),
commit **4ced120669750ca7228fd0414211430c1d923166**. Files were copied from that
checkout, without ROMs, Retro-Go launcher, device drivers or build system.
`UPSTREAM_MANIFEST.json` records original and vendored SHA-256 for each file.

Upstream `COPYING` and `CREDITS` are retained verbatim in `vendor/`. COPYING is
GPL version 2; historical source headers refer to GNU Library GPL version 2.
Those differing upstream statements are preserved, not silently rewritten.
Distribution must include the applicable source and notices and resolve the
file-level license history. This prototype is not an external release.

The wrapper exposes one sequential session through `meow_nes.h`; it borrows
the caller's immutable ROM buffer until close and owns only core state/audio
and video buffers. Allocation/free/log callbacks keep the core independent
of Arduino, FreeRTOS, filesystems and the display/audio drivers. Callbacks must
not reenter the API. The caller supplies ROM identity and save-container
integrity outside the raw Battery-RAM import/export functions.

## Included mapper scope

Registry: **0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 23, 24, 66**. Other upstream mapper
implementations are deliberately not linked yet. Presence is a capability,
not a claim that every game using a mapper works. Mapper 5, 23 and 24 are
reported experimental. Mapper 24 has no VRC6 expansion audio. Mapper 5 retains
upstream incomplete modes and rendering; full save states are not exposed.
The source for mapper 4 contains some additional interface definitions, but
they are not in this port's allowlist or registry.

iNES cartridge files up to 2 MiB and up to 64 KiB PRG-RAM are accepted after
bounds checks. NES 2.0, VS/PlayChoice, FDS, NSF and unknown mappers are rejected.
The exact old DiskDude signature is supported; arbitrary dirty headers are
not guessed. Trailing bytes are excluded from the emulated payload and ROM
database CRC. A recognized CRC can supply region/mapper/mirroring from the
upstream database. No source ROM is rewritten.

## Local changes

- `meow_nes.[ch]` and `meow_nes_port.h`: checked C API, allocator/log bridge,
  real CRC32, video/palette/PCM view, reset/lifecycle and raw Battery-RAM.
- `vendor/nes/utils.h`: standard variadic logging macros and allocation hooks;
  replace GNU-only MIN/MAX (current call sites have no side effects).
- `vendor/config.h`, `vendor/nes/cpu.c`: MSVC C host portability for attributes,
  variadic opcode macros and inlined fast-read functions; zero-page indirect
  word reads correctly wrap from $FF to $00. IRAM placement is not imposed by this port; it
  must follow target link-map and performance measurements.
- `vendor/mappers/mappers.h`: bounded initial mapper registry.
- `vendor/nes/mem.c`, `apu.c`: allocation failure returns, initialized dummy
  memory; 16-bit word reads wrap from $FFFF to $0000. Existing pointer
  rebasing in the CPU/PPU is inherited from upstream.
- `vendor/nes/rom.c`: truncated ROM rejection restored, iNES PRG-RAM count
  implemented with exact DiskDude handling, new RAM zero initialized.
- `vendor/nes/nes.c`: idempotent teardown, BIOS pointer cleanup, Battery-RAM
  survives hard reset, trainer is copied to its $7000 RAM location.
- `vendor/nes/input.c`: held-A strobe reads, snapshot latch on falling edge,
  bounded shift/read count (avoids undefined shifts after repeated reads),
  and clean reset of input state.
- `vendor/mappers/map005.c`: reset all mutable session state; no claim of
  complete MMC5 behavior. Existing absent CHR modes, disabled extended
  rendering and missing save-state functions remain explicit limitations.
- `vendor/mappers/map023.c`: reset CHR bank half-registers to the initial
  linear mapping so bank values cannot leak across sessions or resets.

Audio is mono signed 16-bit at 44,100 or 48,000 Hz. Those rates divide exactly
by this core's integer 60/50 Hz timing. Other rates are rejected so that a
truncated samples-per-frame division cannot quietly accumulate drift. The
returned indexed frame is 256x240 with pitch 272; `pixels` already points
past the left eight padding bytes. Palette entries are host-endian RGB565.

## Initial host evidence, 20 September 2026

MSVC 19.51 x64 Release and AddressSanitizer builds complete. The contract
test covers CRC, malformed inputs, actual CPU stepping (including zero-page
and address-bus wrapping), input serialization,
allocation-failure cleanup, twenty sequential sessions, 64 KiB Battery-RAM,
mapper 23 half-register reset, reset and the known legacy header. No ROM
binary is part of these tests.

Two separately supplied clean ROMs were executed locally:

- Castlevania III - Dracula's Curse (USA), mapper 5: 3,600 frames with ASan,
  2,646,000 PCM samples. Inspected snapshots show name entry and actual
  stage 1-1 / stage 1-2 gameplay; player position and timer advance. One
  upstream "Bogus PRG mapping" warning occurs while the game initially
  switches MMC5 modes; it is retained in the evidence log.
- Super Mario Bros. + Duck Hunt (USA), mapper 66: 1,800 frames, 1,323,000 PCM
  samples; inspected snapshot shows SMB World 1-1 and Mario. Duck Hunt is
  not validated and lightgun input is not implemented.

Both games were rerun after the final CPU corrections, each in two consecutive
sessions in the same AddressSanitizer process. Rolling CRCs of every video
frame and all PCM samples match between sessions; no ASan errors occurred.
Both input buffers retained their initial CRC after stepping. Nonzero PCM
and visible gameplay are an initial smoke test, not an audio-quality verdict,
complete game compatibility result or device-speed measurement. No MeowKit
was flashed and no hardware test was performed by this component work.
Evidence remains outside the source tree under
`C:\Projekte\Meowkit\.work\nes-research-20260920\nofrendo-*-host` and
`nofrendo-cv3-asan`. Final repeat-session regression evidence is in
`nofrendo-cv3-final` and `nofrendo-smbdh-final`, with BMP frames, WAV PCM,
logs and JSON metrics.
