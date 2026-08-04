/*
 * ObxfPort.h - the seam between the OB-Xf engine and the PicoFace core.
 *
 * The engine headers next to this file are taken from OB-Xf
 * (https://github.com/surge-synthesizer/OB-Xf, GPL-3.0-or-later) and keep
 * their original copyright headers. On the desktop they lean on JUCE and on
 * two of OB-Xf's own headers (Utils.h, Constants.h, SynthEngine.h) for a
 * handful of small things. None of that exists here, so this file provides
 * replacements:
 *
 *   1. The constants the engine reads (pi, dc, mult, ...).
 *   2. A tiny juce:: shim - jmax, jlimit, Random. The namespace deliberately
 *      keeps the upstream name so the engine sources stay textually close to
 *      OB-Xf and can be re-synced with a small patch. There is no JUCE here.
 *   3. Float replacements for the transcendentals in the per-sample path.
 *
 * Point 3 is the one that decides whether this instrument runs at all.
 * Upstream calls the DOUBLE precision tan() and atan() once per sample and
 * voice inside the filter, and getPitch() - 440 * exp(ln2/12 * i) - three
 * times per sample and voice. On a Cortex-M33 without double precision
 * hardware that is several thousand cycles per voice and sample; the whole
 * budget at 32 kHz and 444 MHz is 13875 cycles for ALL voices together.
 *
 * The approximations below are the standard ones for this job. See
 * PicoFaceCP's effects/dsp_fastmath.h for the same Pade approach.
 */

#ifndef OBXF_PORT_H
#define OBXF_PORT_H

#include <cstdint>
#include <cmath>
#include <cstdlib>
// The engine headers use std::array, std::min and std::fill without including
// these themselves; newlib hands them out transitively, libc++ (host tests)
// does not.
#include <algorithm>
#include <array>

// RAM residency for the per-sample path. The RP2350 runs code from flash
// through a 16 KB XIP cache; OscillatorBlock::ProcessSample alone is 18 KB of
// code and is executed once per sample AND voice, so from flash it misses
// essentially every time. __not_in_flash_func() is the pico-sdk macro the
// other instruments use for the same purpose.
#if __has_include("pico.h")
#include "pico.h"
#else
#define __not_in_flash_func(f) f
#define __no_inline_not_in_flash_func(f) f
#endif

// ---------------------------------------------------------------------------
// Configuration (upstream: src/configuration.h)
// ---------------------------------------------------------------------------

// Upstream allows 32. Six is what fits the cycle budget with room to spare;
// OB_MAX_VOICES is the one number to raise once the CPU load screen says so.
constexpr int MAX_VOICES{6};

// Upstream oversamples 2x optionally. Not here - the switch stays off, and the
// factor only survives because it sizes a delay line.
constexpr uint8_t OVERSAMPLE_FACTOR{1};

constexpr uint8_t NUM_XPANDER_MODES{15};

// ---------------------------------------------------------------------------
// Constants (upstream: src/core/Constants.h)
// ---------------------------------------------------------------------------

constexpr float dc = 1e-18f;
constexpr float ln2 = 0.69314718056f;
constexpr float mult = ln2 / 12.f;
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.f * pi;
constexpr float halfPi = pi / 2.f;
constexpr float invPi = 1.f / pi;
constexpr float invTwoPi = 1.f / twoPi;
constexpr float twoByPi = 2.f / pi;

// ---------------------------------------------------------------------------
// juce:: shim - only what the engine actually uses
// ---------------------------------------------------------------------------

namespace juce
{
template <typename T> inline T jmax(T a, T b) { return a > b ? a : b; }
template <typename T> inline T jmin(T a, T b) { return a < b ? a : b; }
template <typename T> inline T jlimit(T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }

inline int roundToInt(float v) { return (int)(v + (v < 0.f ? -0.5f : 0.5f)); }

// Upstream seeds voice slop and the noise generator from juce::Random. A
// 32 bit xorshift is plenty for that and costs nothing.
class Random
{
  public:
    static Random &getSystemRandom()
    {
        static Random r;
        return r;
    }

