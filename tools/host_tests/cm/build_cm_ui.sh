#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds cm_ui_shots: the PicoFaceCM panel rendered to PBM frames, without a ROM set.
# Needs the u8g2 archive that ../ui/build_ui_kit.sh builds.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceCM"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
LIB="$HERE/../ui/.u8g2_host"
OUT="$HERE/cm_ui_shots"

if [ ! -f "$LIB/u8g2.a" ]; then
    echo "u8g2 archive missing - run ../ui/build_ui_kit.sh first" >&2
    exit 1
fi

CXX="${CXX:-c++}"

echo "[build] cm_ui_shots"
"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -I"$ROOT/include" -I"$REPO/core/include" -I"$U8G2" \
    -I"$REPO/tools/host_tests/ui" \
    "$HERE/cm_ui_shots.cpp" \
    "$ROOT/src/CM_Controller.cpp" \
    "$REPO/core/src/ui/kit.cpp" \
    "$REPO/core/src/ui/display.cpp" \
    "$LIB/u8g2.a" \
    -o "$OUT"
echo "[ok]   $OUT"
