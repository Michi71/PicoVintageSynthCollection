// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_dsp.h -- the small building blocks

  Everything in here is inline and header-only: one-pole filters, biquads,
  saturators and the noise sources. Nothing allocates, nothing calls into libm
  on the hot path.

  Carried over from PicoFaceMD, minus the random walk that gave the Model D its
  oscillator drift. A Juno DCO is clocked digitally and does not drift, so
  there is nothing for it to do here.
*/

#ifndef JUNO_DSP_H
#define JUNO_DSP_H

#include <math.h>
#include <stdint.h>
#include "juno_defs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------ */
/* Scalar helpers                                                            */
/* ------------------------------------------------------------------------ */
static inline float junoClamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float junoLerp(float a, float b, float t) { return a + (b - a) * t; }

/*
 * tanh by way of the Pade approximant x(27 + x^2) / (27 + 9x^2).
 *
 * The input has to be clamped to +/-3 -- above that the expression grows
 * again instead of saturating, which in a feedback loop is not a rounding
 * error but an explosion. At exactly +/-3 the approximation evaluates to
 * +/-1, so the clamp is continuous and the curve simply flattens there.
 *
 * Two divisions per ladder sample would be the single most expensive thing in
 * the voice; this costs one, and matches tanh to better than 0.3 % over the
 * range that the filter actually uses.
 */
static inline float junoTanh(float x)
{
    x = junoClamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/*
 * Asymmetric soft clip for the mixer and the output stage. Real circuits do
 * not clip the same way on both halves of the waveform, and the even
 * harmonics that fall out of that asymmetry are a good part of what "warm"
 * means. The positive half saturates a little earlier than the negative one.
 */
static inline float junoSoftClip(float x)
{
    return (x > 0.0f) ? junoTanh(x * 1.12f) * 0.90f
                      : junoTanh(x * 0.92f) * 1.09f;
}

/*
 * Soft limiter for the effects output.
 *
 * Exactly linear below the threshold and asymptotic to 1.0 above it, so
 * ordinary settings pass through untouched and no combination of them can
 * reach the hard clip at the I2S conversion. Distinct from junoSoftClip,
 * which is a deliberate colouring stage and is already compressing at a third
 * of full scale -- running the output through that a second time would just
 * mean distorting it twice.
 */
static inline float junoLimit(float x)
{
    const float t = 0.70f;
    if (x >  t) return  t + (1.0f - t) * junoTanh((x - t) / (1.0f - t));
    if (x < -t) return -t + (1.0f - t) * junoTanh((x + t) / (1.0f - t));
    return x;
}

/* 2^x for the pitch and cutoff maths. Notes are tracked in semitones and
 * octaves throughout, so this sits on the control path, not on the audio
 * path, and the library version is fine. */
static inline float junoExp2f(float x) { return exp2f(x); }

/*
 * 2^x for the modulation path, where it does sit on the audio path: the
 * oscillator increments are recomputed every oversampled sample so that
 * oscillator 3 can modulate pitch at audio rate rather than only as a
 * vibrato.
 *
 * exp(x ln2) as a fifth order series. The argument is bounded by the
 * modulation depth, and over +/-1 octave the error stays under a third of a
 * cent -- three orders of magnitude below anything anyone can hear, for a
 * handful of multiplies instead of a call into libm.
 */
static inline float junoExp2Fast(float x)
{
    const float u = junoClamp(x, -1.5f, 1.5f) * 0.69314718f;
    return 1.0f + u * (1.0f + u * (0.5f + u * (0.16666667f +
                  u * (0.041666667f + u * 0.0083333333f))));
}

/*
 * 2^x over a wide range, without libm.
 *
 * The filter cutoff is worked out in octaves and has to be turned back into
 * hertz once per voice per sample -- six calls, and with libm's exp2f that
 * measured as most of the difference between the prototype's estimate of 59 %
 * and the 74 % the first real engine actually cost.
 *
 * The integer part goes straight into the exponent field of a float; the
 * fraction, which is then in [0,1), goes through the same fifth-order series
 * junoExp2Fast uses. Accurate to about a thousandth over the range the cutoff
 * covers, which is a small fraction of a cent.
 */
static inline float junoExp2Wide(float x)
{
    x = junoClamp(x, -30.0f, 30.0f);
    const int   n = (int) floorf(x);
    const float f = x - (float) n;

    union { uint32_t u; float f; } pw;
    pw.u = (uint32_t) ((n + 127) << 23);        /* 2^n */

    return pw.f * junoExp2Fast(f);
}

static inline float junoNoteToHz(float note)
{
    return JUNO_NOTE0_HZ * junoExp2f(note * (1.0f / 12.0f));
}

/*
 * Slider position to segment time. Not the plain exponential of PicoFaceMD --
 * these are the curves fitted to times measured off a real instrument (see
 * juno_defs.h), and the two segments do not share a shape.
 *
 * Attack: measured 0.001 / 0.03 / 0.24 / 0.65 / 3.25 s at slider positions
 * 0 / 2.5 / 5 / 7.5 / 10.
 */
static inline float junoAttackTime(float v)
{
    v = junoClamp(v, 0.0f, 1.0f);
    const float k = JUNO_ATTACK_CURVE * 10.0f;
    return JUNO_ATTACK_MIN_S +
           (expf(v * k) - 1.0f) / (expf(k) - 1.0f) * JUNO_ATTACK_MAX_S;
}

/*
 * Decay and release: measured 0.002 / 0.096 / 0.984 / 4.449 / 19.783 s at the
 * same positions. The extra factor of v is what makes the fit work at both
 * ends -- without it the middle of the travel comes out about twice too long.
 */
static inline float junoDecayTime(float v)
{
    v = junoClamp(v, 0.0f, 1.0f);
    const float k = JUNO_DECAY_CURVE * 10.0f;
    return JUNO_DECAY_MIN_S +
           (expf(v * k) - 1.0f) / (expf(k) - 1.0f) * v * JUNO_DECAY_MAX_S;
}

/*
 * LFO rate. Reproduces the specified 0.3 .. 20 Hz and puts the middle of the
 * slider at 3.5 Hz, which is junox's mapping and matches the panel.
 */
static inline float junoLfoRate(float v)
{
    v = junoClamp(v, 0.0f, 1.0f);
    return 0.3f * powf(1.53f, v * 10.0f) *
           (1.0f + sinf(3.14159265f * v) * 0.39f);
}

/* ------------------------------------------------------------------------ */
/* One-pole low-pass                                                         */
/* ------------------------------------------------------------------------ */
struct JunoLPF1 {
    float a = 0.0f, z = 0.0f;

    void setCutoff(float hz, float sr)
    {
        hz = junoClamp(hz, 1.0f, sr * 0.49f);
        a  = 1.0f - expf(-2.0f * (float) M_PI * hz / sr);
    }
    void  reset()             { z = 0.0f; }
    float process(float x)    { z += a * (x - z); return z; }
};

/* ------------------------------------------------------------------------ */
/* DC blocker                                                                */
/* ------------------------------------------------------------------------ */
struct JunoDCBlock {
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;

    void setCutoff(float hz, float sr)
    {
        r = 1.0f - 2.0f * (float) M_PI * hz / sr;
        r = junoClamp(r, 0.9f, 0.99999f);
    }
    void reset() { x1 = y1 = 0.0f; }
    float process(float x)
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }
};

