#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_dx_sysex.sh -- host build of the PicoFaceDX SysEx round trip.
# Compiles the *unmodified* src/midi_reface.cpp against the stubs in stub/;
# no Pico SDK, no TinyUSB, no audio device, no MIDI port involved.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceDX"
OUT="$HERE/dx_sysex_test"

# stub/ must come first: it shadows DX_Synth_Bridge.h, presets.h,
# midi_output_usb.h and tusb.h, while midi_reface.h, ipc.h and
# dx_engine/RDX_Types.h are picked up from the instrument for real. DIN MIDI
# comes from the shared shim, as in the other host tests.
CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O1 -Wall -Wextra
          -I"$HERE/stub" -I"$ROOT/include" -I"$REPO/core/include" -I"$HERE/../shim")

SRC=("$HERE/dx_sysex_test.cpp"
     "$ROOT/src/midi_reface.cpp"
     "$HERE/../shim/host_midi_serial.cpp")

"$CXX" "${CXXFLAGS[@]}" "${SRC[@]}" -o "$OUT"

echo "[ok]   $OUT"
"$OUT"
