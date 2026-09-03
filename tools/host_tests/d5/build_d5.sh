#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_d5.sh -- host build of the PicoFaceD5 engine checks.
# Pins the PCM pitch law, the TVF base cutoff and the pulse width; no ROM, no audio device.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceD5"
CXX="${CXX:-c++}"
for t in pcm_pitch tvf_cutoff pulse_width; do
    OUT="$HERE/${t}_host_test"
    "$CXX" -std=c++17 -O2 -Wall -DD5_HOST_BUILD -I"$ROOT/include" -I"$REPO/core/include" \
        "$HERE/${t}_host_test.cpp" -o "$OUT"
    echo "[ok]   $OUT"
    "$OUT"
done
