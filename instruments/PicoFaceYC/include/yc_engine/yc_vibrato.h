
// include/yc_engine/yc_vibrato.h
#pragma once
#include "yc_core.h"
#include <cmath>
#include "yc_sine_lut.h"

constexpr int YC_VIBRATO_DELAY_LEN = 512; // ~11.6 ms bei 44.1 kHz

struct yc_vibrato_state_t {
    float delay_buffer[YC_VIBRATO_DELAY_LEN] = {0.0f};
    int   write_index = 0;
    float lfo_phase = 0.0f;  // 0..2*PI, da sinf direkt diese Form erwartet
};

static inline float yc_vibrato_process(yc_vibrato_state_t& vstate, const yc_engine_state_t& state, float dry_sample) {
    vstate.delay_buffer[vstate.write_index] = dry_sample;

    // LFO-Phaseninkrement (6.5 Hz bei 44100 Hz)
    const float lfo_inc = YC_2PI * 6.5f / YC_SAMPLE_RATE;

    if (state.vibcho_depth == 0) {
        vstate.lfo_phase += lfo_inc;
        if (vstate.lfo_phase >= YC_2PI) vstate.lfo_phase -= YC_2PI;
        vstate.write_index = (vstate.write_index + 1) % YC_VIBRATO_DELAY_LEN;
        return dry_sample;
    }

    // Hub-Tabelle: depth 1..4 -> 10,20,30,40 Samples Modulationshub
    const float depth_samples = (float)state.vibcho_depth * 10.0f;

    vstate.lfo_phase += lfo_inc;
    if (vstate.lfo_phase >= YC_2PI) vstate.lfo_phase -= YC_2PI;
    const float lfo_val = yc_sinf_lut(vstate.lfo_phase);

    // Modulierte Delay-Zeit: Basis 256 Samples ± Hub
    const float delay_samples = 256.0f + depth_samples * lfo_val;

    // Lese-Position relativ zum aktuellen Schreibindex (vor Inkrement)
    float read_pos = (float)vstate.write_index - delay_samples;
    while (read_pos < 0.0f) read_pos += (float)YC_VIBRATO_DELAY_LEN;
    while (read_pos >= (float)YC_VIBRATO_DELAY_LEN) read_pos -= (float)YC_VIBRATO_DELAY_LEN;

    int idx0 = (int)read_pos;
    int idx1 = (idx0 + 1) % YC_VIBRATO_DELAY_LEN;
    float fract = read_pos - (float)idx0;

    float wet_sample = vstate.delay_buffer[idx0] * (1.0f - fract) + vstate.delay_buffer[idx1] * fract;

    vstate.write_index = (vstate.write_index + 1) % YC_VIBRATO_DELAY_LEN;

    if (state.vibcho_select == 0) {
        return wet_sample;
    } else {
        return 0.5f * dry_sample + 0.5f * wet_sample;
    }
}

