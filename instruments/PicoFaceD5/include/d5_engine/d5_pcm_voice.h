// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// PCM partial of the LA engine: plays one of the 100 ROM samples at a pitch,
// through the TVA envelope. This is the half of LA synthesis that comes out
// of the sample ROMs; the synth partials (sawtooth/square with TVF, and the
// ring modulation that pairs them) are separate.
//
// The D-50's own table carries a root pitch per sample, but that table lives
// in the MB87136's mask ROM and cannot be read out, so the generated table
// carries a root frequency measured from the material instead. Samples that
// have no pitch to measure (noise, some percussion) report 0 and always play
// at the ROM rate.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_env.h"
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
    void note_on(const PcmSampleRef& s, int note, float velocity,
                 const TvaEnvSpec& env, float sample_rate) {
        s_ = s;
        pos_ = 0.0;
        active_ = s.data != nullptr && s.length > 0;
        gain_ = velocity;
        const float f = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
        // Unpitched material has nothing to transpose from: play it as stored.
        rate_ = (s.root_hz > 0.0f) ? f / s.root_hz : 1.0f;
        rate_ *= sample_rate > 0.0f ? (32000.0f / sample_rate) : 1.0f;
        env_.start(env, sample_rate);
    }

    void note_off() { env_.release(); }
    bool active() const { return active_; }

    float next(const Modulation& mod = Modulation{}) {
        if (!active_) return 0.0f;

        const uint32_t i = static_cast<uint32_t>(pos_);
        if (i + 1 >= s_.length) {
            if (!s_.looped) { active_ = false; return 0.0f; }
            pos_ -= static_cast<double>(s_.length);
        }
        const uint32_t k = s_.start + static_cast<uint32_t>(pos_);
        const float frac = static_cast<float>(pos_ - std::floor(pos_));
        const float a = s_.data[k] * (1.0f / 32768.0f);
        const float b = s_.data[k + 1] * (1.0f / 32768.0f);
        const float sample = a + (b - a) * frac;

        pos_ += rate_ * mod.pitch;
        const float amp = env_.next();
        if (env_.finished()) active_ = false;
        return sample * amp * gain_ * mod.amp;
    }

private:
    PcmSampleRef s_{};
    TvaEnv env_{};
    double pos_ = 0.0;
    float rate_ = 1.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
