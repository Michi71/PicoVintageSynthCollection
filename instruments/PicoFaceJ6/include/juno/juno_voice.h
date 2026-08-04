// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_voice.h -- one of the six voice cards

  DCO -> IR3109 low-pass -> amplifier, with one contour generator feeding the
  amplifier and, through an amount and a polarity switch, the filter.

  Six of these exist and they are identical; everything shared -- the LFO, the
  high-pass filter, the chorus -- sits above them in JunoEngine, exactly as it
  does in the instrument, where the HPF and chorus are single circuits after
  the voices are summed.

  A voice knows nothing about which note is playing where. It is handed a note
  and a gate, and the allocator above decides who gets what.
*/

#ifndef JUNO_VOICE_H
#define JUNO_VOICE_H

#include "juno_defs.h"
#include "juno_dsp.h"
#include "juno_dco.h"
#include "juno_filter.h"
#include "juno_env.h"

/* Settings shared by all six voices. Held once in the engine and passed in by
 * reference, so a panel change does not have to be written six times. */
struct JunoVoiceParams {
    /* DCO */
    float octave     = 0.0f;    /* range switch, in octaves relative to 8'  */
    bool  saw        = true;
    bool  pulse      = false;
    bool  subOn      = false;
    float subLevel   = 0.0f;
    float noise      = 0.0f;
    float pwm        = 0.0f;    /* panel amount                             */
    int   pwmMode    = 0;       /* 0 LFO, 1 manual, 2 contour               */
    float dcoLfo     = 0.0f;    /* LFO to pitch                             */

    /* VCF */
    float cutoffOct  = 5.0f;    /* octaves above JUNO_CUTOFF_MIN_HZ         */
    float resonance  = 0.0f;
    float envAmount  = 0.0f;
    float envPolarity= 1.0f;    /* +1 or -1                                 */
    float lfoAmount  = 0.0f;
    float keyFollow  = 0.0f;

    /* ENV / VCA */
    bool  gateMode   = false;

    /* Global pitch: master tune and the bender, in semitones. */
    float pitchSemis = 0.0f;
};

class JunoVoice
{
public:
    void init(float sampleRate, uint32_t seed)
    {
        osSr_ = sampleRate * (float) JUNO_OVERSAMPLE;
        dco_.init(osSr_, seed);
        vcf_.init(osSr_);
        env_.init(sampleRate);
        note_   = JUNO_CENTER_NOTE;
        active_ = false;
        invOsSr_ = 1.0f / osSr_;
    }

    void reset()
    {
        vcf_.reset();
        env_.reset();
        active_ = false;
        held_   = false;
    }

    /* --- Allocation ----------------------------------------------------- */
    void noteOn(int note, const JunoVoiceParams& p)
    {
        note_   = note;
        active_ = true;
        held_   = true;
        updatePitch(p);
        env_.gateOn();
    }

    /*
     * The unmodulated phase increment. Only the note, the master tune, the
     * bender and the range switch move it, and all four are events rather than
     * per-sample values -- so this is cached instead of being worked out again
     * every sample. Six calls into exp2f per sample was most of the difference
     * between the prototype's 59 % estimate and the 74 % the first version of
     * this engine actually cost.
     */
    void updatePitch(const JunoVoiceParams& p)
    {
        const float semis = (float) note_ + p.pitchSemis + p.octave * 12.0f;
        baseInc_ = junoNoteToHz(semis) * invOsSr_;
    }

    void noteOff()
    {
        held_ = false;
        env_.gateOff();
    }

    bool isHeld() const   { return held_; }
    bool isActive() const { return active_; }
    int  note() const     { return note_; }

    /* How far through its life the voice is, for the stealing rule: a
     * releasing voice with a quiet contour is the cheapest one to take. */
    float claim() const   { return env_.value(); }

    void setEnvelope(float a, float d, float s, float r)
    {
        env_.setAttack(a);
        env_.setDecay(d);
        env_.setSustain(s);
        env_.setRelease(r);
    }

    void setGateMode(bool on) { env_.setGateMode(on); }

    /*
     * One output sample. lfo is the shared low-frequency oscillator, already
     * scaled by its delay envelope; contourToPwm is the same voice's contour
     * from the previous sample, which is what the Env position of the pulse
     * width mode selector uses.
     */
    float process(const JunoVoiceParams& p, float lfo)
    {
        const float e = env_.process();

        if (env_.isIdle()) {
            active_ = false;
            return 0.0f;
        }

        dco_.setIncrement(baseInc_);

        /* --- Pulse width ------------------------------------------------ */
        /* The mode selector decides what moves it: the LFO, nothing, or the
         * contour. The panel control is the depth in the first and last case
         * and the width itself in the middle one. */
        float pw;
        switch (p.pwmMode) {
            case 1:  /* manual */
                pw = JUNO_PW_MIN + p.pwm * (JUNO_PW_MAX - JUNO_PW_MIN);
                break;
            case 2:  /* contour */
                pw = JUNO_PW_MIN + p.pwm * e * (JUNO_PW_MAX - JUNO_PW_MIN);
                break;
            default: /* LFO -- centred, so the width sweeps both ways */
                pw = JUNO_PW_MIN + p.pwm * (0.5f + 0.5f * lfo) *
                                   (JUNO_PW_MAX - JUNO_PW_MIN);
                break;
        }
        dco_.setPulseWidth(pw);
        dco_.setLevels(p.saw, p.pulse, p.subOn ? p.subLevel : 0.0f, p.noise);

        /* --- Cutoff ----------------------------------------------------- */
        /* Everything adds in octaves, which is how the control voltages add
         * in the instrument. Key follow is measured from C4. */
        float oct = p.cutoffOct
                  + p.envAmount * p.envPolarity * e * JUNO_CONTOUR_OCTAVES
                  + p.lfoAmount * lfo * JUNO_LFO_VCF_OCTAVES
                  + p.keyFollow * ((float) note_ - (float) JUNO_CENTER_NOTE)
                                * (1.0f / 12.0f);

        vcf_.setCutoff(JUNO_CUTOFF_MIN_HZ * junoExp2Wide(oct));
        vcf_.setResonance(p.resonance);

        /* --- Audio ------------------------------------------------------ */
        const float pitchMul = (p.dcoLfo > 0.0f)
            ? junoExp2Fast(lfo * p.dcoLfo * (JUNO_LFO_DCO_SEMIS / 12.0f))
            : 1.0f;

        float out = 0.0f;
        for (int os = 0; os < JUNO_OVERSAMPLE; ++os)
            out = vcf_.process(dco_.process(pitchMul) * 0.4f);

        return out * e;
    }

private:
    JunoDco    dco_;
    JunoFilter vcf_;
    JunoEnv    env_;

    int   note_    = JUNO_CENTER_NOTE;
    float baseInc_ = 0.0f;
    bool  active_  = false;
    bool  held_    = false;
    float osSr_    = (float) SAMPLING_RATE * JUNO_OVERSAMPLE;
    float invOsSr_ = 1.0f / ((float) SAMPLING_RATE * JUNO_OVERSAMPLE);
};

#endif /* JUNO_VOICE_H */
