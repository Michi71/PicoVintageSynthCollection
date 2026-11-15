#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_mt_ui.sh -- host build of mt_ui_shots. Needs the u8g2 archive that
# ../ui/build_ui_kit.sh leaves behind, and links the bank objects build_mt.sh
# makes, since the tape names come from the bank table.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceMT"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
LIB="$HERE/../ui/.u8g2_host"
OUT="$HERE/mt_ui_shots"
if [ ! -f "$LIB/u8g2.a" ]; then
    echo "u8g2 archive missing - run ../ui/build_ui_kit.sh first" >&2
    exit 1
fi
"$HERE/build_mt.sh" > /dev/null          # the bank objects in obj/
CXX="${CXX:-c++}"
echo "[build] mt_ui_shots"
"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -I"$ROOT/include" -I"$REPO/core/include" -I"$U8G2" -I"$REPO/tools/host_tests/ui" \
    "$HERE/mt_ui_shots.cpp" \
    $(ls "$HERE"/obj/mello_*.o) \
    "$REPO/core/src/ui/kit.cpp" "$REPO/core/src/ui/display.cpp" "$LIB/u8g2.a" \
    -o "$OUT"
echo "[ok]   $OUT"
