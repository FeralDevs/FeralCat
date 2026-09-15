#!/usr/bin/env bash
# Build a native MeowKit ELF app from <dir>/app_main.c -> <dir>/app.elf,
# using the xtensa toolchain in the meowkit-pio Docker image.
#
# Usage:  sd\ files/apps/build_app.sh <app-dir>
# Example: "sd files/apps/build_app.sh" "sd files/apps/hello_ns"
#
# Loader requirements (see lib/elf_loader, meowkit-elf-loader memory):
#  - PIC shared object, entry = app_main
#  - post-strip the sections the loader neither needs nor tolerates
set -euo pipefail
DIR="${1:?usage: build_app.sh <app-dir>}"
DIR="$(cd "$DIR" && pwd)"
[ -f "$DIR/app_main.c" ] || { echo "no app_main.c in $DIR"; exit 1; }

# The native-app ABI header (mk_app_abi.h) lives with the firmware; mount it
# read-only so apps can #include it without copying.
SDK="$(cd "$(dirname "$0")/../../src/system" && pwd)"

DOCKER=/Applications/Docker.app/Contents/Resources/bin/docker
"$DOCKER" run --rm -v meowkit-pio:/root/.platformio -v "$DIR":/app -v "$SDK":/sdk:ro --entrypoint bash \
  meowkit-pio:local -lc '
    set -e
    BIN=/root/.platformio/packages/toolchain-xtensa-esp32s3/bin
    GCC=$BIN/xtensa-esp32s3-elf-gcc
    STRIP=$BIN/xtensa-esp32s3-elf-strip
    # -O0 + -fno-merge-constants: the elf_loader section loader captures .rodata
    # by name only and does not handle SHF_MERGE|SHF_STRINGS merged-string
    # rodata (ES=1), which -O1+ produces and which faults at run time. -O0 also
    # avoids jump tables (they need extra relocation the loader lacks). This is
    # the proven-working recipe. See meowkit-elf-loader memory / tools README.
    $GCC -mlongcalls -nostartfiles -nostdlib -fPIC -shared -e app_main \
         -fdata-sections -ffunction-sections -Wl,--gc-sections \
         -fvisibility=hidden -O0 -fno-merge-constants -I/sdk \
         -o /app/app.elf /app/app_main.c
    $STRIP --strip-unneeded \
      --remove-section=.comment --remove-section=.got.loc \
      --remove-section=.dynamic --remove-section=.xt.lit \
      --remove-section=.xt.prop --remove-section=.xtensa.info \
      /app/app.elf
    echo "built /app/app.elf ($(stat -c%s /app/app.elf) bytes)"
  '
echo "-> $DIR/app.elf"
