// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_voice.h -- the instrument between the keyboard and the output jack

  One voice, because a Model D has one. Everything the panel table in
  moog_params.h describes ends up here as a cached, derived value; the render
  loop then only multiplies and adds.

  Signal flow, following the panel left to right:

      osc 1 ---\
      osc 2 ----+-- mixer --- ladder low-pass --- amplifier --- output stage
      osc 3 ---/      |            |                 |
      noise ---/      |        filter contour   loudness contour
      feedback -------+            |
                                modulation mix (osc 3 <-> noise) x mod wheel

  Three things about this are worth stating plainly, because they are what
  makes it sound like the instrument rather than like a subtractive synth:

    the mixer is meant to be overdriven
      Five sources, each with its own volume, summing into a stage that
      saturates. Turning everything up does not make it louder, it makes it
      growl. The Drive control sets how hard that stage is pushed.

    the feedback path is real
      On a Model D you patch the output jack back into the external input.
      That is what the feedback control here does; it is tapped after the
      amplifier, before the main volume, so that the character does not
      change every time the volume knob moves.

    oscillator 3 is a control source whether or not it is audible
      The manual is explicit: "Switch (G) does not affect the control signal
      produced by Oscillator 3 via the Modulation Mix." So it runs regardless
      of the mixer switch, and it modulates its own pitch along with the
      other two.
*/

#ifndef MOOG_VOICE_H
#define MOOG_VOICE_H

#include "moog_defs.h"
#include "moog_params.h"
#include "moog_dsp.h"
#include "moog_osc.h"
#include "moog_ladder.h"
#include "moog_env.h"

/* Depth of the modulation mix at a fully raised mod wheel.
 *
 * The schematic does put a number on these, contrary to what stood here
 * before. Fig. 9-2 marks the modulation bus "1.75 P-P MAX", and both
 * destinations are ordinary summing resistors against a node where the
 * keyboard's 1.02 V/oct through 102K is one octave:
 *
 *   oscillators   R5  100K  ->  1.75 V p-p = 1.75 oct p-p = +/-0.875 oct
 *   filter        R52  33K  ->  5.3 oct p-p                = +/-2.65 oct
 *
 * That makes a fully raised wheel worth ten and a half semitones on the
 * oscillators, not the seven the earlier 0.60 gave -- the dive bombs the
 * instrument is known for need the whole of it. The wheel's own taper is
 * dealt with separately, in the squared panel law in applyParameter(). */
#define MOOG_OSC_MOD_OCTAVES  0.875f
#define MOOG_FILT_MOD_OCTAVES 2.65f

class MoogVoice
{
public:
    void init(float sampleRate);
    void setSampleRate(float sampleRate);
    void reset();

    /* --- Panel ---------------------------------------------------------- */
    void  setParameter(int id, float value);
    float parameter(int id) const
    {
        return (id >= 0 && id < MOOG_PARAM_COUNT) ? p_[id] : 0.0f;
    }

    /* --- Keyboard ------------------------------------------------------- */
    void noteOn(int note);
    void noteOff(int note);
    void allNotesOff();
    void sustainPedal(bool on);

    /* --- Wheels --------------------------------------------------------- */
    /* The modulation wheel needs no entry point of its own: it is a panel
     * parameter like any other (MOOG_MOD_WHEEL, on CC 1), so the display
     * follows the wheel without anything having to be wired up for it. */
    void setPitchBend(float normalised);   /* -1 .. +1 */

    /* --- Rendering ------------------------------------------------------ */
    void process(float* out, int frames);

    bool isActive() const { return !envAmp_.isIdle(); }

private:
    void  applyAll();
    void  applyParameter(int id);
    void  updateKeyboard();
    int   pickNote() const;
    void  removeHeld(int note);
    float oscOctaves(int rangeParam) const;

    /* --- Sound sources -------------------------------------------------- */
    MoogOsc   osc1_, osc2_, osc3_;
    MoogNoise noise_;
    MoogNoise hiss_;

    /* --- Modifiers ------------------------------------------------------ */
    MoogLadder ladder_;
    MoogEnv    envFilt_, envAmp_;

    /* --- Vintage instability -------------------------------------------- */
    MoogDrift driftOsc_[3];
    MoogDrift driftFilt_;
    float     tuneError_[3] = {};   /* fixed per-oscillator error, semitones */

    /* --- Output stage --------------------------------------------------- */
    MoogBiquadLP dec_[3];           /* 6th order Butterworth, decimation */
    MoogDCBlock  dcOut_;
    MoogLPF1     toneLp_;
    float        a440Phase_ = 0.0f;
    float        a440Inc_   = 0.0f;
    float        feedback_  = 0.0f;

    /* Modulation mix of the previous oversampled step. Deliberately one step
     * old: oscillator 3 modulates its own pitch, and a loop has to be broken
     * somewhere. One sample at 88.2 kHz is 11 microseconds. */
    float        lastMod_   = 0.0f;

    /* --- Keyboard state -------------------------------------------------- */
    int   held_[MOOG_MAX_HELD_KEYS] = {};
    int   heldCount_ = 0;
    bool  sustained_[128] = {};
    bool  pedal_     = false;
    bool  gate_      = false;
    int   curNote_   = -1;
    float curPitch_  = (float) MOOG_CENTER_NOTE;   /* glide, in semitones */
    float targetPitch_ = (float) MOOG_CENTER_NOTE;
    bool  everPlayed_ = false;

    /* --- Live controllers ------------------------------------------------ */
    float bendSemi_ = 0.0f;
    float bendNorm_ = 0.0f;
    float modWheel_ = 0.0f;

    /* --- Cached panel values --------------------------------------------- */
    float p_[MOOG_PARAM_COUNT] = {};

    float oscOct_[3]  = {};     /* range switch, in octaves relative to 8'   */
    float oscFine_[3] = {};     /* frequency control, in semitones           */
    float mixGain_[4] = {};     /* osc 1..3 and noise, switch already folded */
    float fbGain_     = 0.0f;
    bool  pinkNoise_  = false;

    float tuneSemi_    = 0.0f;
    float glideCoef_   = 1.0f;
    bool  glideOn_     = false;
    bool  oscModOn_    = false;
    bool  osc3Kbd_     = true;
    float modMix_      = 0.0f;
    float bendRange_   = 2.0f;

    float cutoffOct_   = 0.0f;  /* panel cutoff, octaves above the minimum   */
    float resonance_   = 0.0f;
    float contourOct_  = 0.0f;
    float kbTrack_     = 0.0f;
    bool  filtModOn_   = false;

    float inputGain_   = 0.6f;
    float ladderDrive_ = 1.8f;
    float volume_      = 0.7f;
    float driftDepth_  = 0.0f;
    bool  a440On_      = false;

    int   transpose_   = 0;     /* octaves */
    int   priority_    = 0;     /* 0 low, 1 high, 2 last */
    bool  multiTrig_   = false;

    float sr_       = (float) SAMPLING_RATE;
    float osSr_     = (float) SAMPLING_RATE * MOOG_OVERSAMPLE;
    float invOsSr_  = 1.0f / ((float) SAMPLING_RATE * MOOG_OVERSAMPLE);
};

#endif /* MOOG_VOICE_H */
