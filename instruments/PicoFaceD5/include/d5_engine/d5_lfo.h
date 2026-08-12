// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The three LFOs of the common block, and the pitch envelope that sits beside
// them. Both belong to the tone, not to a partial: the partials only choose
// which LFO to listen to and how far.
//
// Ranges are the ones the service notes specify, not invented ones:
//   LFO rate   0.0004 .. 27 Hz      LFO delay  0 .. 10 s
//   P-ENV time 9 ms .. 9 s          P-ENV depth  +/- 2400 cents
//   pitch modulation by LFO         +/- 600 cents
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_fastmath.h"

namespace d5 {

enum class LfoWave : uint8_t { kTriangle = 0, kSawtooth = 1, kSquare = 2,
                               kRandom = 3 };

struct LfoSpec {
    LfoWave wave = LfoWave::kTriangle;
    float rate = 0.5f;        // 0..1, panel 0..100; maps 0.0004 .. 27 Hz
    float delay = 0.0f;       // 0..1, panel 0..100; maps 0 .. 10 s
    bool key_sync = true;     // panel "Sync": restart the phase on note-on
};

// Panel 0..100 to Hz, read from the D-05 firmware's own table (BQ3:Appli at
// 0xE32B4: Q31 phase increments at 32 kHz, doubling every 8 steps of its
// 128-step internal range). Its endpoints are the service notes' 0.0004 and
// 27 Hz to the fourth digit, which is what earns it the place of the
// hand-built exponential -- the two differed by at most a few percent, but
// verbatim beats fitted.
inline constexpr float kLfoRateHz[101] = {
    0.000432134f, 0.000476837f, 0.000581145f, 0.00064075f, 0.000700355f, 0.000759959f,
    0.000938773f, 0.00102818f, 0.00111759f, 0.00117719f, 0.00141561f, 0.00156462f,
    0.00169873f, 0.00199676f, 0.00223517f, 0.00241399f, 0.0026226f, 0.00312924f,
    0.00341237f, 0.00372529f, 0.00405312f, 0.00482798f, 0.00526011f, 0.00573695f,
    0.00625849f, 0.00745058f, 0.00812113f, 0.00885129f, 0.0105351f, 0.0114888f,
    0.012517f, 0.0136644f, 0.0162423f, 0.0177175f, 0.0193119f, 0.0210702f,
    0.0250489f, 0.0273287f, 0.0298023f, 0.035435f, 0.0386387f, 0.0421405f,
    0.0459552f, 0.0546575f, 0.0596046f, 0.0649989f, 0.0708699f, 0.084281f,
    0.0919104f, 0.10024f, 0.119209f, 0.129998f, 0.141755f, 0.154585f,
    0.183836f, 0.20048f, 0.21863f, 0.238419f, 0.283524f, 0.309184f,
    0.337169f, 0.367686f, 0.43726f, 0.476837f, 0.519991f, 0.618368f,
    0.674337f, 0.735372f, 0.801936f, 0.953674f, 1.03998f, 1.13411f,
    1.23675f, 1.47076f, 1.60387f, 1.74904f, 2.07996f, 2.26822f,
    2.47352f, 2.69739f, 3.20776f, 3.49809f, 3.8147f, 4.15994f,
    4.94704f, 5.39479f, 5.88305f, 6.41552f, 7.62939f, 8.3199f,
    9.07293f, 10.7896f, 11.7661f, 12.831f, 13.9924f, 16.6398f,
    18.1459f, 19.7882f, 21.5792f, 25.6621f, 27.9847f};

inline float lfo_rate_hz(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return kLfoRateHz[static_cast<int>(v * 100.0f + 0.5f)];
}

class Lfo {
public:
    void start(const LfoSpec& spec, float sample_rate, uint32_t seed) {
        spec_ = spec;
        sr_ = sample_rate;
        inc_ = lfo_rate_hz(spec.rate) / sr_;
        if (spec_.key_sync) phase_ = 0.0f;
        delay_left_ = spec_.delay * 10.0f * sr_;
        ramp_ = 0.0f;
        rng_ = seed ? seed : 0x2545F491u;
        sample_ = next_random();
    }

