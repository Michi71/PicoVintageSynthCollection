// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71


// include/yc_engine/yc_reverb.h
#pragma once
#include "yc_core.h"

struct yc_reverb_state_t {
    float comb1[1116]{};
    float comb2[1188]{};
    float comb3[1277]{};
    float comb4[1356]{};
    int comb1_idx = 0;
    int comb2_idx = 0;
    int comb3_idx = 0;
    int comb4_idx = 0;

    float allpass1[225]{};
    float allpass2[556]{};
    int allpass1_idx = 0;
    int allpass2_idx = 0;
};

static inline float yc_reverb_comb(float* buf, int& idx, int size, float input, float feedback) {
    float y = buf[idx];
    buf[idx] = input + y * feedback;
    idx++;
    if (idx >= size) idx = 0;
    return y;
}

static inline float yc_reverb_allpass(float* buf, int& idx, int size, float input, float g) {
    float bufout = buf[idx];
    float y = -g * input + bufout;
    buf[idx] = input + g * bufout;
    idx++;
    if (idx >= size) idx = 0;
    return y;
}

// The diffused signal for one mono input sample. The reverb stays mono behind
// the stereo rotary: it is fed the mid of both channels and the same tail is
// mixed into each, which keeps the 18 KB of delay lines single and is how a
// room behind a Leslie sounds anyway - the swirl is in the direct sound.
static inline float yc_reverb_wet(yc_reverb_state_t& rstate, float in) {
    const float comb_fb = 0.84f;
    float c1 = yc_reverb_comb(rstate.comb1, rstate.comb1_idx, 1116, in, comb_fb);
    float c2 = yc_reverb_comb(rstate.comb2, rstate.comb2_idx, 1188, in, comb_fb);
    float c3 = yc_reverb_comb(rstate.comb3, rstate.comb3_idx, 1277, in, comb_fb);
    float c4 = yc_reverb_comb(rstate.comb4, rstate.comb4_idx, 1356, in, comb_fb);

    float comb_out = (c1 + c2 + c3 + c4) * 0.25f;

    const float g = 0.5f;
    float ap1 = yc_reverb_allpass(rstate.allpass1, rstate.allpass1_idx, 225, comb_out, g);
    return yc_reverb_allpass(rstate.allpass2, rstate.allpass2_idx, 556, ap1, g);
}

static inline float yc_reverb_wet_level(const yc_engine_state_t& state) {
    return (state.reverb / 127.0f) * 0.5f;
}

// Stereo in place. With l == r this is the old mono stage bit for bit: the
// mid of two equal samples is that sample.
static inline void yc_reverb_process(yc_reverb_state_t& rstate, const yc_engine_state_t& state, float& l, float& r) {
    if (state.reverb == 0) return;
    const float wet = yc_reverb_wet(rstate, 0.5f * (l + r));
    const float w = yc_reverb_wet_level(state);
    l = l * (1.0f - w) + wet * w;
    r = r * (1.0f - w) + wet * w;
}

