// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_env.h -- contour generator (IR3201)

  One per voice, feeding the amplifier and, with an amount and a polarity, the
  filter. Four controls: attack, decay, sustain, release.

  This is built differently from the Model D envelope, and the reason is a
  measurement rather than a preference.

  PicoFaceMD charges towards a target the way an RC network does, so the time
  to reach the sustain level shortens as that level rises -- which is what
  most analogue envelopes do. A Juno measurably does not. With the sustain at
  0 the decay slider at 10 gave 19.78 s; with the sustain at 5 it gave 17.11 s,
  near enough the same. So the segments here have a fixed duration and the
  level is a function of how far through the segment we are.

  The slopes are curved, from the same measurements:

      attack   (1 - e^-x) / 0.632          measured 0.224 at half a second of a
                                           3.25 s rise, where a straight line
                                           would be at 0.154
      falling  target + (1-target) e^-4.6x  measured 1.000 / 0.764 / 0.616 /
                                           0.511 at 0/1/2/3 s of a 19.8 s
                                           decay; this gives 0.793 / 0.628 /
                                           0.498, a straight line 0.949 /
                                           0.899 / 0.848

  junox interpolates both linearly, which is where its envelopes lose the
  shape.
*/

#ifndef JUNO_ENV_H
#define JUNO_ENV_H

#include "juno_defs.h"
#include "juno_dsp.h"

class JunoEnv
{
public:
    enum Stage { IDLE = 0, ATTACK, DECAY, SUSTAIN, RELEASE };

    void init(float sampleRate)
    {
        sr_ = sampleRate;
        reset();
        setAttack(0.0f);
        setDecay(0.4f);
        setSustain(0.7f);
        setRelease(0.3f);
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        setAttack(aPanel_);
        setDecay(dPanel_);
        setRelease(rPanel_);
    }

    /*
     * Panel positions, 0..1.
     *
     * Each segment keeps two numbers: how far the phase advances per sample,
     * which ends the segment, and the factor its exponential curve is
     * multiplied by per sample.
     *
     * The second one is why. Writing the curves out directly means an expf per
     * voice per sample, and at six voices that measured as the single largest
     * cost in the engine -- it was the whole difference between the 59 % a
     * prototype with multiply-add envelopes predicted and the 74 % the first
     * version of this one cost. e^(-k(p+s)) is e^(-kp) times e^(-ks), and the
     * second factor is constant while the slider is not moving, so the curve
     * can be carried forward with one multiply.
     */
    void setAttack(float v)
    {
        aPanel_ = v;
        aStep_  = stepFor(junoAttackTime(v));
        aMul_   = expf(-aStep_);
    }

    void setDecay(float v)
    {
        dPanel_ = v;
        dStep_  = stepFor(junoDecayTime(v));
        dMul_   = expf(-JUNO_FALL_SHAPE * dStep_);
    }

    void setRelease(float v)
    {
        rPanel_ = v;
        rStep_  = stepFor(junoDecayTime(v));
        rMul_   = expf(-JUNO_FALL_SHAPE * rStep_);
    }

    void setSustain(float v) { sustain_ = junoClamp(v, 0.0f, 1.0f); }

    /*
     * Gate mode. The VCA can be driven by the contour or by a plain gate; the
     * gate is not a square, it has a 3 ms rise and a 6 ms fall (measured) so
     * that switching it does not click.
     */
    void setGateMode(bool on)
    {
        gate_ = on;
        if (on) {
            aStep_ = stepFor(JUNO_GATE_ATTACK_S);
            aMul_  = expf(-aStep_);
            rStep_ = stepFor(JUNO_GATE_RELEASE_S);
            rMul_  = expf(-JUNO_FALL_SHAPE * rStep_);
        } else {
            setAttack(aPanel_);
            setRelease(rPanel_);
        }
    }

    /*
     * A gate holds at full level for as long as the key is down, so the
     * sustain control has no say in it. Forgetting this made every gate-mode
     * patch with the sustain slider at zero silent -- the contour rose in 3 ms
     * and then decayed straight back to nothing.
     */
    float effSustain() const { return gate_ ? 1.0f : sustain_; }

    void gateOn()
    {
        /*
         * Nothing in the circuit discharges the capacitor when a key goes
         * down, so a contour that is still running carries on from where it
         * is rather than snapping to zero. The phase that corresponds to the
         * current level is worked out by inverting the attack curve, which
         * keeps the segment duration meaning what it says.
         */
        if (level_ > 0.0f && level_ < 1.0f) {
            /* The curve variable is e^(-phase), and the attack level is
             * (1-curve)/0.632 -- so it inverts without a logarithm. The phase
             * itself only decides when the segment ends, and starting it from
             * zero simply gives the rest of the rise its full nominal time,
             * which is what a capacitor that never discharged does. */
            curve_ = 1.0f - level_ * JUNO_ATTACK_SHAPE;
            if (curve_ < 0.0f) curve_ = 0.0f;
        } else {
            curve_ = 1.0f;
        }
        phase_ = 0.0f;
        stage_ = ATTACK;
    }

    void gateOff()
    {
        relFrom_ = level_;
        phase_   = 0.0f;
        curve_   = 1.0f;
        stage_   = RELEASE;
    }

