

// include/yc_engine/yc_percussion.h
#pragma once
#include "yc_core.h"
#include "yc_wavetable.h"
#include <cmath>

struct yc_percussion_state_t {
    bool active = false;
    int samples_since_trigger = 0;
    float phase = 0.0f;
    float frequency = 0.0f;
    float decay_t = 0.0f;
    float decay_coef = 0.0f;
    float env = 0.0f;
};

static inline void yc_percussion_trigger(yc_percussion_state_t& pstate, const yc_engine_state_t& state, uint8_t note, bool was_silent_before) {
    if (state.perc_on != 0 && was_silent_before) {
        pstate.active = true;
        pstate.samples_since_trigger = 0;
        pstate.phase = 0.0f;
        pstate.frequency = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
        pstate.decay_t = (80.0f + (float)state.perc_length * 130.0f) / 1000.0f;
        pstate.decay_coef = expf(-1.0f / (pstate.decay_t * YC_SAMPLE_RATE));
        pstate.env = 0.0f;
    }
}

static inline float yc_percussion_render(yc_percussion_state_t& pstate, const yc_engine_state_t& state) {
    if (!pstate.active) return 0.0f;

    const float attack_t = 0.005f;
    float time_s = (float)pstate.samples_since_trigger / YC_SAMPLE_RATE;
    float env_val;
    if (time_s < attack_t) {
        env_val = time_s / attack_t;
        pstate.env = env_val;
    } else {
        pstate.env *= pstate.decay_coef;
        env_val = pstate.env;
    }

    if (env_val < 0.001f) {
        pstate.active = false;
        return 0.0f;
    }

    float ratio = (state.perc_type == 1) ? 3.0f : 2.0f;
    float phase_inc = pstate.frequency * ratio / YC_SAMPLE_RATE * (float)YC_WAVETABLE_SIZE;

    int idx0 = (int)pstate.phase;
    int idx1 = (idx0 + 1) % YC_WAVETABLE_SIZE;
    float frac = pstate.phase - (float)idx0;

    float s0 = yc_wavetable_ram[idx0];
    float s1 = yc_wavetable_ram[idx1];
    float sample = s0 + frac * (s1 - s0);

    pstate.phase += phase_inc;
    if (pstate.phase >= (float)YC_WAVETABLE_SIZE) pstate.phase -= (float)YC_WAVETABLE_SIZE;

    pstate.samples_since_trigger++;

    return sample * env_val;
}

