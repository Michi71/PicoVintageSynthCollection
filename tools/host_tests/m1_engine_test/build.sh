#!/usr/bin/env bash
# Builds m1_render against a configured ROM conversion.
#   build.sh [<gendir>]   default: build/PicoFaceM1_rom (the cmake configure step)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="${1:-$ROOT/build/PicoFaceM1_rom}"
[ -f "$GEN/m1_rom_tables.h" ] || { echo "no $GEN/m1_rom_tables.h - configure with the M1 ROMs first"; exit 1; }
c++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/instruments/PicoFaceM1/include" -I"$GEN" \
    "$HERE/m1_render.cpp" \
    "$ROOT/instruments/PicoFaceM1/src/m1_engine/m1_engine.cpp" \
    "$ROOT/instruments/PicoFaceM1/src/m1_engine/m1_fx.cpp" \
    -o "$HERE/m1_render"
echo "built $HERE/m1_render"
