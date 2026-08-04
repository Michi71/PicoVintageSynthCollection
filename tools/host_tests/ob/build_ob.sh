#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_ob.sh -- host build of the PicoFaceOB engine test.
# Regression checks (pitch bend, LFO->cutoff, presets) plus a WAV render;
# no audio device, no MIDI, no Pico SDK involved.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceOB"
OUT="$HERE/ob_engine_host_test"

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -I"$ROOT/include" \
    "$HERE/ob_engine_host_test.cpp" "$ROOT/src/OB_Engine.cpp" -o "$OUT"

echo "[ok]   $OUT"
