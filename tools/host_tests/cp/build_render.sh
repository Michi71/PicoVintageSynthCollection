#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_render.sh -- offline renderer for A/B comparisons of sample-set builds.
# No CoreAudio, no PortMidi: just the engine and a .raw writer.
#
#   ./build_render.sh                 builds ./render
#   ./build_render.sh out/old         ...and renders the standard note grid
#                                     (voices 1-5, notes 36..96, vel 40/100)
#                                     into that directory
#
# Typical use: render with the committed headers into out/old, swap the
# headers, render into out/new, then  python3 ab_compare.py out/old out/new
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceCP"
OUT="$HERE/render"

CXX="${CXX:-c++}"
echo "[build] $CXX -std=c++17 -O2 -DMDA_HOST_BUILD ..."
"$CXX" -std=c++17 -O2 -Wall -Wno-deprecated-declarations -DMDA_HOST_BUILD \
    -I"$ROOT/include" "$HERE/render.cpp" "$ROOT/src/mdaEPiano.cpp" -o "$OUT"
echo "[ok]   $OUT"

if [ -n "$1" ]; then
    mkdir -p "$1"
    for i in 1 2 3 4 5; do
        for n in 36 48 60 72 84 96; do
            for v in 40 100; do
                "$OUT" $i $n $v 2.0 3.0 "$1/i${i}_n${n}_v${v}.raw"
            done
        done
    done
    echo "[ok]   $(ls "$1" | wc -l | tr -d ' ') renders in $1"
fi
