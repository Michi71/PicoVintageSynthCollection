// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// PCM partial of the LA engine: plays one of the 100 ROM samples at a pitch,
// through the TVA envelope. This is the half of LA synthesis that comes out
// of the sample ROMs; the synth partials (sawtooth/square with TVF, and the
// ring modulation that pairs them) are separate.
//
// The D-50's own table carries a root pitch per sample, but that table lives
// in the MB87136's mask ROM and cannot be read out. For the 29 sustained
// loops it did not need to be: each is one cycle of 2^k words, so the root is
// 32000/length exactly. The attacks have no cycle to measure against and
// carry an estimate. Material with no pitch at all -- Noise, and some
// percussion -- reports 0 and always plays at the ROM rate, because
// transposing noise onto a note turns it into a buzz.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_env.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_mod.h"

namespace d5 {

// The TVA envelope is the shared five-segment shape; the synth partial uses
// the same one for its TVF.
using TvaEnvSpec = Env5Spec;
using TvaEnv = Env5;

struct PcmSampleRef {
    const int16_t* data = nullptr;   // whole PCM space
    uint32_t start = 0;
    uint32_t length = 0;
    bool looped = false;
    float root_hz = 0.0f;
};

class PcmVoice {
public:
    void note_on(const PcmSampleRef& s, float note, float velocity,
                 const TvaEnvSpec& env, float sample_rate,
                 float detune = 1.0f) {
        s_ = s;
        index_ = 0;
        frac_ = 0.0f;
        active_ = s.data != nullptr && s.length > 0;
        gain_ = velocity;
        const float f = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        // Unpitched material has nothing to transpose from: play it as stored.
        rate_ = (s.root_hz > 0.0f) ? f / s.root_hz : 1.0f;
        rate_ *= sample_rate > 0.0f ? (32000.0f / sample_rate) : 1.0f;
        rate_ *= detune;
        env_.start(env, sample_rate);
    }

    void note_off() { env_.release(); }
    bool active() const { return active_; }

    float D5_HOT_TAG(d5_pcm_next, next)(const Modulation& mod = Modulation{}) {
        if (!active_) return 0.0f;

        const uint32_t i = index_;
        // The partner for interpolation is the next word, and at the end of a
        // loop that word is the first one again -- not the one past the
        // sample. Wrapping the position instead of the partner drops the last
        // word of every revolution and plays the first one twice, which on a
        // 128-word cycle is 250 corrupted words a second. It cost up to 2.4%
        // of the signal on VIOLlp, and exactly nothing on Noise, whose last
        // word happens to equal its first.
        const uint32_t j = (i + 1 < s_.length) ? i + 1 : (s_.looped ? 0u : i);
        const float a = s_.data[s_.start + i] * (1.0f / 32768.0f);
        const float b = s_.data[s_.start + j] * (1.0f / 32768.0f);
        const float sample = a + (b - a) * frac_;

        advance(rate_ * mod.pitch);
        const float amp = env_.next();
        if (env_.finished()) active_ = false;
        return sample * amp * gain_ * mod.amp;
    }

private:
    // Position is an exact word index plus a float fraction, not a double.
    // The Cortex-M33 has no double unit, so every step of the old version --
    // the add, the floor, the compare, the truncation -- was a soft-float
    // call, and there were sixteen voices asking for them 32000 times a
    // second. This is also *more* precise: the integer part cannot lose bits
    // to the mantissa however long the note is held.
    //
    // The whole-word step never reaches the sample length: it is
    // freq * length / 32000, and no MIDI note reaches 32 kHz. So one
    // conditional subtract is enough to wrap, no division and no modulo.
    void advance(float step) {
        frac_ += step;
        const uint32_t whole = static_cast<uint32_t>(frac_);
        if (whole) {
            frac_ -= static_cast<float>(whole);
            index_ += whole;
            if (index_ >= s_.length) {
                if (!s_.looped) { active_ = false; return; }
                index_ -= s_.length;
                if (index_ >= s_.length) index_ %= s_.length;   // paranoia
            }
        }
    }

    PcmSampleRef s_{};
    TvaEnv env_{};
    uint32_t index_ = 0;
    float frac_ = 0.0f;
    float rate_ = 1.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
