#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_yc_sysex.sh -- host build of the PicoFaceYC MIDI layer test.
# Compiles the *unmodified* src/midi_reface.cpp and the shared reface layer
# against the header-only organ engine and two stubs; no Pico SDK, no TinyUSB,
# no audio device, no MIDI port involved.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceYC"
OUT="$HERE/yc_sysex_test"

# stub/ must come first: it shadows midi_output_usb.h and tusb.h, while
# midi_reface.h, ipc.h, YC_Synth_Bridge.h and the engine headers are picked up
# from the instrument for real. DIN MIDI and pico/stdlib.h come from the
# shared shim, as in the other host tests.
CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O1 -Wall -Wextra
          -I"$HERE/stub" -I"$ROOT/include" -I"$ROOT/effects" -I"$REPO/core/include" -I"$HERE/../shim")

SRC=("$HERE/yc_sysex_test.cpp"
     "$ROOT/src/midi_reface.cpp"
     "$REPO/core/src/reface/reface_midi.cpp"
     "$HERE/../shim/host_midi_serial.cpp")

"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT"

echo "[ok]   $OUT"
"$OUT"
