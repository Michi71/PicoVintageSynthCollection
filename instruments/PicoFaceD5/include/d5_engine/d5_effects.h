// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The rest of the common block: equalizer and chorus per tone, reverb per
// patch. The signal path is the one the tone diagram on page 3 of the
// Advanced Course draws -- partials, then equalizer, then chorus -- with the
// reverb sitting behind the patch, after both tones have been mixed.
//
// The equalizer and the chorus follow the panel: the low band is a shelf with
// sixteen frequencies and +/-12 dB, the high band a peak with its own Q from
// 0.3 to 6.0, and the chorus has eight types plus rate, depth and balance.
//
// The reverb does not, and cannot. The D-50 puts it in a dedicated chip
// (M8B7126-006 in the parts list) whose 32 types are 188 coefficients each;
// those live in silicon and in the patch data, not in anything readable here.
// What follows is an ordinary Schroeder reverb whose 32 slots are mapped onto
// plausible room, plate and gate settings, so a patch that asks for reverb 12
// gets reverb, of roughly the right character and length -- not the original's
// impulse response. Anyone who measures the real thing can replace the table
// without touching the rest.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_fastmath.h"
#include "d5_engine/d5_hot.h"

namespace d5 {

// ------------------------------------------------------------------ biquad

class Biquad {
public:
    void set_low_shelf(float freq, float gain_db, float sr) {
        const float A = std::pow(10.0f, gain_db / 40.0f);
        const float w = 2.0f * kPi * freq / sr;
        const float cs = std::cos(w), sn = std::sin(w);
        const float beta = std::sqrt(A) / 0.9f;      // shelf slope ~1
        const float b0 = A * ((A + 1) - (A - 1) * cs + beta * sn);
        const float b1 = 2 * A * ((A - 1) - (A + 1) * cs);
        const float b2 = A * ((A + 1) - (A - 1) * cs - beta * sn);
        const float a0 = (A + 1) + (A - 1) * cs + beta * sn;
        const float a1 = -2 * ((A - 1) + (A + 1) * cs);
        const float a2 = (A + 1) + (A - 1) * cs - beta * sn;
        set(b0, b1, b2, a0, a1, a2);
    }

    void set_peaking(float freq, float q, float gain_db, float sr) {
        const float A = std::pow(10.0f, gain_db / 40.0f);
        const float w = 2.0f * kPi * freq / sr;
        const float cs = std::cos(w), sn = std::sin(w);
        const float alpha = sn / (2.0f * (q < 0.05f ? 0.05f : q));
        set(1 + alpha * A, -2 * cs, 1 - alpha * A,
            1 + alpha / A, -2 * cs, 1 - alpha / A);
    }

    void reset() { z1_ = z2_ = 0.0f; }

