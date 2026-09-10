#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_ui_kit.sh -- host build of the shared UI kit, rendering to PBM.
#
# Compiles the *unmodified* core sources (core/src/ui/kit.cpp and the Display
# facade) against the real u8g2, with a null transport in place of the OLED.
# No pico-sdk needed.
#
#   ./build_ui_kit.sh && ./ui_kit_shots out && ./to_png.sh out
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
U8G2="${U8G2_PATH:-$REPO/lib/u8g2/u8g2}/csrc"
OUT="$HERE/ui_kit_shots"

if [ ! -f "$U8G2/u8g2.h" ]; then
    echo "u8g2 sources not found at $U8G2 (set U8G2_PATH)" >&2
    exit 1
fi

CXX="${CXX:-c++}"
CC="${CC:-cc}"

# u8g2 is C and large; build it once into an archive and keep it.
LIB="$HERE/.u8g2_host"
if [ ! -f "$LIB/u8g2.a" ]; then
    echo "[build] u8g2 (once)"
    mkdir -p "$LIB"
    ( cd "$LIB" && "$CC" -O2 -w -I"$U8G2" -c "$U8G2"/*.c && ar rcs u8g2.a ./*.o && rm -f ./*.o )
fi

# The About frame shows the release the tree is based on - the nearest tag,
# as a release image's splash would show it - rather than a made-up number.
# (Not the full git describe: a README sheet with "-2-gbcf0414" after the
# version would read as a stray development build.)
VERSION="$(cd "$REPO" && git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"

echo "[build] ui_kit_shots"
"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -DPICOFACE_VERSION="\"${VERSION:-unknown}\"" \
    -I"$REPO/core/include" -I"$U8G2" -I"$HERE" \
    "$HERE/ui_kit_shots.cpp" \
    "$REPO/core/src/ui/kit.cpp" \
    "$REPO/core/src/ui/display.cpp" \
    "$LIB/u8g2.a" \
    -o "$OUT"

echo "[ok]   $OUT"
