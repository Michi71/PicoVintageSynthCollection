/*
  solina_dsp.h -- filter and helper building blocks

  The one-pole filters and the waveshaper are taken from string-machine
  (sources/dsp/OnePoleFilter.h, sources/dsp/AsymWaveshaper.dsp) and converted
  to single precision, because the RP2350 only has a single-precision FPU.
*/

#ifndef SOLINA_DSP_H
#define SOLINA_DSP_H

#include "solina_defs.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------ */
/* One-pole low-pass (string-machine: OnePoleLPF)                           */
/* ------------------------------------------------------------------------ */
class SolinaLPF1
{
public:
    void init(float sampleRate) { st_ = 1.0f / sampleRate; clear(); }
    void clear() { y1_ = 0.0f; }

    void setCutoff(float hz)
    {
        pole_ = expf(-2.0f * (float) M_PI * hz * st_);
    }

    inline float process(float x)
    {
        y1_ = x * (1.0f - pole_) + y1_ * pole_;
        return y1_;
    }

private:
    float st_ = 0.0f, pole_ = 0.0f, y1_ = 0.0f;
};

/* ------------------------------------------------------------------------ */
/* One-pole high-pass (string-machine: OnePoleHPF)                          */
/* ------------------------------------------------------------------------ */
class SolinaHPF1
{
public:
    void init(float sampleRate) { st_ = 1.0f / sampleRate; clear(); }
    void clear() { x1_ = 0.0f; y1_ = 0.0f; }

    void setCutoff(float hz)
    {
        pole_ = expf(-2.0f * (float) M_PI * hz * st_);
        g_    = 0.5f * (1.0f + pole_);
    }

    inline float process(float x)
    {
        const float y = g_ * (x - x1_) + pole_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float st_ = 0.0f, pole_ = 0.0f, g_ = 0.0f, x1_ = 0.0f, y1_ = 0.0f;
};

/* ------------------------------------------------------------------------ */
/* Biquad (RBJ cookbook): low-pass with Q, and high shelf                   */
/* ------------------------------------------------------------------------ */
class SolinaBiquad
{
public:
    void init(float sampleRate) { sr_ = sampleRate; clear(); }
    void clear() { z1_ = z2_ = 0.0f; }

    void setLowpass(float hz, float q)
    {
        const float w = 2.0f * (float) M_PI * hz / sr_;
        const float cw = cosf(w), sw = sinf(w);
        const float alpha = sw / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0_ = ((1.0f - cw) * 0.5f) / a0;
        b1_ = (1.0f - cw) / a0;
        b2_ = b0_;
        a1_ = (-2.0f * cw) / a0;
        a2_ = (1.0f - alpha) / a0;
    }

    void setHighShelf(float hz, float gainDb, float q)
    {
        const float A  = powf(10.0f, gainDb / 40.0f);
        const float w  = 2.0f * (float) M_PI * hz / sr_;
        const float cw = cosf(w), sw = sinf(w);
        const float alpha = sw / (2.0f * q);
        const float tsa = 2.0f * sqrtf(A) * alpha;
        const float a0 = (A + 1.0f) - (A - 1.0f) * cw + tsa;
        b0_ = (A * ((A + 1.0f) + (A - 1.0f) * cw + tsa)) / a0;
        b1_ = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
        b2_ = (A * ((A + 1.0f) + (A - 1.0f) * cw - tsa)) / a0;
        a1_ = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
        a2_ = ((A + 1.0f) - (A - 1.0f) * cw - tsa) / a0;
    }

