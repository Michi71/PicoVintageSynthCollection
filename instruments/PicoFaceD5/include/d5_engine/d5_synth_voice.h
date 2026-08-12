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
#include "d5_engine/d5_fastmath.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_mod.h"
#include "d5_engine/d5_mod.h"

namespace d5 {

enum class Waveform : uint8_t { kSquare = 0, kSawtooth = 1 };

struct SynthSpec {
    Waveform waveform = Waveform::kSawtooth;
    float pulse_width = 0.5f;      // 0..1, panel "WG Pulse Width"
    float cutoff = 0.7f;           // 0..1, panel "TVF Cutoff Frequency"
    float resonance = 0.3f;        // 0..1, panel "TVF Resonance"
    float tvf_env_depth = 0.4f;    // how far the TVF envelope moves cutoff
    float cutoff_keyfollow = 0.0f; // ratio; the cutoff tracks the keyboard
    float tvf_velo = 0.0f;         // 0..1: how far velocity opens the filter
    float pitch_keyfollow = 1.0f;  // the WG ratio; the TVF tracks the difference
    Env5Spec tvf_env{};
    Env5Spec tva_env{};
};

class SynthPartial {
public:
    void note_on(const SynthSpec& spec, float note, float velocity,
                 float sample_rate, float detune = 1.0f) {
        spec_ = spec;
        sr_ = sample_rate;
        gain_ = velocity;
        phase_ = 0.0f;
        active_ = true;
        freq_ = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f) * detune;
        q_ = 0.7f + 24.0f * spec.resonance * spec.resonance;
        // TVF ENV Velocity Range: a soft strike sweeps less. Folded into the
        // effective depth once per note; block_mod reads it from here.
        tvf_depth_eff_ = spec.tvf_env_depth
                         * (1.0f + spec.tvf_velo * (velocity - 1.0f));
        cyc_ = 0.0f;
        res_env_ = 1.0f;
        inv_cutoff_ = 1.0f / 1000.0f;   // neutral until the first block_mod
        cut_per_sr_ = 0.0f;
        decay_mul_ = 1.0f;
        pw_eff_ = spec.pulse_width;
        // TVF keyfollow is DIFFERENTIAL: the chip tracks the cutoff by the
        // difference between the TVF keyfollow and the pitch keyfollow,
        // pivoted on C4 -- munt's TVF.cpp line 66 states it outright
        // (keyfollowMult[tvf.kf] - keyfollowMult[wg.pitchKeyfollow]). That
        // is why Horn Section stays bright at D#2: TVF 7/8 against pitch 1
        // tracks by -1/8, not by -7/8. The earlier C2-pivot hack was
        // compensating for this missing subtraction and is retired.
        kf_shift_ = (spec.cutoff_keyfollow - spec.pitch_keyfollow)
                    * (note - 60.0f) / (12.0f * kLog2Range);
        inc_ = freq_ / sr_;
        tvf_.start(spec_.tvf_env, sr_);
        tva_.start(spec_.tva_env, sr_);
    }

    void note_off() {
        tvf_.release();
        tva_.release();
    }

    bool active() const { return active_; }

    // The block-rate half, called once per kModPeriod samples: the TVF
    // envelope, the cutoff exponential, the slope reciprocal and the
    // resonance decay constant. None of it moves at audio rate, and per
    // sample it was most of what this partial cost.
    void block_mod(const Modulation& mod) {
        if (!active_) return;
        const float env = tvf_.next_n(kModPeriod);
        float c = spec_.cutoff + kf_shift_ + tvf_depth_eff_ * env + mod.cutoff;
        if (c < 0.02f) c = 0.02f;
        if (c > 1.0f) c = 1.0f;
        // 40 Hz .. 16 kHz -- once std::pow per sample, the single most
        // expensive thing this engine ever did, now one table read per block.
        const float cutoff_hz = 40.0f * fast_exp2(kLog2Range * c);
        inv_cutoff_ = 1.0f / cutoff_hz;
        cut_per_sr_ = cutoff_hz / sr_;
        pw_eff_ = clampf(spec_.pulse_width + mod.pw, 0.05f, 0.95f);
        if (spec_.resonance > 0.0f) {
            // The ring decays by exp(-pi*cycles/q); advancing cycles by
            // cut_per_sr_ each sample makes that one multiply, and the
            // constant needs the table only when the cutoff moves.
            decay_mul_ = fast_exp_neg(kPi * cut_per_sr_ / q_);
        }
    }

