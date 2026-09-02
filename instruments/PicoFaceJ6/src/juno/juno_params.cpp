// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_params.cpp -- the panel table

  One line per control, in panel order. Reordering the front panel means moving
  a line here, not changing code: the menu, the display, the MIDI front end and
  the host test all read this table.

  CC 7 drives the master volume and not the patch's VCA level: Channel Volume
  belongs to the instrument rather than to a stored sound, and a patch change
  must not fight a sequencer for it. The patch's own VCA level is on CC 106.
  CC 69 is Hold 2, which is what the panel HOLD switch is.

  Controller numbers follow the usual meanings wherever one exists. The Juno
  happens to line up unusually well with the MIDI sound controllers -- CC 71
  resonance, 72 release, 73 attack, 74 cutoff, 75 decay, 76 sustain, 93 chorus
  depth -- so most of the envelope and filter fall where a sequencer
  already looks. Everything else sits on controllers the specification leaves
  undefined (3, 9, 20..31, 85..87, 102..105), never on a reserved one.
*/

#include "juno/juno_params.h"

#include <stdio.h>

const char* const kJunoRangeLabels[JUNO_RANGE_COUNT] = { "16'", "8'", "4'" };

/* PWM MODE SELECTOR on Panel Board A. Manual is a fixed width; the other two
 * take the width from the LFO or the contour. */
const char* const kJunoPwmModeLabels[3] = { "LFO", "Man", "Env" };

/* The switch positions, not frequencies -- the instrument selects one of three
 * capacitors or bypasses them, so there is nothing in between. The corners
 * are 154, 339 and 720 Hz. */
const char* const kJunoHpfLabels[JUNO_HPF_POSITIONS] = { "0", "1", "2", "3" };

const char* const kJunoVcaModeLabels[2] = { "Env", "Gate" };

const char* const kJunoChorusLabels[4] = { "Off", "I", "II", "I+II" };

const char* const kJunoTrigLabels[2] = { "Auto", "Man" };

const char* const kJunoTransposeLabels[5] = { "-2", "-1", "0", "+1", "+2" };

/* MODE and RANGE on Panel Board A. */
const char* const kJunoArpModeLabels[3]  = { "Up", "Up/Dn", "Down" };
const char* const kJunoArpRangeLabels[3] = { "1 Oct", "2 Oct", "3 Oct" };

/* Pitch bender travel in semitones, as a rotary switch so the display can name
 * the value without a special case. */
static const char* const kBendLabels[8] = {
    "0", "1", "2", "3", "4", "5", "6", "7"
};

/* The contour polarity switch is drawn on the panel as two shapes rather than
 * labelled plus and minus. */
static const char* const kPolarityLabels[2] = { "-", "+" };

#define CONT(n, cc)          { n, JUNO_T_CONT,    0, cc, 10.0f, nullptr }
#define BIPOLAR(n, cc, s)    { n, JUNO_T_BIPOLAR, 0, cc, s,     nullptr }
#define ROTARY(n, st, cc, l) { n, JUNO_T_ENUM,   st, cc, 1.0f,  l }
#define SWITCH(n, cc)        { n, JUNO_T_SWITCH,  2, cc, 1.0f,  nullptr }