    // -1 .. +1, faded in over the delay time. The chip ramps rather than
    // switching on, which is why a delayed vibrato swells instead of
    // appearing.
    // The block-rate step: value from the current phase, then advance by n.
    float next_n(int32_t n) {
        float v;
        switch (spec_.wave) {
            case LfoWave::kSawtooth: v = 2.0f * phase_ - 1.0f; break;
            case LfoWave::kSquare:   v = phase_ < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::kRandom:   v = sample_; break;
            case LfoWave::kTriangle:
            default: v = phase_ < 0.5f ? (4.0f * phase_ - 1.0f)
                                       : (3.0f - 4.0f * phase_); break;
        }
        phase_ += inc_ * n;
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            sample_ = next_random();
        }
        if (delay_left_ > 0.0f) {
            delay_left_ -= n;
            return 0.0f;
        }
        if (ramp_ < 1.0f) {
            ramp_ += n / (0.05f * sr_);
            if (ramp_ > 1.0f) ramp_ = 1.0f;
        }
        return v * ramp_;
    }

    float next() {
        float v;
        switch (spec_.wave) {
            case LfoWave::kSawtooth:
                v = 2.0f * phase_ - 1.0f;
                break;
            case LfoWave::kSquare:
                v = phase_ < 0.5f ? 1.0f : -1.0f;
                break;
            case LfoWave::kRandom:
                v = sample_;
                break;
            case LfoWave::kTriangle:
            default:
                v = phase_ < 0.5f ? (4.0f * phase_ - 1.0f)
                                  : (3.0f - 4.0f * phase_);
                break;
        }

        phase_ += inc_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            sample_ = next_random();      // random holds for one period
        }

        if (delay_left_ > 0.0f) {
            delay_left_ -= 1.0f;
            return 0.0f;
        }
        if (ramp_ < 1.0f) {
            ramp_ += 1.0f / (0.05f * sr_);   // 50 ms fade-in, no step
            if (ramp_ > 1.0f) ramp_ = 1.0f;
        }
        return v * ramp_;
    }

private:
    float next_random() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    LfoSpec spec_{};
    float sr_ = 32000.0f;
    float inc_ = 0.0f;
    float phase_ = 0.0f;
    float delay_left_ = 0.0f;
    float ramp_ = 0.0f;
    float sample_ = 0.0f;
    uint32_t rng_ = 0x2545F491u;
};

// The pitch envelope: four times and five levels, and unlike TVA/TVF the
// levels are bipolar -- the panel shows them as -50..+50, so a pitch envelope
// can start below the note and rise into it.
struct PitchEnvSpec {
    float t[4] = {0.02f, 0.15f, 0.25f, 0.40f};   // seconds, 9 ms .. 9 s
    float l0 = 0.0f;                             // -1..+1, panel -50..+50
    float l1 = 0.0f;
    float l2 = 0.0f;
    float sustain = 0.0f;
    float end = 0.0f;
    float depth_cents = 0.0f;                    // 0 .. 2400
};

class PitchEnv {
public:
    void start(const PitchEnvSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        level_ = spec_.l0;
        seg_ = 0;
        held_ = true;
        arm(0, spec_.l1);
    }

    void release() {
        if (held_) {
            held_ = false;
            arm(3, spec_.end);
        }
    }

    // Pitch factor, advanced n samples at once (control rate).
    float next_n(int32_t n) {
        while (n > 0) {
            if (remaining_ > 0) {
                const int32_t k = remaining_ < n ? remaining_ : n;
                level_ += step_ * k;
                remaining_ -= k;
                n -= k;
            } else if (held_ && seg_ < 2) {
                arm(seg_ + 1, seg_ + 1 == 1 ? spec_.l2 : spec_.sustain);
            } else if (held_) {
                level_ = spec_.sustain;
                break;
            } else {
                break;
            }
        }
        const float cents = level_ * spec_.depth_cents;
        return cents == 0.0f ? 1.0f : fast_exp2(cents * (1.0f / 1200.0f));
    }

    // Pitch factor to multiply the playback rate / frequency by.
    float next() {
        if (remaining_ > 0) {
            level_ += step_;
            --remaining_;
        } else if (held_ && seg_ < 2) {
            arm(seg_ + 1, seg_ + 1 == 1 ? spec_.l2 : spec_.sustain);
        } else if (held_) {
            level_ = spec_.sustain;
        }
        // Per sample and per voice, and the last libm call left in the
        // audio path once the partials were converted.
        const float cents = level_ * spec_.depth_cents;
        return cents == 0.0f ? 1.0f : fast_exp2(cents * (1.0f / 1200.0f));
    }

private:
    void arm(int seg, float target) {
        seg_ = seg;
        remaining_ = static_cast<int32_t>(spec_.t[seg] * sr_);
        step_ = remaining_ > 0 ? (target - level_) / remaining_ : 0.0f;
        if (remaining_ <= 0) level_ = target;
    }

    PitchEnvSpec spec_{};
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

}  // namespace d5
