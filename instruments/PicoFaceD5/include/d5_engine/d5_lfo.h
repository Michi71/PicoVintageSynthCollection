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
// LFO and envelope counters. Read from the firmware's own timer setup:
// the sound-engine init on EPROM page 3 (CPU 0x81AC, reached from the
// reset path via 0x2815) writes STBC = 0 (uPD78312A full speed, fCLK =
// 12 MHz / 2 = 6 MHz -- the same init sets BRG = 48, which is exactly
// 31250 baud at that clock), then TM1 = MD1 = 0x2800 and starts TM1 on
// fCLK/6 (TMC1 TCLK1 = 0). TM1's underflow is TMF1, vector 0x0010 ->
// 0x2018 -> 0x27F0, the LFO/envelope/portamento tick. So one tick is
// 10240 us: 6 MHz / 6 / 10240 = 97.65625 Hz. The earlier 112 Hz came
// from the D-05's rate table top (a different machine); Roland's D-50
// VST plays 25 Hz at rate 100, i.e. an idealised 100-Hz tick.
inline constexpr float kTickHz = 6.0e6f / 6.0f / 10240.0f;

// Panel 0..100 to Hz, the firmware's own law end to end: the tick engine
// subtracts table 0x0213[2k] from a 16-bit phase per tick, so a cycle
// takes 65536 / T[k] ticks and the rate is T[k] / 65536 * kTickHz. The
// table is the internal ROM's, byte for byte (IC25 0x0213, 101 words,
// within 1 of 16 * 2^(k/10)); panel 100 runs at 16383 / 65536 * 97.66 =
// 24.41 Hz. Roland's D-50 VST plays 25.0 Hz there (an idealised 100-Hz
// tick), the D-05 remake's table tops out at 27.98 Hz (a 112-Hz tick) --
// the hardware timer of the D-50 itself decides between them.
inline constexpr uint16_t kLfoRateInc[101] = {
    16, 17, 18, 20, 21, 23, 24, 26, 28, 30, 32, 34, 37, 39, 42, 45, 49,
    52, 56, 60, 64, 69, 74, 79, 84, 91, 97, 104, 111, 119, 128, 137, 147,
    158, 169, 181, 194, 208, 223, 239, 256, 274, 294, 315, 338, 362, 388,
    416, 446, 478, 512, 549, 588, 630, 676, 724, 776, 832, 891, 955, 1024,
    1097, 1176, 1261, 1351, 1448, 1552, 1663, 1783, 1911, 2048, 2195, 2353,
    2521, 2702, 2896, 3104, 3327, 3566, 3822, 4096, 4390, 4705, 5043, 5405,
    5793, 6208, 6654, 7132, 7643, 8192, 8780, 9410, 10086, 10809, 11585,
    12417, 13308, 14263, 15287, 16383};

// The fade-in after the delay: the firmware waits out the silence, then
// walks an 8-bit ramp by this table's value per tick, indexed by the delay
// byte / 8 (IC25 0x15C6-0x15D3, table at 0x1859). A short delay snaps on
// in one tick; the full 100 swells over 256 ticks, about 2.3 seconds.
inline constexpr uint8_t kLfoFadeStep[13] = {
    255, 128, 64, 32, 16, 8, 4, 3, 2, 2, 1, 1, 1};

inline float lfo_rate_hz(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return kLfoRateInc[static_cast<int>(v * 100.0f + 0.5f)] * (kTickHz / 65536.0f);
}

class Lfo {
public:
    void start(const LfoSpec& spec, float sample_rate, uint32_t seed) {
        spec_ = spec;
        sr_ = sample_rate;
        inc_ = lfo_rate_hz(spec.rate) / sr_;
        // A synced LFO starts at phase 0, the top of its triangle (Roland's
        // D-50 VST: the vibrato starts at its highest pitch and falls, the
        // tremolo fully ducked). A free-running one starts half a cycle
        // in, at the bottom: the VST's rate-0 tremolo sits open right after
        // the patch loads, and Ham and Organ lives on that.
        phase_ = spec_.sync != 0 ? 0.0f : 0.5f;
        half_ = phase_ >= 0.5f;
        rng_ = seed ? seed : 0x2545F491u;
        sample_ = next_random();
        restart_delay();
    }

    // Sync mode 2: any new note in the tone restarts this LFO's phase and
    // its delay in the voices already sounding.
    void retrigger() {
        phase_ = 0.0f;
        half_ = false;
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
        // Shapes as the VST plays them on the pitch route: the triangle
        // starts at +1 and falls, the sawtooth falls from +1 to -1, the
        // square is +/-0.5 (half the triangle's swing, high first), the
        // random value is drawn from +/-0.5 and holds for HALF a period.
        switch (spec_.wave) {
            case LfoWave::kSawtooth: v = 1.0f - 2.0f * phase_; break;
            case LfoWave::kSquare:   v = phase_ < 0.5f ? 0.5f : -0.5f; break;
            case LfoWave::kRandom:   v = sample_; break;
            case LfoWave::kTriangle:
            default: v = phase_ < 0.5f ? (1.0f - 4.0f * phase_)
                                       : (4.0f * phase_ - 3.0f); break;
        }
        raw_ = v;
        phase_ += inc_ * n;
        if (!half_ && phase_ >= 0.5f) {
            half_ = true;
            sample_ = next_random();
        }
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            half_ = false;
            sample_ = next_random();
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

    // The D-50's LFOs are tone-global: the 97.66-Hz tick walks exactly one
    // phase word per LFO per tone (IC25 0x1508-0x160D), and every sounding
    // voice reads the same words from the CD40 merge area. So the tone owns
    // the LFOs and steps them once per sample...
    float next() { return next_n(1); }

    // ...while a voice's block rate only READS the shared state -- calling
    // next_n() here would advance the phase once per listening voice.
    float value() const { return raw_ * gate(); }

    float phase() const { return phase_; }   // diagnostic handle

private:
    float next_random() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ >> 8) * (1.0f / 16777216.0f) - 0.5f;   // +/-0.5, the VST's random swing
    }

    LfoSpec spec_{};
    float sr_ = 32000.0f;
    float inc_ = 0.0f;
    float phase_ = 0.0f;
    bool half_ = false;           // second half of the period reached (random redraw)
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