const JunoParamDesc kJunoParams[JUNO_TOTAL_COUNT] = {
    /* --- LFO ------------------------------------------------------------ */
    /* JUNO_LFO_RATE      */ CONT  ("Rate",   3),
    /* JUNO_LFO_DELAY     */ CONT  ("Delay",  9),

    /* --- DCO ------------------------------------------------------------ */
    /* JUNO_DCO_RANGE     */ ROTARY("Range", JUNO_RANGE_COUNT, 21, kJunoRangeLabels),
    /* JUNO_DCO_LFO       */ CONT  ("LFO",    22),
    /* JUNO_DCO_PWM       */ CONT  ("PWM",    23),
    /* JUNO_DCO_PWM_MODE  */ ROTARY("PWM Mode", 3, 24, kJunoPwmModeLabels),
    /* JUNO_DCO_SAW       */ SWITCH("Saw",    25),
    /* JUNO_DCO_PULSE     */ SWITCH("Pulse",  26),
    /* JUNO_DCO_SUB       */ SWITCH("Sub",    27),
    /* JUNO_DCO_SUB_LEVEL */ CONT  ("Sub Lvl",28),
    /* JUNO_DCO_NOISE     */ CONT  ("Noise",  29),

    /* --- HPF ------------------------------------------------------------ */
    /* JUNO_HPF           */ ROTARY("HPF", JUNO_HPF_POSITIONS, 30, kJunoHpfLabels),

    /* --- VCF ------------------------------------------------------------ */
    /* JUNO_VCF_FREQ      */ CONT  ("Freq",   74),
    /* JUNO_VCF_RES       */ CONT  ("Res",    71),
    /* JUNO_VCF_ENV       */ CONT  ("Env",    31),
    /* JUNO_VCF_POLARITY  */ ROTARY("Polarity", 2, 102, kPolarityLabels),
    /* JUNO_VCF_LFO       */ CONT  ("LFO",    85),
    /* JUNO_VCF_KYBD      */ CONT  ("Kybd",   86),

    /* --- VCA ------------------------------------------------------------ */
    /* JUNO_VCA_LEVEL     */ CONT  ("Level", 106),
    /* JUNO_VCA_MODE      */ ROTARY("Mode", 2, 103, kJunoVcaModeLabels),

    /* --- ENV ------------------------------------------------------------ */
    /* JUNO_ENV_ATTACK    */ CONT  ("Attack", 73),
    /* JUNO_ENV_DECAY     */ CONT  ("Decay",  75),
    /* JUNO_ENV_SUSTAIN   */ CONT  ("Sustain",76),
    /* JUNO_ENV_RELEASE   */ CONT  ("Release",72),

    /* --- Chorus --------------------------------------------------------- */
    /* JUNO_CHORUS        */ ROTARY("Chorus", 4, 93, kJunoChorusLabels),

    /* --- Not on the panel ----------------------------------------------- */
    /* JUNO_TUNE          */ BIPOLAR("Tune",  87, JUNO_TUNE_CENTS),
    /* JUNO_BEND_RANGE    */ ROTARY ("Bend",  8, 20, kBendLabels),
    /* JUNO_LFO_TRIG      */ ROTARY ("Trig",  2, 104, kJunoTrigLabels),
    /* JUNO_TRANSPOSE     */ ROTARY ("Octave",5, 105, kJunoTransposeLabels),

    /* --- Instrument settings, not part of a patch ------------------------ */
    /* JUNO_ARP_ON        */ SWITCH ("Arp",   107),
    /* JUNO_ARP_MODE      */ ROTARY ("Mode",  JUNO_ARP_MODES,  108, kJunoArpModeLabels),
    /* JUNO_ARP_RANGE     */ ROTARY ("Range", JUNO_ARP_RANGES, 109, kJunoArpRangeLabels),
    /* JUNO_ARP_RATE      */ CONT   ("Rate",  110),
    /* JUNO_HOLD          */ SWITCH ("Hold",   69),
    /* JUNO_MASTER        */ CONT   ("Master",  7),
};

#undef CONT
#undef BIPOLAR
#undef ROTARY
#undef SWITCH

static_assert(sizeof(kJunoParams) / sizeof(kJunoParams[0]) == JUNO_TOTAL_COUNT,
              "panel table and JunoParam enum have drifted apart");

/* ------------------------------------------------------------------------ */
void junoFormatValue(int id, float v, char* dst, size_t n)
{
    if (!dst || n == 0) return;
    if (id < 0 || id >= JUNO_TOTAL_COUNT) { dst[0] = 0; return; }

    const JunoParamDesc& d = kJunoParams[id];
    v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);

    switch (d.type) {
        case JUNO_T_SWITCH:
            snprintf(dst, n, "%s", junoParamOn(v) ? "on" : "off");
            break;

        case JUNO_T_ENUM: {
            const int s = junoParamStep(v, d.steps);
            if (d.labels) snprintf(dst, n, "%s", d.labels[s]);
            else          snprintf(dst, n, "%d", s);
            break;
        }

        case JUNO_T_BIPOLAR: {
            float x = (v * 2.0f - 1.0f) * d.scale;
            if (x > -0.05f && x < 0.05f) x = 0.0f;
            snprintf(dst, n, "%+.1f", (double) x);
            break;
        }

        case JUNO_T_CONT:
        default:
            snprintf(dst, n, "%.1f", (double) (v * d.scale));
            break;
    }
}

float junoInstrumentDefault(int id)
{
    switch (id) {
        case JUNO_MASTER:    return 1.0f;
        case JUNO_ARP_ON:    return 0.0f;
        case JUNO_ARP_MODE:  return junoParamFromStep(0, JUNO_ARP_MODES);   /* up      */
        case JUNO_ARP_RANGE: return junoParamFromStep(0, JUNO_ARP_RANGES);  /* 1 octave*/
        case JUNO_ARP_RATE:  return 0.35f;                                  /* ~5 Hz   */
        case JUNO_HOLD:      return 0.0f;
        default:             return 0.0f;
    }
}

int junoParamForCc(uint8_t cc)
{
    for (int i = 0; i < JUNO_TOTAL_COUNT; ++i)
        if (kJunoParams[i].cc == cc)
            return i;
    return -1;
}
