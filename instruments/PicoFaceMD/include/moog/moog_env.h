// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_env.h -- contour generator

  The Model D has two, one for the filter and one for the loudness, and both
  have the same three controls: attack time, decay time, sustain level. There
  is no release knob. What there is instead is the DECAY switch on the
  left-hand panel, and it governs both contours at once: off, a released key
  stops almost immediately; on, it falls at the decay time.

  Two details separate this from a textbook ADSR, and both are audible:

    the attack is an RC curve, not a ramp
        A capacitor charging through a resistor heads for a voltage above the
        one the circuit stops at. The result is fast at the bottom and eases
        in at the top -- charging towards 1.2 and stopping at 1.0 puts the
        knee where the original has it.

    a retrigger does not start from zero
        There is nothing in the circuit to discharge the capacitor when a new
        key goes down. The contour simply starts charging again from wherever
        it happens to be, which is why fast repeated notes on a Model D swell
        rather than restarting cleanly.

  Times run from 10 ms to 10 s, the range the manual quotes for both controls.
*/

#ifndef MOOG_ENV_H
#define MOOG_ENV_H

#include "moog_defs.h"
#include "moog_dsp.h"

class MoogEnv
{
public:
    enum Stage { IDLE = 0, ATTACK, DECAY, SUSTAIN, RELEASE };

    void init(float sampleRate)
    {
        sr_ = sampleRate;
        level_ = 0.0f;
        stage_ = IDLE;
        setAttack(0.0f);
        setDecay(0.0f);
        setSustain(0.5f);
        setDecaySwitch(false);
    }

    void setSampleRate(float sampleRate)
    {
        sr_ = sampleRate;
        setAttack(attackPanel_);
        setDecay(decayPanel_);
        setDecaySwitch(decaySw_);
    }

    /* Panel positions, 0..1. */
    void setAttack(float panel)
    {
        attackPanel_ = panel;
        /* ln(1.2 / 0.2): the number of time constants it takes to climb from
         * nothing to 1.0 when the target is 1.2. */
        aCoef_ = coefFor(moogEnvTime(panel), 1.7918f);
    }

    void setDecay(float panel)
    {
        decayPanel_ = panel;
        /* ln(10): the stage covers 90 % of the distance in the nominal time,
         * which is what an analogue decay is normally measured as. */
        dCoef_ = coefFor(moogEnvTime(panel), 2.3026f);
        if (decaySw_) rCoef_ = dCoef_;
    }

    void setSustain(float level) { sustain_ = moogClamp(level, 0.0f, 1.0f); }

    /* The DECAY switch: on, the release takes the decay time; off, the
     * contour drops in a few milliseconds. */
    void setDecaySwitch(bool on)
    {
        decaySw_ = on;
        rCoef_ = on ? dCoef_ : coefFor(MOOG_ENV_FAST_REL_S, 2.3026f);
    }

    /*
     * Key down. retrigger is false for the single-trigger behaviour of the
     * original when a key is already held (legato playing runs on without
     * restarting the contours), true for multiple triggering.
     */
    void gateOn(bool retrigger)
    {
        if (!retrigger && stage_ != IDLE && stage_ != RELEASE)
            return;
        stage_ = ATTACK;
    }

    void gateOff() { stage_ = RELEASE; }

    /* Hard stop, for all-notes-off and engine reset. */
    void reset() { stage_ = IDLE; level_ = 0.0f; }

    bool isIdle() const { return stage_ == IDLE; }
    float value() const { return level_; }

    float process()
    {
        switch (stage_) {
            case ATTACK:
                level_ += (MOOG_ENV_OVERSHOOT - level_) * aCoef_;
                if (level_ >= 1.0f) {
                    level_ = 1.0f;
                    /* With sustain at the top there is nothing to decay to;
                     * the manual describes exactly this as "no decay after
                     * the initial rise". */
                    stage_ = (sustain_ >= 0.999f) ? SUSTAIN : DECAY;
                }
                break;

            case DECAY:
                level_ += (sustain_ - level_) * dCoef_;
                if (level_ - sustain_ < 0.0005f) {
                    level_ = sustain_;
                    stage_ = SUSTAIN;
                }
                break;

            case SUSTAIN:
                level_ = sustain_;
                break;

            case RELEASE:
                level_ -= level_ * rCoef_;
                if (level_ < 0.0002f) {
                    level_ = 0.0f;
                    stage_ = IDLE;
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
    /* One-pole coefficient for a stage that should cover its nominal ground
     * in `seconds`. k says how many time constants that is. */
    float coefFor(float seconds, float k) const
    {
        const float n = moogClamp(seconds * sr_, 1.0f, 1.0e7f);
        return 1.0f - expf(-k / n);
    }

    float sr_      = (float) SAMPLING_RATE;
    float level_   = 0.0f;
    float sustain_ = 0.5f;

    float attackPanel_ = 0.0f;
    float decayPanel_  = 0.0f;
    float aCoef_ = 0.1f, dCoef_ = 0.1f, rCoef_ = 0.1f;

    bool  decaySw_ = false;
    int   stage_   = IDLE;
};

#endif /* MOOG_ENV_H */
