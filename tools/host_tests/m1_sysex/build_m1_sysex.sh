#!/usr/bin/env bash
# Builds and runs the PicoFaceM1 SysEx test. Needs no ROMs; a factory preload
# .syx in instruments/PicoFaceM1/roms is loaded as an extra check if present.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
M1="$ROOT/instruments/PicoFaceM1"
c++ -std=c++17 -O1 -Wall -Wextra -I"$M1/include" \
    "$HERE/m1_sysex_test.cpp" "$M1/src/M1_SysEx.cpp" "$M1/src/m1_memory.cpp" \
    -o "$HERE/m1_sysex_test"
PRE=$(ls "$M1"/roms/*.syx 2>/dev/null | head -1 || true)
"$HERE/m1_sysex_test" ${PRE:+"$PRE"}
