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

CXX="${CXX:-c++}"
{ "$CXX" -O2 -std=c++17 -DRD_XIP_TRACE \
    -I "$R/include" -I "$R/include/rd_engine" \
    -o "$OUT/rd_xip_probe" \
    "$HERE/rd_xip_probe.cpp" \
    "$R/src/rd_engine/rd_new_engine.cpp" \
    "$R/src/rd_engine/rd_packs_data.cpp" \
    "$R/src/rd_engine/program_tables.cpp" \
    "$R/src/rd_engine/rd_samples_pk4_a.cpp" \
    "$R/src/rd_engine/rd_samples_pk4_b.cpp" \
    "$R/src/rd_engine/rd_samples_pk4_m.cpp"; } >&2

echo "[ok]   $OUT/rd_xip_probe" >&2
exec "$OUT/rd_xip_probe" "$@"
