#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_md_ui.sh -- host build of the PicoFaceMD panel, rendering to PBM.
#
# Compiles the *unmodified* MD_Controller and the *unmodified* shared UI kit
# against the real u8g2 with no display behind it. No pico-sdk, no audio.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceMD"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
LIB="$REPO/tools/host_tests/ui/.u8g2_host"
OUT="$HERE/md_ui_shots"

if [ ! -f "$LIB/u8g2.a" ]; then
    echo "u8g2 host archive missing - run tools/host_tests/ui/build_ui_kit.sh first" >&2
    exit 1
fi

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -DMOOG_HOST_BUILD \
    -I"$ROOT/include" -I"$REPO/core/include" -I"$U8G2" \
    -I"$REPO/tools/host_tests/ui" -I"$HERE/../shim" \
    "$HERE/md_ui_shots.cpp" \
    "$ROOT/src/MD_Controller.cpp" \
    "$ROOT/src/MD_Midi.cpp" \
    "$ROOT/src/moog/moog_params.cpp" \
    "$ROOT/src/moog/moog_presets.cpp" \
    "$REPO/core/src/ui/kit.cpp" \
    "$REPO/core/src/ui/display.cpp" \
    "$HERE/../shim/host_midi_serial.cpp" \
    "$LIB/u8g2.a" \
    -o "$OUT"

echo "[ok]   $OUT"
