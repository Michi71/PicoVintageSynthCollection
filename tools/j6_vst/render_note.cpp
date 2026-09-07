// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  render_note.cpp -- the J6 engine's side of the VST comparison

  Renders one note through the unmodified Juno engine and writes interleaved
  stereo float32 at the engine's own 44.1 kHz, which is also the rate the
  Roland plugin renders at -- so neither side is resampled.

      render_note out.f32 <idx> <note> <vel> <hold> <tail> [dry]
      render_note out.f32 --params <p0,p1,...,p28> <note> <vel> <hold> <tail>
      render_note dumpbank bank.txt

  "dry" turns the chorus off on this side; compare.py does the same to the
  plugin. dumpbank writes the 56 factory patches as one line of 29 normalised
  parameters each, which is what the Python side sets on the plugin -- the two
  sides then play the same panel rather than the same patch number.

  The master volume is an instrument setting and not part of a patch, so it is
  written explicitly here: a comparison must not depend on where a power-on
  default happens to sit.
*/

#include "juno/juno.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc >= 3 && !strcmp(argv[1], "dumpbank")) {
        FILE* f = fopen(argv[2], "w");
        if (!f) { perror(argv[2]); return 1; }
        for (int i = 0; i < JUNO_NPROGRAMS; ++i) {
            fprintf(f, "%d\t%s", i, junoPrograms[i].name);
            for (int k = 0; k < JUNO_PARAM_COUNT; ++k)
                fprintf(f, "\t%.6f", junoPrograms[i].param[k]);
            fputc('\n', f);
        }
        fclose(f);
        return 0;
    }

    if (argc < 7) {
        fprintf(stderr, "usage: render_note out.f32 <idx|--params list> <note> <vel> <hold> <tail> [dry]\n"
                        "       render_note dumpbank bank.txt\n");
        return 1;
    }

    const char* out   = argv[1];
    const bool  byPar = !strcmp(argv[2], "--params");
    int         argi  = byPar ? 4 : 3;

    Juno juno;
    juno.setSampleRate(44100.0f);

    if (byPar) {
        std::vector<float> p;
        for (const char* s = argv[3]; *s; ) {
            p.push_back(strtof(s, (char**) &s));
            if (*s == ',') ++s;
        }
        if ((int) p.size() != JUNO_PARAM_COUNT) {
            fprintf(stderr, "--params wants %d values, got %zu\n", JUNO_PARAM_COUNT, p.size());
            return 1;
        }
        for (int k = 0; k < JUNO_PARAM_COUNT; ++k) juno.setParameter(k, p[k]);
    } else {
        juno.setProgram(atoi(argv[2]));
    }

    const int   note = atoi(argv[argi + 0]);
    const int   vel  = atoi(argv[argi + 1]);
    const float hold = strtof(argv[argi + 2], nullptr);
    const float tail = strtof(argv[argi + 3], nullptr);
    const bool  dry  = (argc > argi + 4) && !strcmp(argv[argi + 4], "dry");

    if (dry) juno.setParameter(JUNO_CHORUS, 0.0f);

    /* Instrument settings: full master, no arpeggio, nothing latched. The
     * plugin is set the same way. */
    juno.setParameter(JUNO_MASTER, 1.0f);
    juno.setParameter(JUNO_ARP_ON, 0.0f);
    juno.setParameter(JUNO_HOLD,   0.0f);

    const int sr = 44100;
    const int nh = (int) (hold * sr), nt = (int) (tail * sr);
    std::vector<float> l(nh + nt), r(nh + nt);

    juno.noteOn(note, vel);
    juno.processFloat(l.data(), r.data(), nh);
    juno.noteOff(note);
    juno.processFloat(l.data() + nh, r.data() + nh, nt);

    FILE* f = fopen(out, "wb");
    if (!f) { perror(out); return 1; }
    for (size_t i = 0; i < l.size(); ++i) { fwrite(&l[i], 4, 1, f); fwrite(&r[i], 4, 1, f); }
    fclose(f);
    return 0;
}
