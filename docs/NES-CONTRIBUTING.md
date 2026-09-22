# NES development and review

The native SD app contains the ROM browser and pause menu. The firmware owns
the Nofrendo core, display/audio services, input and cartridge SRAM storage.
The app requires NES ABI major version 1 (`nes_api=1`). Install the matching
firmware before opening the SD app. No game ROM or private signing seed is
included in this repository.

## Reproduce the host checks

Use Windows, Python 3.11+, CMake and Visual Studio C/C++ build tools. From the
repository root, this is the same Debug configuration used by `nes.yml`:

```powershell
$suites = @('nes_storage', 'nes_core', 'nes_input', 'nes_av', 'nes_path', 'nes_app', 'nes_service', 'elf_loader', 'app_sign')
foreach ($suite in $suites) {
    $configure = @()
    if ($suite -eq 'nes_core') { $configure += '-DCMAKE_C_FLAGS_DEBUG=/Zi' }
    cmake -S "test/$suite" -B "build/nes-ci/$suite" -A x64 @configure
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $suite" }
    cmake --build "build/nes-ci/$suite" --config Debug --parallel 2
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $suite" }
    ctest --test-dir "build/nes-ci/$suite" -C Debug --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $suite" }
}
python test/nes_package/nes_package_test.py --signer build/nes-ci/app_sign/signer/Debug/meowsign.exe -v
```

The core explicitly compiles with `/O2`, so its Debug configuration omits
incompatible `/RTC1` instrumentation while retaining symbols and assertions.
Other suites use the normal Debug flags.

The package tests create and discard temporary test keys. They do not need
the development signing seed. The separate existing Lua/media workflow also
builds ESP32-S3 firmware and checks the shared services.

## Build and sign

Build firmware with PlatformIO 6.1.19 (`python -m platformio run -e esp32s3box`).
On Windows, `tools/build-nes.ps1` supports an explicit Python executable and
short PlatformIO cache path. Compile the native app separately:

```powershell
python tools/build-native-app.py 'sd files/apps/nes' --toolchain <xtensa-bin> --output .build/native-nes/app.elf
```

Sign the final, stripped ELF using the procedure in
[Native app signing](../tools/app_signing/README.md), then verify it before
replacing the checked-in `app.elf` and detached signature. Recompiling changes
the signed bytes; an existing signature must never be reused for another ELF.
Keep every private seed outside the checkout and packages.

This fork preserves the upstream public key and adds a local development
public key. That additional trust applies to **all native apps** signed by
the key, not just NES. Before an upstream release, maintainers should decide
the signing policy and, if appropriate, sign the final app with their own
trusted key and remove the development trust root. CI uses public verification
and temporary test keys only; it cannot produce an official signed release.

## Validation limits

The user reported smooth graphics with the earlier `nes.1` firmware, but
intermittent sound in both supplied games. `nes.2` changes audio pacing and
buffering and moves the menus into an ELF app. Host tests and firmware builds
do not establish device audio quality or actual Xtensa ELF execution.
Remaining device checks include signed app loading and repeated exit,
ten-minute playback of both games, pause/resume, sound toggle, sleep/wake and
SRAM saving with a battery-backed cartridge. See
[current validation](NES-VALIDATION-2026-09-22.json) for recorded evidence.

Mapper availability is not universal game compatibility. Mappers 5, 23 and
24 remain experimental; Zapper input and VRC6 expansion audio are absent.
Preserve the imported component's [provenance and license notices](../lib/nes_core/PROVENANCE.md).
