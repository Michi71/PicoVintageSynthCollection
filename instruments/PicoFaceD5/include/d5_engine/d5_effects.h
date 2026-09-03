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
// What stands in for it is the reverb of its sister machine: the MT-32's
// board carries a Boss reverb chip (the RRV-10 family) whose data lines
// have been read out and modelled exactly (the munt project's
// BReverbModel, thanks to Lord_Nightmare, balrog and Mok, LGPL-2.1+). The
// topology below follows that model -- an entrance delay with damped
// injection, three series allpasses with half gain, three parallel combs
// carrying one-pole damping and per-time feedback, and left/right read at
// different tap positions inside the three loops, which is where the
// stereo field of these machines comes from. The thirty-two panel types of
// the D-50 map onto the Boss room, hall and plate cores plus a tapped
// delay line for the delay family; the calibrated T60 anchors from the
// reference recordings carry over. It remains a stand-in for the real
// chip, but now a stand-in with a Roland-era circuit in it.
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
        retune(spec, sr);
        low_.reset();
        high_.reset();
    }

    // New coefficients, same filter state: turning an EQ knob while a chord
    // rings must not restart the filters, which would step the output.
    void retune(const EqSpec& spec, float sr) {
        const int lf = clamp_index(spec.low_freq, 16);
        const int hf = clamp_index(spec.high_freq, 22);
        const int hq = clamp_index(spec.high_q, 9);
        low_.set_low_shelf(kLowEqFreq[lf], spec.low_gain_db, sr);
        high_.set_peaking(kHighEqFreq[hf], kHighEqQ[hq], spec.high_gain_db, sr);
        // A boost costs headroom: Roland's D-50 VST pulls the whole signal
        // down by half the boost (+12 dB low shelf: shelf +5.5, the rest
        // -6.2; +6: +2.5 and -3.5), cuts leave the rest alone. Shape,
        // corner frequencies and Q match ours without it.
        const float boost = (spec.low_gain_db > 0.0f ? spec.low_gain_db : 0.0f)
                          + (spec.high_gain_db > 0.0f ? spec.high_gain_db : 0.0f);
        makeup_ = std::pow(10.0f, -0.5f * boost / 20.0f);
    }

    float D5_HOT(process)(float x) { return makeup_ * high_.process(low_.process(x)); }

private:
    float makeup_ = 1.0f;
    static int clamp_index(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }
    Biquad low_{};
    Biquad high_{};
};

// ------------------------------------------------------------------ chorus

// The eight panel types by the Owner's Manual's names (p. 29): Chorus 1,
// Chorus 2, Flanger 1, Flanger 2, Feedback Chorus, Tremolo, Chorus Tremolo,
// Dimension. Two of them are measured on Roland's D-50 VST with one
// sawtooth partial, chorus balance 100 (wet only), reverb off (03.09.2026):
// type 1 is a single modulated delay whose two outputs sweep in opposite
// directions and are each other's inverse (depth 0: L = -R exactly), and
// type 6 is a stereo tremolo with no pitch modulation at all, its two
// sides in counter-phase, about 15 dB deep at panel depth 50 and running
// at twice the chorus LFO. The base delays, spreads, feedbacks and voice
// counts of the other six are still by name only.
struct ChorusType {
    float base_ms;
    float spread_ms;
    int voices;          // modulated delay reads; 0 = no delay at all
    float feedback;
    bool tremolo;        // amplitude modulation on top (types 6 and 7)
};

