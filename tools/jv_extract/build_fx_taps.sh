#!/usr/bin/env bash
# build_fx_taps.sh -- build and run the effect-chip address trace.
#
# The trace needs one hook inside the emulator's eram accessors, so this script
# generates a patched COPY of pcm.cpp into a scratch directory and compiles that
# instead of the original. Nothing of the emulator is checked into this
# repository -- its licence forbids more than linking and running it, and the
# rest of this engine was built the same way. See README.md, "Credit".
#
#   JV=/path/to/jv880_juce/Source/emulator  ./build_fx_taps.sh <romdir>
#
# JV defaults to the checkout this was developed against.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
JV="${JV:-$HOME/GitHub/jv880pi/src/jvemu/src}"
OUT="${OUT:-$HERE/.fx_taps_build}"

if [ ! -f "$JV/pcm.cpp" ]; then
    echo "no emulator at $JV -- set JV to your checkout of the reference emulator" >&2
    exit 1
fi
if [ $# -lt 1 ]; then
    echo "usage: [JV=<emulator src>] $0 <romdir> [patchIndexInBankA]" >&2
    exit 1
fi

mkdir -p "$OUT"

# The hook. Two call sites, both immediately after the address is masked, so the
# value logged is exactly what the chip put on its address lines.
python3 - "$JV/pcm.cpp" "$OUT/pcm_traced.cpp" <<'PY'
import sys
src = open(sys.argv[1]).read()
decl = "extern void jv_fx_log(int rw, int addr, int tvc);\n\n"
u_old = "inline int eram_unpack(pcm_t *pcm, int addr, int type = 0)\n{\n    addr &= 0x3fff;"
u_new = (decl + u_old + "\n    jv_fx_log(0, addr, (int) pcm->tv_counter);")
p_old = "inline void eram_pack(pcm_t *pcm, int addr, int val)\n{\n    addr &= 0x3fff;"
p_new = p_old + "\n    jv_fx_log(1, addr, (int) pcm->tv_counter);"
for old, new in ((u_old, u_new), (p_old, p_new)):
    if old not in src:
        sys.exit("eram accessor not found -- the emulator has changed shape, "
                 "re-derive the hook by hand")
    src = src.replace(old, new, 1)
open(sys.argv[2], "w").write(src)
PY

# Everything the compiler says goes to stderr, filtered but never to stdout:
# stdout is the CSV, and a stray warning in the middle of it silently corrupts
# the run. The emulator builds with a few of its own warnings.
CXX="${CXX:-c++}"
{ "$CXX" -O2 -std=c++17 -I"$JV" -I"$JV/resample" -o "$OUT/jv_fx_taps" \
    "$HERE/jv_fx_taps.cpp" "$OUT/pcm_traced.cpp" \
    "$JV"/mcu.cpp "$JV"/mcu_opcodes.cpp "$JV"/mcu_interrupt.cpp \
    "$JV"/mcu_timer.cpp "$JV"/submcu.cpp "$JV"/lcd.cpp "$JV"/resample/*.c 2>&1 |
    grep -v 'treating .c. input as .c++.'; } >&2 || true

echo "[ok]   $OUT/jv_fx_taps" >&2
exec "$OUT/jv_fx_taps" "$@"
