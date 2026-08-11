// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// A D-50 tone: two partials plus the common block that decides how they meet.
//
// The seven structures are the table on page 22 of the Advanced Course, and
// the block diagrams there carry a detail worth spelling out: in a ring
// structure the second partial is never heard on its own. What reaches the
// output is partial 1 plus the ring modulation of the two -- which is why
// those structures stay recognisable as partial 1 with something metallic
// growing out of it, rather than turning into two separate voices.
//
//   Str  P1  P2  output
//    1   S   S   P1 + P2
//    2   S   S   P1 + ring(P1, P2)
//    3   P   S   P1 + P2
//    4   P   S   P1 + ring(P1, P2)
//    5   S   P   P1 + ring(P1, P2)
//    6   P   P   P1 + P2
//    7   P   P   P1 + ring(P1, P2)
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_lfo.h"
#include "d5_engine/d5_pcm_voice.h"
#include "d5_engine/d5_synth_voice.h"

namespace d5 {

enum class PartialType : uint8_t { kSynth = 0, kPcm = 1 };

struct Structure {
    PartialType p1;
    PartialType p2;
    bool ring;
};

inline constexpr Structure kStructures[7] = {
    {PartialType::kSynth, PartialType::kSynth, false},   // 1
    {PartialType::kSynth, PartialType::kSynth, true },   // 2
    {PartialType::kPcm,   PartialType::kSynth, false},   // 3
    {PartialType::kPcm,   PartialType::kSynth, true },   // 4
    {PartialType::kSynth, PartialType::kPcm,   true },   // 5
    {PartialType::kPcm,   PartialType::kPcm,   false},   // 6
    {PartialType::kPcm,   PartialType::kPcm,   true },   // 7
};

// How far a partial follows one of the three LFOs. The panel writes this as
// +1..+3 / -1..-3 (which LFO, and in which direction) plus a depth.
struct LfoRoute {
    int lfo = 0;            // 0..2
    float depth = 0.0f;     // -1..+1, sign is the panel's polarity
};

// What the pitch envelope does to a partial: the panel's WG Mod P-ENV Mode,
// off / rising with the envelope / inverted.
enum class PEnvMode : uint8_t { kOff = 0, kPositive = 1, kNegative = 2 };

struct ToneSpec {
    int structure = 1;              // 1..7, panel "Structure No."
    float balance = 0.5f;           // 0..1, panel "Partial Balance", .5 = even
    // Bit 0 lets partial 1 sound, bit 1 partial 2. The panel calls this
    // "Partial Mute" and shows it as two bits; which bit is which is not
    // verified against hardware.
    uint8_t partials_on = 0x3;
    // Per-partial pitch in semitones, the panel's "WG Pitch Coarse". Detuning
    // the second partial is what makes a ring structure inharmonic rather
    // than just brighter.
    int coarse[2] = {0, 0};

    SynthSpec synth[2]{};           // used where the structure says S
    PcmSampleRef pcm[2]{};          // used where it says P
    Env5Spec pcm_env[2]{};

    // ---- common block: three LFOs and the pitch envelope, shared by both
    LfoSpec lfo[3]{};
    PitchEnvSpec penv{};

    // pitch modulation: the LFO route reaches +/- 600 cents at full depth,
    // the pitch envelope up to +/- 2400 (service notes, "PITCH MODULATION")
    LfoRoute pitch_lfo[2]{};
    PEnvMode penv_mode[2] = {PEnvMode::kOff, PEnvMode::kOff};

    LfoRoute pw_lfo[2]{};           // panel "WG PW LFO Select / Depth"
    LfoRoute tvf_lfo[2]{};          // panel "TVF Mod LFO Select / Depth"
    LfoRoute tva_lfo[2]{};          // panel "TVA Mod LFO Select / Depth"
};

class Tone {
public:
    void note_on(const ToneSpec& spec, int note, float velocity,
                 float sample_rate) {
        spec_ = spec;
        const Structure& st = structure();
        const PartialType types[2] = {st.p1, st.p2};
        for (int i = 0; i < 3; ++i) {
            lfo_[i].start(spec_.lfo[i], sample_rate, 0x9E3779B9u * (i + 1));
        }
        penv_.start(spec_.penv, sample_rate);

        for (int i = 0; i < 2; ++i) {
            const int n = note + spec_.coarse[i];
            if (types[i] == PartialType::kPcm) {
                pcm_[i].note_on(spec_.pcm[i], n, velocity,
                                spec_.pcm_env[i], sample_rate);
            } else {
                synth_[i].note_on(spec_.synth[i], n, velocity, sample_rate);
            }
        }
    }