inline constexpr ChorusType kChorusTypes[8] = {
    {12.0f, 0.0f,  1, 0.00f, false},   // 1  Chorus 1 (measured)
    {18.0f, 0.0f,  1, 0.20f, false},   // 2  Chorus 2
    { 2.0f, 0.0f,  1, 0.50f, false},   // 3  Flanger 1
    { 3.5f, 0.0f,  1, 0.70f, false},   // 4  Flanger 2
    {12.0f, 0.0f,  1, 0.45f, false},   // 5  Feedback Chorus
    { 0.0f, 0.0f,  0, 0.00f, true },   // 6  Tremolo (measured)
    {12.0f, 0.0f,  1, 0.00f, true },   // 7  Chorus Tremolo
    { 8.0f, 5.0f,  2, 0.00f, false},   // 8  Dimension
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
        inc_ = rate_hz(spec.rate) / sr;
        for (int i = 0; i < kMaxDelay; ++i) buf_[i] = 0.0f;
        write_ = 0;
    }

    // Changing the mix must not touch the delay line: turning a knob while a
    // chord rings should not restart the chorus.
    void set_balance(float b) { spec_.balance = clamp01(b); }
    // Type and depth are read per sample, the rate only sets the LFO step:
    // none of them needs the delay line cleared, so a knob turn is silent.
    void set_type(int t) { spec_.type = t < 0 ? 0 : (t > 7 ? 7 : t); }
    void set_depth(float d) { spec_.depth = clamp01(d); }
    void set_rate(float r) {
        spec_.rate = clamp01(r);
        inc_ = rate_hz(spec_.rate) / sr_;
    }

    // Panel rate to Hz, from the VST's wet pitch modulation: 1.3-1.6 Hz at
    // panel 50, 7.4 Hz at 100, about half a hertz at 0 -- an exponential of
    // 0.28 Hz to 7.4 Hz. The specification sheet's 0.098-20 Hz that stood
    // here was the chip's range, not the panel's.
    static float rate_hz(float r) { return 0.28f * std::pow(26.0f, clamp01(r)); }
    // Panel depth to the delay swing, linear: +-2.4 ms at 100, +-1.1 to
    // 2.1 at 50 on the VST (rms of the wet's pitch track with the quiet
    // moments dropped -- percentiles of that track count the spikes where
    // the sweeping wet cancels, and read four times too much; that first
    // reading turned Arco Strings into a swarm of bees). Through the
    // pitch-mod depth curve it was 0.125 ms at 50.
    static constexpr float kSwingMs = 2.4f;

    // Mono is the L/MONO jack of the real unit: dry + wet, exactly what the
    // left side carries. Averaging l and r would cancel the anti-phase wet
    // and the chorus would vanish from every mono path.
    float D5_HOT(process)(float x) {
        float l, r;
        process(x, l, r);
        return l;
    }

    // Stereo the way the Roland effects of the era do it, and the way the
    // VST measures: the left side takes dry + wet, the right side dry minus
    // a second wet whose delay sweeps the other way. At depth 0 the two
    // wets coincide and the sides are exact inverses (VST: correlation
    // -1.00); with depth the sweeps pull them apart. The reverb no longer
    // sees this pair -- it has its own mono send (Reverb::process) -- so
    // the inversion can be exact without starving the room.
    void D5_HOT(process)(float x, float& l, float& r) {
        const ChorusType& t = kChorusTypes[clamp_index(spec_.type, 8)];
        const float d = clamp01(spec_.depth);
        float wl = 0.0f, wr = 0.0f;
        if (t.voices == 0) {
            wl = x;
            wr = x;
        } else {
            const float swing = d * kSwingMs;
            for (int v = 0; v < t.voices; ++v) {
                const float ph = phase_ + static_cast<float>(v) / t.voices;   // wraps on its own
                const float ms = t.base_ms + t.spread_ms * v;
                // The right side sweeps 0.3 of a turn behind the left, not
                // half: the VST's two pitch tracks correlate at -0.2 to -0.6
                // across depths and rates, a pure counter-sweep would sit
                // at -1 and coincide with the left twice per cycle.
                wl += read((ms + swing * fast_sin(ph)) * 0.001f * sr_);
                wr += read((ms + swing * fast_sin(ph + 0.3f)) * 0.001f * sr_);
            }
            wl /= static_cast<float>(t.voices);
            wr /= static_cast<float>(t.voices);
        }
        if (t.tremolo) {
            // Counter-phased amplitude modulation at twice the LFO rate
            // (VST type 6: 2.65 Hz at panel rate 50 against 1.26 Hz of
            // pitch sweep on type 1), about 14 dB deep at panel depth 50.
            const float m = d * 1.6f > 1.0f ? 1.0f : d * 1.6f;
            const float sq = fast_sin(2.0f * phase_);
            wl *= 1.0f - m * 0.5f * (1.0f + sq);
            wr *= 1.0f - m * 0.5f * (1.0f - sq);
        }

        buf_[write_] = x + wl * t.feedback;
        if (++write_ >= kMaxDelay) write_ = 0;

        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        const float b = clamp01(spec_.balance);
        const float dry = x * (1.0f - b);
        l = dry + wl * b;
        r = dry - wr * b;
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
        // A hair below zero rounds UP to exactly kMaxDelay in float32 (the
        // ulp at 1536 is ~1.2e-4, wider than the negative numbers that
        // land here), and pos == kMaxDelay would index one slot past the
        // ring -- a single garbage sample, audible as a pop. Wrap it back.
        if (pos >= kMaxDelay) pos -= kMaxDelay;
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

// The thirty-two panel types, mapped onto the Boss core of the sister
// machine (see the file header) plus a tapped delay line for the delay
// family. Type names follow the panel list; rows are zero-based (pb30),
// the panel shows row + 1. Decay times are calibrated on Roland's D-50
// VST (one note, reverb balance 100, per type), as the slope of the late
// tail 0.8 to 2.6 s after the note -- the same metric on both sides; an
// energy-decay fit from the first 100 ms reads 30 % shorter on this
// core: Small Hall 1.6 s, Medium Hall 2.9, Large Hall 3.2, Chapel 6.4,
// Medium Large Room 2.0, Large Room 2.1 (its sides inverted, correlation
// -0.95); the VST's tail decays alike in every band; the short rooms and the
// gates hide under the source's own 55 dB/s release there, so they only
// carry an upper bound of about a second. The delays showed the direct
// copy on the right and the delayed tap on the left, at 0.35..0.5 of it,
// Delay 248 with the left side inverted; the feedback follows the measured
// tails (2.7 s at 248 ms, 2.4 s at 252 ms). The Boss loop is stable only
// below fb 0.625 (DC loop gain fb/(1 - 0.375)); the wet column holds the
// steady-state level of the wet part near -3 dB re the send at balance 1,
// the delays' right side included.
struct ReverbType {
    int mode;               // 0 room, 1 hall, 2 plate, 3 tapped delay
    int time;               // Boss feedback index 0..7 (modes 0..2)
    float wet;              // steady-state level normalization
    float tap_l_ms;         // mode 3: echo positions
    float tap_r_ms;
    float fb;               // mode 3: feedback of the trailing echo
    float gate_ms;          // >0: wet is cut this long after the note starts
    float reverse_ms;       // >0: wet rises over this window, then cuts
    float fb_ov;            // >0, modes 0..2: fine feedback override. The Boss
                            // chip quantizes time to eight steps; the D-50
                            // decay anchors (measured tail dB/s of the
                            // reference recordings) land between them, so
                            // these interpolate while keeping the geometry.
    float echo = 1.0f;      // mode 3: gain of the left (delayed) tap, sign =
                            // its polarity; the right tap is the direct copy
    float pol_r = 1.0f;     // polarity of the right wet side (-1 inverts)
};

inline constexpr ReverbType kReverbTypes[32] = {
    {1, 1, 1.23f,   0,   0, 0.0f,   0.0f,   0.0f, 0.3550f}, //  1 Small Hall   (VST late tail 1.6 s)
    {1, 6, 0.85f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4680f}, //  2 Medium Hall  (VST late tail 2.9 s)
    {1, 5, 0.89f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4830f}, //  3 Large Hall   (VST late tail 3.2 s)
    {1, 5, 0.95f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5620f}, //  4 Chapel       (VST late tail 6.4 s)
    {0, 0, 1.26f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  5 Box
    {2, 2, 1.02f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  6 Small Metal Room (plate ring)
    {0, 1, 1.20f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  7 Small Room   (T60 0.9)
    {0, 3, 1.00f,   0,   0, 0.0f,   0.0f,   0.0f, 0.3000f}, //  8 Medium Room  (VST: under the 55 dB/s source; 1.0 s)
    {1, 2, 1.15f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4000f}, //  9 Medium Large Room (VST late tail 2.0 s)
    {0, 3, 0.92f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4450f, 1.0f, -1.0f}, // 10 Large Room (VST late tail 2.1 s, sides inverted, corr -0.95)
    {3, 0, 1.20f, 102,   0, 0.50f,  0.0f,   0.0f, 0.0000f, -0.5f}, // 11 Single Delay (102 ms; as 20)
    {3, 0, 1.20f, 180,   0, 0.30f,  0.0f,   0.0f, 0.0000f,  0.35f}, // 12 Cross Delay (180 ms; VST: right direct, left 180, tail under the source)
    {3, 0, 1.20f, 224,   0, 0.40f,  0.0f,   0.0f, 0.0000f,  0.40f}, // 13 Cross Delay (224 ms; as 23)
    {3, 0, 1.20f, 148,   0, 0.40f,  0.0f,   0.0f, 0.0000f,  0.40f}, // 14 Cross Delay (148 ms; as 23)
    {1, 3, 1.05f,   0,   0, 0.0f, 200.0f,   0.0f, 0.0000f}, // 15 Short Gate (200 ms)
    {1, 3, 1.05f,   0,   0, 0.0f, 480.0f,   0.0f, 0.0000f}, // 16 Long Gate (480 ms)
    {1, 5, 0.89f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5000f}, // 17 Bright Hall (2.7 s, easier damping)
    {1, 6, 0.80f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4600f}, // 18 Large Cave (dark: loop damping x1.25, T60 4.4; fb < 0.53 or it rings)
    {2, 5, 0.77f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, // 19 Steel Pan (plate, metallic)
    {3, 0, 1.20f, 248,   0, 0.53f,  0.0f,   0.0f, 0.0000f, -0.5f}, // 20 Delay (248 ms; VST: right direct, left inverted, T60 2.7)
    {3, 0, 1.20f, 338,   0, 0.53f,  0.0f,   0.0f, 0.0000f, -0.5f}, // 21 Delay (338 ms; as 20)
    {3, 0, 1.20f, 157,   0, 0.40f,  0.0f,   0.0f, 0.0000f,  0.40f}, // 22 Cross Delay (157 ms; as 23)
    {3, 0, 1.20f, 252,   0, 0.48f,  0.0f,   0.0f, 0.0000f,  0.45f}, // 23 Cross Delay (252 ms; VST: right direct, left 252 in phase, T60 2.4)
    {3, 0, 1.20f, 274,   0, 0.40f,  0.0f,   0.0f, 0.0000f,  0.40f}, // 24 Cross Delay (274 ms; as 23)
    {1, 4, 0.98f,   0,   0, 0.0f, 300.0f,   0.0f, 0.0000f}, // 25 Gate Reverb
    {1, 4, 0.98f,   0,   0, 0.0f,   0.0f, 360.0f, 0.0000f}, // 26 Reverse Gate (360 ms)
    {1, 4, 0.98f,   0,   0, 0.0f,   0.0f, 480.0f, 0.0000f}, // 27 Reverse Gate (480 ms)
    {3, 0, 1.20f,  80,   0, 0.00f,  0.0f,   0.0f, 0.0000f,  0.50f}, // 28 Slap Back (short)
    {3, 0, 1.20f, 160,   0, 0.00f,  0.0f,   0.0f, 0.0000f,  0.50f}, // 29 Slap Back (mid)
    {3, 0, 1.20f, 240,   0, 0.00f,  0.0f,   0.0f, 0.0000f,  0.50f}, // 30 Slap Back (long)
    {1, 7, 0.65f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, // 31 Twisted Space (T60 ~14 s)
    {1, 6, 0.84f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5600f}, // 32 Space (6.2 s)
};

struct ReverbSpec {
    int type = 4;             // 0..31, panel 1..32
    float balance = 0.3f;     // 0..1, panel "Reverb Balance"
};

// Delay-line slices carved from one pool, so the tapped-delay types can use
// the same memory as the comb banks (only one mode is ever live).
class ReverbLine {
public:
    void bind(float* buf, int size) { buf_ = buf; size_ = size; i_ = 0; }
    void clear() { for (int i = 0; i < size_; ++i) buf_[i] = 0.0f; i_ = 0; }
    float next() {
        if (++i_ >= size_) i_ = 0;
        return buf_[i_];
    }
    float at(int pos) const {
        int j = i_ - pos;
        if (j < 0) j += size_;
        return buf_[j];
    }
    float* buf_ = nullptr;
    int size_ = 0;
    int i_ = 0;
};

// Boss allpass, both gains one half: store in - out/2, emit out + stored/2.
class ReverbAllpass {
public:
    void bind(float* buf, int size) { line_.bind(buf, size); line_.clear(); }
    float process(float in) {
        const float out = line_.next();
        line_.buf_[line_.i_] = in - 0.5f * out;
        return out + 0.5f * line_.buf_[line_.i_];
    }
private:
    ReverbLine line_;
};

// Boss comb: one-pole damped feedback loop; the output taps can read any
// position, and the two channels read different ones.
class ReverbComb {
public:
    void bind(float* buf, int size, float filter, float feedback) {
        line_.bind(buf, size);
        line_.clear();
        filt_ = filter;
        fb_ = feedback;
    }
    void process(float in) {
        const float last = line_.buf_[line_.i_];
        const float filter_in = in + fb_ * line_.next();
        line_.buf_[line_.i_] = filt_ * last - filter_in;
    }
    float out_at(int pos) const { return line_.at(pos); }
private:
    ReverbLine line_;
    float filt_ = 0.375f, fb_ = 0.5f;
};

// Entrance: delay with a damped injection (what the chip does with one of
// its combs).
class ReverbEntrance {
public:
    void bind(float* buf, int size, float filter, float amp) {
        line_.bind(buf, size);
        line_.clear();
        filt_ = filter;
        amp_ = amp;
    }
    void process(float in) {
        const float last = line_.buf_[line_.i_];
        line_.next();
        line_.buf_[line_.i_] = amp_ * (filt_ * last + in);
    }
    float out_at(int pos) const { return line_.at(pos); }
private:
    ReverbLine line_;
    float filt_ = 0.5f, amp_ = 0.375f;
};

// CM-32L / LAPC-I geometry, the newer revision of the Boss chip: three
// modes (0 room, 1 hall, 2 plate), feedback by time index 0..7.
struct BossMode {
    int all_sizes[3];
    int comb_sizes[4];        // entrance + three tail loops
    int out_l[3];
    int out_r[3];
    uint8_t filter[4];        // per loop, /256
    uint8_t feedback[8];      // by time index (same for the three loops)
    uint8_t lpf_amp;
};

inline const BossMode& boss_mode(int mode) {
    static constexpr BossMode kModes[3] = {
        {{994, 729, 78}, {706, 2349, 2839, 3632}, {2349, 141, 1960},
         {1174, 1570, 145}, {0xA0, 0x60, 0x60, 0x60},
         {0x28, 0x48, 0x60, 0x78, 0x80, 0x88, 0x90, 0x98}, 0x60},
        {{1324, 809, 176}, {962, 2619, 3545, 4519}, {2618, 1760, 4518},
         {1300, 3532, 2274}, {0x80, 0x60, 0x60, 0x60},
         {0x28, 0x48, 0x60, 0x70, 0x78, 0x80, 0x90, 0x98}, 0x60},
        {{969, 644, 157}, {117, 2259, 2839, 3539}, {2259, 718, 1769},
         {1136, 2128, 1}, {0, 0x20, 0x20, 0x20},
         {0x30, 0x58, 0x78, 0x88, 0xA0, 0xB8, 0xC0, 0xD0}, 0x80},
    };
    return kModes[(mode < 0 || mode > 2) ? 1 : mode];
}

class Reverb {
public:
    void configure(const ReverbSpec& spec, float sr) {
        spec_ = spec;
        sr_ = sr;
        const ReverbType& t = kReverbTypes[clamp_index(spec.type, 32)];
        wet_ = t.wet;
        echo_ = t.echo;
        pol_r_ = t.pol_r;
        mode_ = t.mode < 3 ? t.mode : 3;
        age_ = 0;
        follow_ = 0.0f;
        hold_ = 0;
        gain_ = 0.0f;
        gate_ = static_cast<int>(t.gate_ms * 0.001f * sr);
        reverse_ = static_cast<int>(t.reverse_ms * 0.001f * sr);
        if (mode_ < 3) {
            const BossMode& m = boss_mode(mode_);
            // Panel types 17 and 18 (rows 16/17) tune the damping: the bright
            // hall eases both, the cave tightens the loop. Rows, not panel
            // numbers: the swapped comparison once gave Bright Hall the
            // cave's damping, and it rang at 140 Hz forever.
            const float filt_scale = spec.type == 16 ? 0.85f
                                   : (spec.type == 17 ? 1.25f : 1.0f);
            const float lpf = (spec.type == 16 ? 0x80 : m.lpf_amp) / 256.0f;
            const float scale = sr / 32000.0f;  // geometry is 32 kHz native
            for (int a = 0; a < 3; ++a) {
                ap_[a].bind(pool_ + 1950 * a,
                            static_cast<int>(m.all_sizes[a] * scale));
            }
            entr_.bind(pool_ + 5850, static_cast<int>(m.comb_sizes[0] * scale),
                       m.filter[0] / 256.0f, lpf);
            const float fb = t.fb_ov > 0.0f
                ? t.fb_ov
                : m.feedback[t.time < 0 ? 0 : (t.time > 7 ? 7 : t.time)] / 256.0f;
            for (int c = 0; c < 3; ++c) {
                comb_[c].bind(pool_ + 6900 + 4600 * c,
                              static_cast<int>(m.comb_sizes[1 + c] * scale),
                              m.filter[1 + c] / 256.0f * filt_scale, fb);
            }
            boss_ = &m;
            sr_scale_ = scale;
        } else {
            // at(1) is the word just written: a 0 ms tap is the direct copy
            tap_l_ = static_cast<int>(t.tap_l_ms * 0.001f * sr) + 1;
            tap_r_ = static_cast<int>(t.tap_r_ms * 0.001f * sr) + 1;
            tap_fb_ = t.fb;
            tap_line_.bind(pool_, 16200);
            tap_line_.clear();
        }
    }

    void set_balance(float b) { spec_.balance = clamp01(b); }

    // Rebind the three tail loops with another feedback, geometry and
    // damping unchanged. Calibration and tests only: it clears the loops.
    void set_feedback(float fb, float damp_scale = 1.0f) {
        if (mode_ >= 3) return;
        const BossMode& m = *boss_;
        const float filt_scale = (spec_.type == 16 ? 0.85f : (spec_.type == 17 ? 1.25f : 1.0f)) * damp_scale;
        for (int c = 0; c < 3; ++c) {
            comb_[c].bind(pool_ + 6900 + 4600 * c,
                          static_cast<int>(m.comb_sizes[1 + c] * sr_scale_),
                          m.filter[1 + c] / 256.0f * filt_scale, fb);
        }
    }

    void note_activity() { age_ = 0; }      // a gate restarts with the note

    float D5_HOT(process)(float x) {
        float l, r;
        process(x, x, x, l, r);
        return l;
    }

    // The chip folds its stereo input to mono and builds the field of the
    // reverb from tap positions; the dry side passes straight through, so
    // the chorus width of the tones lives in the dry part of the mix.
    // `send` feeds the room (the tones' L/MONO signals, dry + chorus wet,
    // summed by the patch); xl and xr are what passes to the outputs dry.
    // The Boss chip folds to mono at its input, and so does this -- but
    // from a send that cannot cancel, not from the stereo pair, whose
    // chorus halves are exact inverses now.
    void D5_HOT(process)(float send, float xl, float xr, float& l, float& r) {
        float wl, wr;
        // 0.316: the VST's wet-only sits 7.5 dB under the dry alone (Medium
        // Hall on a held C4, Arco Upper P1), which 0.595 reproduced -- at
        // 262 Hz. Three combs at a loop gain near 0.75 give this core a
        // steady-state gain that swings 22 dB between neighbouring
        // frequencies (sines 50..1000 Hz: mean -2.4 dB, 262 Hz -7.9), so a
        // one-note calibration lands on a dip; the send is set 5.5 dB lower
        // to put the mean where the VST's C4 sits. A sweep of the loop
        // length by a few samples did nothing to the ripple (the modes move
        // 0.3 %); smoothing it would take tens of samples, audible chorusing.
        const float x = 0.316f * send;
        if (mode_ < 3) {
            const BossMode& m = *boss_;
            entr_.process(x);
            float link = entr_.out_at(static_cast<int>(m.comb_sizes[0] * sr_scale_) - 1);
            link = ap_[0].process(link);
            link = ap_[1].process(link);
            const float early_r = link;
            link = ap_[2].process(link);
            const float out_l1 = comb_[0].out_at(static_cast<int>(m.out_l[0] * sr_scale_) - 1);
            for (int c = 0; c < 3; ++c) comb_[c].process(link);
            // Early part plus tail. Roland's D-50 VST puts most of the wet
            // energy into the first tens of milliseconds and keeps the
            // diffuse tail some 20 dB below it (Medium Hall on a held Arco
            // note: the wet follows the dry's own release for 0.4 s before
            // the 20 dB/s tail shows; a plucked Jazz Guitar Duo leaves a
            // tail 28 dB under its peak). The Boss core alone is all tail
            // -- normalized to the same steady level it rang 15-20 dB too
            // loud after every note. The early signal is the diffused
            // input: after three allpasses on the left, two on the right.
            wl = kTailMix * (1.5f * (out_l1 + comb_[1].out_at(static_cast<int>(m.out_l[1] * sr_scale_)))
                             + comb_[2].out_at(static_cast<int>(m.out_l[2] * sr_scale_)))
               + kEarlyMix * link;
            wr = kTailMix * (1.5f * (comb_[0].out_at(static_cast<int>(m.out_r[0] * sr_scale_))
                                     + comb_[1].out_at(static_cast<int>(m.out_r[1] * sr_scale_)))
                             + comb_[2].out_at(static_cast<int>(m.out_r[2] * sr_scale_)))
               + kEarlyMix * early_r;
            // A type with an inverted right side sends the same wet to both
            // outputs (the VST's Large Room: L/R correlation -0.95); the
            // chip's own tap pairs are decorrelated and would stay so.
            if (pol_r_ < 0.0f) wr = wl;
        } else {
            // Tapped delay: one line, two read positions, the left tap feeds
            // back so cross delays alternate sides.
            tap_line_.buf_[tap_line_.i_] = x + tap_fb_ * tap_line_.at(tap_l_);
            tap_line_.next();
            wl = echo_ * tap_line_.at(tap_l_);
            wr = tap_line_.at(tap_r_);
        }

        // Gates work on the wet part, the way a gated reverb does: Short
        // and Long Gate hold full level and then cut; the reverse gates
        // rise over their window and cut at its end. The cut is a 5 ms
        // close, not a switch: the analog gate has a closing time, and a
        // one-sample cut of a sounding tail reads as a pop.
        float g = 1.0f;
        if (gate_ > 0) {
            // The gate follows the send, not the note: a gated reverb opens
            // while its input is above the threshold and closes the gate
            // time after it fell below. Intruder FX is the proof -- Long
            // Gate at balance 100 (no dry at all) under an Upper tone that
            // swells in over seconds and releases over more: cut 480 ms
            // after the note starts, the patch was a half-second blip and
            // then digital silence under the held key. Followed, the tail
            // rides through, and the release is cut 480 ms after it sinks
            // under the threshold -- the tone after the key that the VST
            // plays. Threshold and hold are PLAUSIBLE, not measured.
            const float a = x < 0.0f ? -x : x;
            follow_ = a > follow_ ? a : follow_ * kFollowDecay;
            if (follow_ > kGateThreshold) {
                hold_ = gate_;
            } else if (hold_ > 0) {
                --hold_;
            }
            const float step = 1.0f / (sr_ * 0.005f);   // 5 ms ramps
            const float target = hold_ > 0 ? 1.0f : 0.0f;
            gain_ += gain_ < target ? (target - gain_ < step ? target - gain_ : step)
                                    : (gain_ - target < step ? target - gain_ : -step);
            g = gain_;
        } else if (reverse_ > 0) {
            const int fade = static_cast<int>(sr_ * 0.005f);
            g = age_ < reverse_ ? static_cast<float>(age_) / reverse_
                : (age_ < reverse_ + fade
                       ? static_cast<float>(reverse_ + fade - age_) / fade
                       : 0.0f);
            ++age_;
        }

        // The D-50's crossfade (EPROM page 2, the send rows at 0xB64C with
        // R6 from the balance): below 50 the wet rises linearly and the dry
        // stays, above 50 the dry falls linearly and the wet stays. Roland's
        // D-50 VST measures the same, Medium Hall on one note: wet -6.8 dB
        // at 25 re 50, dry -7 dB at 75 re 0, both flat on their other half.
        // The old x^1.8 amount curve on a 1-b/b mix was 15 dB short of wet
        // at 25 and 50 -- the bank's median balance is 40.
        const float b = clamp01(spec_.balance);
        const float dry = b > 0.5f ? 2.0f * (1.0f - b) : 1.0f;
        const float wet = b < 0.5f ? 2.0f * b : 1.0f;
        l = xl * dry + wl * wet * wet_ * g;
        r = xr * dry + pol_r_ * wr * wet * wet_ * g;
    }

private:
    static constexpr int kPool = 21000;  // floats: 3x1950 allpass, 1050 entrance, 3x4600 combs (up to 20700), or 16.2k tap line
#ifndef D5_REV_EARLY
#define D5_REV_EARLY 2.2f
#endif
#ifndef D5_REV_TAIL
#define D5_REV_TAIL 0.6f
#endif
    static constexpr float kEarlyMix = D5_REV_EARLY;
    static constexpr float kTailMix = D5_REV_TAIL;

    static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static int clamp_index(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

    ReverbSpec spec_{};
    float sr_ = 32000.0f;
    float sr_scale_ = 1.0f;
    int mode_ = 1;
    float wet_ = 1.0f;
    int gate_ = 0;
    int reverse_ = 0;
    int age_ = 0;
    // Gate follower state: rectified send with a 30 ms decay, the hold
    // countdown, and the ramped gain.
    static constexpr float kGateThreshold = 1e-5f;     // -100 dBFS on x: the VST's Long Gate cuts Intruder FX only at -106 dB, i.e. when the input is gone
    static constexpr float kFollowDecay = 0.99896f;    // exp(-1/(0.03*32000))
    float follow_ = 0.0f;
    int hold_ = 0;
    float gain_ = 0.0f;

    float pool_[kPool] = {};
    ReverbAllpass ap_[3];
    ReverbEntrance entr_;
    ReverbComb comb_[3];
    const BossMode* boss_ = &boss_mode(1);
    ReverbLine tap_line_;
    int tap_l_ = 0, tap_r_ = 0;
    float tap_fb_ = 0.0f;
    float echo_ = 1.0f;
    float pol_r_ = 1.0f;
};

}  // namespace d5