    /* Transposed Direct Form II */
    inline float process(float x)
    {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    float sr_ = 44100.0f;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

/* ------------------------------------------------------------------------ */
/* Asymmetric soft clipper (string-machine: AsymWaveshaper.dsp)              */
/*                                                                           */
/*     kubic(k,x) = x - (k*x)^3/3                                            */
/*     nl(k,x)    = x > 0 ? x : kubic(k, max(x, lm(k)))                      */
/*     lm(k)      = -sqrt(k^3)/k^3   (local minimum)                         */
/*     y          = nl(k, x - 2/3) + 2/3                                     */
/*                                                                           */
/* Models the one-sided limiting of the gate circuit; it produces the even   */
/* harmonics that give the sound its "strings" roughness.                    */
/* ------------------------------------------------------------------------ */
class SolinaShaper
{
public:
    void setAmount(float k)
    {
        if (k < 0.1f) k = 0.1f;
        if (k > 1.0f) k = 1.0f;
        k_  = k;
        lm_ = -sqrtf(k * k * k) / (k * k * k);

        /*
         * The curve does not pass through the origin: at input 0 it returns a
         * DC offset (0.099 at amount 1.0). Summed over five keyboard groups
         * and two string registers, that produces a step at power-up which
         * the DC blocker behind it only removes over some milliseconds --
         * audible as a plop.
         *
         * string-machine hangs a fi.dcblockerat(35.) directly behind the
         * waveshaper in AsymWaveshaper.dsp. Subtracting the zero point gives
         * the same result without a filter: no settling time, no phase shift
         * in the bass, no CPU cost.
         */
        offset_ = shape(0.0f);
    }

    inline float process(float x) const { return shape(x) - offset_; }

private:
    inline float shape(float x) const
    {
        const float z = 2.0f / 3.0f;
        float v = x - z;
        if (v <= 0.0f)
        {
            if (v < lm_) v = lm_;
            const float kx = k_ * v;
            v = v - kx * kx * kx * (1.0f / 3.0f);
        }
        return v + z;
    }

    float k_ = 1.0f;
    float lm_ = -1.0f;
    float offset_ = 0.0f;
};

/* ------------------------------------------------------------------------ */
/* DC blocker                                                                */
/* ------------------------------------------------------------------------ */
class SolinaDCBlock
{
public:
    void init(float sampleRate, float hz = 20.0f)
    {
        r_ = 1.0f - (2.0f * (float) M_PI * hz / sampleRate);
        clear();
    }
    void clear() { x1_ = y1_ = 0.0f; }

    inline float process(float x)
    {
        const float y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float r_ = 0.999f, x1_ = 0.0f, y1_ = 0.0f;
};

/* ------------------------------------------------------------------------ */
/* Sine table for the control oscillators                                    */
/*                                                                           */
/* The ensemble needs six sine values per sample (two rows times three       */
/* phases). On the RP2350 that would be roughly 265000 sinf calls a second,  */
/* and the Cortex-M33 computes those in software. string-machine solves it   */
/* the same way in LFO3PhaseDual.dsp, through a small interpolated table     */
/* (smallTableSin, 128 entries).                                             */
/* ------------------------------------------------------------------------ */
#define SOLINA_SINTAB_BITS 8
#define SOLINA_SINTAB_SIZE (1 << SOLINA_SINTAB_BITS)

class SolinaSineTable
{
public:
    static void init()
    {
        if (inited_)
            return;
        inited_ = true;
        for (int i = 0; i <= SOLINA_SINTAB_SIZE; ++i)
            tab_[i] = sinf(2.0f * (float) M_PI * ((float) i)
                           / (float) SOLINA_SINTAB_SIZE);
    }

    /* Phase 0..1 -> sine, linearly interpolated */
    static inline float lookup(float phase)
    {
        const float x  = phase * (float) SOLINA_SINTAB_SIZE;
        const int   i  = (int) x;
        const float mu = x - (float) i;
        const int   j  = i & (SOLINA_SINTAB_SIZE - 1);
        return tab_[j] + (tab_[j + 1] - tab_[j]) * mu;
    }

private:
    static bool  inited_;
    /* one entry more, so the interpolation needs no special case */
    static float tab_[SOLINA_SINTAB_SIZE + 1];
};

/* ------------------------------------------------------------------------ */
/* Soft limiting of the output stage                                         */
/*                                                                           */
/* The Output Amplifier of the original limits softly against its +/-15 V    */
/* rails. Below the threshold the curve is exactly linear, above it the      */
/* curve approaches 1.0 asymptotically -- so the output can never exceed     */
/* 0 dBFS while normal playing is left completely untouched.                 */
/* ------------------------------------------------------------------------ */
static inline float solinaSoftClip(float x, float threshold)
{
    const float a = (x < 0.0f) ? -x : x;
    if (a <= threshold)
        return x;

    const float over = (a - threshold) / (1.0f - threshold);
    const float y = threshold + (1.0f - threshold) * tanhf(over);
    return (x < 0.0f) ? -y : y;
}

/* ------------------------------------------------------------------------ */
/* Band-limited sawtooth (polyBLEP)                                          */
/*                                                                           */
/* Phase and step size come from the divider chain, so they are exact powers */
/* of two relative to each other -- just as in the original, where every     */
/* note is divided down from one master oscillator.                          */
/* ------------------------------------------------------------------------ */
static inline float solinaPolyBlep(float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float solinaBlSaw(float phase, float dt)
{
    return (2.0f * phase - 1.0f) - solinaPolyBlep(phase, dt);
}

#endif /* SOLINA_DSP_H */
