#!/usr/bin/env bash
# Proves the 4 MB variants play what the full build plays.
#
#     check_variants.sh [<builddir>]
#
# PICOFACERD_MODEL=MKS20 and =MK80 each ship half the patches, renumbered from
# zero, so that PicoFaceRD fits a 4 MB Pico 2. The rule is that renumbering is
# the only difference: MK80 patch 0 must be the full build's patch 8, to the
# sample, at the same rate and under the same name.
#
# Needs a ROM set in instruments/PicoFaceRD/roms. No emulator.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/instruments/PicoFaceRD"
WORK="${1:-$REPO/build_variants}"
CXX="${CXX:-c++}"

for m in BOTH MKS20 MK80; do
    case $m in
        BOTH)  base=0; count=16; gen="$WORK/$m/PicoFaceRD_rom" ;;
        MKS20) base=0; count=8;  gen="$WORK/$m/PicoFaceRD_rom_$m" ;;
        MK80)  base=8; count=8;  gen="$WORK/$m/PicoFaceRD_rom_$m" ;;
    esac
    echo "== configuring $m"
    cmake -S "$REPO" -B "$WORK/$m" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceRD -DPICOFACERD_MODEL=$m >/dev/null
    "$CXX" -O2 -w -std=c++17 -I "$R/include" -I "$R/include/rd_engine" -I "$R/effects" \
        -DRD_PATCH_BASE=$base -DRD_PATCH_COUNT=$count \
        -o "$WORK/hash_$m" "$HERE/rd_patch_hash.cpp" \
        "$R/src/RD_Synth_Bridge_v2.cpp" "$R/src/rd_effects.cpp" \
        "$R/src/rd_engine/rd_new_engine.cpp" \
        "$gen/rd_packs_tables.cpp" "$gen/rd_packs_blob.S" \
        "$gen/rd_rom_blob.S" "$gen/rd_rom_tables.S"
done

fail=0
compare () {           # $1 = variant, $2 = offset into the full build
    echo "== $1 patch n  vs  BOTH patch n+$2"
    for i in 0 1 2 3 4 5 6 7; do
        a="$("$WORK/hash_$1" $i)"
        b="$("$WORK/hash_BOTH" $((i + $2)))"
        if [ "$a" = "$b" ]; then
            printf '   p%-2d ok   %s\n' "$i" "$a"
        else
            printf '   p%-2d MISMATCH\n      %s: %s\n      BOTH: %s\n' "$i" "$1" "$a" "$b"
            fail=1
        fi
    done
}
compare MKS20 0
compare MK80 8

if [ $fail -eq 0 ]; then echo "VARIANTS PASS (16 checks)"; else echo "VARIANTS FAIL"; fi
exit $fail
