// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_params.h -- the front panel as a table

  Every control of the Model D is one entry here, in the order of the panel:
  Controllers, Oscillator Bank, Mixer, Modifiers, Output. After those come the
  handful of settings that are component values or trimmers in the original
  and have no knob of their own.

  All parameters are normalised to 0..1, which is what the IPC ring carries
  (as per mille) and what the preset table stores. The descriptor says how a
  value is to be read: a continuous control, a switch, or a rotary switch with
  named positions.

  The MIDI controller number lives here as well, so that the firmware, the
  host test and the table in the README cannot drift apart -- there is exactly
  one place that says which CC does what.
*/

#ifndef MOOG_PARAMS_H
#define MOOG_PARAMS_H

#include <stdint.h>
#include "moog_defs.h"
#include "moog_fx.h"

enum MoogParam {
    /* --- Controllers ---------------------------------------------------- */
    MOOG_TUNE = 0,          /* (1)  master tune                             */
    MOOG_GLIDE,             /* (2)  portamento time                         */
    MOOG_GLIDE_ON,          /* (R)  glide switch                   (switch) */
    MOOG_MOD_MIX,           /* (3)  osc 3 <-> noise                         */
    MOOG_MOD_WHEEL,         /* (29) modulation wheel, also on CC 1          */
    MOOG_OSC_MOD,           /* (A)  oscillator modulation switch   (switch) */
    MOOG_OSC3_CTRL,         /* (B)  osc 3 keyboard control switch  (switch) */
    MOOG_BEND_RANGE,        /* (28) travel of the pitch wheel               */

    /* --- Oscillator Bank ------------------------------------------------ */
    MOOG_OSC1_RANGE,        /* (4)                                   (enum) */
    MOOG_OSC1_WAVE,         /* (5)                                   (enum) */
    MOOG_OSC2_RANGE,        /* (6)                                   (enum) */
    MOOG_OSC2_FREQ,         /* (7)                                          */
    MOOG_OSC2_WAVE,         /* (8)                                   (enum) */
    MOOG_OSC3_RANGE,        /* (9)                                   (enum) */
    MOOG_OSC3_FREQ,         /* (10)                                         */
    MOOG_OSC3_WAVE,         /* (11)                                  (enum) */

    /* --- Mixer ---------------------------------------------------------- */
    MOOG_OSC1_VOL,          /* (12)                                         */
    MOOG_OSC1_ON,           /* (C)                                 (switch) */
    MOOG_OSC2_VOL,          /* (13)                                         */
    MOOG_OSC2_ON,           /* (E)                                 (switch) */
    MOOG_OSC3_VOL,          /* (14)                                         */
    MOOG_OSC3_ON,           /* (G)                                 (switch) */
    MOOG_NOISE_VOL,         /* (16)                                         */
    MOOG_NOISE_ON,          /* (F)                                 (switch) */
    MOOG_NOISE_COLOR,       /* (H)  white / pink                     (enum) */
    MOOG_FEEDBACK_VOL,      /* (15) external input volume                   */
    MOOG_FEEDBACK_ON,       /* (D)                                 (switch) */

    /* --- Modifiers ------------------------------------------------------ */
    MOOG_CUTOFF,            /* (17)                                         */
    MOOG_EMPHASIS,          /* (18)                                         */
    MOOG_CONTOUR_AMT,       /* (19) amount of contour                       */
    MOOG_FILTER_MOD,        /* (J)                                 (switch) */
    MOOG_KB_CTRL_1,         /* (K)                                 (switch) */
    MOOG_KB_CTRL_2,         /* (L)                                 (switch) */
    MOOG_FILT_ATTACK,       /* (20)                                         */
    MOOG_FILT_DECAY,        /* (21)                                         */
    MOOG_FILT_SUSTAIN,      /* (22)                                         */
    MOOG_LOUD_ATTACK,       /* (23)                                         */
    MOOG_LOUD_DECAY,        /* (24)                                         */
    MOOG_LOUD_SUSTAIN,      /* (25)                                         */
    MOOG_DECAY_SW,          /* (S)  decay switch, both contours    (switch) */

    /* --- Output --------------------------------------------------------- */
    MOOG_VOLUME,            /* (26) main output volume                      */
    MOOG_A440,              /* (Q)  tuning tone                    (switch) */

