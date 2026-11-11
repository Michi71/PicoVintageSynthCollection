#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_vs.sh -- host build of the PicoFaceVS test.
#
# Generates the engine's data the way a build without any ROM does (harmonic
# table and generating rules only; whatever is in instruments/PicoFaceVS/roms
# is deliberately not used, so the test is the same everywhere), then compiles
# the *unmodified* engine, parameter list, flash store and VS_Controller with
# the shared UI kit against the real u8g2. No pico-sdk, no audio.
#
# Needs Python 3 with numpy, and the u8g2 archive from ../ui/build_ui_kit.sh.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceVS"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
LIB="$REPO/tools/host_tests/ui/.u8g2_host"
GEN="$HERE/gen"
OUT="$HERE/vs_test"

if [ ! -f "$LIB/u8g2.a" ]; then
    echo "u8g2 host archive missing - run tools/host_tests/ui/build_ui_kit.sh first" >&2
    exit 1
fi

rm -rf "$GEN"
mkdir -p "$GEN/provs/gen" "$GEN/no_roms"
"${PYTHON:-python3}" "$REPO/tools/vs_data/gen_data.py" --no-rom \
    --out "$GEN/provs/gen" --roms "$GEN/no_roms"
# No Behringer firmware either: this only writes the header that says so.
"${PYTHON:-python3}" "$REPO/tools/vs_data/gen_pvm.py" \
    --out "$GEN/provs/gen" --roms "$GEN/no_roms" --no-auto --search "$GEN/no_roms"

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -DVS_STORE_HOST_TEST \
    -I"$ROOT/include" -I"$GEN" -I"$REPO/core/include" -I"$U8G2" \
    -I"$REPO/tools/host_tests/ui" \
    "$HERE/vs_test.cpp" \
    "$ROOT/src/VS_Controller.cpp" \
    "$ROOT/src/vs_params.cpp" \
    "$ROOT/src/vs_store.cpp" \
    "$ROOT/src/provs_engine.cpp" \
    "$REPO/core/src/ui/kit.cpp" \
    "$REPO/core/src/ui/display.cpp" \
    "$REPO/core/src/ui/list_view.cpp" \
    "$LIB/u8g2.a" \
    -o "$OUT"

echo "[ok]   $OUT"
