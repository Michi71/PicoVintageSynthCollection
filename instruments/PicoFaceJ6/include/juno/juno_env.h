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
        updateAttack();
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
        updateRelease();
    }

    /*
     * The sustain slider is not the sustain level. Measured against Roland's
     * plugin on both routes at once -- as a held amplitude with the filter
     * open, and as the filter's own corner with the contour driving it -- the
     * level is 1 - (1-s)^1.6, which is well above the setting everywhere in
     * between: the slider at a third holds at 0.45, not at 0.33.
     *
     * Taking the slider for the level made every patch that sustains below
     * full about an octave too dark on a positive contour and most of an
     * octave too bright on a negative one, and it was the largest thing left
     * in the comparison. The fit is inside 0.02 from a fifth of the travel
     * upward and 0.035 below it.
     *
     * The two routes agreeing is what makes this the envelope's law rather
     * than the filter's, so it belongs here and not at either consumer.
     */
    void setSustain(float v)
    {
        v = junoClamp(v, 0.0f, 1.0f);
        sustain_ = 1.0f - powf(1.0f - v, JUNO_SUSTAIN_CURVE);
    }

    /*
     * The gate's own rise and fall, and the contour's, live in the same two
     * pairs of coefficients, so whichever was written last used to win. The
     * panel is written in its own order and the VCA switch comes before the
     * envelope sliders, so selecting a patch always ended by overwriting the
     * gate with the contour: every gate-mode patch in the bank ran on its
     * envelope's attack and release, and only the forced sustain still marked
     * it as a gate. These two put the choice back where it is made.
     */
    void updateAttack()
    {
        aStep_ = stepFor(gate_ ? JUNO_GATE_ATTACK_S : junoAttackTime(aPanel_));
        aMul_  = expf(-aStep_);
    }

    void updateRelease()
    {
        rStep_ = stepFor(gate_ ? JUNO_GATE_RELEASE_S : junoDecayTime(rPanel_));
        rMul_  = expf(-JUNO_FALL_SHAPE * rStep_);
    }

    /*
     * Turns this contour into the plain gate the VCA switch can select
     * instead. The gate is not a square: it has a 3 ms rise and a 6 ms fall
     * (measured) so that switching it does not click.
     *
     * It is a shape a whole contour is set to, once, and not a mode laid over
     * a running one -- the instrument has a switch in front of the amplifier,
     * not a second setting on its contour generator, and the voice keeps a
     * second JunoEnv for it. Laying it over the contour, as this did, forced
     * the sustain to full for the filter as well.
     */
    void setGateShape()
    {
        gate_ = true;
        updateAttack();
        updateRelease();
    }

    /*
     * A gate holds at full level for as long as the key is down; the instance
     * that carries the gate shape is simply given a sustain of one, so there
     * is no special case here. There used to be, and it reached the filter's
     * contour too.
     */
    float effSustain() const { return sustain_; }

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
                /*
                 * The segment's nominal length carries the exponential to
                 * -40 dB, and a decay that is heading for a sustain of zero
                 * used to be cut off there. Roland's plugin carries the same
                 * rate on down: a decay of half travel falls at a steady
                 * 33 dB a second past -110 dB without a kink anywhere. Cutting
                 * it at -40 turns the tail of every percussive patch into a
                 * step, and on a patch whose only sound source is the filter
                 * singing -- Synth Drum, and the rest of bank 7 -- it silences
                 * the note outright where the plugin holds a clean tone.
                 *
                 * So the phase only ends the segment when there is a sustain
                 * to end it at; heading for silence it runs until it is
                 * silent.
                 */
                if (s > kSilence) {
                    if (phase_ >= 1.0f) { level_ = s; stage_ = SUSTAIN; }
                    else                { level_ = s + (1.0f - s) * curve_; }
                } else {
                    level_ = curve_;
                    if (level_ <= kSilence) { level_ = 0.0f; stage_ = SUSTAIN; }
                }
                break;
            }

            case SUSTAIN:
                level_ = effSustain();
                break;

            case RELEASE:
                /* Same as the decay: a release always heads for silence, so
                 * the rate carries it there rather than the segment length
                 * cutting it off at -40 dB. */
                phase_ += rStep_;
                curve_ *= rMul_;
                level_ = relFrom_ * curve_;
                if (level_ <= kSilence) { level_ = 0.0f; stage_ = IDLE; }
                break;

            case IDLE:
            default:
                level_ = 0.0f;
                break;
        }
        return level_;
    }

private:
    /* Below this a contour counts as arrived: -120 dB, under the voice's own
     * noise floor and under anything 16 bits can carry. */
    static constexpr float kSilence = 1.0e-6f;

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
