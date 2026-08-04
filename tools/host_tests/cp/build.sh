#!/usr/bin/env bash
# build.sh -- macOS-Host-Build fuer den mdaEPiano-Test
# Kompiliert die *unveraenderte* Pico-Engine (src/mdaEPiano.cpp) zusammen mit
# test/test.cpp gegen CoreAudio + PortMidi. Die Pico-Audio-Subsystem wird ueber
# -DMDA_HOST_BUILD ausgeblendet (siehe Guard in include/mdaEPiano.h).
set -e

# Path setup for the collection: this script sits in tools/host_tests/<x>/,
# the engine under instruments/<INSTRUMENT>/. In the standalone repository the
# two were siblings, which is why everything below still says "$ROOT/src".
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
ROOT="$REPO/instruments/PicoFaceCP"
OUT="$HERE/mdaepiano_test"

CXX="${CXX:-c++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -DMDA_HOST_BUILD
          -I"$ROOT/include"
         
          -I/opt/homebrew/include)
LDFLAGS=(-L/opt/homebrew/lib -lportmidi
         -framework CoreAudio -framework AudioToolbox
         -framework CoreFoundation)

echo "[build] $CXX ${CXXFLAGS[*]} ..."
"$CXX" "${CXXFLAGS[@]}" \
    "$HERE/test.cpp" "$ROOT/src/mdaEPiano.cpp" \
    -o "$OUT" "${LDFLAGS[@]}"

echo "[ok]   $OUT"
