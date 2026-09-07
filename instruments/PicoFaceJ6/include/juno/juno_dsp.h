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
/*
 * The DCO's level sliders -- sub-oscillator and noise -- against the level
 * they actually produce, measured off Roland's plugin at 0.05 steps. Both
 * sliders follow it to a hundredth of a decibel of each other, so it is one
 * taper and not two.
 *
 * It is a fader taper in two straight-ish halves with an exact knee at the
 * middle: the lower half is linear at 0.406 of the setting, so the slider at
 * a half reaches only a fifth of full level, and the upper half covers the
 * remaining 14 dB. Reading it as linear -- which is what this did -- makes the
 * sub eight decibels too loud through the middle of its travel, and most of
 * the factory patches that use it sit exactly there.
 */
/*
 * What a VCF modulation-depth slider is worth, as a fraction of its full
 * scale. Measured off Roland's plugin at 0.05 steps by tracking a resonant
 * peak -- once with a very slow LFO driving the corner and once with the
 * contour holding it -- and the two came out the same curve to within a
 * hundredth at every point. One taper, one slider design, two destinations.
 *
 * It is nothing like the straight line these used to be. The bottom third of
 * the travel barely moves the filter at all: a hundredth of full scale at 0.1,
 * a twentieth at 0.2. Thirteen of the fifteen factory patches that use the LFO
 * route sit between 0.10 and 0.30, and the contour route is on nearly every
 * patch in the bank -- so the straight line was wrong where it mattered most,
 * by three to ten times.
 *
 * The two full scales differ: 3.6 octaves either side for the LFO,
 * JUNO_CONTOUR_OCTAVES for the contour.
 */
static const float kJunoVcfDepth[21] = {
    0.0000f, 0.0022f, 0.0064f, 0.0170f, 0.0381f, 0.0778f, 0.1226f,
    0.1810f, 0.2558f, 0.3267f, 0.4117f, 0.4879f, 0.5538f, 0.6283f,
    0.6897f, 0.7401f, 0.7948f, 0.8315f, 0.8704f, 0.9411f, 1.0000f
};

static inline float junoVcfDepth(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    const float x = v * 20.0f;
    const int   i = (int) x;
    const float f = x - (float) i;
    return kJunoVcfDepth[i] + f * (kJunoVcfDepth[i + 1] - kJunoVcfDepth[i]);
}

/*
 * The cutoff slider against the corner it produces, in octaves above
 * JUNO_CUTOFF_MIN_HZ, measured off Roland's plugin at 0.05 steps by reading
 * the self-oscillation.
 *
 * It was a straight line in octaves with the ends clamped, which is right from
 * a third of the travel upward and wrong below it: the plugin's corner keeps
 * falling to 12.3 Hz at the bottom of the slider where the clamp held it at
 * 20, and the clamped stretch reached a sixth of the way up. That is half an
 * octave, and it does not stay quiet -- a Juno patch routinely leaves the
 * cutoff near zero and lets the contour do the work, so the whole contour
 * rides on it. UFO, Clavichord 1 and Reed 1 all sit there.
 *
 * The top is held at the specification's 18 kHz from 0.75 up, which is where
 * the plugin's own reading passes it and stops being measurable.
 */
static const float kJunoCutoffOct[21] = {
    -0.697f, -0.515f, -0.179f,  0.263f,  0.861f,  1.561f,  2.288f,
     3.132f,  4.027f,  4.950f,  5.881f,  6.734f,  7.632f,  8.485f,
     9.215f,  9.813f,  9.813f,  9.813f,  9.813f,  9.813f,  9.813f
};

static inline float junoCutoffOct(float v)
{
    if (v <= 0.0f) return kJunoCutoffOct[0];
    if (v >= 1.0f) return kJunoCutoffOct[20];
    const float x = v * 20.0f;
    const int   i = (int) x;
    const float f = x - (float) i;
    return kJunoCutoffOct[i] + f * (kJunoCutoffOct[i + 1] - kJunoCutoffOct[i]);
}

