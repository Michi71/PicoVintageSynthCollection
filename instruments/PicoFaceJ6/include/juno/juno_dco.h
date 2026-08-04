// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_dco.h -- the digitally controlled oscillator

  One per voice, and unlike the three oscillators of a Model D it produces all
  of its waveforms at once from a single phase: a sawtooth, a pulse with
  variable width, and a square one octave down. Each has its own switch in the
  mixer, and the noise source is summed in alongside them.

  What makes it a DCO rather than a VCO is that the timing is digital, so it
  does not drift. That removes the whole business of modelling instability
  that the Model D needed -- and it means two Junos playing the same note in
  unison beat against each other only as much as their tuning is set to.

  junox models the digital clock as round(sampleRate / frequency), quantising
  the period to whole audio samples. That is not what the hardware does: the
  counter runs in the megahertz, so its steps are far finer than one audio
  sample. Reproducing it at 44.1 kHz would put the top octave tens of cents
  out of tune -- a note at 4 kHz would land on a period of 11 samples, which is
  4009 Hz. A float accumulator is both correct and cheaper, so that is what is
  used here; the stability that makes a DCO a DCO comes for free.

  Header-only: this is called once per voice per oversampled step, which at six
  voices and 2x is over half a million times a second.
*/

#ifndef JUNO_DCO_H
#define JUNO_DCO_H

#include "juno_defs.h"
#include "juno_dsp.h"

/*
 * polyBLEP correction around a step discontinuity. Within one sample either
 * side of an edge the ideal step is replaced by the integral of a windowed
 * impulse, which takes the worst of the aliasing out for two comparisons on
 * most samples.
 */
static inline float junoPolyBlep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

class JunoDco
{
public:
    void init(float sampleRate, uint32_t seed)
    {
        sr_    = sampleRate;
        noise_.seed(seed);
        noiseLp_.setCutoff(JUNO_NOISE_LP_HZ, sampleRate);
        noiseLp_.reset();
        /* Free-running from a phase of its own. The instrument does not reset
         * its oscillators on a key press either. */
        phase_    = (float) (seed & 0xFFFFu) / 65536.0f;
        subPhase_ = phase_ * 0.5f;
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        noiseLp_.setCutoff(JUNO_NOISE_LP_HZ, sampleRate);
    }

    /* Unmodulated phase increment, worked out once per output sample by the
     * voice. Scaled per oversampled step for the LFO. */
    void setIncrement(float inc) { inc_ = junoClamp(inc, 0.0f, 0.49f); }

    /* Mixer switches and levels, all 0..1. The sawtooth and pulse are on or
     * off in the original; the sub and the noise have level controls. */
    void setLevels(bool saw, bool pulse, float subLevel, float noiseLevel)
    {
        saw_   = saw;
        pulse_ = pulse;
        sub_   = junoClamp(subLevel, 0.0f, 1.0f);
        nz_    = junoClamp(noiseLevel, 0.0f, 1.0f);
    }

    void setPulseWidth(float pw)
    {
        pw_ = junoClamp(pw, JUNO_PW_MIN, JUNO_PW_MAX);
    }

    float process(float pitchMul)
    {
        const float dt = inc_ * pitchMul;
        const float t  = phase_;

        float out = 0.0f;

        if (saw_) {
            /* The Juno sawtooth falls rather than rises, which matters only
             * for how it sums with the pulse. */
            out += (2.0f * t - 1.0f) - junoPolyBlep(t, dt);
        }

        if (pulse_) {
            float p = (t < pw_) ? 1.0f : -1.0f;
            p += junoPolyBlep(t, dt);
            float t2 = t - pw_;
            if (t2 < 0.0f) t2 += 1.0f;
            p -= junoPolyBlep(t2, dt);
            /* A narrow pulse carries far less energy than a square. Without
             * this the pulse-width control would double as a volume
             * control. */
            out += p * (0.6f + 0.8f * (pw_ - JUNO_PW_MIN) /
                                      (JUNO_PW_MAX - JUNO_PW_MIN));
        }

        if (sub_ > 0.0f) {
            const float dts = dt * 0.5f;
            float s = (subPhase_ < 0.5f) ? 1.0f : -1.0f;
            s += junoPolyBlep(subPhase_, dts);
            float t3 = subPhase_ - 0.5f;
            if (t3 < 0.0f) t3 += 1.0f;
            s -= junoPolyBlep(t3, dts);
            out += s * sub_;

            subPhase_ += dts;
            if (subPhase_ >= 1.0f) subPhase_ -= 1.0f;
        } else {
            /* Kept running even when silent, so switching the sub on does not
             * produce a click from a stale phase. */
            subPhase_ += dt * 0.5f;
            if (subPhase_ >= 1.0f) subPhase_ -= 1.0f;
        }

        if (nz_ > 0.0f) {
            /* Low-passed at 5 kHz -- the note on the AR80017A filter clone
             * says the instrument's noise source is, and without it the noise
             * sits on top of the tone instead of inside it. */
            out += noiseLp_.process(noise_.white()) * nz_ * 1.6f;
        }

        phase_ += dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        return out;
    }

private:
    float     phase_    = 0.0f;
    float     subPhase_ = 0.0f;
    float     inc_      = 0.0f;
    float     pw_       = JUNO_PW_MIN;
    bool      saw_      = true;
    bool      pulse_    = false;
    float     sub_      = 0.0f;
    float     nz_       = 0.0f;
    JunoNoise noise_;
    JunoLPF1  noiseLp_;
    float     sr_ = (float) SAMPLING_RATE;
};

#endif /* JUNO_DCO_H */
