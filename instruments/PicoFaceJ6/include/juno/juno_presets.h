/*
  juno_presets.h -- the patch list

  A Juno-60 stores 56 patches of its own, eight per bank across seven banks.
  The factory set is not in the service notes; these 48 come from the patch
  table of junox (GPL v3), which uses the same parameters and supplied the
  names.

  Selecting a patch writes every parameter, so a patch is a starting point and
  never a layer: turn a control afterwards and only that control moves.
*/

#ifndef JUNO_PRESETS_H
#define JUNO_PRESETS_H

#include "juno_params.h"

#define JUNO_NPROGRAMS 48

struct JunoProgram {
    char  name[16];
    float param[JUNO_PARAM_COUNT];
};

extern const JunoProgram junoPrograms[JUNO_NPROGRAMS];

#endif /* JUNO_PRESETS_H */
