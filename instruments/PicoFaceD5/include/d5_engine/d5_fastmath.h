// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// Table-driven sine and exp2 for the audio inner loop.
//
// The LA model is built out of cosine segments, a synchronous cosine for the
// sawtooth, a decaying sine for the resonance and an exponential cutoff map,
// and every one of those was calling libm once per sample per partial. On the
// host that is invisible; on the RP2350 it is the whole problem. Sixteen
// voices at 32 kHz meant about three million cos/exp/pow calls a second, and
// a Cortex-M33 has no hardware for any of them -- the instrument reported a
// peak load of 170% and 210 I2S underruns on hardware.
//
// Both tables live in flash and are read with linear interpolation. The sine
// is 512 points over one turn, which puts its worst-case error at about
// -94 dBFS; exp2 is 256 points over one octave with the integer part folded
// into the float exponent, which is accurate to about 1e-6 relative and so is
// exact for anything an ear is measuring.
//
// Arguments are in TURNS, not radians. Every call site had a 2*pi or 4*pi in
// it, and dropping those saves a multiply as well as making the wrap trivial.
#pragma once

#include <cstdint>
#include <cstring>

namespace d5 {

inline constexpr int kSinBits = 9;
inline constexpr int kSinSize = 1 << kSinBits;          // 512 points per turn
inline constexpr int kExpBits = 8;
inline constexpr int kExpSize = 1 << kExpBits;          // 256 points per octave

namespace detail {

struct SinTable {
    float v[kSinSize + 1];
    constexpr SinTable() : v{} {
        // constexpr, so the table is built at compile time and lands in flash
        // rather than costing RAM and a startup pass.
        for (int i = 0; i <= kSinSize; ++i) {
            const double t = 6.283185307179586476925286766559 * i / kSinSize;
            // Taylor about the nearest quadrant keeps the constexpr evaluation
            // short and stays well inside float precision.
            double x = t;
            while (x > 3.14159265358979323846) x -= 6.28318530717958647692;
            const double x2 = x * x;
            double term = x, sum = x;
            for (int k = 1; k <= 9; ++k) {
                term *= -x2 / ((2.0 * k) * (2.0 * k + 1.0));
                sum += term;
            }
            v[i] = static_cast<float>(sum);
        }
    }
};

struct Exp2Table {
    float v[kExpSize + 1];
    constexpr Exp2Table() : v{} {
        for (int i = 0; i <= kExpSize; ++i) {
            const double x = static_cast<double>(i) / kExpSize;
            const double t = x * 0.69314718055994530942;   // x * ln 2
            double term = 1.0, sum = 1.0;
            for (int k = 1; k <= 16; ++k) {
                term *= t / k;
                sum += term;
            }
            v[i] = static_cast<float>(sum);
        }
    }
};

inline constexpr SinTable kSin{};
inline constexpr Exp2Table kExp2{};

}  // namespace detail

// sin(2*pi*turns). Any real argument; the wrap is a floor and a subtract.
inline float fast_sin(float turns) {
    const float f = turns - static_cast<float>(static_cast<int>(turns)) +
                    (turns < 0.0f ? 1.0f : 0.0f);
    const float x = f * kSinSize;
    // A tiny negative argument makes f round to exactly 1.0, and then i
    // indexes one past the last interpolation pair. The table has one spare
    // entry so i+1 is always readable, but i itself has to stay inside it.
    int i = static_cast<int>(x);
    float frac = x - static_cast<float>(i);
    if (i >= kSinSize) { i = kSinSize - 1; frac = 1.0f; }
    if (i < 0) { i = 0; frac = 0.0f; }
    const float a = detail::kSin.v[i];
    return a + (detail::kSin.v[i + 1] - a) * frac;
}

inline float fast_cos(float turns) { return fast_sin(turns + 0.25f); }

// 2^x, for x anywhere the audio path can produce it.
inline float fast_exp2(float x) {
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    const int n = static_cast<int>(x) - (x < 0.0f ? 1 : 0);   // floor
    const float f = x - static_cast<float>(n);
    const float y = f * kExpSize;
    int i = static_cast<int>(y);
    float frac = y - static_cast<float>(i);
    if (i >= kExpSize) { i = kExpSize - 1; frac = 1.0f; }   // f rounded to 1.0
    if (i < 0) { i = 0; frac = 0.0f; }
    const float a = detail::kExp2.v[i];
    const float m = a + (detail::kExp2.v[i + 1] - a) * frac;
    // scale by 2^n through the exponent field rather than a second table
    uint32_t bits;
    std::memcpy(&bits, &m, sizeof bits);
    const int e = static_cast<int>((bits >> 23) & 0xFF) + n;
    if (e <= 0) return 0.0f;
    if (e >= 255) return m * 1e30f;
    bits = (bits & 0x807FFFFFu) | (static_cast<uint32_t>(e) << 23);
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

// e^-x for x >= 0, which is every use here (decays, never growth).
inline float fast_exp_neg(float x) {
    return x <= 0.0f ? 1.0f : fast_exp2(-x * 1.44269504088896340736f);
}

// lo * (hi/lo)^t, the exponential parameter map, with t already in [0,1].
inline float fast_exp_map(float lo, float ratio_log2, float t) {
    return lo * fast_exp2(ratio_log2 * t);
}

}  // namespace d5
