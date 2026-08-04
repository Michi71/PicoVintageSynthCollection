/*
  moog_params.cpp -- the panel table

  One line per control. Reordering the front panel means moving a line here,
  not changing code: the menu, the display, the MIDI front end and the host
  test all read this table.

  MIDI controller numbers follow the usual meanings wherever one exists --
  CC 1 modulation, CC 5 and 65 portamento, CC 7 volume, CC 71 resonance,
  CC 73/75 attack and decay, CC 74 cutoff -- so that a generic controller does
  something sensible before anyone opens a mapping editor. Everything else
  sits on controllers that the MIDI specification leaves undefined (3, 9,
  14..15, 20..31, 85..90, 102..119), never on a reserved one.
*/

#include "moog/moog_params.h"

#include <stdio.h>

const char* const kMoogRangeLabels[MOOG_RANGE_COUNT] = {
    "LO", "32'", "16'", "8'", "4'", "2'"
};

/* Oscillators 1 and 2. The manual, left to right: "triangular,
 * sawtooth-triangular, sawtooth, square, wide rectangular, and narrow
 * rectangular". */
const char* const kMoogWave12Labels[MOOG_WAVE_COUNT] = {
    "Tri", "TriSaw", "Saw", "Square", "Wide", "Narrow"
};

/* "(Oscillator 3 substitutes a reverse sawtooth for the sawtooth-
 * triangular.)" */
const char* const kMoogWave3Labels[MOOG_WAVE_COUNT] = {
    "Tri", "RevSaw", "Saw", "Square", "Wide", "Narrow"
};

const char* const kMoogFxLabels[MOOG_FX_KIND_COUNT] = {
    "--", "Chorus", "Delay", "Reverb"
};

const char* const kMoogNoiseLabels[2]    = { "White", "Pink" };
const char* const kMoogPriorityLabels[3] = { "Low", "High", "Last" };
const char* const kMoogTriggerLabels[2]  = { "Single", "Multi" };
const char* const kMoogTransposeLabels[5]= { "-2", "-1", "0", "+1", "+2" };

/* Pitch wheel travel in semitones. A rotary switch with thirteen positions
 * rather than a continuous control, so that the display can name the value
 * without a special case. */
static const char* const kBendLabels[13] = {
    "0", "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "10", "11", "12"
};

/*
 * The scale is the number the panel of the instrument is marked with: the
 * knobs run 0..10, the Cutoff control -4..+4, and the frequency controls of
 * oscillators 2 and 3 -7..+7 -- which on those two is also the number of
 * semitones they shift by.
 */
#define CONT(n, cc)         { n, MOOG_T_CONT,    0, cc, 10.0f, nullptr }
#define BIPOLAR(n, cc, s)   { n, MOOG_T_BIPOLAR, 0, cc, s,     nullptr }
#define SWITCH(n, cc)       { n, MOOG_T_SWITCH,  2, cc, 1.0f,  nullptr }
#define ROTARY(n, st, cc, l) { n, MOOG_T_ENUM,  st, cc, 1.0f,  l }

