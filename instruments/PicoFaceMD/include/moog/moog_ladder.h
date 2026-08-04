/*
  moog_ladder.h -- the transistor ladder low-pass

  Four one-pole sections in series inside a feedback loop, 24 dB per octave,
  with the saturating transistor stage at the input that the whole reputation
  of this filter rests on.

  The model is Aaron Krajeski's variant of Huovilainen's, taken from the Bela
  example project named in the brief (stated by its author to be under no
  copyright). What it gets right and a linear ladder does not:

    - the resonance eats the bass. Feeding the loop back into the input
      cancels low frequencies, exactly as the original does; the gComp term
      controls how much of that is compensated for
    - it self-oscillates. The manual promises a "pure sine wave tone" with
      Emphasis at 10, and the model delivers one a little above a feedback of
      1.0, which is why the panel range reaches past that
    - the saturation is inside the loop, not after it. Overdriving the mixer
      therefore changes the shape of the resonance rather than just adding
      distortion at the end

  Header-only, for the same reason as the oscillator: this runs at the
  oversampled rate.
*/

#ifndef MOOG_LADDER_H
#define MOOG_LADDER_H

#include "moog_defs.h"
#include "moog_dsp.h"

class MoogLadder
{
public:
    void init(float sampleRate)
    {
        sr_ = sampleRate;
        reset();
        setCutoff(1000.0f);
        setResonance(0.0f);
        setDrive(1.0f);
    }

    void setSampleRate(float sampleRate) { sr_ = sampleRate; }

    void reset()
    {
        for (int i = 0; i < 5; ++i) { state_[i] = 0.0f; delay_[i] = 0.0f; }
    }

    /*
     * Cutoff in Hz. wc is the angular frequency normalised to the sample
     * rate; the polynomial is Huovilainen's fit for the tuning of the one
     * pole sections, which keeps the cutoff where it is asked for instead of
     * flattening out towards Nyquist the way a plain bilinear transform does.
     *
     * Clamped to 0.49 of the sample rate: the fit is only valid below
     * Nyquist, and beyond it the loop goes unstable rather than simply
     * sounding wrong.
     */
    void setCutoff(float hz)
    {
        hz = moogClamp(hz, 5.0f, sr_ * 0.49f);
        wc_ = 2.0f * (float) M_PI * hz / sr_;

        const float w2 = wc_ * wc_;
        const float w3 = w2 * wc_;
        const float w4 = w3 * wc_;

        g_ = 0.9892f * wc_ - 0.4342f * w2 + 0.1381f * w3 - 0.0202f * w4;

        /* The resonance correction depends on the cutoff, so it has to be
         * recomputed here as well -- otherwise a filter sweep at a fixed
         * Emphasis setting would change its resonance on the way. */
        updateRes();
    }

    /* 0 .. MOOG_RESONANCE_MAX; self-oscillation sets in a little above 1. */
    void setResonance(float r)
    {
        res_ = moogClamp(r, 0.0f, MOOG_RESONANCE_MAX);
        updateRes();
    }

    /* How hard the input is pushed into the saturating stage. */
    void setDrive(float d) { drive_ = moogClamp(d, 0.1f, 12.0f); }

    float process(float in)
    {
        /* Input stage: the signal minus the feedback, through the transistor
         * pair. gComp_ feeds a little of the input back into the loop, which
         * is what stops the resonance from hollowing out the bass entirely. */
        state_[0] = moogTanh(drive_ * (in - 4.0f * gRes_ *
                                       (state_[4] - gComp_ * in)));

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
    float drive_ = 1.0f;
    float gComp_ = 0.5f;

    float state_[5] = {};
    float delay_[5] = {};
};

#endif /* MOOG_LADDER_H */
