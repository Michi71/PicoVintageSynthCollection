#!/bin/bash
# Builds the engine-side renderer against a configured D5 build directory
# (default build-d5), which carries the generated patch table.
set -e
cd "$(dirname "$0")/../.."
BUILD=${1:-build-d5}
clang++ -std=c++17 -O2 -DD5_HOST_BUILD -Iinstruments/PicoFaceD5/include -Icore/include \
    -I"$BUILD/PicoFaceD5_rom" tools/host_tests/d5/render_note.cpp -o tools/d5_vst/render_note
echo "[ok] tools/d5_vst/render_note (pcm: $BUILD/PicoFaceD5_rom/d5_pcm.bin)"
