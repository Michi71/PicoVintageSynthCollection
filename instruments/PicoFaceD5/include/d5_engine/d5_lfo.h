// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The three LFOs of the common block, and the pitch envelope that sits beside
// them. Both belong to the tone, not to a partial: the partials only choose
// which LFO to listen to and how far.
//
// Ranges after the IC25 disassembly (workflow wgt50aax0):
//   LFO rate   0.033 .. 34 Hz       LFO delay  0 .. ~9 s, then a fade
//   P-ENV time 9 ms .. 9.1 s        P-ENV depth velocity-scaled, max +/- 2381 ct
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
    float rate = 0.5f;        // 0..1, panel 0..100; doubles every 10 steps
    uint8_t delay_byte = 0;   // panel 0..100, raw: silence, then a fade
    // Panel "Sync" 0..2: 0 free-runs, 1 restarts the phase on note-on, 2
    // additionally restarts LFO-1 in every sounding voice of the tone when
    // any new note arrives (EPROM 0x2929/0x2952/0x2981 gate that call on
    // the byte being exactly 2).
    uint8_t sync = 1;
};

// The engine's control tick, the rate at which the D-50's CPU walks its
// LFO and envelope counters. Not read from a register: pinned by the
// service notes' P-ENV span against the tick tables below -- 1 tick and
// 1020 ticks against "9 ms .. 9 s" both land at ~112 Hz.
inline constexpr float kTickHz = 112.0f;

// Panel 0..100 to Hz. The SHAPE is proven from the D-50's own mask ROM:
// the tick engine subtracts table 0x0213[2k] from a 16-bit phase per tick,
// and that table is exactly 16 * 2^(k/10) -- the rate doubles every 10
// panel steps, a span of 2^10 across the knob (IC25 0x151C-0x152C, tables
// byte-verified). The ABSOLUTE anchor is not: it hangs on the tick rate,
// and the three soft anchors (delay 10 s, P-ENV 9 ms/9 s, both service
// notes) disagree by up to 20% with the vibrato measured in the Living
// Calliope reference. The anchor stays on the calibrated band -- 5.6 Hz
// at byte 74, the value the last approved build shipped -- so the patches
// already reviewed keep their rate and every other byte now sits on the
// proven curve relative to them.
inline constexpr float kLfoRateHz[101] = {
    0.0331563f, 0.035536f, 0.0380866f, 0.0408202f, 0.04375f, 0.0468901f,
    0.0502556f, 0.0538626f, 0.0577285f, 0.0618718f, 0.0663126f, 0.0710721f,
    0.0761732f, 0.0816404f, 0.0875f, 0.0937802f, 0.100511f, 0.107725f,
    0.115457f, 0.123744f, 0.132625f, 0.142144f, 0.152346f, 0.163281f,
    0.175f, 0.18756f, 0.201022f, 0.21545f, 0.230914f, 0.247487f,
    0.26525f, 0.284288f, 0.304693f, 0.326562f, 0.35f, 0.375121f,
    0.402044f, 0.430901f, 0.461828f, 0.494975f, 0.530501f, 0.568577f,
    0.609385f, 0.653123f, 0.7f, 0.750241f, 0.804089f, 0.861801f,
    0.923656f, 0.989949f, 1.061f, 1.13715f, 1.21877f, 1.30625f,
    1.4f, 1.50048f, 1.60818f, 1.7236f, 1.84731f, 1.9799f,
    2.122f, 2.27431f, 2.43754f, 2.61249f, 2.8f, 3.00097f,
    3.21636f, 3.4472f, 3.69462f, 3.9598f, 4.24401f, 4.54861f,
    4.87508f, 5.22498f, 5.6f, 6.00193f, 6.43271f, 6.89441f,
    7.38924f, 7.9196f, 8.48801f, 9.09723f, 9.75017f, 10.45f,
    11.2f, 12.0039f, 12.8654f, 13.7888f, 14.7785f, 15.8392f,
    16.976f, 18.1945f, 19.5003f, 20.8999f, 22.4f, 24.0077f,
    25.7308f, 27.5776f, 29.557f, 31.6784f, 33.9521f};

// The fade-in after the delay: the firmware waits out the silence, then
// walks an 8-bit ramp by this table's value per tick, indexed by the delay
// byte / 8 (IC25 0x15C6-0x15D3, table at 0x1859). A short delay snaps on
// in one tick; the full 100 swells over 256 ticks, about 2.3 seconds.
inline constexpr uint8_t kLfoFadeStep[13] = {
    255, 128, 64, 32, 16, 8, 4, 3, 2, 2, 1, 1, 1};

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
        if (spec_.sync != 0) phase_ = 0.0f;
        rng_ = seed ? seed : 0x2545F491u;
        sample_ = next_random();
        restart_delay();
    }

    // Sync mode 2: any new note in the tone restarts this LFO's phase and
    // its delay in the voices already sounding.
    void retrigger() {
        phase_ = 0.0f;
        restart_delay();
    }

    // The delay is two distinct phases, per the tick engine at IC25
    // 0x15A5-0x15D3: dead silence for 1024 * 2^((d-100)/10) ticks -- the
    // same doubling law as the rates, run in reverse -- and then a linear
    // fade stepped from the table above. Only after both is the LFO fully
    // on. The gate is exposed separately because the performance controls
    // do not wait for it: the firmware multiplies only the standing
    // P-Mod depth by the fade ramp (0x177B-0x17A3); lever and aftertouch
    // vibrato speak immediately, delay or no delay.
    void restart_delay() {
        const int d = spec_.delay_byte > 100 ? 100 : spec_.delay_byte;
        delay_left_ = 1024.0f * fast_exp2((d - 100) * 0.1f) * (sr_ / kTickHz);
        gate_ = 0.0f;
        gate_step_ = kLfoFadeStep[d >> 3] * (1.0f / 256.0f) * (kTickHz / sr_);
    }

    // The block-rate step: value from the current phase, then advance by n.
    // Returns the gated value; raw() and gate() expose the parts.
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
        raw_ = v;
        phase_ += inc_ * n;
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            sample_ = next_random();      // random holds for one period
        }
        if (delay_left_ > 0.0f) {
            delay_left_ -= n;
        } else if (gate_ < 1.0f) {
            gate_ += gate_step_ * n;
            if (gate_ > 1.0f) gate_ = 1.0f;
        }
        return v * gate();
    }

    float raw() const { return raw_; }
    float gate() const { return delay_left_ > 0.0f ? 0.0f : gate_; }

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
    float gate_ = 0.0f;
    float gate_step_ = 1.0f;
    float raw_ = 0.0f;
    float sample_ = 0.0f;
    uint32_t rng_ = 0x2545F491u;
};

