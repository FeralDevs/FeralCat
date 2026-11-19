# Host verification

The test suite generates its tiny 6502 test cartridge in RAM; no proprietary
ROM is checked in. All build and output directories below are outside the
firmware checkout.

```powershell
cmake -S test/nes_core -B C:/Projekte/Meowkit/.work/nes-research-20260920/host-build -G "Visual Studio 18 2026" -A x64
cmake --build C:/Projekte/Meowkit/.work/nes-research-20260920/host-build --config Release
ctest --test-dir C:/Projekte/Meowkit/.work/nes-research-20260920/host-build -C Release --output-on-failure
```

GCC/Clang can use their usual CMake generator. For a separate ASan build add
`-DMEOW_NES_ASAN=ON`. On Windows, the matching MSVC `bin/Hostx64/x64` folder
must be on PATH so its ASan runtime DLL is found. The same option uses GCC/
Clang AddressSanitizer on other hosts.

```text
nes_core_host ROM_PATH EXISTING_OUTPUT_DIRECTORY [frames=1800] [play|idle] [sessions=1]
```

The harness reads a local ROM, runs real emulation, writes a WAV and BMP every
120 frames, and prints JSON metrics. `play` is a documented fixed input
sequence: Start at zero-based frames 180/181, 420/421 and 720/721; after frame
850 hold Right+B and hold A for the first 20 of each 90 frames. This reaches
SMB World 1-1 and Castlevania III stage 1 in the tested USA files. It is not a
general gameplay solver. `idle` supplies no input.

`sessions=2` closes and reopens the core in the same process and reruns the
same input sequence. It compares all frame CRCs and every PCM sample through
rolling CRCs, fails on a difference, and writes media only for the first run.
This exercises reset of global mapper, CPU, PPU and APU state between games.

`host_seconds_with_io` includes image/PCM generation and CRC work; it is not
a target-processor benchmark; it measures only the first session. The harness verifies the borrowed ROM buffer
remains unchanged. Runtime-generated cartridge graphics/audio stay in the
external evidence directory and must not be added to the repository.
