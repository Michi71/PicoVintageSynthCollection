#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_yc.sh -- host build of the YC organ engine test.
# Renders a WAV file; no audio device, no MIDI, no Pico SDK involved.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceYC"
OUT="$HERE/yc_engine_host_test"

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -I"$ROOT/include" -I"$ROOT/effects" \
    "$HERE/yc_engine_host_test.cpp" -o "$OUT"

echo "[ok]   $OUT"
