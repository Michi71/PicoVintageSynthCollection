// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// render.cpp - offline render of the PicoFaceCP engine, for comparing two
// sample-set builds without a sound card. One note, one voice, mono int16 at
// 44.1 kHz (the firmware rate), left channel.
//
//   render <instrument 0..5> <note> <velocity> <hold_s> <total_s> <out.raw>
//
// Built by build_render.sh against whatever headers are in
// instruments/PicoFaceCP/include; ab_compare.py reads the .raw files.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "mdaEPiano.h"

int main(int argc, char** argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: %s instr note vel hold_s total_s out.raw\n", argv[0]);
        return 1;
    }
    const int instr = atoi(argv[1]), note = atoi(argv[2]), vel = atoi(argv[3]);
    const double hold = atof(argv[4]), total = atof(argv[5]);

    mdaEPiano synth(16);
    synth.setSampleRate(44100.0f);
    synth.setProgram(0);
    synth.setInstrument(instr);

    const long nHold = (long) (hold * 44100), nTotal = (long) (total * 44100);
    std::vector<int16_t> out;
    out.reserve(nTotal + I2S_BUFFER_WORDS);
    int16_t l[I2S_BUFFER_WORDS], r[I2S_BUFFER_WORDS];
    bool on = false, off = false;
    for (long n = 0; n < nTotal; n += I2S_BUFFER_WORDS) {
        if (!on) { synth.noteOn(note, vel); on = true; }
        if (!off && n >= nHold) { synth.noteOff(note); off = true; }
        synth.process(r, l);
        for (int i = 0; i < I2S_BUFFER_WORDS; ++i) out.push_back(l[i]);
    }
    FILE* f = fopen(argv[6], "wb");
    if (!f) { perror(argv[6]); return 1; }
    fwrite(out.data(), sizeof(int16_t), out.size(), f);
    fclose(f);
    return 0;
}
