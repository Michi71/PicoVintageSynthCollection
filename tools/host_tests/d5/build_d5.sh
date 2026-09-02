#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_d5.sh -- host build of the PicoFaceD5 engine checks.
# Pins the PCM pitch law with synthetic cycles; no ROM, no audio device.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceD5"
OUT="$HERE/pcm_pitch_host_test"

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -DD5_HOST_BUILD -I"$ROOT/include" -I"$REPO/core/include" \
    "$HERE/pcm_pitch_host_test.cpp" -o "$OUT"

echo "[ok]   $OUT"
"$OUT"
