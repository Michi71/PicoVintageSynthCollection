#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_ui.sh -- macOS host build of the panel and patch-store tests
# Compiles the *unmodified* controller and store (src/J6_Controller.cpp,
# src/j6_patchstore.cpp) together with test/j6_ui_test.cpp. No audio and no
# MIDI: -DJUNO_HOST_BUILD swaps the flash path in j6_patchstore.cpp for a RAM
# bank, and the test supplies the one J6_Midi method the controller calls.
set -e

# Path setup for the collection: this script sits in tools/host_tests/<x>/,
# the engine under instruments/<INSTRUMENT>/. In the standalone repository the
# two were siblings, which is why everything below still says "$ROOT/src".
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceJ6"
OUT="$HERE/j6_ui_test"

CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -DJUNO_HOST_BUILD
          -I"$ROOT/include" -I"$REPO/core/include" -I"$HERE/../shim" -I"$HERE")

SRC=("$HERE/j6_ui_test.cpp"
     "$ROOT/src/J6_Controller.cpp"
     "$ROOT/src/j6_patchstore.cpp"
     "$ROOT/src/juno/juno.cpp"
     "$ROOT/src/juno/juno_params.cpp"
     "$ROOT/src/juno/juno_presets.cpp"
     "$ROOT/src/juno/juno_fx.cpp"
     "$HERE/../shim/host_midi_serial.cpp")

echo "[build] $CXX ${CXXFLAGS[*]} ..."
"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT"

echo "[ok]   $OUT"
