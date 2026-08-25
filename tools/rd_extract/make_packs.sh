#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# Captures the sixteen descriptor packs PicoFaceRD plays, from a local ROM set.
#
#   RDPIANO=/path/to/rdpiano RDPIANO_REF=/path/to/librdpiano \
#       make_packs.sh <romdir> <outdir> [patch ...]
#
# The packs are the one input to a PicoFaceRD build that cannot be downloaded:
# they are derived from Roland's ROMs, so they are not in this repository and
# not in any release. This is how they are made.
#
# For each patch the reference emulator plays all 88 keys at four velocities
# while a hook records every register write the original firmware makes to the
# sound chip; the analyzer distils those into per-note descriptors and the
# packer writes the binary. 352 entries a patch.
#
# It takes a while -- the emulator plays about six seconds of audio per note,
# 5632 notes in all. Name patches on the command line to do a few at a time;
# with none it does all sixteen.
#
# RDPIANO_REF is the *adapted* emulator, the one whose Mcu takes a model index
# and carries the model tables (Michi71/librdpiano). The capture drives it
# through loadPatch, which the upstream library does not have.

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "usage: RDPIANO=... RDPIANO_REF=... $0 <romdir> <outdir> [patch ...]" >&2
    exit 1
fi
romdir=$1; outdir=$2; shift 2
patches=("$@")
if [ ${#patches[@]} -eq 0 ]; then
    patches=(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
fi

REF="${RDPIANO_REF:-${RDPIANO:-}}"
if [ -z "$REF" ]; then
    echo "$0: set RDPIANO_REF to a checkout of the adapted emulator" >&2
    echo "    git clone https://github.com/Michi71/librdpiano" >&2
    exit 1
fi
RD=""
for cand in "$REF/rdpiano" "$REF/librdpiano" "$REF"; do
    if [ -f "$cand/src/mcu.cpp" ] && [ -f "$cand/src/mks20a_tables.cpp" ]; then RD="$cand"; break; fi
done
if [ -z "$RD" ]; then
    echo "$0: $REF has no adapted emulator (src/mcu.cpp plus the model tables)" >&2
    exit 1
fi

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$outdir" "$work/include" "$work/src"

# Only what the capture needs. That checkout also carries a desktop wrapper
# with JUCE and libresample behind it, which has nothing to do with this.
# The adapted emulator reads its samples from the interleaved banks, which are
# ROM-derived and therefore in neither repository. Build them here, the same way
# the firmware builds its packed ones -- that needs the upstream descrambler.
if [ -z "${RDPIANO:-}" ]; then
    echo "$0: set RDPIANO to a checkout of the upstream emulator as well" >&2
    echo "    git clone https://github.com/Michi71/rdpiano" >&2
    exit 1
fi
echo "make_packs: building the sample banks"
RDPIANO="$RDPIANO" "$here/rd_make_rom.sh" "$romdir" "$work/rd_rom.blob" "$work/rd_ilv.bin" >/dev/null

# A stale checkout is the trap here: it builds, it runs, and it quietly
# produces different packs. The instrument's own copy of this emulator had
# cached part state that the published fork does not; capturing without it
# gives a pack of a different size and an instrument that sounds different
# from the one every measurement in this repository was taken against.
if ! grep -q 'wave_loop_inv' "$RD/src/sound_chip.cpp"; then
    echo "" >&2
    echo "make_packs: WARNING -- $RD looks like the older reference emulator." >&2
    echo "    The packs it captures will NOT match the ones PicoFaceRD was" >&2
    echo "    validated against, and the instrument will sound different." >&2
    echo "    See instruments/PicoFaceRD/README.md, 'Where the packs come from'." >&2
    echo "" >&2
fi

cp "$RD"/include/{mcu,mcu_ops,sound_chip,mame_utils,rom_tables}.h "$work/include/"
cp "$here/rd_capture.h" "$work/include/"
for f in mcu sound_chip mks20a_tables mks20b_tables mk80_tables program_tables; do
    cp "$RD/src/$f.cpp" "$work/src/"
done

# The hook, injected rather than vendored: two globals at the top of the sound
# chip and one push_back at the end of its register write. Everything else in
# that checkout is untouched.
python3 - "$work/src/sound_chip.cpp" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p).read()
if 'g_rd_capture' in s:
    sys.exit(0)                       # already hooked, nothing to do
head = s.index('\n', s.index('#include'))
s = (s[:head] + '\n#include "rd_capture.h"\n'
     'std::vector<RdCaptureEvent>* g_rd_capture       = nullptr;\n'
     'uint64_t                     g_rd_capture_clock = 0;\n' + s[head:])

m = re.search(r'\nvoid SoundChip::write\([^)]*\)\s*\{', s)
if not m:
    sys.stderr.write('make_packs: no SoundChip::write to hook\n'); sys.exit(2)
# Walk to the closing brace of that function and insert before it.
depth, i = 0, m.end() - 1
while True:
    if s[i] == '{': depth += 1
    elif s[i] == '}':
        depth -= 1
        if depth == 0: break
    i += 1
s = (s[:i] + '    if (g_rd_capture)\n'
     '        g_rd_capture->push_back({g_rd_capture_clock, voiceI, partI, field, data});\n' + s[i:])
open(p, 'w').write(s)
PY

echo "make_packs: building the capture against $RD"
c++ -O2 -w -std=c++17 -I "$work/include" -o "$work/rd_extract" \
    "$here/rd_extract.cpp" "$work"/src/*.cpp "$work/rd_ilv.S"

# Which sample bank each patch plays; the emulator's own patchToRomSet.
banks=(0 0 0 1 1 1 1 1 2 2 2 2 2 2 2 2)
notes=$(seq -s, 21 108)

for patch in "${patches[@]}"; do
    jsonl="$work/p${patch}.jsonl"
    rm -f "$jsonl"
    for vel in 40 80 110 127; do
        echo "make_packs: patch $patch, velocity $vel"
        "$work/rd_extract" "$patch" "$jsonl" "$notes" "$vel" >/dev/null
    done
    python3 "$here/rd_analyze.py" "$jsonl" "$work/p${patch}.json"
    python3 "$here/rd_pack.py" "$work/p${patch}.json" \
            "$outdir/pack_p${patch}.rdp" "${banks[$patch]}"
    echo "make_packs: $outdir/pack_p${patch}.rdp"
done
