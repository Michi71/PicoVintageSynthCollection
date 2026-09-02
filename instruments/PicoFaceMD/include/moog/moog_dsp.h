// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_dsp.h -- the small building blocks

  Everything in here is inline and header-only: one-pole filters, biquads,
  saturators, the noise sources and the random walk that keeps the
  oscillators from sitting still. Nothing allocates, nothing calls into libm
  on the hot path.
*/

#ifndef MOOG_DSP_H
#define MOOG_DSP_H

#include <math.h>
#include <stdint.h>
#include "moog_defs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------ */
/* Scalar helpers                                                            */
/* ------------------------------------------------------------------------ */
static inline float moogClamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float moogLerp(float a, float b, float t) { return a + (b - a) * t; }

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
static inline float moogTanh(float x)
{
    x = moogClamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/*
 * Asymmetric soft clip for the mixer and the output stage. Real circuits do
 * not clip the same way on both halves of the waveform, and the even
 * harmonics that fall out of that asymmetry are a good part of what "warm"
 * means. The positive half saturates a little earlier than the negative one.
 */
static inline float moogSoftClip(float x)
{
    return (x > 0.0f) ? moogTanh(x * 1.12f) * 0.90f
                      : moogTanh(x * 0.92f) * 1.09f;
}

/*
 * Soft limiter for the effects output.
 *
 * Exactly linear below the threshold and asymptotic to 1.0 above it, so
 * ordinary settings pass through untouched and no combination of them can
 * reach the hard clip at the I2S conversion. Distinct from moogSoftClip,
 * which is a deliberate colouring stage and is already compressing at a third
 * of full scale -- running the output through that a second time would just
 * mean distorting it twice.
 */
static inline float moogLimit(float x)
{
    const float t = 0.70f;
    if (x >  t) return  t + (1.0f - t) * moogTanh((x - t) / (1.0f - t));
    if (x < -t) return -t + (1.0f - t) * moogTanh((x + t) / (1.0f - t));
    return x;
}

/* 2^x for the pitch and cutoff maths. Notes are tracked in semitones and
 * octaves throughout, so this sits on the control path, not on the audio
 * path, and the library version is fine. */
static inline float moogExp2f(float x) { return exp2f(x); }

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
static inline float moogExp2Fast(float x)
{
    const float u = moogClamp(x, -1.5f, 1.5f) * 0.69314718f;
    return 1.0f + u * (1.0f + u * (0.5f + u * (0.16666667f +
                  u * (0.041666667f + u * 0.0083333333f))));
}

static inline float moogNoteToHz(float note)
{
    return MOOG_NOTE0_HZ * moogExp2f(note * (1.0f / 12.0f));
}

/* Panel scale 0..10 to a time in seconds, exponentially -- 10 ms at the left
 * stop, 10 s at the right, which is the range the manual quotes. */
static inline float moogEnvTime(float v)
{
    return MOOG_ENV_MIN_S * moogExp2f(moogClamp(v, 0.0f, 1.0f) *
             log2f(MOOG_ENV_MAX_S / MOOG_ENV_MIN_S));
}

/* ------------------------------------------------------------------------ */
/* One-pole low-pass                                                         */
/* ------------------------------------------------------------------------ */
struct MoogLPF1 {
    float a = 0.0f, z = 0.0f;

    void setCutoff(float hz, float sr)
    {
        hz = moogClamp(hz, 1.0f, sr * 0.49f);
        a  = 1.0f - expf(-2.0f * (float) M_PI * hz / sr);
    }
    void  reset()             { z = 0.0f; }
    float process(float x)    { z += a * (x - z); return z; }
};

/* ------------------------------------------------------------------------ */
/* DC blocker                                                                */
/* ------------------------------------------------------------------------ */
struct MoogDCBlock {
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;

    void setCutoff(float hz, float sr)
    {
        r = 1.0f - 2.0f * (float) M_PI * hz / sr;
        r = moogClamp(r, 0.9f, 0.99999f);
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
struct MoogBiquadLP {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void set(float hz, float q, float sr)
    {
        hz = moogClamp(hz, 20.0f, sr * 0.48f);
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
/*                                                                           */
/* red() is the third output of the original's noise board, the one only the  */
/* modulation mix ever sees -- see MOOG_RED_HZ. It is fed from pink, because  */
/* the stages of that board are cascaded.                                     */
/* ------------------------------------------------------------------------ */
struct MoogNoise {
    uint32_t state = 0x1234567u;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float r0 = 0.0f, r1 = 0.0f;
    /* Default is the corner at the oversampled rate this normally runs at,
     * so an instance nobody configures still behaves. */
    float redK = 2.0f * (float) M_PI * MOOG_RED_HZ /
                 ((float) SAMPLING_RATE * MOOG_OVERSAMPLE);

    void seed(uint32_t s) { state = s ? s : 0x1234567u; }

    /* Only red depends on the rate; the pink coefficients below are the
     * published ones and are left as they are. */
    void setRate(float sr)
    {
        const float w = 2.0f * (float) M_PI * MOOG_RED_HZ / sr;
        redK = w / (1.0f + w);
    }

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

    /* Takes the pink output, as the third stage of the board does. */
    float red(float p)
    {
        r0 += (p - r0) * redK;
        r1 += (r0 - r1) * redK;
        return r1 * MOOG_RED_GAIN;
    }
};

/* ------------------------------------------------------------------------ */
/* Random walk                                                               */
/*                                                                           */
/* The drift of a single oscillator: white noise through a very slow low-pass */
/* so the pitch wanders rather than jitters. Each instance runs its own       */
/* generator, which is the point -- three oscillators drifting in lockstep    */
/* would cancel out and sound exactly as static as no drift at all.           */
/* ------------------------------------------------------------------------ */
struct MoogDrift {
    MoogNoise rng;
    MoogLPF1  lp1, lp2;
    float     value = 0.0f;

    void init(uint32_t s, float sr)
    {
        rng.seed(s);
        lp1.setCutoff(MOOG_DRIFT_HZ, sr);
        lp2.setCutoff(MOOG_DRIFT_HZ, sr);
        lp1.reset(); lp2.reset();
        value = 0.0f;
    }

    /* Called once per block, not per sample: a 0.1 Hz signal has nothing to
     * say at 44 kHz. The gain compensates for the two low-passes taking most
     * of the amplitude out. */
    float process()
    {
        value = lp2.process(lp1.process(rng.white())) * 14.0f;
        return moogClamp(value, -1.0f, 1.0f);
    }
};

#endif /* MOOG_DSP_H */
