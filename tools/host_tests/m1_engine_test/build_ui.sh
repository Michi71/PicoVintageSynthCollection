#!/usr/bin/env bash
# Renders the PicoFaceM1 panel to PBM (out/ui). Needs the u8g2 host archive
# (tools/host_tests/ui/build_ui_kit.sh) and a configured ROM conversion.
#   build_ui.sh [<gendir>]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
GEN="${1:-$REPO/build/PicoFaceM1_rom}"
M1="$REPO/instruments/PicoFaceM1"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
LIB="$REPO/tools/host_tests/ui/.u8g2_host"
[ -f "$LIB/u8g2.a" ] || "$REPO/tools/host_tests/ui/build_ui_kit.sh" >/dev/null
c++ -std=c++17 -O1 -Wall -Wextra -I"$M1/include" -I"$GEN" -I"$REPO/core/include" -I"$U8G2" \
    -I"$REPO/tools/host_tests/ui" -I"$REPO/tools/host_tests/shim" \
    "$HERE/m1_ui_shots.cpp" "$M1/src/M1_Bridge.cpp" "$M1/src/M1_Controller.cpp" "$M1/src/M1_SysEx.cpp" \
    "$M1/src/m1_memory.cpp" "$M1/src/m1_params.cpp" "$M1/src/m1_engine/m1_engine.cpp" "$M1/src/m1_engine/m1_fx.cpp" \
    "$REPO/core/src/ui/kit.cpp" "$REPO/core/src/ui/display.cpp" "$REPO/core/src/ui/list_view.cpp" \
    "$LIB/u8g2.a" -o "$HERE/m1_ui_shots"
mkdir -p "$HERE/out/ui"
"$HERE/m1_ui_shots" "$HERE/out/ui"
"$REPO/tools/host_tests/ui/to_png.sh" "$HERE/out/ui" >/dev/null 2>&1 || true
