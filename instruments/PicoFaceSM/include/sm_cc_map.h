// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// sm_cc_map.h - controller numbers for the PicoFaceSM panel parameters.
//
// The Solina has no controller assignment of its own: the original is a purely
// electromechanical instrument, and the engine here answers to five standard
// controllers only. The table below is this project's assignment:
//
//   - where a standard sound controller genuinely matches the function it is
//     used: 72 release for the sustain circuit's decay, 73 attack for the
//     crescendo, 74 brightness for the tone low-pass,
//   - the modulation sections take the General MIDI effect-depth slots that
//     match them: 92 tremolo, 93 chorus (the ensemble IS a chorus), 94 detune
//     for master tune, 95 phaser,
//   - the six register switches take a contiguous block at 102..107, the way
//     the reface YC maps its drawbars,
//   - everything else comes from the undefined range, so nothing collides with
//     a standard controller.
//
// CC 7 stays out of the table on purpose: the engine already treats it as
// channel volume through its own path, and a second route to the same value
// would be ambiguous. The same applies to 64, 120, 121 and 123.
//
// The same table serves both directions - the panel sends through it and
// SM_Midi receives through it - so send and receive can never drift apart.

#ifndef SM_CC_MAP_H
#define SM_CC_MAP_H

#include <stdint.h>
#include "solina/solina.h"

#define SM_CC_NONE 0xFFu

static const uint8_t kSolinaCc[SOLINA_PARAM_COUNT] = {
    /* SOLINA_CONTRABASS     */ 102,   // 16' bass register
    /* SOLINA_CELLO          */ 103,   //  8' bass register
    /* SOLINA_VIOLA          */ 104,   //  8' gate register
    /* SOLINA_VIOLIN         */ 105,   //  4' gate register
    /* SOLINA_TRUMPET        */ 106,   //  8' formant register
    /* SOLINA_HORN           */ 107,   //  4' formant register
    /* SOLINA_BASS_VOLUME    */ 108,
    /* SOLINA_CRESCENDO      */  73,   // standard: attack time
    /* SOLINA_SUSTAIN        */  72,   // standard: release time
    /* SOLINA_VOLUME         */ 109,
    /* SOLINA_TUNE           */  94,   // GM: celeste / detune
    /* SOLINA_ENSEMBLE       */  93,   // GM: chorus depth
    /* SOLINA_TREMOLO_RATE   */ 110,
    /* SOLINA_TREMOLO_DEPTH  */  92,   // GM: tremolo depth
    /* SOLINA_CHORUS_RATE    */ 111,
    /* SOLINA_CHORUS_DEPTH   */ 112,
    /* SOLINA_ENSEMBLE_TONE  */ 113,
    /* SOLINA_ENSEMBLE_WIDTH */ 114,
    /* SOLINA_PHASER         */  95,   // GM: phaser depth
    /* SOLINA_PHASER_RATE    */ 115,
    /* SOLINA_PHASER_COLOR   */ 116,
    /* SOLINA_TONE_LOWPASS   */  74,   // standard: brightness
    /* SOLINA_TONE_HIGHPASS  */ 117,
    /* SOLINA_TONE_SHELF     */ 118,
    /* SOLINA_FORMANT        */ 119,
    /* SOLINA_SHAPER         */   3,
};

// Reverse lookup: -1 when the controller is not mapped to a panel parameter.
static inline int solinaParamForCc(uint8_t cc)
{
    for (int i = 0; i < SOLINA_PARAM_COUNT; ++i) {
        if (kSolinaCc[i] == cc) return i;
    }
    return -1;
}

#endif // SM_CC_MAP_H
