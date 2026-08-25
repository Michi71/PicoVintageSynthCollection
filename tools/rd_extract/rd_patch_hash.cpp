// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// Renders one patch through the bridge and prints a checksum of the audio, the
// sample rate and the name it reported.
//
//     rd_patch_hash <index>
//
// This exists for the 4 MB variants. PicoFaceRD normally ships both machines
// and sixteen patches; PICOFACERD_MODEL=MKS20 or MK80 ships one machine and
// eight, renumbered from zero. Nothing about the sound may change with that --
// MK80 patch 0 has to be the full build's patch 8, down to the sample. Two
// builds that slice the table differently are compared by running this over
// both and diffing, which check_variants.sh does.
//
// The stuck-voice test next door cannot answer this: it reports a tail RMS of
// zero for every healthy patch, so every patch looks alike to it.
#include "RD_Synth_Bridge_v2.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main(int argc, char** argv) {
    int instr = argc > 1 ? atoi(argv[1]) : 0;
    RD_Synth_Bridge br;
    br.init();
    br.setInstrument((uint8_t)instr);

    char name[64] = {0};
    br.instrumentName(name, sizeof(name) - 1);
    uint32_t rate = br.currentSampleRate();

    static int32_t out[128];
    const int notes[4] = {48, 60, 67, 72};
    for (int n = 0; n < 4; n++) br.noteOn((uint8_t)notes[n], 100);

    uint64_t h = 1469598103934665603ull;                  // FNV-1a over both channels
    const uint32_t total = rate * 3;                      // 2 s held, 1 s release
    for (uint32_t s = 0; s < total; s += 64) {
        int chunk = (int)((total - s) < 64 ? (total - s) : 64);
        if (s >= rate * 2 && s < rate * 2 + 64)
            for (int n = 0; n < 4; n++) br.noteOff((uint8_t)notes[n]);
        br.fill_buffer_i32(out, chunk);
        for (int k = 0; k < chunk * 2; k++) {
            uint32_t v = (uint32_t)out[k];
            for (int b = 0; b < 4; b++) { h ^= (v >> (8 * b)) & 0xFF; h *= 1099511628211ull; }
        }
    }
    printf("%016llx  %6u Hz  %s\n", (unsigned long long)h, rate, name);
    return 0;
}