    /* --- Voicing (component values and trimmers in the original) -------- */
    MOOG_DRIVE,             /* how hard the mixer pushes the ladder         */
    MOOG_DRIFT,             /* oscillator and filter instability            */
    MOOG_TONE,              /* bandwidth of the audio path                  */
    MOOG_NOTE_PRIORITY,     /* low / high / last                     (enum) */
    MOOG_TRIGGER,           /* single / multiple                     (enum) */
    MOOG_TRANSPOSE,         /* octave shift of incoming notes        (enum) */

    /* --- Effects (no counterpart on the instrument) --------------------- */
    /* Two slots, each empty or holding one of the three effects; the signal
     * runs A then B. See moog_fx.h for why there are two and not three. */
    MOOG_FX_SLOT_A,         /* off / chorus / delay / reverb         (enum) */
    MOOG_FX_SLOT_B,         /*                                       (enum) */

    MOOG_CHORUS_RATE,
    MOOG_CHORUS_DEPTH,
    MOOG_CHORUS_MIX,
    MOOG_CHORUS_FB,

    MOOG_DELAY_TIME,
    MOOG_DELAY_FB,
    MOOG_DELAY_MIX,
    MOOG_DELAY_TONE,

    MOOG_REVERB_SIZE,
    MOOG_REVERB_DAMP,
    MOOG_REVERB_MIX,
    MOOG_REVERB_WIDTH,

    MOOG_PARAM_COUNT
};

/* ------------------------------------------------------------------------ */
/* Descriptor                                                                */
/* ------------------------------------------------------------------------ */
enum MoogParamType {
    MOOG_T_CONT = 0,   /* continuous, shown on the panel scale 0..10        */
    MOOG_T_BIPOLAR,    /* continuous, centre detent, shown -x..+x           */
    MOOG_T_SWITCH,     /* off / on                                          */
    MOOG_T_ENUM        /* rotary switch with named positions                */
};

struct MoogParamDesc {
    const char*  name;      /* <= 9 characters: the display fits 15 per line */
    uint8_t      type;      /* MoogParamType                                 */
    uint8_t      steps;     /* MOOG_T_ENUM: number of positions              */
    uint8_t      cc;        /* MIDI controller, 0xFF = none                  */
    float        scale;     /* full scale of the printed value               */
    const char* const* labels;  /* MOOG_T_ENUM: names of the positions       */
};

#define MOOG_CC_NONE 0xFFu

extern const MoogParamDesc kMoogParams[MOOG_PARAM_COUNT];

/*
 * Print a parameter the way the panel of the instrument is marked: the knobs
 * run 0..10, the Cutoff control is marked -4..+4, and the frequency controls
 * of oscillators 2 and 3 are marked -7..+7 -- which on those two is also the
 * number of semitones. Rotary switches print the name of their position.
 *
 * Shared by the front panel, the MIDI front end and the host test, so that
 * what the display says and what the engine does cannot come apart.
 */
void moogFormatValue(int id, float v, char* dst, size_t n);

/* Reverse lookup of the CC table. Returns -1 for a controller that is not
 * mapped to a parameter. */
int moogParamForCc(uint8_t cc);

/* Position names of the rotary switches. Kept short enough to sit next to a
 * label on a 128 px display. */
extern const char* const kMoogRangeLabels[MOOG_RANGE_COUNT];
extern const char* const kMoogWave12Labels[MOOG_WAVE_COUNT];
extern const char* const kMoogWave3Labels[MOOG_WAVE_COUNT];
extern const char* const kMoogNoiseLabels[2];
extern const char* const kMoogPriorityLabels[3];
extern const char* const kMoogTriggerLabels[2];
extern const char* const kMoogTransposeLabels[5];
extern const char* const kMoogFxLabels[];

/* ------------------------------------------------------------------------ */
/* Helpers shared by the engine, the front panel and the host test           */
/* ------------------------------------------------------------------------ */

/* Position of a rotary switch from its normalised value. The value sits in
 * the middle of its slot (see moogParamFromStep), so rounding errors on the
 * way through the per mille IPC cannot move a switch. */
static inline int moogParamStep(float v, int steps)
{
    if (steps < 2) return 0;
    int s = (int) (v * (float) steps);
    if (s < 0) s = 0;
    if (s >= steps) s = steps - 1;
    return s;
}

/* The normalised value that selects a given position. */
static inline float moogParamFromStep(int step, int steps)
{
    if (steps < 2) return 0.0f;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    return ((float) step + 0.5f) / (float) steps;
}

static inline bool moogParamOn(float v) { return v >= 0.5f; }

#endif /* MOOG_PARAMS_H */
