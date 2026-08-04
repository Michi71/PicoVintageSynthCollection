// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// rd_cc_map.h - controller numbers for the PicoFaceRD panel parameters.
//
// The RD has no controller assignment of its own: the original instrument
// answers to sustain and a handful of mode messages, nothing more. The table
// below is this project's assignment, chosen so that
//
//   - the three switches keep the numbers RD_Midi already accepted, which
//     follow the General MIDI effect-depth block (92 tremolo, 93 chorus,
//     95 phaser),
//   - master tune uses 94, the GM "celeste / detune" slot,
//   - everything else comes from the undefined range 102..119, so nothing
//     collides with a standard controller.
//
// CC 7 stays out of the table on purpose: the engine already treats it as
// channel volume through its own path, and a second route to the same value
// would be ambiguous.
//
// The same table serves both directions - the panel sends through it and
// RD_Midi receives through it - so an edit made here changes send and receive
// together and they can never drift apart.

#ifndef RD_CC_MAP_H
#define RD_CC_MAP_H

#include <stdint.h>
#include "rd_params.h"

#define RD_CC_NONE 0xFFu

static const uint8_t kRdCc[RD_PARAM_COUNT] = {
    /* RD_PARAM_VOLUME         */ 108,
    /* RD_PARAM_CHORUS_ON      */  93,   // GM: chorus depth
    /* RD_PARAM_CHORUS_RATE    */ 102,
    /* RD_PARAM_CHORUS_DEPTH   */ 103,
    /* RD_PARAM_TREM_ON        */  92,   // GM: tremolo depth
    /* RD_PARAM_TREM_RATE      */ 104,
    /* RD_PARAM_TREM_DEPTH     */ 105,
    /* RD_PARAM_BASS           */ 106,
    /* RD_PARAM_TREBLE         */ 107,
    /* RD_PARAM_DAC_FILTER_ON  */ 109,
    /* RD_PARAM_PHASER_ON      */  95,   // GM: phaser depth
    /* RD_PARAM_PHASER_RATE    */ 110,
    /* RD_PARAM_PHASER_DEPTH   */ 111,
    /* RD_PARAM_VOICE_MODE     */ RD_CC_NONE,   // raw enum, not a 0..127 value
    /* RD_PARAM_MASTER_TUNE    */  94,   // GM: celeste / detune
};

// Reverse lookup: -1 when the controller is not mapped to a panel parameter.
static inline int rdParamForCc(uint8_t cc)
{
    for (int i = 0; i < RD_PARAM_COUNT; ++i) {
        if (kRdCc[i] == cc) return i;
    }
    return -1;
}

#endif // RD_CC_MAP_H
