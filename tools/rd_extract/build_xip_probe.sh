#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_xip_probe.sh -- how much of the render is waiting for flash.
#
# Builds the engine for the host with RD_XIP_TRACE defined, which turns on the
# one hook in rd_new_engine.cpp's sample read. No firmware build defines it, so
# the hot path is untouched on the device.
#
#   ./build_xip_probe.sh [patch 0-15] [voices] [blocks]
#
# Defaults: patch 0, 12 voices (the 32 kHz base limit), 200 blocks of 64.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../.." && pwd)/instruments/PicoFaceRD"
OUT="${OUT:-$HERE/.xip_probe_build}"
mkdir -p "$OUT"

# The sample banks and packs are built from roms/ the same way the firmware
# builds them; nothing generated is in the tree any more. RDPIANO is needed
# because the descrambling lives in the reference emulator.
ROMS="$R/roms"
if [ -z "${RDPIANO:-}" ]; then
    echo "build_xip_probe: set RDPIANO to a checkout of the upstream emulator" >&2
    echo "    git clone https://github.com/Michi71/rdpiano" >&2
    exit 1
fi
RDPIANO="$RDPIANO" "$HERE/rd_make_rom.sh" "$ROMS" "$OUT/rd_rom.blob" >&2
python3 "$HERE/rd_embed_packs.py" "$ROMS" "$OUT" >&2

CXX="${CXX:-c++}"
{ "$CXX" -O2 -w -std=c++17 -DRD_XIP_TRACE \
    -I "$R/include" -I "$R/include/rd_engine" \
    -o "$OUT/rd_xip_probe" \
    "$HERE/rd_xip_probe.cpp" \
    "$R/src/rd_engine/rd_new_engine.cpp" \
    "$OUT/rd_packs_tables.cpp" \
    "$OUT/rd_packs_blob.S" \
    "$OUT/rd_rom_blob.S" \
    "$OUT/rd_rom_tables.S"; } >&2

echo "[ok]   $OUT/rd_xip_probe" >&2
exec "$OUT/rd_xip_probe" "$@"