    void reset()
    {
        stage_ = IDLE; level_ = 0.0f; phase_ = 0.0f;
        curve_ = 1.0f; relFrom_ = 0.0f;
    }

    bool  isIdle() const  { return stage_ == IDLE; }
    float value() const   { return level_; }
    int   stage() const   { return stage_; }

    float process()
    {
        switch (stage_) {
            case ATTACK:
                phase_ += aStep_;
                curve_ *= aMul_;                    /* e^(-phase)          */
                if (phase_ >= 1.0f) {
                    level_ = 1.0f;
                    phase_ = 0.0f;
                    curve_ = 1.0f;
                    /* With the sustain at the top there is nothing to decay
                     * to, so the decay segment is skipped entirely. */
                    stage_ = (effSustain() >= 0.999f) ? SUSTAIN : DECAY;
                } else {
                    level_ = (1.0f - curve_) * (1.0f / JUNO_ATTACK_SHAPE);
                    if (level_ > 1.0f) level_ = 1.0f;
                }
                break;

            case DECAY: {
                phase_ += dStep_;
                curve_ *= dMul_;                    /* e^(-k phase)        */
                const float s = effSustain();
                if (phase_ >= 1.0f) {
                    level_ = s;
                    stage_ = SUSTAIN;
                } else {
                    level_ = s + (1.0f - s) * curve_;
                }
                break;
            }

            case SUSTAIN:
                level_ = effSustain();
                break;

            case RELEASE:
                phase_ += rStep_;
                curve_ *= rMul_;
                if (phase_ >= 1.0f) {
                    level_ = 0.0f;
                    stage_ = IDLE;
                } else {
                    level_ = relFrom_ * curve_;
                }
                break;

            case IDLE:
            default:
                level_ = 0.0f;
                break;
        }
        return level_;
    }

private:
    /* Phase advance per sample for a segment of the given duration. */
    float stepFor(float seconds) const
    {
        const float n = junoClamp(seconds * sr_, 1.0f, 1.0e7f);
        return 1.0f / n;
    }

    float sr_      = (float) SAMPLING_RATE;
    float level_   = 0.0f;
    float phase_   = 0.0f;
    float sustain_ = 0.7f;
    float relFrom_ = 0.0f;

    float curve_   = 1.0f;   /* the running exponential of the segment */

    float aPanel_ = 0.0f, dPanel_ = 0.4f, rPanel_ = 0.3f;
    float aStep_ = 0.01f, dStep_ = 0.01f, rStep_ = 0.01f;
    float aMul_  = 0.99f, dMul_  = 0.99f, rMul_  = 0.99f;

    bool  gate_  = false;
    int   stage_ = IDLE;
};

/* ------------------------------------------------------------------------ */
/* LFO                                                                       */
/*                                                                           */
/* One for the whole instrument, triangle, with a delay. Specifications page: */
/* RATE 0.3 .. 20 Hz, DELAY TIME 0 .. 1.5 s.                                  */
/*                                                                           */
/* The delay is two things, and the measurements separate them: a stretch of  */
/* silence after the trigger, and then a fade in. At the top of the slider    */
/* that came out as 2.786 s of silence followed by a second of fade, which    */
/* again overshoots the specified 1.5 s. The measurement is used.              */
/* ------------------------------------------------------------------------ */
class JunoLfo
{
public:
    void init(float sampleRate)
    {
        sr_ = sampleRate;
        phase_ = 0.0f;
        setRate(0.4f);
        setDelay(0.0f);
        trigger();
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        setRate(ratePanel_);
        setDelay(delayPanel_);
    }

    void setRate(float v)
    {
        ratePanel_ = v;
        inc_ = junoLfoRate(v) / sr_;
    }

    void setDelay(float v)
    {
        delayPanel_ = junoClamp(v, 0.0f, 1.0f);
        holdSamples_ = delayPanel_ * JUNO_LFO_DELAY_MAX_S * sr_;
        const float fade = delayPanel_ * JUNO_LFO_FADE_MAX_S * sr_;
        fadeStep_ = (fade < 1.0f) ? 1.0f : (1.0f / fade);
    }

    /* Restarted when a key is pressed with nothing else held -- the panel has
     * a trigger mode switch for whether every key does this or only the first
     * of a phrase. */
    void trigger()
    {
        held_  = 0.0f;
        depth_ = (holdSamples_ < 1.0f && fadeStep_ >= 1.0f) ? 1.0f : 0.0f;
    }

    /* Bipolar, -1..+1, already scaled by the delay envelope. */
    float process()
    {
        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        /* Triangle. */
        const float tri = 1.0f - 4.0f * fabsf(phase_ - 0.5f);

        if (held_ < holdSamples_) {
            held_ += 1.0f;
        } else if (depth_ < 1.0f) {
            depth_ += fadeStep_;
            if (depth_ > 1.0f) depth_ = 1.0f;
        }
        return tri * depth_;
    }

    float depth() const { return depth_; }

private:
    float sr_          = (float) SAMPLING_RATE;
    float phase_       = 0.0f;
    float inc_         = 0.0f;
    float ratePanel_   = 0.4f;
    float delayPanel_  = 0.0f;
    float holdSamples_ = 0.0f;
    float held_        = 0.0f;
    float fadeStep_    = 1.0f;
    float depth_       = 1.0f;
};

#endif /* JUNO_ENV_H */
