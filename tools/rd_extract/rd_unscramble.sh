#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
#   RDPIANO=/path/to/rdpiano rd_unscramble.sh <romdir> <progrom> <paramsrom> \
#                                             <ic5> <out-program> <out-params>
#
# Descrambles a program and parameter ROM pair for reading. See
# tools/rd_extract/RD_FIRMWARE.md, and rd_make_rom.sh for the same arrangement
# applied to the sample ROMs.

set -euo pipefail
if [ $# -ne 6 ]; then
    echo "usage: RDPIANO=... $0 <romdir> <progrom> <paramsrom> <ic5> <out-program> <out-params>" >&2
    exit 1
fi
if [ -z "${RDPIANO:-}" ]; then
    echo "$0: set RDPIANO to a checkout of the upstream emulator" >&2
    echo "    git clone https://github.com/Michi71/rdpiano" >&2
    exit 1
fi
for cand in "$RDPIANO/librdpiano" "$RDPIANO/rdpiano" "$RDPIANO"; do
    if [ -f "$cand/src/mcu.cpp" ] && [ -f "$cand/include/mcu.h" ]; then upstream=$cand; break; fi
done
if [ -z "${upstream:-}" ]; then echo "$0: no src/mcu.cpp under $RDPIANO" >&2; exit 1; fi
if ! grep -q 'SoundChip(const u8 \*' "$upstream/include/sound_chip.h"; then
    echo "$0: $upstream is the adapted emulator; this needs the upstream one." >&2
    exit 1
fi

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
mkdir -p "$work/include" "$work/src"
sed 's/^private:/public:/' "$upstream/include/mcu.h" > "$work/include/mcu.h"
cp "$upstream/include/"{sound_chip.h,mame_utils.h,mcu_ops.h} "$work/include/"
cp "$upstream/src/"{mcu.cpp,sound_chip.cpp} "$work/src/"

c++ -O2 -w -std=c++17 -I "$work/include" -o "$work/rd_unscramble" \
    "$here/rd_unscramble.cpp" "$work"/src/*.cpp
"$work/rd_unscramble" "$@"
