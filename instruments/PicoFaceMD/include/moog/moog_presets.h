// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_presets.h -- the preset list

  A Model D has no memory: the sound is where the knobs are, and a patch is a
  sheet of paper. The list here is the replacement for that sheet -- a full
  panel setting per entry, in the order of moog_params.h.

  Selecting a preset writes every parameter, so a preset is a starting point
  and not a layer: turn a knob afterwards and only that knob moves.
*/

#ifndef MOOG_PRESETS_H
#define MOOG_PRESETS_H

#include "moog_params.h"

#define MOOG_NPROGRAMS 25

struct MoogProgram {
    char  name[16];
    float param[MOOG_PARAM_COUNT];
};

extern const MoogProgram moogPrograms[MOOG_NPROGRAMS];

#endif /* MOOG_PRESETS_H */