const MoogParamDesc kMoogParams[MOOG_PARAM_COUNT] = {
    /* --- Controllers ---------------------------------------------------- */
    /* MOOG_TUNE        */ BIPOLAR("Tune",      3, 5.0f),
    /* MOOG_GLIDE       */ CONT   ("Glide",     5),
    /* MOOG_GLIDE_ON    */ SWITCH ("Glide Sw", 65),
    /* MOOG_MOD_MIX     */ CONT   ("Mod Mix",   9),
    /* MOOG_MOD_WHEEL   */ CONT   ("Mod Whl",   1),
    /* MOOG_OSC_MOD     */ SWITCH ("Osc Mod",  14),
    /* MOOG_OSC3_CTRL   */ SWITCH ("Osc3 Kbd", 15),
    /* MOOG_BEND_RANGE  */ ROTARY ("Bend", 13, 20, kBendLabels),

    /* --- Oscillator Bank ------------------------------------------------ */
    /* MOOG_OSC1_RANGE  */ ROTARY ("Range", MOOG_RANGE_COUNT, 21, kMoogRangeLabels),
    /* MOOG_OSC1_WAVE   */ ROTARY ("Wave",  MOOG_WAVE_COUNT,  22, kMoogWave12Labels),
    /* MOOG_OSC2_RANGE  */ ROTARY ("Range", MOOG_RANGE_COUNT, 23, kMoogRangeLabels),
    /* MOOG_OSC2_FREQ   */ BIPOLAR("Freq",  24, MOOG_FREQ_SEMITONES),
    /* MOOG_OSC2_WAVE   */ ROTARY ("Wave",  MOOG_WAVE_COUNT,  25, kMoogWave12Labels),
    /* MOOG_OSC3_RANGE  */ ROTARY ("Range", MOOG_RANGE_COUNT, 26, kMoogRangeLabels),
    /* MOOG_OSC3_FREQ   */ BIPOLAR("Freq",  27, MOOG_FREQ_SEMITONES),
    /* MOOG_OSC3_WAVE   */ ROTARY ("Wave",  MOOG_WAVE_COUNT,  28, kMoogWave3Labels),

    /* --- Mixer ---------------------------------------------------------- */
    /* MOOG_OSC1_VOL    */ CONT   ("Volume",  29),
    /* MOOG_OSC1_ON     */ SWITCH ("On",     102),
    /* MOOG_OSC2_VOL    */ CONT   ("Volume",  30),
    /* MOOG_OSC2_ON     */ SWITCH ("On",     103),
    /* MOOG_OSC3_VOL    */ CONT   ("Volume",  31),
    /* MOOG_OSC3_ON     */ SWITCH ("On",     104),
    /* MOOG_NOISE_VOL   */ CONT   ("Volume",  85),
    /* MOOG_NOISE_ON    */ SWITCH ("On",     105),
    /* MOOG_NOISE_COLOR */ ROTARY ("Colour", 2, 106, kMoogNoiseLabels),
    /* MOOG_FEEDBACK_VOL*/ CONT   ("Volume",  86),
    /* MOOG_FEEDBACK_ON */ SWITCH ("On",     107),

    /* --- Modifiers ------------------------------------------------------ */
    /* MOOG_CUTOFF      */ BIPOLAR("Cutoff",   74, 4.0f),
    /* MOOG_EMPHASIS    */ CONT   ("Emphasis", 71),
    /* MOOG_CONTOUR_AMT */ CONT   ("Contour",  87),
    /* MOOG_FILTER_MOD  */ SWITCH ("Filt Mod",108),
    /* MOOG_KB_CTRL_1   */ SWITCH ("Kbd 1",   109),
    /* MOOG_KB_CTRL_2   */ SWITCH ("Kbd 2",   110),
    /* MOOG_FILT_ATTACK */ CONT   ("Attack",   88),
    /* MOOG_FILT_DECAY  */ CONT   ("Decay",    89),
    /* MOOG_FILT_SUSTAIN*/ CONT   ("Sustain",  90),
    /* MOOG_LOUD_ATTACK */ CONT   ("Attack",   73),
    /* MOOG_LOUD_DECAY  */ CONT   ("Decay",    75),
    /* MOOG_LOUD_SUSTAIN*/ CONT   ("Sustain",  76),
    /* MOOG_DECAY_SW    */ SWITCH ("Decay Sw",111),

    /* --- Output --------------------------------------------------------- */
    /* MOOG_VOLUME      */ CONT   ("Volume",  7),
    /* MOOG_A440        */ SWITCH ("A-440", 112),

    /* --- Voicing -------------------------------------------------------- */
    /* MOOG_DRIVE       */ CONT   ("Drive", 70),
    /* MOOG_DRIFT       */ CONT   ("Drift", 77),
    /* MOOG_TONE        */ CONT   ("Tone",  78),
    /* MOOG_NOTE_PRIORITY*/ROTARY ("Priority", 3, 113, kMoogPriorityLabels),
    /* MOOG_TRIGGER     */ ROTARY ("Trigger",  2, 114, kMoogTriggerLabels),
    /* MOOG_TRANSPOSE   */ ROTARY ("Octave",   5, 115, kMoogTransposeLabels),

    /* --- Effects -------------------------------------------------------- */
    /* CC 91 and 93 are the reverb and chorus depth controllers of the MIDI
     * specification, so the two mixes sit where a sequencer already expects
     * to find them. Delay has no assigned controller; the rest use numbers
     * the specification leaves undefined. */
    /* MOOG_FX_SLOT_A   */ ROTARY ("Slot A", MOOG_FX_KIND_COUNT, 116, kMoogFxLabels),
    /* MOOG_FX_SLOT_B   */ ROTARY ("Slot B", MOOG_FX_KIND_COUNT, 117, kMoogFxLabels),

    /* MOOG_CHORUS_RATE */ CONT   ("Rate",     12),
    /* MOOG_CHORUS_DEPTH*/ CONT   ("Depth",    13),
    /* MOOG_CHORUS_MIX  */ CONT   ("Mix",      93),
    /* MOOG_CHORUS_FB   */ CONT   ("Feedback", 16),

    /* MOOG_DELAY_TIME  */ CONT   ("Time",     17),
    /* MOOG_DELAY_FB    */ CONT   ("Feedback", 18),
    /* MOOG_DELAY_MIX   */ CONT   ("Mix",      19),
    /* MOOG_DELAY_TONE  */ CONT   ("Tone",     79),

    /* MOOG_REVERB_SIZE */ CONT   ("Size",     80),
    /* MOOG_REVERB_DAMP */ CONT   ("Damping",  81),
    /* MOOG_REVERB_MIX  */ CONT   ("Mix",      91),
    /* MOOG_REVERB_WIDTH*/ CONT   ("Width",    82),
};

