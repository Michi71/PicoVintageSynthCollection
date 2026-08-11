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

namespace d5 {

// Five-segment TVA envelope in the D-50's own terms: three timed levels, a
// sustain the note holds at, and a release to the end level.
struct TvaEnvSpec {
    float t[5] = {0.004f, 0.10f, 0.20f, 0.30f, 0.40f};   // seconds
    float l[3] = {1.0f, 0.85f, 0.7f};
    float sustain = 0.6f;
    float end = 0.0f;
};

class TvaEnv {
public:
    void start(const TvaEnvSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        seg_ = 0;
        level_ = 0.0f;
        held_ = true;
        arm(0, spec_.l[0]);
    }

    void release() {
        if (held_) {
            held_ = false;
            seg_ = 4;
            arm(4, spec_.end);
        }
    }

    bool finished() const { return !held_ && seg_ >= 5; }

    float next() {
        if (remaining_ > 0) {
            level_ += step_;
            --remaining_;
        } else if (held_ && seg_ < 3) {
            ++seg_;
            arm(seg_, seg_ < 3 ? spec_.l[seg_] : spec_.sustain);
        } else if (held_ && seg_ == 3) {
            level_ = spec_.sustain;          // hold until release
        } else if (!held_ && seg_ == 4) {
            seg_ = 5;
            level_ = spec_.end;
        }
        return level_ < 0.0f ? 0.0f : level_;
    }

private:
    void arm(int seg, float target) {
        seg_ = seg;
        remaining_ = static_cast<int32_t>(spec_.t[seg] * sr_);
        step_ = remaining_ > 0 ? (target - level_) / remaining_ : 0.0f;
        if (remaining_ <= 0) level_ = target;
    }

    TvaEnvSpec spec_{};
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

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

    float next() {
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

        pos_ += rate_;
        const float amp = env_.next();
        if (env_.finished()) active_ = false;
        return sample * amp * gain_;
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