/*
 * The resonance compensation, against the panel's resonance setting.
 *
 * A transistor ladder takes its feedback from inside the ladder, so the low
 * end drains away as the resonance comes up; an OTA cascade with a separate
 * feedback amplifier does not, and this term is how much of the input is fed
 * forward to hold it. It stood at a flat 0.85 on that reasoning -- "a Juno
 * with the resonance up is never thin" -- and the reasoning was never
 * measured.
 *
 * Roland's plugin says a Juno does get thinner: its passband loses 7.2 dB
 * between no resonance and full, where ours lost 1.9. Measured at 0.05 steps
 * on a fundamental well inside the passband, and turned into the term this
 * topology needs by (1 + k*g)/(1 + k). It comes out near the Model D's 0.5 at
 * the bottom of the travel and falls to a third at the top.
 *
 * Left as it was, a resonant patch carried about 4.4 dB too much gain across
 * the whole band and a bass bump the plugin does not have -- which is most of
 * what was left on the organs once the VCA switch stopped opening their
 * filter.
 */
static const float kJunoVcfGComp[21] = {
    0.4899f, 0.4899f, 0.4805f, 0.4814f, 0.4728f, 0.4661f, 0.4635f,
    0.4577f, 0.4525f, 0.4476f, 0.4429f, 0.4396f, 0.4349f, 0.4301f,
    0.4259f, 0.4175f, 0.3995f, 0.3810f, 0.3632f, 0.3488f, 0.3347f
};

static inline float junoVcfGComp(float res01)
{
    if (res01 <= 0.0f) return kJunoVcfGComp[0];
    if (res01 >= 1.0f) return kJunoVcfGComp[20];
    const float x = res01 * 20.0f;
    const int   i = (int) x;
    const float f = x - (float) i;
    return kJunoVcfGComp[i] + f * (kJunoVcfGComp[i + 1] - kJunoVcfGComp[i]);
}

static const float kJunoDcoLevel[21] = {
    0.0000f, 0.0206f, 0.0412f, 0.0602f, 0.0808f, 0.1014f, 0.1204f,
    0.1410f, 0.1616f, 0.1822f, 0.2028f, 0.2311f, 0.2861f, 0.3613f,
    0.4442f, 0.5436f, 0.6479f, 0.7514f, 0.8487f, 0.9285f, 1.0000f
};

static inline float junoDcoLevel(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    const float x = v * 20.0f;
    const int   i = (int) x;
    const float f = x - (float) i;
    return kJunoDcoLevel[i] + f * (kJunoDcoLevel[i + 1] - kJunoDcoLevel[i]);
}

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
/*
 * The attack slider against the time it takes, measured off Roland's plugin at
 * 0.05 steps as the time to half amplitude (with a Hilbert envelope on a
 * filtered tone, because a boxcar over a raw sawtooth quantises the reading
 * badly at the short end) and divided by the 0.400 of a segment that our own
 * attack curve needs to get there.
 *
 * The shape it replaces was junox's, and only the range under it came from the
 * specifications page. It ran nearly three times slow over the first third of
 * the travel and a sixth fast at the top.
 *
 * Both ends against the specification page's 1 ms .. 3 s: the bottom comes out
 * at 1.25 ms, and the top at 3.6 s -- 20 % over. That is well inside how
 * approximate these figures are; the same page puts the decay at 12 s where a
 * measured instrument gave 19.8.
 */
static const float kJunoAttackTime[21] = {
    0.00125f, 0.0030f, 0.0050f, 0.0095f, 0.0168f, 0.0283f, 0.0455f,
    0.0720f,  0.1098f, 0.1610f, 0.2275f, 0.3063f, 0.4125f, 0.5470f,
    0.7008f,  0.9125f, 1.1838f, 1.5440f, 2.0285f, 2.6445f, 3.5985f
};

static inline float junoAttackTime(float v)
{
    v = junoClamp(v, 0.0f, 1.0f);
    const float x = v * 20.0f;
    const int   i = (int) x;
    if (i >= 20) return kJunoAttackTime[20];
    const float f = x - (float) i;
    return kJunoAttackTime[i] + f * (kJunoAttackTime[i + 1] - kJunoAttackTime[i]);
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