// The pitch envelope: four times and five levels, and unlike TVA/TVF the
// levels are bipolar -- the panel shows them as -50..+50, so a pitch envelope
// can start below the note and rise into it.
//
// Everything here is now the tick engine's own arithmetic (IC25, workflow
// wgt50aax0). The levels are panel magnitudes through the 51-entry curve at
// 0x14D5 (about x^1.7, byte-verified), scaled per note by the velocity mode
// c[11] -- the chain at 0x05DC..0x0641 multiplies curve * s and shifts
// right twice, and 252 * 65 >> 2 = 4095 is exactly one octave in the
// chip's 1/4096-octave unit. So full scale is not a constant: mode 0 is
// +/-1200 cents flat, mode 2 at velocity 127 reaches +/-2381.
struct PitchEnvSpec {
    uint8_t t_idx[4] = {0, 0, 0, 0};   // panel 0..50, index into kPEnvTicks
    uint8_t time_kf = 0;               // c[12], 0..4: keys shorten the times
    uint8_t velo_mode = 0;             // c[11]: 0 fixed, 1 half, 2 full velo
    float l0 = 0.0f;                   // -1..+1 of the 252-unit curve
    float l1 = 0.0f;
    float l2 = 0.0f;
    float sustain = 0.0f;
    float end = 0.0f;
};

// P-ENV level magnitude, panel 0..50 -> 0..252 curve units (IC25 0x14D5).
inline constexpr uint8_t kPEnvLevel[51] = {
    0, 1, 2, 3, 4, 5, 7, 9, 11, 13, 15, 17, 19, 21, 24, 27, 30, 33, 36,
    39, 42, 46, 50, 54, 58, 63, 68, 73, 78, 84, 89, 94, 99, 105, 112,
    119, 126, 133, 140, 147, 154, 161, 168, 178, 189, 199, 210, 220,
    231, 241, 252};

// P-ENV segment duration in ticks (IC25 0x14A2; the ROM stores entries
// from index 32 up divided by 4 and the reader shifts them back -- these
// are the unfolded values). Index 0 is not a duration at all: the tick
// engine jumps straight to the target level and advances.
inline constexpr uint16_t kPEnvTicks[51] = {
    1, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 15, 17, 19, 21, 23, 26, 29, 32,
    36, 40, 44, 49, 55, 61, 68, 76, 84, 94, 105, 117, 130, 144, 160,
    180, 200, 224, 248, 288, 308, 344, 384, 428, 476, 532, 592, 660,
    736, 820, 916, 1020};

class PitchEnv {
public:
    void start(const PitchEnvSpec& spec, float sample_rate,
               int key_rel60 = 0, float vel127 = 64.0f) {
        spec_ = spec;
        sr_ = sample_rate;
        // Velocity scale s per c[11] (IC25 0x0606-0x0615 and 0x1444-0x146E):
        // 65 fixed, (vel+65)/2, or vel+2. One curve unit is then
        // s * 1200/16384 cents.
        const float s = spec_.velo_mode == 0 ? 65.0f
                      : spec_.velo_mode == 1 ? (vel127 + 65.0f) * 0.5f
                                             : vel127 + 2.0f;
        depth_cents_ = s * 18.457031f;    // 252 * s/4 * 1200/4096, per unit l
        // Time keyfollow c[12]: the key (with key shift, relative C4)
        // shifts every segment's table index down -- higher keys run the
        // envelope faster, arithmetic shift by 5-c[12] (IC25 0x1470-0x1490).
        const int kf = spec_.time_kf > 4 ? 4 : spec_.time_kf;
        const int off = kf ? -(key_rel60 >> (5 - kf)) : 0;
        for (int i = 0; i < 4; ++i) {
            int idx = static_cast<int>(spec_.t_idx[i]) + off;
            idx = idx < 0 ? 0 : (idx > 50 ? 50 : idx);
            t_[i] = idx == 0 ? 0.0f : kPEnvTicks[idx] * (1.0f / kTickHz);
        }
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
        const float cents = level_ * depth_cents_;
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
        const float cents = level_ * depth_cents_;
        return cents == 0.0f ? 1.0f : fast_exp2(cents * (1.0f / 1200.0f));
    }

private:
    void arm(int seg, float target) {
        seg_ = seg;
        remaining_ = static_cast<int32_t>(t_[seg] * sr_);
        step_ = remaining_ > 0 ? (target - level_) / remaining_ : 0.0f;
        if (remaining_ <= 0) level_ = target;
    }

    PitchEnvSpec spec_{};
    float t_[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // seconds, resolved per note
    float depth_cents_ = 0.0f;
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

}  // namespace d5
