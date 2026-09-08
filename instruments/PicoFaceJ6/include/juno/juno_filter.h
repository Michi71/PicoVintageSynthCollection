// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_filter.h -- the IR3109 low-pass, and the switched high-pass

  The service notes name the part: six IR3109s, IC2, 5, 8, 11, 14 and 17 on the
  CPU board, one per voice, with a BA662 operational transconductance amplifier
  setting the amount of feedback around each.

  That is four OTA integrator sections in series inside a feedback loop -- the
  same class of circuit as the transistor ladder in a Model D. The four
  one-poles are topology-preserving transforms and the loop around them is
  solved for the current sample rather than delayed, with one saturating stage
  at the input.

  It was Huovilainen's polynomial fit before, and that fit is only good to
  about one radian per sample: past that its resonance term crosses zero and
  the feedback turns positive. The filter therefore carried a ceiling at
  sr/2pi -- 7.0 kHz at this rate, and in practice a resonant peak that stopped
  at 3.8 kHz. Every patch with the cutoff slider past the middle was duller
  than it should be, which is what measuring against Roland's plugin showed.
  A TPT ladder is tuned correctly at every frequency up to Nyquist, so the
  ceiling is gone and nothing takes its place.

  junox models it as a diode ladder instead. A diode ladder is an EMS and
  TB-303 topology; it is not an IR3109, and it costs about twice as much to
  run. That junox offers a Moog filter as an alternative in the same patch
  format suggests its authors were not certain either.

  What separates this from the Model D filter, and it is audible rather than
  academic: a transistor ladder takes its feedback from inside the ladder, so
  the low end drains away as the resonance comes up. An OTA cascade with a
  separate feedback amplifier does not. That is the gComp term, and it is set
  to 0.85 here against 0.5 for the Moog -- a Juno with the resonance up is
  never thin, and that is most of why its filter sweeps sound the way they do.

  Specifications page: RESONANCE (0 - Self Oscillation).
*/

#ifndef JUNO_FILTER_H
#define JUNO_FILTER_H

#include "juno_defs.h"
#include "juno_dsp.h"

class JunoFilter
{
public:
    void init(float sampleRate)
    {
        setSampleRate(sampleRate);
        reset();
        setCutoffOct(5.0f);
        setResonance(0.0f);
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        buildTable(sampleRate);
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i) s_[i] = 0.0f;
    }

    /*
     * Cutoff in octaves above JUNO_CUTOFF_MIN_HZ, which is the form everything
     * upstream already has: the panel, the contour, key follow and the LFO all
     * add in octaves, exactly as their control voltages do in the instrument.
     * Taking octaves here saves the exponential the voice used to do, and the
     * table is indexed by them directly.
     */
    void setCutoffOct(float oct)
    {
        float x = (oct - kOctMin) * kPerOct;
        if (x < 0.0f) x = 0.0f;
        if (x > (float) (kTableN - 1)) x = (float) (kTableN - 1);
        const int   i = (int) x;
        const float f = x - (float) i;
        g_ = table_[i] + f * (table_[i + 1] - table_[i]);   /* g/(1+g) */
        updateCoeffs();
    }

    /* Kept for callers that think in Hz -- the host tests do. */
    void setCutoff(float hz)
    {
        setCutoffOct(log2f(junoClamp(hz, 1.0f, 1.0e6f) / JUNO_CUTOFF_MIN_HZ));
    }

    /* 0 .. JUNO_RESONANCE_MAX; self-oscillates a little above 1, which is what
     * the specifications page means by "0 - Self Oscillation". The ladder
     * sings at a loop gain of four, so the panel value is the loop gain over
     * four and needs no frequency correction of its own -- that correction
     * existed only to patch up the polynomial fit. */
    void setResonance(float r)
    {
        r = junoClamp(r, 0.0f, JUNO_RESONANCE_MAX);
        if (r != res_) { res_ = r; updateCoeffs(); }
    }

    float process(float in)
    {
        /*
         * gComp lifts the input as the feedback rises, so the low end stays
         * where it is instead of draining away: that is the difference between
         * an OTA cascade with its own feedback amplifier and a transistor
         * ladder, and most of why a Juno with the resonance up is never thin.
         */
        const float x = in * (1.0f + k_ * gComp_);

        /* What the four stages will contribute from their current states, so
         * the loop can be closed on this sample instead of the last one. */
        const float S = c3_ * s_[0] + c2_ * s_[1] + c1_ * s_[2] + c0_ * s_[3];

        float y = junoTanh((x - k_ * S) * invDen_);

        for (int i = 0; i < 4; ++i) {
            const float v = (y - s_[i]) * g_;
            y    = v + s_[i];
            s_[i] = y + v;
        }
        return y;
    }

