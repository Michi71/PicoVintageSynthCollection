
#pragma once
#include "yc_core.h"
#include "yc_wavetable.h"
#include "yc_lut_data.h"
#include <cmath>

static inline float yc_tonegen_render_voice(yc_voice_t& voice, const yc_engine_state_t& state) {
    if (!voice.active) {
        return 0.0f;
    }
    float mixed_sample = 0.0f;
    for (int i = 0; i < YC_NUM_FOOTAGES; ++i) {
        // Frueh aussteigen, wenn der Zugriegel auf 0 steht: Beitrag ist ohnehin 0
        // und das Phasen-Advance kann entfallen (Phase wird einfach eingefroren).
        uint8_t footage_level = state.footage[i];
        if (footage_level == 0) continue;
        if (footage_level > 6) footage_level = 6;

        float phase = voice.footage_phase[i];

        // Phase mit vorberechnetem Inkrement weiterschieben, Wrap ohne fmodf.
        float next_phase = phase + voice.phase_inc[i];
        while (next_phase >= (float)YC_WAVETABLE_SIZE) next_phase -= (float)YC_WAVETABLE_SIZE;
        voice.footage_phase[i] = next_phase;

        int p0 = (int)phase; // phase >= 0, Truncation == floor
        int p1 = p0 + 1;
        if (p1 >= YC_WAVETABLE_SIZE) p1 = 0;
        float frac = phase - (float)p0;
        float s0 = yc_wavetable_ram[p0];
        float s1 = yc_wavetable_ram[p1];
        float interp_sample = s0 + (s1 - s0) * frac;
        mixed_sample += interp_sample * yc_footage_gain_lut[footage_level];
    }
    float normalize_factor = 1.0f / (float)YC_NUM_FOOTAGES;
    if (voice.releasing) {
        voice.amp -= 1.0f / (float)YC_RELEASE_SAMPLES;
        if (voice.amp <= 0.0f) { voice.amp = 0.0f; voice.active = false; voice.releasing = false; return 0.0f; }
    } else if (voice.amp < 1.0f) {
        voice.amp += 1.0f / (float)YC_ATTACK_SAMPLES;
        if (voice.amp > 1.0f) voice.amp = 1.0f;
    }
    return mixed_sample * normalize_factor * voice.vel_gain * state.vol_gain * voice.amp;
}

static inline void yc_tonegen_note_on(yc_engine_state_t& state, uint8_t note, uint8_t velocity) {
    int fi = -1;
    for (int i = 0; i < YC_MAX_VOICES; ++i) {
        if (!state.voices[i].active) { fi = i; break; }
    }

    int target_voice;
    if (fi != -1) {
        target_voice = fi;
        state.active_idx[state.active_count++] = (uint8_t)fi;
    } else {
        // Voice-Stealing: aelteste Stimme (Liste ist in Note-On-Reihenfolge).
        // Vorher wurde IMMER Slot 0 geraubt, bei >16 gespielten Noten blieben
        // so alle anderen Stimmen fuer haengen. Der geraubte Slot wandert ans
        // Listenende, der naechste Raub trifft die naechstaelteste Stimme.
        target_voice = state.active_idx[0];
        for (int j = 0; j + 1 < state.active_count; ++j) {
            state.active_idx[j] = state.active_idx[j + 1];
        }
        state.active_idx[state.active_count - 1] = (uint8_t)target_voice;
    }

    yc_voice_t& v = state.voices[target_voice];
    v.active = true;
    v.sustained = false;
    v.releasing = false;
    v.note = note;
    v.velocity = velocity;
    v.vel_gain = (float)velocity / 127.0f;
    // Oktavlage einrechnen (wurde bisher nur gespeichert, nie angewendet).
    // Clamp aus Sicherheitsgruenden, die UI begrenzt ohnehin auf -2..+2.
    int oct = state.octave;
    if (oct < -3) oct = -3;
    if (oct >  3) oct =  3;
    v.base_freq = 440.0f * powf(2.0f, (float)(note - 69) / 12.0f) * exp2f((float)oct); // einmalig hier berechnet statt pro Sample
    v.phase = 0.0f;
    v.amp = 0.0f;
    for (int i = 0; i < YC_NUM_FOOTAGES; ++i) {
        v.footage_phase[i] = 0.0f;
        v.phase_inc[i] = (v.base_freq * YC_FOOTAGE_PITCH_RATIO[i] / YC_SAMPLE_RATE) * (float)YC_WAVETABLE_SIZE; // einmalig hier berechnet statt pro Sample
    }
}

static inline void yc_tonegen_note_off(yc_engine_state_t& state, uint8_t note, bool sustain_held) {
    for (int k = 0; k < state.active_count; ++k) {
        yc_voice_t& v = state.voices[state.active_idx[k]];
        if (v.active && v.note == note) {
            if (sustain_held) {
                v.sustained = true;
                continue;
            } else {
                v.releasing = true;
                continue;
            }
        }
    }
}

