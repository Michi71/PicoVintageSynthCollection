

// include/yc_engine/yc_engine.h
#pragma once
#include "yc_core.h"
#include "yc_sine_lut.h"
#include "yc_tonegen.h"
#include "yc_percussion.h"
#include "yc_vibrato.h"
#include "yc_rotary.h"
#include "yc_fx.h"
#include "yc_reverb.h"
#include "ram_hot.h"

static inline void yc_engine_init(yc_engine_state_t& state) {
    state.wave = 0;
    yc_wavetable_select(state.wave);
    state.octave = 0;
    state.footage[0] = 6;
    state.footage[1] = 6;
    state.footage[2] = 5;
    state.footage[3] = 5;
    state.footage[4] = 4;
    state.footage[5] = 3;
    state.footage[6] = 2;
    state.footage[7] = 1;
    state.footage[8] = 0;

    state.perc_on = 0;
    state.perc_type = 0;
    state.perc_length = 2;
    state.vibcho_select = 0;
    state.vibcho_depth = 0;
    state.rotary_speed = 0;
    state.distortion = 0;
    state.reverb = 0;
    state.volume = 127;
    state.vol_gain = (float)state.volume / 127.0f;
    state.sustain_held = false;
    state.active_count = 0;

    for (uint8_t i = 0; i < YC_MAX_VOICES; ++i) {
        state.voices[i].active = false;
        state.voices[i].releasing = false;
        state.voices[i].sustained = false;
        state.voices[i].amp = 0.0f;
    }
}

static inline void yc_engine_note_on(yc_engine_state_t& state, yc_percussion_state_t& pstate, uint8_t note, uint8_t velocity) {
    int active_before = state.active_count;
    yc_tonegen_note_on(state, note, velocity);
    yc_percussion_trigger(pstate, state, note, active_before == 0);
}

static inline void yc_engine_note_off(yc_engine_state_t& state, uint8_t note) {
    yc_tonegen_note_off(state, note, state.sustain_held);
}

static inline void yc_engine_release_sustained(yc_engine_state_t& state) {
    for (int k = 0; k < state.active_count; ++k) {
        yc_voice_t& v = state.voices[state.active_idx[k]];
        if (v.active && v.sustained) { v.sustained = false; v.releasing = true; }
    }
}

static inline void yc_engine_all_notes_off(yc_engine_state_t& state) {
    state.active_count = 0;
    for (auto& v : state.voices) {
        v.active = false;
        v.releasing = false;
        v.sustained = false;
        v.amp = 0.0f;
    }
}

static inline void yc_engine_set_param(yc_engine_state_t& state, uint8_t param_id, uint16_t value) {
    // Die neun Zugriegel (Footages) liegen als zusammenhaengender ID-Block vor,
    // daher ein Bereichs-Check statt neun einzelner cases.
    if (param_id >= YC_PARAM_FOOTAGE_16 && param_id <= YC_PARAM_FOOTAGE_1) {
        state.footage[param_id - YC_PARAM_FOOTAGE_16] = (uint8_t)(value & 0xFF);
        return;
    }

    switch (param_id) {
        case YC_PARAM_WAVE:
            state.wave = (uint8_t)(value & 0xFF);
            yc_wavetable_select(state.wave);
            break;
        case YC_PARAM_OCTAVE:
            state.octave = (int8_t)(value & 0xFF);
            break;
        case YC_PARAM_PERC_ON:
            state.perc_on = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_PERC_TYPE:
            state.perc_type = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_PERC_LENGTH:
            state.perc_length = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_VIBCHO_SELECT:
            state.vibcho_select = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_VIBCHO_DEPTH:
            state.vibcho_depth = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_DISTORTION:
            state.distortion = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_REVERB:
            state.reverb = (uint8_t)(value & 0xFF);
            break;
        case YC_PARAM_VOLUME:
            state.volume = (uint8_t)(value & 0xFF);
            state.vol_gain = (float)state.volume / 127.0f;
            break;
        default:
            (void)0; // unbekannte param_id ignorieren
            break;
    }
}

inline void RAM_HOT(yc_engine_render_block)(yc_engine_state_t& state, yc_rotary_state_t& rotary_state, yc_vibrato_state_t& vibrato_state, yc_percussion_state_t& pstate, yc_reverb_state_t& reverb_state, float* out_l, float* out_r, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float sample = 0.0f;
        int k = 0;
        while (k < state.active_count) {
            yc_voice_t& v = state.voices[state.active_idx[k]];
            sample += yc_tonegen_render_voice(v, state);
            if (!v.active) {
                state.active_idx[k] = state.active_idx[--state.active_count];
            } else {
                ++k;
            }
        }
        sample += yc_percussion_render(pstate, state);
        sample = yc_vibrato_process(vibrato_state, state, sample);
        sample = yc_overdrive_process(state, sample);
        sample = yc_rotary_process(rotary_state, state, sample);
        sample = yc_reverb_process(reverb_state, state, sample);
        sample = yc_soft_clip(sample);
        out_l[i] = sample;
        out_r[i] = sample;
    }
} // TODO M1-M4: Stereo (Rotary erzeugt eigentlich L/R-Differenz durch Panning), hier vorerst Mono auf beide Kanaele

