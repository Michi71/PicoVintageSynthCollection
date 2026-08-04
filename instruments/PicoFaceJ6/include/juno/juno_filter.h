// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_filter.h -- the IR3109 low-pass, and the switched high-pass

  The service notes name the part: six IR3109s, IC2, 5, 8, 11, 14 and 17 on the
  CPU board, one per voice, with a BA662 operational transconductance amplifier
  setting the amount of feedback around each.

  That is four OTA integrator sections in series inside a feedback loop -- the
  same class of circuit as the transistor ladder in a Model D, and modelled the
  same way: Huovilainen's tuning of the four one-poles, with Krajeski's
  arrangement of the feedback and one saturating stage at the input.

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
        sr_ = sampleRate;
        reset();
        setCutoff(1000.0f);
        setResonance(0.0f);
    }

    void setSampleRate(float sampleRate) { sr_ = sampleRate; }

    void reset()
    {
        for (int i = 0; i < 5; ++i) { state_[i] = 0.0f; delay_[i] = 0.0f; }
    }

    /*
     * Cutoff in Hz. wc is the angular frequency normalised to the sample rate;
     * the polynomial is Huovilainen's fit for the tuning of the one-pole
     * sections, which keeps the corner where it is asked for instead of
     * flattening out towards Nyquist as a plain bilinear transform does.
     */
    void setCutoff(float hz)
    {
        hz = junoClamp(hz, 5.0f, sr_ * 0.49f);
        wc_ = 2.0f * (float) M_PI * hz / sr_;

        const float w2 = wc_ * wc_;
        const float w3 = w2 * wc_;
        const float w4 = w3 * wc_;

        g_ = 0.9892f * wc_ - 0.4342f * w2 + 0.1381f * w3 - 0.0202f * w4;

        /* The resonance correction depends on the cutoff, so it has to come
         * out again here -- otherwise a sweep at a fixed Resonance setting
         * would change its resonance on the way. */
        updateRes();
    }

    /* 0 .. JUNO_RESONANCE_MAX; self-oscillates a little above 1, which is what
     * the specifications page means by "0 - Self Oscillation". */
    void setResonance(float r)
    {
        res_ = junoClamp(r, 0.0f, JUNO_RESONANCE_MAX);
        updateRes();
    }

    float process(float in)
    {
        /* Input stage through the saturating pair. gComp feeds part of the
         * input back into the loop; at 0.85 the low end stays where it is as
         * the resonance rises, which is the difference between an OTA cascade
         * and a transistor ladder. */
        state_[0] = junoTanh(in - 4.0f * gRes_ *
                                  (state_[4] - JUNO_VCF_GCOMP * in));

        for (int i = 0; i < 4; ++i) {
            state_[i + 1] += g_ * (0.230769f * state_[i]      /* 0.3/1.3 */
                                 + 0.769231f * delay_[i]      /* 1.0/1.3 */
                                 - state_[i + 1]);
            delay_[i] = state_[i];
        }
        return state_[4];
    }

private:
    void updateRes()
    {
        const float w2 = wc_ * wc_;
        const float w3 = w2 * wc_;
        gRes_ = res_ * (1.0029f + 0.0526f * wc_ - 0.926f * w2 + 0.0218f * w3);
    }

    float sr_    = (float) SAMPLING_RATE;
    float wc_    = 0.1f;
    float g_     = 0.1f;
    float res_   = 0.0f;
    float gRes_  = 0.0f;

    float state_[5] = {};
    float delay_[5] = {};
};

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
