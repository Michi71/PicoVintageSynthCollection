#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_juno.sh -- macOS host build of the Juno engine
# Compiles the *unmodified* Pico engine (src/juno/) together with
# test/juno_test.cpp against CoreAudio + PortMidi. The Pico audio subsystem is
# switched out through -DJUNO_HOST_BUILD (see the guard in
# include/juno/juno_defs.h).
set -e

# Path setup for the collection: this script sits in tools/host_tests/<x>/,
# the engine under instruments/<INSTRUMENT>/. In the standalone repository the
# two were siblings, which is why everything below still says "$ROOT/src".
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceJ6"
OUT="$HERE/juno_test"

CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -ffast-math -DJUNO_HOST_BUILD
          -I"$ROOT/include"
          -I/opt/homebrew/include)
LDFLAGS=(-L/opt/homebrew/lib -lportmidi
         -framework CoreAudio -framework AudioToolbox
         -framework CoreFoundation)

SRC=("$HERE/juno_test.cpp"
     "$ROOT/src/juno/juno.cpp"
     "$ROOT/src/juno/juno_params.cpp"
     "$ROOT/src/juno/juno_presets.cpp"
     "$ROOT/src/juno/juno_fx.cpp")

echo "[build] $CXX ${CXXFLAGS[*]} ..."
"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT" "${LDFLAGS[@]}"

echo "[ok]   $OUT"
