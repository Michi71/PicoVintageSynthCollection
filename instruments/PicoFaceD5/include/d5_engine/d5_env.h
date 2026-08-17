// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The D-50's envelope shape, shared by TVA and TVF: three timed levels, a
// sustain the note holds at, and a release to an end level. The panel calls
// them T1..T5 and L1..L3; the parameter ranges are in the MIDI implementation
// chart under "Each partial block".
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_fastmath.h"
#include "d5_engine/d5_hot.h"

namespace d5 {

struct Env5Spec {
    float t[5] = {0.004f, 0.10f, 0.20f, 0.30f, 0.40f};   // seconds
    float l[3] = {1.0f, 0.85f, 0.7f};
    float sustain = 0.6f;
    float end = 0.0f;
    // The LA envelopes run linear in decibels, not in amplitude: a single
    // note of the reference recording decays at a constant -34 dB/s, which
    // an amplitude-linear segment cannot do -- it holds energy up and then
    // dives. TVA envelopes set this; TVF keeps linear segments because its
    // output feeds a cutoff that is already exponential.
    bool log_segments = false;
    // Per-segment decay RATES in dB/s, used for falling log segments when
    // non-zero. The LA chip's time bytes set rates, not durations -- the
    // proof is Horn Section, whose measured release of -37.8 dB/s equals
    // its byte through the fitted map, while duration semantics predicted
    // -108 -- and munt implements the MT-32 sibling the same way. Rising
    // segments keep durations.
    float r[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// The firmware's own envelope arithmetic (IC25 disassembly, workflows
// wgt50aax0 and wni28ji2j). The chip runs each segment as a hardware ramp:
// the CPU computes ONE rate index per segment, the ramp doubles speed per
// +8 on it, and the CPU's time-law table compensates the level distance so
// inner segments come out time-constant. The release skips the law -- its
// lookup is computed and then overwritten, dead code in the ROM -- so it
// is rate-constant: release time grows with the level it starts from.
//
// The raw panel bytes of one envelope, as the conversion engine reads them.
struct EnvBytes {
    uint8_t t[5] = {0, 0, 0, 0, 0};    // T1..T5, panel 0..100
    uint8_t l[4] = {0, 0, 0, 0};       // L1..L3 + sustain, panel 0..100
    uint8_t end = 0;                   // 0: release to silence, else to full
    uint8_t time_kf = 0;               // TVF p21 / TVA p50, 0..4
    uint8_t vel_kf = 0;                // TVA p49 only, 0..4
};

// The time law at IC25 0x008C, byte-verified: law[0]=1, then
// floor(8*log2(103/d)). Adding it to a rate index makes the ramp finish a
// distance-d segment in (nearly) the same time as a distance-103 one.
inline constexpr uint8_t kEnvLaw[102] = {
    1, 53, 45, 40, 37, 34, 32, 31, 29, 28, 26, 25, 24, 23, 23, 22, 21,
    20, 20, 19, 18, 18, 17, 17, 16, 16, 15, 15, 15, 14, 14, 13, 13, 13,
    12, 12, 12, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9, 8, 8, 8,
    8, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4,
    4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};

// The chip ramp's absolute clock: kRampClock * 2^(-idx/8) units per second.
// 64000 at 32 kHz, which is munt's derivation for the same chip in the MT-32.
//
// This carried 64000 * sqrt(2) = 90509.7 for months, on four measurements that
// were all taken on played phrases with the patch's reverb in them (Soundtrack's
// swell, Staccato's release, Pizzagogo's body). Two of those four were later
// retracted for measuring something else entirely -- Horn Section's "release"
// was a type-8 reverb tail, Stereo Polysynth's was its filter closing -- and
// the sqrt(2) rested on the remainder.
//
// A dry single note settles it. The release is rate-constant by construction
// (no law term, see below), so it is the one segment that measures this clock
// directly, and with reverb and chorus at zero it is a straight line in dB:
//
//   Soundtrack, C5, dry:  -18.25 dB/s, and the fit moves by 0.1 % whether it
//                         is taken over 10, 20 or 50 ms windows
//   this engine at 64000: -18.16 dB/s   (0.6 % out)
//   at 90509.7:           -25.65 dB/s   (40 % out)
//
// A second dry note, Choir at C5, agrees: -206 dB/s measured, -240 at 64000
// (16 % out, inside that recording's own +/-18 % spread -- 0.2 s of stepped,
// vibrato-ridden fall is a far weaker measurement) and -337 at 90509.7, which
// is 64 % out and outside any reading of it.
//
// So the factor that fell out is exactly the one we put in. What remains open
// is the last digit: Soundtrack says 64000, Choir on its own would say 56000,
// and only a slow-release patch recorded dry on real hardware would separate
// them. The source here is Roland's own software model, not the machine.
//
// Overridable so a calibration run can sweep it without editing the source.
#ifndef D5_RAMP_CLOCK
#define D5_RAMP_CLOCK 64000.0f
#endif
inline constexpr float kRampClock = D5_RAMP_CLOCK;

inline float env_ramp_seconds(int dist_units, int idx) {
    if (dist_units <= 0) return 0.0f;
    return static_cast<float>(dist_units)
           * fast_exp2(static_cast<float>(idx) * 0.125f) * (1.0f / kRampClock);
}

inline int env_law(int dist) {
    return kEnvLaw[dist < 0 ? 0 : (dist > 101 ? 101 : dist)];
}

inline int env_clamp_idx(int idx) {
    return idx < 1 ? 1 : (idx > 127 ? 127 : idx);
}

// Arithmetic shift by a keyfollow byte's complement, zero-gated where the
// ROM gates it. C++'s >> on a negative int is the arithmetic shift the
// 78K/III performs (MOV1 CY,X.7; SUBC chains).
inline int env_kf_shift(int value, int kf, int base) {
    if (kf <= 0) return 0;
    if (kf > 4) kf = 4;
    return value >> (base - kf);
}

// The TVA bias magnitude curve at IC25 0x0AB2, indexed by the bias level
// byte p38 (0..12 = panel -12..0): level steps lost per semitone past the
// bias point are (curve * distance) >> 5, with a hard mute once the
// product reaches 0x1000 (workflow ws3rfxpjr, 0x09C4-0x09DB).
inline constexpr uint8_t kTvaBiasCurve[13] = {
    255, 187, 137, 100, 74, 54, 40, 29, 21, 15, 10, 5, 0};

// The complete TVA level basis, byte-exact from the note-on chain
// (0x040C tone-compile loop + 0x09E8 velocity term + 0x09C4 bias):
// everything is additive in the chip's 16-per-octave log unit. p35+31
// minus a velocity headroom of 1.5*|p36-50| minus half the resonance
// (synth partials only -- the resonance term compensates the resonance
// recipe's loudness), plus the velocity term, minus the keyboard bias.
// Nominal full level (p35=100, neutral velocity range) is 131; a hot
// strike on a full positive range reaches 155, which is the design
// ceiling -- 155 + envelope 100 = 255, the chip's full scale.
inline int tva_chip_level(int p35, int p36, int p14, bool is_pcm,
                          int p37, int p38, int key, int vel127) {
    const int d50 = p36 - 50;
    const int mag = d50 < 0 ? -d50 : d50;
    int base = p35 + 31 - (mag + (mag >> 1)) - (is_pcm ? 0 : p14 >> 1);
    if (base < 0) base = 0;
    const int velT = d50 >= 0 ? (4 * d50 * vel127) >> 8
                              : (4 * (-d50) * (128 - vel127)) >> 8;
    int bias = 0;
    const int pt = (p37 & 0x3F) - 27;
    const int dist = (p37 & 0x40) ? (key - pt) : (pt - key);
    if (dist > 0) {
        const int prod = kTvaBiasCurve[p38 > 12 ? 12 : p38] * dist;
        if (prod >= 0x1000) return 0;      // hard mute past the curve
        bias = prod >> 5;
    }
    int chip = base + velT - bias;
    return chip < 0 ? 0 : chip;
}

// TVF envelope from bytes. D is the effective depth 0..255 the firmware
// computes at note-on -- min(255, (p18 * velocity-bias) >> 6) -- and every
// level distance is scaled by it before the law: the TVF ramp thinks in
// chip cutoff units, (L * D) >> 8. All five phases share the time
// keyfollow; the attack byte has NO 1.25 factor, the inner segments and
// the release do. Levels stay the engine's 0..1 of full depth -- the
// distances here only shape the times.
inline void build_tvf_env(const EnvBytes& b, int D, int key, Env5Spec& out) {
    const int kf = env_kf_shift(key, b.time_kf, 5);
    out.log_segments = false;
    const int lp[5] = {0, b.l[0], b.l[1], b.l[2], b.l[3]};
    for (int k = 0; k < 4; ++k) {
        const int dist = ((lp[k + 1] > lp[k] ? lp[k + 1] - lp[k]
                                             : lp[k] - lp[k + 1]) * D) >> 8;
        int idx;
        if (k == 0) {
            // Byte 0 is the fastest RAMP, not an instant jump: the chip's
            // rate table has a floor (idx 1 tops out near 3 ms for a
            // full-scale swing). Snapping here made every percussive
            // attack a single-sample step -- the dust-grain pop in fast
            // sequences.
            if (dist == 0) { out.t[0] = 0.0f; continue; }
            idx = env_clamp_idx(env_law(dist) + b.t[0] - kf);
        } else {
            int u = b.t[k] - kf;
            if (u < 1) u = 1;
            int u125 = u + (u >> 2);
            if (u125 > 127) u125 = 127;
            idx = env_clamp_idx(env_law(dist) + u125);
        }
        out.t[k] = env_ramp_seconds(dist, idx);
        out.r[k] = 0.0f;
    }
    // Release: no law term -- rate-constant, 1.25 on the byte. r[4] is in
    // env-level units (0..1 of full depth) per second.
    int u = b.t[4] - kf;
    if (u < 0) u = 0;
    const int idx5 = env_clamp_idx(u + (u >> 2));
    const float rate_units = kRampClock * fast_exp2(-idx5 * 0.125f);
    const int full = (100 * (D < 1 ? 1 : D)) >> 8;
    out.r[4] = rate_units / static_cast<float>(full < 1 ? 1 : full);
    out.t[4] = ((b.end ? 100 - b.l[3] : b.l[3]) * D >> 8) / rate_units;
}

// TVA envelope from bytes. The ramp thinks in raw panel level units --
// they ARE the chip's log-amp units, 16 per octave (0.376 dB each; the
// ROM writes target = base + L with no conversion, and munt's LA32 pins
// the exponent at 2^(value/16)). So the levels come out exponential, the
// falling segments run at constant dB/s over the firmware's duration, and
// a sustain of 0 is the one absolute silence the ROM special-cases.
// Velocity shifts only the attack; the key keyfollow shifts every phase.
inline void build_tva_env(const EnvBytes& b, int key, int vel127,
                          float level, Env5Spec& out) {
    const int kf = env_kf_shift(key, b.time_kf, 4);
    const int voff = (vel127 - 64) >> (6 - (b.vel_kf > 4 ? 4 : b.vel_kf));
    out.log_segments = true;
    for (int k = 0; k < 3; ++k)
        out.l[k] = fast_exp2((static_cast<int>(b.l[k]) - 100) * 0.0625f) * level;
    out.sustain = b.l[3] == 0
        ? 0.0f : fast_exp2((static_cast<int>(b.l[3]) - 100) * 0.0625f) * level;
    out.end = b.end ? level : 0.0f;

    // Attack: the law on the target level either way. Byte 0 is the
    // fastest ramp the chip has (idx clamps to 1, a full-scale swing in
    // ~3 ms), NOT an instant jump -- a snapped attack steps the sample by
    // a quarter of full scale and crackles in sequences. Only a zero
    // target has no distance to travel.
    if (b.l[0] == 0) {
        out.t[0] = 0.0f;
    } else {
        const int idx = env_clamp_idx(b.t[0] + env_law(b.l[0]) - voff - kf);
        out.t[0] = env_ramp_seconds(160 + b.l[0], idx);
    }
    out.r[0] = 0.0f;
    for (int k = 1; k < 4; ++k) {
        const int dist = b.l[k] > b.l[k - 1] ? b.l[k] - b.l[k - 1]
                                             : b.l[k - 1] - b.l[k];
        int u = b.t[k] - kf;
        if (u < 1) u = 1;
        int u125 = u + (u >> 2);
        if (u125 > 127) u125 = 127;
        const int idx = env_clamp_idx(env_law(dist) + u125);
        out.t[k] = env_ramp_seconds(dist, idx);
        out.r[k] = 0.0f;
    }
    // Release: rate-constant, 1.25 on the byte, no law. In dB/s through
    // the 0.376 dB chip unit; Env5's rate semantics resolve the duration
    // from wherever the level actually is at note-off.
    int u = b.t[4] - kf;
    if (u < 0) u = 0;
    const int idx5 = env_clamp_idx(u + (u >> 2));
    out.r[4] = kRampClock * fast_exp2(-idx5 * 0.125f) * (6.0206f / 16.0f);
    out.t[4] = (b.end ? (100 - b.l[3]) : b.l[3]) * 0.376f / out.r[4];
}

class Env5 {
public:
    void start(const Env5Spec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        // No level reset here: a retriggered voice keeps what the ramp
        // currently holds and glides into its new attack from there. That
        // is the chip's behaviour -- munt's LA32Ramp: "the starting point
        // of the ramp is whatever internal value the LA-32 had when the
        // registers were set". Zeroing levels on a stolen voice cut a
        // sounding note in one sample. Only construction starts at 0.
        held_ = true;
        arm(0, spec_.l[0]);
    }

    void release() {
        if (held_) {
            held_ = false;
            arm(4, spec_.end);
        }
    }

    // The CPU governor's exit: fade what is left to nothing over a few
    // milliseconds and be done. Not a D-50 behaviour -- the chip had the
    // silicon to let every tail run -- but a tail already on its way down,
    // faded over 20 ms, is inaudible, and an underrun is not.
    void quick_release(float sample_rate, float ms = 20.0f) {
        held_ = false;
        seg_ = 4;
        remaining_ = static_cast<int32_t>(sample_rate * ms * 0.001f);
        if (remaining_ < 1) remaining_ = 1;
        const float kFloor = 1.0e-3f;
        const float from = level_ < kFloor ? kFloor : level_;
        seg_log_ = true;
        factor_ = std::pow(kFloor / from, 1.0f / static_cast<float>(remaining_));
        step_ = -level_ / static_cast<float>(remaining_);
        if (level_ < kFloor) level_ = kFloor;
    }

    bool finished() const { return !held_ && seg_ >= 5; }
    float level() const {
        if (spec_.log_segments && level_ <= 1.05e-3f && remaining_ <= 0) return 0.0f;
        return level_ < 0.0f ? 0.0f : level_;
    }

    // Advance n samples at once -- the control-rate path. Segment changes
    // land on block edges; the shortest documented segment (4 ms) still
    // spans eight blocks, so nothing audible is lost.
    float next_n(int32_t n) {
        while (n > 0) {
            if (remaining_ > 0) {
                const int32_t k = remaining_ < n ? remaining_ : n;
                if (seg_log_) {
                    for (int32_t j = 0; j < k; ++j) level_ *= factor_;
                } else {
                    level_ += step_ * k;
                }
                remaining_ -= k;
                n -= k;
            } else if (held_ && seg_ < 3) {
                arm(seg_ + 1, seg_ + 1 < 3 ? spec_.l[seg_ + 1] : spec_.sustain);
            } else if (held_) {
                level_ = spec_.sustain;
                break;
            } else if (seg_ == 4) {
                seg_ = 5;
                level_ = spec_.end;
                break;
            } else {
                break;
            }
        }
        return level();
    }

    float D5_HOT_TAG(d5_env_next, next)() {
        if (remaining_ > 0) {
            if (seg_log_) level_ *= factor_;
            else level_ += step_;
            --remaining_;
        } else if (held_ && seg_ < 3) {
            arm(seg_ + 1, seg_ + 1 < 3 ? spec_.l[seg_ + 1] : spec_.sustain);
        } else if (held_ && seg_ == 3) {
            level_ = spec_.sustain;          // hold until release
        } else if (!held_ && seg_ == 4) {
            seg_ = 5;
            level_ = spec_.end;
        }
        return level();
    }

private:
    void arm(int seg, float target) {
        seg_ = seg;
        remaining_ = static_cast<int32_t>(spec_.t[seg] * sr_);
        // Rate semantics OUTRANK the stored duration: a rate-constant
        // release runs from wherever the level is now, and its duration
        // cannot be precomputed. The instant-jump shortcut used to be
        // checked first, so a sustain of 0 -- whose release duration
        // precomputes to zero -- cut held notes to silence in one sample.
        // That was the light pop on every pluck released early.
        //
        // Log-linear glide for FALLING segments only: a decay at constant
        // dB/s is what the chip's linear log-domain ramp produces, but a
        // rise in the log domain spends most of its time inaudibly near
        // the floor -- attacks keep the linear ramp.
        seg_log_ = spec_.log_segments && target < level_;
        // -60 dB, not -96: the last segment glides to "zero" through this
        // floor, and the deeper it lies the steeper that dive reads in
        // dB/s against what a recording shows.
        const float kFloor = 1.0e-3f;
        if (seg_log_) {
            const float from = level_ < kFloor ? kFloor : level_;
            const float to = target < kFloor ? kFloor : target;
            if (spec_.r[seg] > 0.0f) {
                // Rate semantics: duration follows from the dB distance.
                const float dist_db = 20.0f * std::log10(from / to);
                remaining_ = static_cast<int32_t>(dist_db / spec_.r[seg] * sr_);
            }
            if (remaining_ <= 0) {
                level_ = target; step_ = 0.0f; factor_ = 1.0f;
                remaining_ = 0; seg_log_ = false;
                return;
            }
            step_ = 0.0f;
            factor_ = std::pow(to / from, 1.0f / static_cast<float>(remaining_));
            if (level_ < kFloor) level_ = kFloor;
            return;
        }
        if (spec_.r[seg] > 0.0f && target < level_) {
            // Linear falling with a rate (the TVF release).
            remaining_ = static_cast<int32_t>((level_ - target) / spec_.r[seg] * sr_);
            if (remaining_ < 1) remaining_ = 1;
        }
        if (remaining_ <= 0) { level_ = target; step_ = 0.0f; factor_ = 1.0f; return; }
        step_ = (target - level_) / remaining_;
        factor_ = 1.0f;
    }

    Env5Spec spec_{};
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    float factor_ = 1.0f;
    bool seg_log_ = false;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

}  // namespace d5