    uint32_t nextInt()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    // [0, 1)
    float nextFloat() { return (float)(nextInt() >> 8) * (1.f / 16777216.f); }

  private:
    uint32_t s{0x9E3779B9u};
};
} // namespace juce

// ---------------------------------------------------------------------------
// Float transcendentals for the per-sample path
// ---------------------------------------------------------------------------

// tan() for the ZDF prewarp. The argument is g * pi / sampleRate and therefore
// always inside (0, pi/2), which is exactly where this Pade approximant is
// accurate - the error stays below 1e-5 up to about 1.4 rad and the pole at
// pi/2 sits in the same place as the real one.
inline float ob_tan(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return x * (945.f - 105.f * x2 + x4) / (945.f - 420.f * x2 + 15.f * x4);
}

// atan() as the damping saturator in the 4 pole filter. Only the shape matters
// there, not the last digit.
inline float ob_atan(float x)
{
    const float ax = fabsf(x);
    // Rational approximation, max error ~1e-3 over the whole range.
    const float z = (ax < 1.f) ? ax : (1.f / ax);
    const float z2 = z * z;
    float r = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f +
                                                                                z2 * 0.05265332f))));
    if (ax >= 1.f)
        r = halfPi - r;
    return (x < 0.f) ? -r : r;
}

// 2^x for x in a moderate range, via the exponent field plus a degree 4
// polynomial on the fraction. About 15 cycles instead of ~150 for expf().
inline float ob_exp2(float x)
{
    if (x < -126.f)
        return 0.f;
    if (x > 126.f)
        x = 126.f;

    const float xf = floorf(x);
    const float f = x - xf;

    // 2^f, f in [0,1), minimax polynomial
    const float p = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));

    union
    {
        uint32_t i;
        float f;
    } u;
    u.i = (uint32_t)((int)xf + 127) << 23;
    return p * u.f;
}

// Upstream: 440 * exp(mult * index), called three times per sample and voice
// (both oscillator pitches and the filter cutoff).
inline float getPitch(float index) { return 440.f * ob_exp2(index * (1.f / 12.f)); }

// sin() over [-pi, pi] for the LFO. Upstream calls
// juce::dsp::FastMathApproximations::sin in DOUBLE precision; this is the same
// Pade[7/6] shape in float.
inline float ob_sin(float x)
{
    const float x2 = x * x;
    const float num = -x * (-11511339840.f + x2 * (1640635920.f + x2 * (-52785432.f + x2 * 479249.f)));
    const float den = 11511339840.f + x2 * (277920720.f + x2 * (3177720.f + x2 * 18361.f));
    return num / den;
}

// Tempo-synced LFO rate table (upstream: src/core/Constants.h). Kept because
// Lfo.h indexes it; the Pico has no host tempo, so nothing selects it today.
constexpr int syncedRatesCount{21};
struct ObSyncedRates
{
    float v[syncedRatesCount]{1.f / 12.f, 1.f / 8.f, 1.f / 6.f, 3.f / 16.f, 1.f / 4.f,
                              1.f / 3.f,  3.f / 8.f, 1.f / 2.f, 2.f / 3.f,  3.f / 4.f,
                              1.f,        3.f / 2.f, 4.f / 3.f, 2.f,        8.f / 3.f,
                              3.f,        4.f,       6.f,       8.f,        12.f,
                              16.f};
    constexpr float operator[](int i) const { return v[i]; }
    constexpr int size() const { return syncedRatesCount; }
};
static const ObSyncedRates syncedRates{};

inline float linsc(float param, const float min, const float max)
{
    return (param) * (max - min) + min;
}

inline float logsc(float param, const float min, const float max, const float rolloff = 19.f)
{
    return ((ob_exp2(param * (logf(rolloff + 1.f) * (1.f / ln2))) - 1.f) / (rolloff)) *
               (max - min) +
           min;
}

// The engine logs through this macro; on the Pico it must vanish completely.
#define OBLOG(cond, ...)

#endif // OBXF_PORT_H