private:
    void updateCoeffs()
    {
        k_ = 4.0f * res_;
        /* How much of the input is fed forward to hold the low end up as the
         * feedback rises. Measured against Roland's plugin -- see
         * kJunoVcfGComp -- and not the flat 0.85 that used to stand here. */
        gComp_ = junoVcfGComp(res_ * (1.0f / JUNO_RESONANCE_MAX));
        const float om = 1.0f - g_;
        const float g2 = g_ * g_;
        c0_ = om;
        c1_ = g_ * om;
        c2_ = g2 * om;
        c3_ = g2 * g_ * om;
        invDen_ = 1.0f / (1.0f + k_ * g2 * g2);
    }

    /*
     * g/(1+g) with g = tan(pi f / sr), tabulated against the cutoff in
     * octaves. A tangent per voice per sample is the one thing this filter
     * cannot afford -- setCutoffOct is called six times per sample -- and the
     * curve is smooth enough that 32 points per octave interpolate to better
     * than a thousandth. Above Nyquist the entry saturates at 1, which makes
     * the stage a wire: the right answer for a filter asked to open further
     * than the sample rate can express.
     */
    static constexpr float kOctMin = -3.0f;   /* 2.5 Hz  */
    static constexpr float kOctMax = 16.0f;   /* 1.3 MHz, clamped to Nyquist */
    static constexpr int   kPerOct = 32;
    static constexpr int   kTableN = (int) ((kOctMax - kOctMin) * kPerOct) + 1;

    static void buildTable(float sr)
    {
        if (sr == tableSr_) return;
        tableSr_ = sr;
        const float nyq = 0.4995f * sr;
        for (int i = 0; i < kTableN + 1; ++i) {
            float hz = JUNO_CUTOFF_MIN_HZ *
                       exp2f(kOctMin + (float) i / (float) kPerOct);
            if (hz > nyq) hz = nyq;
            const float g = tanf((float) M_PI * hz / sr);
            table_[i] = g / (1.0f + g);
        }
    }

    static float table_[kTableN + 1];
    static float tableSr_;

    float sr_   = (float) SAMPLING_RATE;
    float g_    = 0.1f;      /* g/(1+g) of one stage */
    float res_  = -1.0f;
    float k_    = 0.0f;      /* loop gain, four at self-oscillation */
    float c0_ = 0.0f, c1_ = 0.0f, c2_ = 0.0f, c3_ = 0.0f;
    float gComp_  = 0.49f;
    float invDen_ = 1.0f;

    float s_[4] = {};
};

inline float JunoFilter::table_[JunoFilter::kTableN + 1] = {};
inline float JunoFilter::tableSr_ = 0.0f;

/* ------------------------------------------------------------------------ */
/* High-pass                                                                 */
/*                                                                           */
/* One for all six voices, sitting after the sum. In the instrument it is an   */
/* HD14051B switching between three capacitors and a bypass, so the control    */
/* has four positions and nothing in between -- there is no continuous corner  */
/* frequency to sweep.                                                        */
/*                                                                           */
/* Corners from the component values, F = 1/(2*pi*R*C): 154, 339 and 720 Hz,   */
/* one pole each. junox uses 250 / 520 / 1220 Hz and says in a comment that it */
/* kept them because they sound good rather than because they are right.       */
/* ------------------------------------------------------------------------ */
class JunoHpf
{
public:
    void init(float sampleRate)
    {
        sr_ = sampleRate;
        setPosition(0);
        lp_.reset();
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        setPosition(pos_);
    }

    void setPosition(int p)
    {
        pos_ = (p < 0) ? 0 : (p >= JUNO_HPF_POSITIONS ? JUNO_HPF_POSITIONS - 1 : p);
        static const float kHz[JUNO_HPF_POSITIONS] = {
            0.0f, JUNO_HPF_HZ_1, JUNO_HPF_HZ_2, JUNO_HPF_HZ_3
        };
        hz_ = kHz[pos_];
        if (hz_ > 0.0f) lp_.setCutoff(hz_, sr_);
    }

    void reset() { lp_.reset(); }

    float process(float in)
    {
        /* Position 0 is a straight bypass, not a very low corner: the switch
         * takes the capacitor out of circuit entirely. */
        if (hz_ <= 0.0f) return in;
        /* A one-pole high-pass as the difference from its low-pass. */
        return in - lp_.process(in);
    }

private:
    JunoLPF1 lp_;
    float    hz_  = 0.0f;
    int      pos_ = 0;
    float    sr_  = (float) SAMPLING_RATE;
};

#endif /* JUNO_FILTER_H */
