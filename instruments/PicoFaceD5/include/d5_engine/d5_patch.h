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
#include "d5_engine/d5_hot.h"
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
        // LFO-1 Sync 2: every new note restarts the vibrato of the voices
        // already sounding, so legato play pulses together (EPROM
        // 0x2929/0x2952/0x2981 gate this on the byte being exactly 2).
        if (spec_.voice.lfo[0].sync == 2) {
            for (int i = 0; i < kVoices; ++i) {
                if (i != slot && active_[i]) voices_[i].retrigger_lfo1();
            }
        }
    }

    void note_off(int note) {
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i] && note_[i] == note) voices_[i].note_off();
        }
    }

    void set_wheel(float w) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_wheel(w);
    }

    float D5_HOT(next)() {
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

    // Live edits that must not interrupt sounding notes.
    void set_level(float v) { spec_.level = v; }
    void set_chorus_balance(float b) { chorus_.set_balance(b); }
    void set_master_cents(float c) { spec_.voice.master_cents = c; }
    void set_bend_semis(float st) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_bend_semis(st);
    }
    void set_aftertouch(float a) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_aftertouch(a);
    }

    // CC65/CC5 override the patch's portamento while it plays; the mode
    // stays the patch's, so this can only quiet a tone the patch excluded,
    // never add one.
    void set_porta(bool sw, int time) {
        spec_.voice.porta_switch = sw;
        spec_.voice.porta_time = time;
        for (int i = 0; i < kVoices; ++i) voices_[i].set_porta(sw, time);
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
    // Panel "Bender Range", pb[26], 0..12 semitones. Patch-common: the
    // firmware copies it to both tone slots at load time (EPROM 0x5D60,
    // C59A -> FE04/FE0C) and lets an RPN-0 data entry overwrite it until
    // the next load (0x4E72, clamped to 12).
    int bend_range = 2;
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

    float D5_HOT(next)() {
        // Tone balance per the firmware's mixer (bank code 0xB397): each
        // tone's factor is min(4*b, 255)/200 of its side, so the center
        // is 1.0 each and a full tilt reaches +2.1 dB on the loud side --
        // in whole mode both stay at 1.0 (the ROM forces factor 200).
        // Direction PROVEN: the copy routine at 0x686E feeds pb33's
        // factor into the gain words written to the upper tone's DSP
        // slots -- above 50 the upper tone wins.
        float uw = 1.0f, lw = 1.0f;
        if (spec_.key_mode != KeyMode::kWhole) {
            const float b = spec_.balance < 0 ? 0 : (spec_.balance > 1 ? 1 : spec_.balance);
            uw = 2.0f * b; if (uw > 1.275f) uw = 1.275f;
            lw = 2.0f * (1.0f - b); if (lw > 1.275f) lw = 1.275f;
        }
        const float mix = upper_.next() * uw + lower_.next() * lw;
        return saturate(reverb_.process(mix) * spec_.volume);
    }

    // Sixteen voices plus a reverb tail can ask for more than full scale, and
    // a converter answers that with hard clipping. This stays linear below
    // -3 dB and bends smoothly above, so loud chords lose their peaks instead
    // of tearing.
    static float saturate(float x) {
        constexpr float kKnee = 0.7f;
        const float a = x < 0.0f ? -x : x;
        if (a <= kKnee) return x;
        const float over = (a - kKnee) / (1.0f - kKnee);
        const float shaped = kKnee + (1.0f - kKnee) * (over / (1.0f + over));
        return x < 0.0f ? -shaped : shaped;
    }

    bool sounding() const { return upper_.sounding() || lower_.sounding(); }

    // Panel controls that apply while the patch is playing. Anything that
    // would resize a delay line or restart a voice belongs in configure().
    void set_volume(float v) { spec_.volume = v; }
    void set_reverb_balance(float b) { reverb_.set_balance(b); }
    void set_chorus_balance(float b) {
        upper_.set_chorus_balance(b);
        lower_.set_chorus_balance(b);
    }
    void set_master_cents(float c) {
        upper_.set_master_cents(c);
        lower_.set_master_cents(c);
    }
    void set_bend_semis(float st) {
        upper_.set_bend_semis(st);
        lower_.set_bend_semis(st);
    }
    void set_mod_wheel(float w) {
        upper_.set_wheel(w);
        lower_.set_wheel(w);
    }
    void set_aftertouch(float a) {
        upper_.set_aftertouch(a);
        lower_.set_aftertouch(a);
    }
    void set_porta(bool sw, int time) {
        upper_.set_porta(sw, time);
        lower_.set_porta(sw, time);
    }

private:
    PatchSpec spec_{};
    Tone<8> upper_{};
    Tone<8> lower_{};
    Reverb reverb_{};
};

}  // namespace d5
