#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# build_render.sh -- the engine side of the Juno-60 VST comparison.
# Compiles the unmodified src/juno/ sources, no audio device and no PortMidi.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceJ6"
CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -ffast-math -DJUNO_HOST_BUILD -I"$ROOT/include" \
    "$HERE/render_note.cpp" \
    "$ROOT/src/juno/juno.cpp" "$ROOT/src/juno/juno_params.cpp" \
    "$ROOT/src/juno/juno_presets.cpp" "$ROOT/src/juno/juno_fx.cpp" \
    -o "$HERE/render_note"
echo "[ok]   $HERE/render_note"
