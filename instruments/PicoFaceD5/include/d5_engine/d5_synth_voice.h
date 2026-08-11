// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// Synth partial of the LA engine: the half that is generated rather than
// sampled, and the half responsible for the D-50's sustains.
//
// The LA32 has no filter. It builds the *already filtered* waveform directly:
// a square is assembled from a rising cosine segment, a high plateau, a
// falling cosine segment and a low plateau, and the cutoff sets how wide the
// cosine segments are -- a low cutoff means long, soft slopes and few
// harmonics. Resonance is a decaying sine at the cutoff frequency, restarted
// every cycle and windowed so it does not click. A sawtooth is that square
// multiplied by a cosine at the fundamental. This follows the model munt
// documents for the chip (mt32emu, LA32WaveGenerator); no code is taken from
// it -- LGPL-2.1 upstream, and the description is what matters here.
//
// Doing it this way is also what makes the partial cheap enough for the
// RP2350: no filter state, no oversampling, one cosine table.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_env.h"

namespace d5 {

enum class Waveform : uint8_t { kSquare = 0, kSawtooth = 1 };

struct SynthSpec {
    Waveform waveform = Waveform::kSawtooth;
    float pulse_width = 0.5f;      // 0..1, panel "WG Pulse Width"
    float cutoff = 0.7f;           // 0..1, panel "TVF Cutoff Frequency"
    float resonance = 0.3f;        // 0..1, panel "TVF Resonance"
    float tvf_env_depth = 0.4f;    // how far the TVF envelope moves cutoff
    Env5Spec tvf_env{};
    Env5Spec tva_env{};
};

class SynthPartial {
public:
    void note_on(const SynthSpec& spec, int note, float velocity,
                 float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        gain_ = velocity;
        phase_ = 0.0f;
        res_phase_ = 0.0f;
        active_ = true;
        freq_ = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
        inc_ = freq_ / sr_;
        tvf_.start(spec_.tvf_env, sr_);
        tva_.start(spec_.tva_env, sr_);
    }

    void note_off() {
        tvf_.release();
        tva_.release();
    }

    bool active() const { return active_; }

    float next() {
        if (!active_) return 0.0f;

        // Cutoff in harmonics of the fundamental: the cosine slopes take one
        // period of the cutoff frequency, so a cutoff near the fundamental
        // leaves almost a sine, and a high one leaves nearly square edges.
        const float env = tvf_.next();
        float c = spec_.cutoff + spec_.tvf_env_depth * env;
        if (c < 0.02f) c = 0.02f;
        if (c > 1.0f) c = 1.0f;
        const float cutoff_hz = 40.0f * std::pow(400.0f, c);   // 40 Hz .. 16 kHz
        float slope = freq_ / cutoff_hz;                       // cycle fraction
        if (slope > 0.45f) slope = 0.45f;
        if (slope < 1.0f / 64.0f) slope = 1.0f / 64.0f;

        const float pw = clampf(spec_.pulse_width, 0.05f, 0.95f);
        float out = segment(phase_, pw, slope);

        if (spec_.waveform == Waveform::kSawtooth) {
            // the chip's sawtooth: the square multiplied by a synchronous
            // cosine, which cancels every other harmonic's mirror and leaves
            // the asymmetric slope
            out *= std::cos(2.0f * kPi * phase_);
        }

        if (spec_.resonance > 0.0f) {
            // A sine at the cutoff, restarted every cycle of the fundamental
            // and decaying inside it -- the chip's stand-in for a resonant
            // filter. The decay has to be counted in cycles of the cutoff,
            // not in samples: a resonance of Q rings for about Q of them, and
            // measuring it per sample makes it an impulse (which reads as a
            // click and as broadband energy, not as resonance).
            const float cycles = cutoff_hz * res_phase_ / sr_;
            const float q = 0.7f + 24.0f * spec_.resonance * spec_.resonance;
            const float decay = std::exp(-kPi * cycles / q);
            // fade the ring in over its first quarter cycle so its restart
            // does not click
            const float w = cycles < 0.25f
                                ? 0.5f - 0.5f * std::cos(4.0f * kPi * cycles)
                                : 1.0f;
            out += spec_.resonance * decay * w *
                   std::sin(2.0f * kPi * cycles);
            res_phase_ += 1.0f;
        }

        phase_ += inc_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            res_phase_ = 0.0f;          // resonance restarts with the cycle
        }

        const float amp = tva_.next();
        if (tva_.finished()) active_ = false;
        return out * amp * gain_ * 0.5f;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    static float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // One cycle: rise over `slope`, high until `pw`, fall over `slope`, low.
    // The cosine slopes are what a low cutoff does to a square edge.
    static float segment(float p, float pw, float slope) {
        if (p < slope) {
            return -std::cos(kPi * p / slope);            // -1 -> +1
        }
        if (p < pw) {
            return 1.0f;
        }
        if (p < pw + slope) {
            return std::cos(kPi * (p - pw) / slope);      // +1 -> -1
        }
        return -1.0f;
    }

    SynthSpec spec_{};
    Env5 tvf_{};
    Env5 tva_{};
    float sr_ = 32000.0f;
    float freq_ = 440.0f;
    float inc_ = 0.0f;
    float phase_ = 0.0f;
    float res_phase_ = 0.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
