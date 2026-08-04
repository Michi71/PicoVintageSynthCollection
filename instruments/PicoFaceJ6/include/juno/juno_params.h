/*
  juno_params.h -- the front panel as a table

  Every control of the Juno-60, in the order it sits on the panel: LFO, DCO,
  HPF, VCF, VCA, ENV, Chorus. The list follows the pots and switches named on
  Panel Board A and B in the service notes rather than a guess at what the
  instrument ought to have.

  All parameters are normalised to 0..1, which is what the IPC ring carries (as
  per mille), what the patch table stores and what the settings record keeps.
  The descriptor says how a value is to be read: a continuous control, a
  switch, or a rotary switch with named positions.

  The MIDI controller number lives here too, so the firmware, the host test and
  the table in the README cannot drift apart.

  The list is in two halves. The first is a patch and is written whenever one
  is selected; the second holds instrument settings that no patch change may
  touch -- the arpeggiator and the output volume. See the note at
  JUNO_PARAM_COUNT.
*/

#ifndef JUNO_PARAMS_H
#define JUNO_PARAMS_H

#include <stdint.h>
#include "juno_defs.h"

enum JunoParam {
    /* --- LFO ------------------------------------------------------------ */
    JUNO_LFO_RATE = 0,
    JUNO_LFO_DELAY,

    /* --- DCO ------------------------------------------------------------ */
    JUNO_DCO_RANGE,         /* 16' / 8' / 4'                         (enum) */
    JUNO_DCO_LFO,           /* LFO to pitch                                 */
    JUNO_DCO_PWM,           /* pulse width / modulation depth               */
    JUNO_DCO_PWM_MODE,      /* LFO / Manual / Env                    (enum) */
    JUNO_DCO_SAW,           /* sawtooth on/off                     (switch) */
    JUNO_DCO_PULSE,         /* pulse on/off                        (switch) */
    JUNO_DCO_SUB,           /* sub-oscillator on/off               (switch) */
    JUNO_DCO_SUB_LEVEL,
    JUNO_DCO_NOISE,

    /* --- HPF ------------------------------------------------------------ */
    JUNO_HPF,               /* four positions, one for all voices    (enum) */

    /* --- VCF (IR3109) --------------------------------------------------- */
    JUNO_VCF_FREQ,
    JUNO_VCF_RES,
    JUNO_VCF_ENV,           /* amount of contour                            */
    JUNO_VCF_POLARITY,      /* contour polarity, + or -            (switch) */
    JUNO_VCF_LFO,
    JUNO_VCF_KYBD,          /* key follow, 0..100 %                         */

    /* --- VCA ------------------------------------------------------------ */
    JUNO_VCA_LEVEL,
    JUNO_VCA_MODE,          /* contour or gate                       (enum) */

    /* --- ENV (IR3201) --------------------------------------------------- */
    JUNO_ENV_ATTACK,
    JUNO_ENV_DECAY,
    JUNO_ENV_SUSTAIN,
    JUNO_ENV_RELEASE,

    /* --- Chorus --------------------------------------------------------- */
    JUNO_CHORUS,            /* off / I / II / I+II                   (enum) */

    /* --- Not on the panel ----------------------------------------------- */
    JUNO_TUNE,              /* +/- 50 cents, the rear-panel control         */
    JUNO_BEND_RANGE,        /* travel of the bender                  (enum) */
    JUNO_LFO_TRIG,          /* LFO retrigger: auto or manual         (enum) */
    JUNO_TRANSPOSE,         /* octave shift of incoming notes        (enum) */

    /*
     * Everything above is part of a patch and is written when one is
     * selected. Everything below is not.
     *
     * That split is how the instrument works rather than a convenience. A
     * Juno-60 stores the sound in its 56 memories; the arpeggiator switches
     * and the output volume are live panel controls read from the switch
     * matrix, and no patch change touches them. Which is also the only
     * sensible behaviour: a patch change in the middle of a performance must
     * not stop the arpeggio or reset the volume.
     */
    JUNO_PARAM_COUNT,

    /* --- Instrument settings -------------------------------------------- */
    JUNO_ARP_ON = JUNO_PARAM_COUNT,     /*                         (switch) */
    JUNO_ARP_MODE,          /* up / up&down / down                   (enum) */
    JUNO_ARP_RANGE,         /* 1 / 2 / 3 octaves                     (enum) */
    JUNO_ARP_RATE,          /* 1.5 .. 50 Hz                                 */
    JUNO_HOLD,              /* latches held notes                  (switch) */
    JUNO_MASTER,            /* master volume                                */

    JUNO_TOTAL_COUNT
};

/* ------------------------------------------------------------------------ */
/* Descriptor                                                                */
/* ------------------------------------------------------------------------ */
enum JunoParamType {
    JUNO_T_CONT = 0,   /* continuous, shown on the panel scale 0..10        */
    JUNO_T_BIPOLAR,    /* continuous, centre detent, shown -x..+x           */
    JUNO_T_SWITCH,     /* off / on                                          */
    JUNO_T_ENUM        /* rotary switch with named positions                */
};

struct JunoParamDesc {
    const char*  name;      /* <= 9 characters: the display fits 15 per line */
    uint8_t      type;
    uint8_t      steps;     /* JUNO_T_ENUM: number of positions              */
    uint8_t      cc;        /* MIDI controller, 0xFF = none                  */
    float        scale;     /* full scale of the printed value               */
    const char* const* labels;
};

#define JUNO_CC_NONE 0xFFu

extern const JunoParamDesc kJunoParams[JUNO_TOTAL_COUNT];

extern const char* const kJunoRangeLabels[JUNO_RANGE_COUNT];
extern const char* const kJunoPwmModeLabels[3];
extern const char* const kJunoHpfLabels[JUNO_HPF_POSITIONS];
extern const char* const kJunoVcaModeLabels[2];
extern const char* const kJunoChorusLabels[4];
extern const char* const kJunoTrigLabels[2];
extern const char* const kJunoTransposeLabels[5];
extern const char* const kJunoArpModeLabels[3];
extern const char* const kJunoArpRangeLabels[3];

/*
 * Print a parameter the way the panel is marked: sliders run 0..10, the
 * contour polarity switch reads as a shape, and rotary switches print the name
 * of their position.
 */
void junoFormatValue(int id, float v, char* dst, size_t n);

/* Reverse lookup of the CC table; -1 for a controller that is not mapped. */
int junoParamForCc(uint8_t cc);

/*
 * Default for an instrument setting, 0 for anything in the patch half.
 *
 * The patch half gets its values from whichever patch is selected, but nothing
 * writes the instrument half -- so it needs defaults, and they have to come
 * from one place. The engine and the front panel each keep their own copy of
 * the values, and when they disagreed the display read zero while the sound
 * did not. Worse, the master volume defaulting to zero made the whole
 * instrument silent at power-up.
 */
float junoInstrumentDefault(int id);

/* ------------------------------------------------------------------------ */
/* Helpers shared by the engine, the front panel and the host test           */
/* ------------------------------------------------------------------------ */
static inline int junoParamStep(float v, int steps)
{
    if (steps < 2) return 0;
    int s = (int) (v * (float) steps);
    if (s < 0) s = 0;
    if (s >= steps) s = steps - 1;
    return s;
}

static inline float junoParamFromStep(int step, int steps)
{
    if (steps < 2) return 0.0f;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    return ((float) step + 0.5f) / (float) steps;
}

static inline bool junoParamOn(float v) { return v >= 0.5f; }

#endif /* JUNO_PARAMS_H */
