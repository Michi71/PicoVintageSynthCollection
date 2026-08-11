// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The two levels above a voice, as the D-50 arranges them:
//
//   Patch ─┬─ Tone (Upper) ─ voices ─ equalizer ─ chorus ─┐
//          └─ Tone (Lower) ─ voices ─ equalizer ─ chorus ─┴─ reverb ─ out
//
// A tone is what the panel edits: two partials, a structure, LFOs, and its
// own equalizer and chorus. A patch pairs two of them and adds the reverb.
// Voice allocation lives here too, because the D-50's polyphony is counted in
// partial pairs: sixteen for a single tone, eight each when both play.
#pragma once

#include <cstdint>

#include "d5_engine/d5_effects.h"
#include "d5_engine/d5_voice.h"

namespace d5 {

struct ToneSpec {
    VoiceSpec voice{};
    EqSpec eq{};
    ChorusSpec chorus{};
    float level = 1.0f;
};

// One tone: its voices, then its two insert effects.
template <int kVoices = 8>
class Tone {
public:
    void configure(const ToneSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        eq_.configure(spec.eq, sample_rate);
        chorus_.configure(spec.chorus, sample_rate);
    }

    void note_on(int note, float velocity) {
        int slot = -1;
        for (int i = 0; i < kVoices; ++i) {
            if (!active_[i]) { slot = i; break; }
        }
        if (slot < 0) {                 // steal the oldest sounding voice
            slot = 0;
            for (int i = 1; i < kVoices; ++i) {
                if (age_[i] > age_[slot]) slot = i;
            }
        }
        voices_[slot].note_on(spec_.voice, note, velocity, sr_);
        note_[slot] = note;
        active_[slot] = true;
        age_[slot] = 0;
        for (int i = 0; i < kVoices; ++i) {
            if (i != slot && active_[i]) ++age_[i];
        }
    }

    void note_off(int note) {
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i] && note_[i] == note) voices_[i].note_off();
        }
    }

    float next() {
        float sum = 0.0f;
        for (int i = 0; i < kVoices; ++i) {
            if (!active_[i]) continue;
            sum += voices_[i].next();
            if (!voices_[i].active()) active_[i] = false;
        }
        return chorus_.process(eq_.process(sum)) * spec_.level;
    }

    bool sounding() const {
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i]) return true;
        }
        return false;
    }

private:
    ToneSpec spec_{};
    float sr_ = 32000.0f;
    Voice voices_[kVoices]{};
    Equalizer eq_{};
    Chorus<> chorus_{};
    int note_[kVoices] = {};
    int age_[kVoices] = {};
    bool active_[kVoices] = {};
};

enum class KeyMode : uint8_t { kWhole = 0, kDual = 1, kSplit = 2 };

struct PatchSpec {
    ToneSpec upper{};
    ToneSpec lower{};
    KeyMode key_mode = KeyMode::kWhole;
    int split_point = 60;         // panel "Split Point", C4 by default
    float balance = 0.5f;         // panel "Tone Balance", upper to lower
    ReverbSpec reverb{};
    float volume = 1.0f;
};

class Patch {
public:
    void configure(const PatchSpec& spec, float sample_rate) {
        spec_ = spec;
        upper_.configure(spec.upper, sample_rate);
        lower_.configure(spec.lower, sample_rate);
        reverb_.configure(spec.reverb, sample_rate);
    }

    void note_on(int note, float velocity) {
        reverb_.note_activity();
        switch (spec_.key_mode) {
            case KeyMode::kDual:
                upper_.note_on(note, velocity);
                lower_.note_on(note, velocity);
                break;
            case KeyMode::kSplit:
                if (note >= spec_.split_point) upper_.note_on(note, velocity);
                else lower_.note_on(note, velocity);
                break;
            case KeyMode::kWhole:
            default:
                upper_.note_on(note, velocity);
                break;
        }
    }

    void note_off(int note) {
        upper_.note_off(note);
        lower_.note_off(note);
    }

    float next() {
        const float b = spec_.balance < 0 ? 0 : (spec_.balance > 1 ? 1 : spec_.balance);
        const float mix = upper_.next() * (b > 0.5f ? 2.0f * (1.0f - b) : 1.0f) +
                          lower_.next() * (b < 0.5f ? 2.0f * b : 1.0f);
        return reverb_.process(mix) * spec_.volume;
    }

    bool sounding() const { return upper_.sounding() || lower_.sounding(); }

private:
    PatchSpec spec_{};
    Tone<8> upper_{};
    Tone<8> lower_{};
    Reverb reverb_{};
};

}  // namespace d5
