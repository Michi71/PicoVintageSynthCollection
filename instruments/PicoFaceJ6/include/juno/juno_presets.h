// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_presets.h -- the patch list

  A Juno-60 stores 56 patches of its own, eight per bank across seven banks.
  All 56 are here, transcribed from the chart on pages 25 to 28 of the owner's
  manual, which prints every slider and switch of every factory patch. The
  chorus column of that chart sits at the page edge and is the one part of it
  this scan cannot resolve; it is taken instead from the LED graphics of the
  Juno-60 Patch Book (Sunshine Jones, 2021), which redraws the same data
  legibly and agrees with the chart everywhere the chart can be read.

  Selecting a patch writes every parameter, so a patch is a starting point and
  never a layer: turn a control afterwards and only that control moves.
*/

#ifndef JUNO_PRESETS_H
#define JUNO_PRESETS_H

#include "juno_params.h"

#define JUNO_NPROGRAMS 56

struct JunoProgram {
    char  name[16];
    float param[JUNO_PARAM_COUNT];
};

extern const JunoProgram junoPrograms[JUNO_NPROGRAMS];

#endif /* JUNO_PRESETS_H */
