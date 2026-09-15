#!/usr/bin/env bash
# Build the meowsign host tool against the vendored Monocypher source.
# Output: tools/app_signing/meowsign
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MC="$HERE/../../lib/monocypher/src"
cc -O2 -std=c99 -Wall -Wextra \
   -I"$MC" \
   "$HERE/meowsign.c" "$MC/monocypher.c" "$MC/monocypher-ed25519.c" \
   -o "$HERE/meowsign"
echo "built: $HERE/meowsign"
