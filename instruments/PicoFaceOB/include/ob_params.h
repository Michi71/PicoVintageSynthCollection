// ob_params.h - the parameter set PicoFaceOB exposes on its three encoders
// and over MIDI CC.
//
// Every value is normalised 0..1, which is exactly what the OB-Xf parameter
// setters expect; the engine does the linsc/logsc mapping to native units.
// The table is the single source of truth for the panel, the CC map and the
// persisted settings record - the same arrangement as sm_cc_map.h.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).
#ifndef OB_PARAMS_H
#define OB_PARAMS_H

#include <stdint.h>

enum ObParam : uint8_t {
    // oscillators
    OB_OSC1_MIX = 0,
    OB_OSC2_MIX,
    OB_OSC2_DETUNE,
    OB_OSC1_SAW,
    OB_OSC1_PULSE,
    OB_OSC2_SAW,
    OB_OSC2_PULSE,
    OB_PULSE_WIDTH,
    OB_OSC_SYNC,
    OB_CROSSMOD,
    OB_NOISE_MIX,
    OB_RINGMOD_MIX,
    OB_OSC1_PITCH,
    OB_OSC2_PITCH,
    OB_BRIGHTNESS,
    // filter
    OB_CUTOFF,
    OB_RESONANCE,
    OB_FOUR_POLE,
    OB_FILTER_ENV_AMT,
    OB_FILTER_KEYTRACK,
    OB_MULTIMODE,
    OB_PUSH_2POLE,
    // envelopes
    OB_FILT_ATTACK,
    OB_FILT_DECAY,
    OB_FILT_SUSTAIN,
    OB_FILT_RELEASE,
    OB_AMP_ATTACK,
    OB_AMP_DECAY,
    OB_AMP_SUSTAIN,
    OB_AMP_RELEASE,
    // LFO 1 (global)
    OB_LFO_RATE,
    OB_LFO_WAVE,
    OB_LFO_TO_PITCH,
    OB_LFO_TO_PW,
    OB_LFO_TO_CUTOFF,
    // global
    OB_PORTAMENTO,
    OB_VOICE_SLOP,
    OB_VOLUME,
    OB_PARAM_COUNT
};

struct ObParamDesc {
    const char* name;   // <= 10 chars, the 128 px display is not wide
    float       def;    // power-on default, 0..1
    uint8_t     cc;     // MIDI CC for send and receive, 0xFF = none
    uint8_t     steps;  // 0 = continuous, otherwise the number of discrete positions
};

// CC assignment follows the house rules from ARCHITECTURE.md section 6a:
// standard controllers where they genuinely fit (71 resonance, 72 release,
// 73 attack, 74 cutoff), the GM effect block for modulation, the rest out of
// the undefined range. CC 7, 64 and 120/121/123 stay free.
inline const ObParamDesc obParams[OB_PARAM_COUNT] = {
    {"Osc1 Mix",  0.80f, 102, 0},
    {"Osc2 Mix",  0.00f, 103, 0},
    {"Detune",    0.10f, 104, 0},
    {"Osc1 Saw",  1.00f, 105, 2},
    {"Osc1 Puls", 0.00f, 106, 2},
    {"Osc2 Saw",  1.00f, 107, 2},
    {"Osc2 Puls", 0.00f, 108, 2},
    {"PulseWid",  0.50f,  70, 0},
    {"Sync",      0.00f, 109, 2},
    {"Crossmod",  0.00f, 110, 0},
    {"Noise",     0.00f, 111, 0},
    {"RingMod",   0.00f, 112, 0},
    {"Osc1 Semi", 0.50f,  84, 0},
    {"Osc2 Semi", 0.50f, 113, 0},
    {"Bright",    1.00f,  75, 0},

    {"Cutoff",    1.00f,  74, 0},
    {"Resonance", 0.00f,  71, 0},
    {"4 Pole",    1.00f, 114, 2},
    {"Env Amt",   0.00f, 115, 0},
    {"Keytrack",  0.00f, 116, 0},
    {"Multimode", 0.00f, 117, 0},
    {"Push",      0.00f, 118, 2},

    {"F Attack",  0.00f,  76, 0},
    {"F Decay",   0.30f,  77, 0},
    {"F Sustain", 0.00f,  78, 0},
    {"F Release", 0.20f,  79, 0},
    {"A Attack",  0.00f,  73, 0},
    {"A Decay",   0.30f,  80, 0},
    {"A Sustain", 1.00f,  81, 0},
    {"A Release", 0.20f,  72, 0},

    {"LFO Rate",  0.30f,  92, 0},
    {"LFO Wave",  0.00f,  93, 5},
    {"LFO Pitch", 0.00f,  94, 0},
    {"LFO PW",    0.00f,  95, 0},
    {"LFO Cutof", 0.00f, 119, 0},

    {"Portamnto", 0.00f,   5, 0},
    {"Slop",      0.20f,  86, 0},
    {"Volume",    0.80f,  87, 0},
};

#endif // OB_PARAMS_H