    void note_off() {
        penv_.release();
        const Structure& st = structure();
        const PartialType types[2] = {st.p1, st.p2};
        for (int i = 0; i < 2; ++i) {
            if (types[i] == PartialType::kPcm) pcm_[i].note_off();
            else synth_[i].note_off();
        }
    }

    bool active() const {
        const Structure& st = structure();
        const bool a = (st.p1 == PartialType::kPcm) ? pcm_[0].active()
                                                    : synth_[0].active();
        const bool b = (st.p2 == PartialType::kPcm) ? pcm_[1].active()
                                                    : synth_[1].active();
        // A ring structure has nothing left to say once partial 1 is gone:
        // its product is silent without it.
        return st.ring ? a : (a || b);
    }

    float next() {
        const Structure& st = structure();

        const float l[3] = {lfo_[0].next(), lfo_[1].next(), lfo_[2].next()};
        const float pitch_env = penv_.next();

        Modulation mod[2];
        for (int i = 0; i < 2; ++i) {
            const LfoRoute& pr = spec_.pitch_lfo[i];
            const float cents = 600.0f * pr.depth * lfo_value(l, pr);
            float factor = cents != 0.0f
                               ? std::pow(2.0f, cents / 1200.0f) : 1.0f;
            if (spec_.penv_mode[i] == PEnvMode::kPositive) {
                factor *= pitch_env;
            } else if (spec_.penv_mode[i] == PEnvMode::kNegative) {
                factor /= pitch_env;
            }
            mod[i].pitch = factor;
            mod[i].pw = 0.5f * spec_.pw_lfo[i].depth * lfo_value(l, spec_.pw_lfo[i]);
            mod[i].cutoff = 0.5f * spec_.tvf_lfo[i].depth * lfo_value(l, spec_.tvf_lfo[i]);
            // amplitude modulation only ever ducks, never boosts past unity
            const float am = spec_.tva_lfo[i].depth * lfo_value(l, spec_.tva_lfo[i]);
            mod[i].amp = 1.0f + 0.5f * (am - std::fabs(spec_.tva_lfo[i].depth));
            if (mod[i].amp < 0.0f) mod[i].amp = 0.0f;
        }

        float a = (st.p1 == PartialType::kPcm) ? pcm_[0].next(mod[0])
                                               : synth_[0].next(mod[0]);
        float b = (st.p2 == PartialType::kPcm) ? pcm_[1].next(mod[1])
                                               : synth_[1].next(mod[1]);
        if (!(spec_.partials_on & 0x1)) a = 0.0f;
        if (!(spec_.partials_on & 0x2)) b = 0.0f;

        // The chip multiplies in the log domain, which is an ordinary product
        // once decoded: sum and difference frequencies, and silence whenever
        // either side is silent.
        const float second = st.ring ? a * b : b;

        const float bal = spec_.balance < 0.0f ? 0.0f
                        : (spec_.balance > 1.0f ? 1.0f : spec_.balance);
        const float w1 = bal > 0.5f ? 2.0f * (1.0f - bal) : 1.0f;
        const float w2 = bal < 0.5f ? 2.0f * bal : 1.0f;
        return a * w1 + second * w2;
    }

    const Structure& structure() const {
        const int i = (spec_.structure < 1 || spec_.structure > 7)
                          ? 0 : spec_.structure - 1;
        return kStructures[i];
    }

private:
    static float lfo_value(const float l[3], const LfoRoute& r) {
        const int i = (r.lfo < 0 || r.lfo > 2) ? 0 : r.lfo;
        return l[i];
    }

    ToneSpec spec_{};
    PcmVoice pcm_[2]{};
    SynthPartial synth_[2]{};
    Lfo lfo_[3]{};
    PitchEnv penv_{};
};

}  // namespace d5