    float D5_HOT(process)(float x) {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    void set(float b0, float b1, float b2, float a0, float a1, float a2) {
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
        a1_ = a1 / a0; a2_ = a2 / a0;
    }

    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// --------------------------------------------------------------- equalizer

// Panel value to Hz, the tables printed in the MIDI implementation.
inline constexpr float kLowEqFreq[16] = {
    63, 75, 88, 105, 125, 150, 175, 210, 250, 300, 350, 420, 500, 600, 700, 840};
inline constexpr float kHighEqFreq[22] = {
    250, 300, 350, 420, 500, 600, 700, 840, 1000, 1200, 1400, 1700,
    2000, 2400, 2800, 3400, 4000, 4800, 5700, 6700, 8000, 9500};
inline constexpr float kHighEqQ[9] = {0.3f, 0.5f, 0.7f, 1.0f, 1.4f,
                                      2.0f, 3.0f, 4.2f, 6.0f};

struct EqSpec {
    int low_freq = 8;         // index into kLowEqFreq
    float low_gain_db = 0.0f; // -12 .. +12
    int high_freq = 12;       // index into kHighEqFreq
    int high_q = 3;           // index into kHighEqQ
    float high_gain_db = 0.0f;
};

class Equalizer {
public:
    void configure(const EqSpec& spec, float sr) {
        const int lf = clamp_index(spec.low_freq, 16);
        const int hf = clamp_index(spec.high_freq, 22);
        const int hq = clamp_index(spec.high_q, 9);
        low_.set_low_shelf(kLowEqFreq[lf], spec.low_gain_db, sr);
        high_.set_peaking(kHighEqFreq[hf], kHighEqQ[hq], spec.high_gain_db, sr);
        low_.reset();
        high_.reset();
    }

    float D5_HOT(process)(float x) { return high_.process(low_.process(x)); }

private:
    static int clamp_index(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }
    Biquad low_{};
    Biquad high_{};
};

// ------------------------------------------------------------------ chorus

// The eight types differ in how many voices move, how far apart they sit and
// whether the delayed signal is fed back; rate, depth and balance are the
// panel's own controls on top.
struct ChorusType {
    float base_ms;
    float spread_ms;
    int voices;
    float feedback;
};

inline constexpr ChorusType kChorusTypes[8] = {
    {12.0f, 0.0f,  1, 0.00f},   // 1  single voice, gentle
    {18.0f, 0.0f,  1, 0.20f},   // 2  deeper, a little feedback
    {10.0f, 6.0f,  2, 0.00f},   // 3  two voices apart
    {14.0f, 9.0f,  2, 0.15f},   // 4
    { 8.0f, 5.0f,  3, 0.00f},   // 5  three voices, ensemble
    {16.0f, 11.0f, 3, 0.10f},   // 6
    { 3.5f, 1.5f,  2, 0.45f},   // 7  short and resonant, flanger-ish
    { 2.0f, 0.8f,  2, 0.60f},   // 8
};

struct ChorusSpec {
    int type = 0;             // 0..7, panel 1..8
    float rate = 0.35f;       // 0..1
    float depth = 0.5f;       // 0..1
    float balance = 0.5f;     // 0..1, dry to wet
};

template <int kMaxDelay = 1536>
class Chorus {
public:
    void configure(const ChorusSpec& spec, float sr) {
        spec_ = spec;
        sr_ = sr;
        phase_ = 0.0f;
        // 0.098 .. 20 Hz per the specification sheet's "CHORUS LFO"
        inc_ = (0.098f * std::pow(20.0f / 0.098f, clamp01(spec.rate))) / sr;
        for (int i = 0; i < kMaxDelay; ++i) buf_[i] = 0.0f;
        write_ = 0;
    }

    // Changing the mix must not touch the delay line: turning a knob while a
    // chord rings should not restart the chorus.
    void set_balance(float b) { spec_.balance = clamp01(b); }

    float D5_HOT(process)(float x) {
        const ChorusType& t = kChorusTypes[clamp_index(spec_.type, 8)];
        float wet = 0.0f;
        for (int v = 0; v < t.voices; ++v) {
            const float ph = phase_ + static_cast<float>(v) / t.voices;
            const float lfo = fast_sin(ph);   // wraps on its own
            const float ms = t.base_ms + t.spread_ms * v +
                             clamp01(spec_.depth) * 4.0f * lfo;
            wet += read(ms * 0.001f * sr_);
        }
        wet /= static_cast<float>(t.voices);

        buf_[write_] = x + wet * t.feedback;
        if (++write_ >= kMaxDelay) write_ = 0;

        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        const float b = clamp01(spec_.balance);
        return x * (1.0f - b) + wet * b;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static int clamp_index(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

    float read(float delay_samples) const {
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        if (delay_samples > kMaxDelay - 2) delay_samples = kMaxDelay - 2;
        float pos = static_cast<float>(write_) - delay_samples;
        while (pos < 0.0f) pos += kMaxDelay;
        const int i0 = static_cast<int>(pos);
        const int i1 = (i0 + 1) % kMaxDelay;
        const float f = pos - static_cast<float>(i0);
        return buf_[i0] + (buf_[i1] - buf_[i0]) * f;
    }

    ChorusSpec spec_{};
    float sr_ = 32000.0f;
    float phase_ = 0.0f;
    float inc_ = 0.0f;
    float buf_[kMaxDelay] = {};
    int write_ = 0;
};

// ------------------------------------------------------------------ reverb

// One entry per panel reverb type. See the file header: these are plausible
// settings, not the chip's coefficients.
struct ReverbType {
    float decay;        // feedback of the comb bank, 0..1
    float damping;      // how fast the tail loses its highs
    float predelay_ms;
    float gate_ms;      // 0 = no gate; otherwise the tail is cut there
};

// One entry per panel reverb type. The decay column is calibrated against
// the reference recordings: three that share type 3 (Cathedral Organ,
// Staccato Heaven, Pizzagogo) all decay at -10..-13 dB/s, so type 3 -- the
// Large Hall -- carries T60 5.4 s; Soundtrack pins its Chapel, type 4, at
// 3.6 s; and type 9 sits at 1.6 s so Horn Section's recorded -37.8 dB/s
// tail reads as its reverb, which the numbers demand. The rest follows the
// type list's character around those anchors. Damping, predelay and the
// gated block's gates are the original values.
inline constexpr ReverbType kReverbTypes[32] = {
    {0.824f, 0.30f,   0.0f,   0.0f},   // 1: T60 1.0 s
    {0.941f, 0.30f,   8.0f,   0.0f},   // 2: T60 3.2 s
    {0.965f, 0.28f,  15.0f,   0.0f},   // 3: T60 5.4 s
    {0.948f, 0.26f,  22.0f,   0.0f},   // 4: T60 3.6 s
    {0.724f, 0.24f,  30.0f,   0.0f},   // 5: T60 0.6 s
    {0.807f, 0.22f,  38.0f,   0.0f},   // 6: T60 0.9 s
    {0.785f, 0.20f,  45.0f,   0.0f},   // 7: T60 0.8 s
    {0.862f, 0.18f,  55.0f,   0.0f},   // 8: T60 1.3 s
    {0.886f, 0.45f,   0.0f,   0.0f},   // 9: T60 1.6 s
    {0.886f, 0.42f,  10.0f,   0.0f},   // 10: T60 1.6 s
    {0.908f, 0.40f,  18.0f,   0.0f},   // 11: T60 2.0 s
    {0.923f, 0.38f,  26.0f,   0.0f},   // 12: T60 2.4 s
    {0.933f, 0.35f,  34.0f,   0.0f},   // 13: T60 2.8 s
    {0.941f, 0.33f,  42.0f,   0.0f},   // 14: T60 3.2 s
    {0.948f, 0.30f,  50.0f,   0.0f},   // 15: T60 3.6 s
    {0.955f, 0.28f,  60.0f,   0.0f},   // 16: T60 4.2 s
    {0.879f, 0.35f,   0.0f,  60.0f},   // 17: T60 1.5 s
    {0.892f, 0.33f,   5.0f,  90.0f},   // 18: T60 1.7 s
    {0.903f, 0.31f,  10.0f, 120.0f},   // 19: T60 1.9 s
    {0.912f, 0.29f,  15.0f, 160.0f},   // 20: T60 2.1 s
    {0.879f, 0.50f,  20.0f, 200.0f},   // 21: T60 1.5 s
    {0.892f, 0.48f,  25.0f, 260.0f},   // 22: T60 1.7 s
    {0.903f, 0.46f,  30.0f, 320.0f},   // 23: T60 1.9 s
    {0.912f, 0.44f,  35.0f, 400.0f},   // 24: T60 2.1 s
    {0.679f, 0.55f,   0.0f,   0.0f},   // 25: T60 0.5 s
    {0.759f, 0.52f,  12.0f,   0.0f},   // 26: T60 0.7 s
    {0.807f, 0.50f,  24.0f,   0.0f},   // 27: T60 0.9 s
    {0.839f, 0.48f,  36.0f,   0.0f},   // 28: T60 1.1 s
    {0.871f, 0.46f,  48.0f,   0.0f},   // 29: T60 1.4 s
    {0.898f, 0.44f,  60.0f,   0.0f},   // 30: T60 1.8 s
    {0.923f, 0.40f,  75.0f,   0.0f},   // 31: T60 2.4 s
    {0.968f, 0.15f,  90.0f,   0.0f},   // 32: T60 6.0 s
};

struct ReverbSpec {
    int type = 4;             // 0..31, panel 1..32
    float balance = 0.3f;     // 0..1, panel "Reverb Balance"
};

class Reverb {
public:
    void configure(const ReverbSpec& spec, float sr) {
        spec_ = spec;
        sr_ = sr;
        const ReverbType& t = kReverbTypes[clamp_index(spec.type, 32)];
        decay_ = t.decay;
        damping_ = t.damping;
        predelay_ = static_cast<int>(t.predelay_ms * 0.001f * sr);
        if (predelay_ >= kPre) predelay_ = kPre - 1;
        gate_ = static_cast<int>(t.gate_ms * 0.001f * sr);
        age_ = 0;
        for (int i = 0; i < kPre; ++i) pre_[i] = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < kComb[c]; ++i) comb_[c][i] = 0.0f;
            comb_i_[c] = 0;
            store_[c] = 0.0f;
        }
        for (int a = 0; a < 2; ++a) {
            for (int i = 0; i < kAll[a]; ++i) all_[a][i] = 0.0f;
            all_i_[a] = 0;
        }
        pre_i_ = 0;
    }

    void set_balance(float b) { spec_.balance = clamp01(b); }

    void note_activity() { age_ = 0; }      // a gate restarts with the note

    float D5_HOT(process)(float x) {
        pre_[pre_i_] = x;
        int r = pre_i_ - predelay_;
        if (r < 0) r += kPre;
        float in = pre_[r];
        if (++pre_i_ >= kPre) pre_i_ = 0;

        if (gate_ > 0) {
            if (age_ > gate_) in = 0.0f;
            ++age_;
        }

        float sum = 0.0f;
        for (int c = 0; c < 4; ++c) {
            const float y = comb_[c][comb_i_[c]];
            store_[c] = y * (1.0f - damping_) + store_[c] * damping_;
            comb_[c][comb_i_[c]] = in + store_[c] * decay_;
            if (++comb_i_[c] >= kComb[c]) comb_i_[c] = 0;
            sum += y;
        }
        sum *= 0.25f;

        for (int a = 0; a < 2; ++a) {
            const float y = all_[a][all_i_[a]];
            const float v = sum + y * 0.5f;
            all_[a][all_i_[a]] = v;
            if (++all_i_[a] >= kAll[a]) all_i_[a] = 0;
            sum = y - v * 0.5f;
        }

        const float b = clamp01(spec_.balance);
        return x * (1.0f - b) + sum * b;
    }

private:
    // Prime-ish lengths at 32 kHz, so the comb resonances do not line up.
    static constexpr int kComb[4] = {809, 863, 929, 983};
    static constexpr int kAll[2] = {401, 317};
    static constexpr int kPre = 3200;      // 100 ms of pre-delay

    static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static int clamp_index(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

    ReverbSpec spec_{};
    float sr_ = 32000.0f;
    float decay_ = 0.8f;
    float damping_ = 0.3f;
    int predelay_ = 0;
    int gate_ = 0;
    int age_ = 0;

    float pre_[kPre] = {};
    int pre_i_ = 0;
    float comb_[4][983] = {};
    int comb_i_[4] = {};
    float store_[4] = {};
    float all_[2][401] = {};
    int all_i_[2] = {};
};

}  // namespace d5