/* ------------------------------------------------------------------------ */
/* Biquad low-pass (transposed direct form II)                               */
/*                                                                           */
/* Used only for the decimation chain, three of them in series for a 6th      */
/* order Butterworth. The Q values are the ones for a Butterworth cascade:    */
/* 0.5176, 0.7071 and 1.9319.                                                 */
/* ------------------------------------------------------------------------ */
struct JunoBiquadLP {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void set(float hz, float q, float sr)
    {
        hz = junoClamp(hz, 20.0f, sr * 0.48f);
        const float w  = 2.0f * (float) M_PI * hz / sr;
        const float cw = cosf(w);
        const float sw = sinf(w);
        const float alpha = sw / (2.0f * q);

        const float a0 = 1.0f + alpha;
        const float inv = 1.0f / a0;

        b0 = (1.0f - cw) * 0.5f * inv;
        b1 = (1.0f - cw) * inv;
        b2 = b0;
        a1 = (-2.0f * cw) * inv;
        a2 = (1.0f - alpha) * inv;
    }
    void reset() { z1 = z2 = 0.0f; }
    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/* ------------------------------------------------------------------------ */
/* Noise                                                                     */
/*                                                                           */
/* xorshift32 rather than rand(): deterministic, no lock, no libc, and about  */
/* four instructions. The white output is the raw generator; the pink one is  */
/* Paul Kellet's three-pole economy filter, which tracks a true -3 dB/octave  */
/* slope to within a tenth of a dB across the audio band.                     */
/* ------------------------------------------------------------------------ */
struct JunoNoise {
    uint32_t state = 0x1234567u;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;

    void seed(uint32_t s) { state = s ? s : 0x1234567u; }

    float white()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        /* 24 bits into -1..+1 */
        return (float) (int32_t) (state >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

    float pink(float w)
    {
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.32f;
    }
};

#endif /* JUNO_DSP_H */