#undef CONT
#undef BIPOLAR
#undef SWITCH
#undef ROTARY

static_assert(sizeof(kMoogParams) / sizeof(kMoogParams[0]) == MOOG_PARAM_COUNT,
              "panel table and MoogParam enum have drifted apart");

/* ------------------------------------------------------------------------ */
/* Printing and CC lookup                                                    */
/* ------------------------------------------------------------------------ */
void moogFormatValue(int id, float v, char* dst, size_t n)
{
    if (!dst || n == 0) return;
    if (id < 0 || id >= MOOG_PARAM_COUNT) { dst[0] = 0; return; }

    const MoogParamDesc& d = kMoogParams[id];
    v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);

    switch (d.type) {
        case MOOG_T_SWITCH:
            snprintf(dst, n, "%s", moogParamOn(v) ? "on" : "off");
            break;

        case MOOG_T_ENUM: {
            const int s = moogParamStep(v, d.steps);
            if (d.labels) snprintf(dst, n, "%s", d.labels[s]);
            else          snprintf(dst, n, "%d", s);
            break;
        }

        case MOOG_T_BIPOLAR: {
            /* Centre detent. Printed with a sign, so that "0.0" and "-0.0"
             * cannot both appear on the display for the same position. */
            float x = (v * 2.0f - 1.0f) * d.scale;
            if (x > -0.05f && x < 0.05f) x = 0.0f;
            snprintf(dst, n, "%+.1f", (double) x);
            break;
        }

        case MOOG_T_CONT:
        default:
            snprintf(dst, n, "%.1f", (double) (v * d.scale));
            break;
    }
}

int moogParamForCc(uint8_t cc)
{
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i)
        if (kMoogParams[i].cc == cc)
            return i;
    return -1;
}
