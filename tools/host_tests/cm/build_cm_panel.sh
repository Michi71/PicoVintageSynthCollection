#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs cm_panel_test: the PicoFaceCM panel against the real engine.
# Needs a CM-32L ROM set in instruments/PicoFaceCM/roms; without one it says so
# and exits successfully, like the build of the instrument itself.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceCM"
GEN="$HERE/.gen"
OUT="$HERE/cm_panel_test"

control=""; pcm=""
for f in "$ROOT"/roms/*; do
    [ -f "$f" ] || continue
    case "$(wc -c < "$f" | tr -d ' ')" in
        65536) control="$f" ;;
        1048576) pcm="$f" ;;
    esac
done
if [ -z "$control" ] || [ -z "$pcm" ]; then
    echo "[skip] cm_panel_test: no CM-32L ROM set in $ROOT/roms"
    exit 0
fi

mkdir -p "$GEN"
python3 "$REPO/tools/cm_extract/cm_make_tables.py" "$GEN/TablesData.inc"

CXX="${CXX:-c++}"
E="$ROOT/mt32emu"
echo "[build] cm_panel_test"
"$CXX" -std=c++17 -O2 -w \
    -I"$ROOT/include" -I"$E" -I"$GEN" \
    "$HERE/cm_panel_test.cpp" \
    "$ROOT/src/CM_Controller.cpp" \
    "$E/HotPath.cpp" "$E/Analog.cpp" "$E/BReverbModel.cpp" "$E/Display.cpp" "$E/File.cpp" \
    "$E/LA32FloatWaveGenerator.cpp" "$E/Part.cpp" "$E/PartialManager.cpp" "$E/Poly.cpp" \
    "$E/ROMInfo.cpp" "$E/Synth.cpp" "$E/Tables.cpp" "$E/sha1/sha1.cpp" \
    -o "$OUT"
"$OUT" "$control" "$pcm"