    float D5_HOT(next)(const Modulation& mod = Modulation{}) {
        if (!active_) return 0.0f;

        // Cutoff in harmonics of the fundamental: the cosine slopes take one
        // period of the cutoff frequency, so a cutoff near the fundamental
        // leaves almost a sine, and a high one leaves nearly square edges.
        const float freq = freq_ * mod.pitch;
        // The cosine edge lasts about a third of the cutoff's period, not a
        // whole one. With the full period the waveform rolled off from far
        // below the cutoff -- Horn Section held three harmonics where the
        // recording of the real machine holds ten. The 0.35 is anchored on
        // that recording; it is the transition width of the chip's slope,
        // not a resonance.
        float slope = 0.35f * freq * inv_cutoff_;              // cycle fraction
        if (slope > 0.45f) slope = 0.45f;
        // The old floor of 1/64 meant a fully open filter still rounded its
        // edges by 1.6% of the period -- the waveform could never become the
        // true square or saw the chip produces at high cutoff. 1/512 is
        // below audibility and merely keeps the cosine argument sane.
        if (slope < 1.0f / 512.0f) slope = 1.0f / 512.0f;

        float out = segment(phase_, pw_eff_, slope);

        if (spec_.waveform == Waveform::kSawtooth) {
            // the chip's sawtooth: the square multiplied by a synchronous
            // cosine, which cancels every other harmonic's mirror and leaves
            // the asymmetric slope
            out *= fast_cos(phase_);
        }

        if (spec_.resonance > 0.0f) {
            // A sine at the cutoff, restarted every cycle of the fundamental
            // and decaying inside it -- the chip's stand-in for a resonant
            // filter. The decay counts in cycles of the cutoff, not samples.
            cyc_ += cut_per_sr_;
            res_env_ *= decay_mul_;
            // fade the ring in over its first quarter cycle so its restart
            // does not click
            const float w = cyc_ < 0.25f
                                ? 0.5f - 0.5f * fast_cos(2.0f * cyc_)
                                : 1.0f;
            out += spec_.resonance * res_env_ * w * fast_sin(cyc_);
        }

        phase_ += inc_ * mod.pitch;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            cyc_ = 0.0f;                // resonance restarts with the cycle
            res_env_ = 1.0f;
        }

        const float amp = tva_.next();
        if (tva_.finished()) active_ = false;
        return out * amp * gain_ * mod.amp * 0.5f;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kLog2Range = 8.6438561897747246f;   // log2(400)

    static float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // One cycle: rise over `slope`, high until `pw`, fall over `slope`, low.
    // The cosine slopes are what a low cutoff does to a square edge.
    static float segment(float p, float pw, float slope) {
        if (p < slope) {
            return -fast_cos(0.5f * p / slope);           // -1 -> +1
        }
        if (p < pw) {
            return 1.0f;
        }
        if (p < pw + slope) {
            return fast_cos(0.5f * (p - pw) / slope);     // +1 -> -1
        }
        return -1.0f;
    }

    SynthSpec spec_{};
    Env5 tvf_{};
    Env5 tva_{};
    float sr_ = 32000.0f;
    float freq_ = 440.0f;
    float kf_shift_ = 0.0f;
    float inc_ = 0.0f;
    float phase_ = 0.0f;
    float inv_cutoff_ = 0.001f;
    float cut_per_sr_ = 0.0f;
    float decay_mul_ = 1.0f;
    float pw_eff_ = 0.5f;
    float cyc_ = 0.0f;
    float res_env_ = 1.0f;
    float q_ = 0.7f;
    float tvf_depth_eff_ = 0.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
