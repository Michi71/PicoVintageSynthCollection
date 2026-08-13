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

    // Mono is the L/MONO jack of the real unit: dry + wet, exactly what the
    // left side carries. Averaging l and r would cancel the anti-phase wet
    // and the chorus would vanish from every mono path.
    float D5_HOT(process)(float x) {
        float l, r;
        process(x, l, r);
        return l;
    }

    // Stereo the way the Roland effects of the era do it: left gets
    // dry + wet, right gets dry - wet. The same modulated wet, inverted --
    // even a whisper of depth then opens the field around a steady center,
    // which is the chorus width the instrument is known for. The mono path
    // above takes the left side (dry + wet), identical to the fold a L/MONO
    // jack hears on the real unit.
    void D5_HOT(process)(float x, float& l, float& r) {
        const ChorusType& t = kChorusTypes[clamp_index(spec_.type, 8)];
        float wet = 0.0f;
        for (int v = 0; v < t.voices; ++v) {
            const float ph = phase_ + static_cast<float>(v) / t.voices;
            const float ms = t.base_ms + t.spread_ms * v +
                             clamp01(spec_.depth) * 4.0f * fast_sin(ph);   // wraps on its own
            wet += read(ms * 0.001f * sr_);
        }
        wet /= static_cast<float>(t.voices);

        buf_[write_] = x + wet * t.feedback;
        if (++write_ >= kMaxDelay) write_ = 0;

        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        const float b = clamp01(spec_.balance);
        const float dry = x * (1.0f - b);
        l = dry + wet * b;
        r = dry - wet * b;
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

// The thirty-two panel types, mapped onto the Boss core of the sister
// machine (see the file header) plus a tapped delay line for the delay
// family. Type names follow the panel list; the T60 anchors measured from
// the reference recordings (type 3 Large Hall 5.4 s, type 4 Chapel 3.6 s,
// type 2 Medium Hall 3.2 s, type 9 1.6 s, type 32 6.0 s) pick the time
// index inside each mode. The wet column normalizes the steady-state wet
// level to a constant across time settings (measured on the model: -13.0
// to -6.5 dBFS over time 0..7, normalized to -11).
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
};

