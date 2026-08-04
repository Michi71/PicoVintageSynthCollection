#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_moog.sh -- macOS host build of the Moog engine
# Compiles the *unmodified* Pico engine (src/moog/) together with
# test/moog_test.cpp against CoreAudio + PortMidi. The Pico audio subsystem is
# switched out through -DMOOG_HOST_BUILD (see the guard in
# include/moog/moog_defs.h).
set -e

# Path setup for the collection: this script sits in tools/host_tests/<x>/,
# the engine under instruments/<INSTRUMENT>/. In the standalone repository the
# two were siblings, which is why everything below still says "$ROOT/src".
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceMD"
OUT="$HERE/moog_test"

CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -ffast-math -DMOOG_HOST_BUILD
          -I"$ROOT/include"
          -I/opt/homebrew/include)
LDFLAGS=(-L/opt/homebrew/lib -lportmidi
         -framework CoreAudio -framework AudioToolbox
         -framework CoreFoundation)

SRC=("$HERE/moog_test.cpp"
     "$ROOT/src/moog/moog.cpp"
     "$ROOT/src/moog/moog_voice.cpp"
     "$ROOT/src/moog/moog_params.cpp"
     "$ROOT/src/moog/moog_presets.cpp"
     "$ROOT/src/moog/moog_fx.cpp")

echo "[build] $CXX ${CXXFLAGS[*]} ..."
"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT" "${LDFLAGS[@]}"

echo "[ok]   $OUT"
