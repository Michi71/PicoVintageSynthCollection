// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71


// include/yc_engine/yc_rotary.h
#pragma once
#include "yc_core.h"
#include "yc_sine_lut.h"

// constexpr statt const: gleiche Werte, aber keine internen Kopien pro TU.
constexpr float YC_ROTARY_2PI = YC_2PI;
constexpr float YC_ROTARY_RAMP_RATE = 6.5f / (2.0f * YC_SAMPLE_RATE); // ~2 s Hochlauf wie ein echtes Leslie
constexpr float YC_ROTARY_CROSSOVER_ALPHA = 0.10772478580474854f; // 1 - expf(-2pi * 800 Hz / 44100 Hz), float32-exakt vorberechnet
constexpr float YC_ROTARY_HORN_DEPTH = 0.4f;
constexpr float YC_ROTARY_DRUM_DEPTH = 0.2f;

// Bewegt current schrittweise auf target zu (Leslie-Hochlauf/Auslauf).
static inline float yc_approach(float current, float target, float step) {
    if (current < target) {
        current += step;
        if (current > target) current = target;
    } else if (current > target) {
        current -= step;
        if (current < target) current = target;
    }
    return current;
}

struct yc_rotary_state_t {
    float horn_phase = 0.0f;
    float drum_phase = 0.0f;
    float horn_speed = 0.0f;
    float drum_speed = 0.0f;
    float lp_state = 0.0f;
};

// rotary_speed: 0=OFF (harter Bypass), 1=STOP (Rotoren laufen aus und bleiben
// stehen: statische Klangfaerbung, wie ein geparktes echtes Leslie),
// 2=SLOW, 3=FAST. STOP ist ein gewollter Modus (GUI/MIDI/Doku), kein Restzustand.
static inline float yc_rotary_process(yc_rotary_state_t& rstate, const yc_engine_state_t& state, float dry_sample) {
    if (state.rotary_speed == 0) {
        return dry_sample;
    }

    // STOP (1): Zielgeschwindigkeiten 0 -> Auslauf-Rampe, danach eingefroren.
    float target_horn_speed = 0.0f;
    float target_drum_speed = 0.0f;

    if (state.rotary_speed == 2) {
        target_horn_speed = 0.8f;
        target_drum_speed = 0.6f;
    } else if (state.rotary_speed == 3) {
        target_horn_speed = 6.5f;
        target_drum_speed = 5.0f;
    }

    rstate.horn_speed = yc_approach(rstate.horn_speed, target_horn_speed, YC_ROTARY_RAMP_RATE);
    rstate.drum_speed = yc_approach(rstate.drum_speed, target_drum_speed, YC_ROTARY_RAMP_RATE);

    rstate.horn_phase += YC_ROTARY_2PI * rstate.horn_speed / YC_SAMPLE_RATE;
    if (rstate.horn_phase >= YC_ROTARY_2PI) rstate.horn_phase -= YC_ROTARY_2PI;
    rstate.drum_phase += YC_ROTARY_2PI * rstate.drum_speed / YC_SAMPLE_RATE;
    if (rstate.drum_phase >= YC_ROTARY_2PI) rstate.drum_phase -= YC_ROTARY_2PI;

    rstate.lp_state += YC_ROTARY_CROSSOVER_ALPHA * (dry_sample - rstate.lp_state);
    float low = rstate.lp_state;
    float high = dry_sample - low;

    float horn_mod = 1.0f - YC_ROTARY_HORN_DEPTH + YC_ROTARY_HORN_DEPTH * (0.5f + 0.5f * yc_sinf_lut(rstate.horn_phase));
    float drum_mod = 1.0f - YC_ROTARY_DRUM_DEPTH + YC_ROTARY_DRUM_DEPTH * (0.5f + 0.5f * yc_sinf_lut(rstate.drum_phase));

    return (low * drum_mod) + (high * horn_mod);
}