inline constexpr ReverbType kReverbTypes[32] = {
    {1, 1, 1.23f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  1 Small Hall
    {1, 6, 0.85f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5469f}, //  2 Medium Hall  (anchor -10.5 dB/s, fb 8C)
    {1, 5, 0.89f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5312f}, //  3 Large Hall   (anchor -12 dB/s, fb 88)
    {1, 5, 0.95f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5078f}, //  4 Chapel       (T60 3.6, anchor 3.6, fb 82)
    {0, 0, 1.26f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  5 Box
    {2, 2, 1.02f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  6 Small Metal Room (plate ring)
    {0, 1, 1.20f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  7 Small Room   (T60 0.9)
    {0, 3, 1.00f,   0,   0, 0.0f,   0.0f,   0.0f, 0.4609f}, //  8 Medium Room  (anchor -28 dB/s, fb 76)
    {1, 2, 1.15f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, //  9 Medium Large Room (T60 1.65, anchor 1.6)
    {0, 3, 0.92f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5000f}, // 10 Large Room   (T60 2.8, fb 80)
    {3, 0, 2.50f, 102, 102, 0.20f,  0.0f,   0.0f, 0.0000f}, // 11 Single Delay (102 ms)
    {3, 0, 2.50f, 180, 360, 0.30f,  0.0f,   0.0f, 0.0000f}, // 12 Cross Delay (180 ms)
    {3, 0, 2.50f, 224, 448, 0.30f,  0.0f,   0.0f, 0.0000f}, // 13 Cross Delay (224 ms)
    {3, 0, 2.50f, 148, 296, 0.30f,  0.0f,   0.0f, 0.0000f}, // 14 Cross Delay (148/296 ms)
    {1, 3, 1.05f,   0,   0, 0.0f, 200.0f,   0.0f, 0.0000f}, // 15 Short Gate (200 ms)
    {1, 3, 1.05f,   0,   0, 0.0f, 480.0f,   0.0f, 0.0000f}, // 16 Long Gate (480 ms)
    {1, 5, 0.89f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5312f}, // 17 Bright Hall (brighter injection, fb 88)
    {1, 6, 0.80f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, // 18 Large Cave (dark, long)
    {2, 5, 0.77f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, // 19 Steel Pan (plate, metallic)
    {3, 0, 2.50f, 248, 248, 0.25f,  0.0f,   0.0f, 0.0000f}, // 20 Delay (248 ms)
    {3, 0, 2.50f, 338, 338, 0.25f,  0.0f,   0.0f, 0.0000f}, // 21 Delay (338 ms)
    {3, 0, 2.50f, 157, 314, 0.30f,  0.0f,   0.0f, 0.0000f}, // 22 Cross Delay (157 ms)
    {3, 0, 2.50f, 252, 504, 0.30f,  0.0f,   0.0f, 0.0000f}, // 23 Cross Delay (252 ms)
    {3, 0, 2.50f, 274, 137, 0.30f,  0.0f,   0.0f, 0.0000f}, // 24 Cross Delay (274/137 ms)
    {1, 4, 0.98f,   0,   0, 0.0f, 300.0f,   0.0f, 0.0000f}, // 25 Gate Reverb
    {1, 4, 0.98f,   0,   0, 0.0f,   0.0f, 360.0f, 0.0000f}, // 26 Reverse Gate (360 ms)
    {1, 4, 0.98f,   0,   0, 0.0f,   0.0f, 480.0f, 0.0000f}, // 27 Reverse Gate (480 ms)
    {3, 0, 2.50f,  80,  80, 0.00f,  0.0f,   0.0f, 0.0000f}, // 28 Slap Back (short)
    {3, 0, 2.50f, 160, 160, 0.00f,  0.0f,   0.0f, 0.0000f}, // 29 Slap Back (mid)
    {3, 0, 2.50f, 240, 240, 0.00f,  0.0f,   0.0f, 0.0000f}, // 30 Slap Back (long)
    {1, 7, 0.65f,   0,   0, 0.0f,   0.0f,   0.0f, 0.0000f}, // 31 Twisted Space (T60 ~14 s)
    {1, 6, 0.84f,   0,   0, 0.0f,   0.0f,   0.0f, 0.5508f}, // 32 Space (T60 ~6.0, anchor 6.0, fb 8D)
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
        mode_ = t.mode < 3 ? t.mode : 3;
        age_ = 0;
        gate_ = static_cast<int>(t.gate_ms * 0.001f * sr);
        reverse_ = static_cast<int>(t.reverse_ms * 0.001f * sr);
        if (mode_ < 3) {
            const BossMode& m = boss_mode(mode_);
            // Types 17 and 18 tune the injection: the bright hall eases the
            // entrance damping, the cave tightens the loop damping.
            const float filt_scale = spec.type == 17 ? 0.85f
                                   : (spec.type == 16 ? 1.25f : 1.0f);
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
            tap_l_ = static_cast<int>(t.tap_l_ms * 0.001f * sr);
            tap_r_ = static_cast<int>(t.tap_r_ms * 0.001f * sr);
            tap_fb_ = t.fb;
            tap_line_.bind(pool_, 16200);
            tap_line_.clear();
        }
    }

    void set_balance(float b) { spec_.balance = clamp01(b); }

    void note_activity() { age_ = 0; }      // a gate restarts with the note

    float D5_HOT(process)(float x) {
        float l, r;
        process(x, x, l, r);
        return l;
    }

    // The chip folds its stereo input to mono and builds the field of the
    // reverb from tap positions; the dry side passes straight through, so
    // the chorus width of the tones lives in the dry part of the mix.
    void D5_HOT(process)(float xl, float xr, float& l, float& r) {
        float wl, wr;
        const float x = 0.25f * (xl + xr);
        if (mode_ < 3) {
            const BossMode& m = *boss_;
            entr_.process(x);
            float link = entr_.out_at(static_cast<int>(m.comb_sizes[0] * sr_scale_) - 1);
            link = ap_[0].process(link);
            link = ap_[1].process(link);
            link = ap_[2].process(link);
            const float out_l1 = comb_[0].out_at(static_cast<int>(m.out_l[0] * sr_scale_) - 1);
            for (int c = 0; c < 3; ++c) comb_[c].process(link);
            wl = 1.5f * (out_l1 + comb_[1].out_at(static_cast<int>(m.out_l[1] * sr_scale_)))
                     + comb_[2].out_at(static_cast<int>(m.out_l[2] * sr_scale_));
            wr = 1.5f * (comb_[0].out_at(static_cast<int>(m.out_r[0] * sr_scale_))
                     + comb_[1].out_at(static_cast<int>(m.out_r[1] * sr_scale_)))
                     + comb_[2].out_at(static_cast<int>(m.out_r[2] * sr_scale_));
        } else {
            // Tapped delay: one line, two read positions, the left tap feeds
            // back so cross delays alternate sides.
            tap_line_.buf_[tap_line_.i_] = x + tap_fb_ * tap_line_.at(tap_l_);
            tap_line_.next();
            wl = tap_line_.at(tap_l_);
            wr = tap_line_.at(tap_r_);
        }

        // Gates work on the wet part, the way a gated reverb does: Short
        // and Long Gate hold full level and then cut; the reverse gates
        // rise over their window and cut at its end.
        float g = 1.0f;
        if (gate_ > 0) {
            g = age_ < gate_ ? 1.0f : 0.0f;
            ++age_;
        } else if (reverse_ > 0) {
            g = age_ < reverse_ ? static_cast<float>(age_) / reverse_ : 0.0f;
            ++age_;
        }

        const float b = clamp01(spec_.balance);
        l = xl * (1.0f - b) + wl * b * wet_ * g;
        r = xr * (1.0f - b) + wr * b * wet_ * g;
    }

private:
    static constexpr int kPool = 21000;  // floats: 3x1950 allpass, 1050 entrance, 3x4600 combs (up to 20700), or 16.2k tap line

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

    float pool_[kPool] = {};
    ReverbAllpass ap_[3];
    ReverbEntrance entr_;
    ReverbComb comb_[3];
    const BossMode* boss_ = &boss_mode(1);
    ReverbLine tap_line_;
    int tap_l_ = 0, tap_r_ = 0;
    float tap_fb_ = 0.0f;
};

}  // namespace d5
