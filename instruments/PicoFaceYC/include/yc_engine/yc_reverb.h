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

static inline float yc_reverb_process(yc_reverb_state_t& rstate, const yc_engine_state_t& state, float dry_sample) {
    if (state.reverb == 0) {
        return dry_sample;
    }

    const float comb_fb = 0.84f;
    float c1 = yc_reverb_comb(rstate.comb1, rstate.comb1_idx, 1116, dry_sample, comb_fb);
    float c2 = yc_reverb_comb(rstate.comb2, rstate.comb2_idx, 1188, dry_sample, comb_fb);
    float c3 = yc_reverb_comb(rstate.comb3, rstate.comb3_idx, 1277, dry_sample, comb_fb);
    float c4 = yc_reverb_comb(rstate.comb4, rstate.comb4_idx, 1356, dry_sample, comb_fb);

    float comb_out = (c1 + c2 + c3 + c4) * 0.25f;

    const float g = 0.5f;
    float ap1 = yc_reverb_allpass(rstate.allpass1, rstate.allpass1_idx, 225, comb_out, g);
    float ap2 = yc_reverb_allpass(rstate.allpass2, rstate.allpass2_idx, 556, ap1, g);

    float wet_level = (state.reverb / 127.0f) * 0.5f;
    return dry_sample * (1.0f - wet_level) + ap2 * wet_level;
}

