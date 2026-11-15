#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_mt.sh -- host build of the PicoFaceMT engine with its sample bank.
# Needs nothing but a C++17 compiler. The bank is 3 MB of PCM as source text,
# so the objects are kept in obj/ and only rebuilt when their source changed.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceMT"
CXX="${CXX:-c++}"
mkdir -p "$HERE/obj"
for src in "$ROOT"/src/data/*.cpp "$ROOT/src/mello/engine.cpp" "$HERE/mt_render.cpp"; do
    obj="$HERE/obj/$(basename "${src%.cpp}").o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$ROOT/include/mello/engine.h" -nt "$obj" ]; then
        "$CXX" -std=c++17 -O2 -Wall -Wextra -Wdouble-promotion \
            -I"$ROOT/include" -I"$ROOT/src/data" -c "$src" -o "$obj"
    fi
done
"$CXX" -o "$HERE/mt_engine_test" "$HERE"/obj/*.o
echo "[ok]   $HERE/mt_engine_test"
