// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_fx.h -- the effects section

  Three effects, two slots. Each slot is empty or holds one of them, and the
  signal runs slot A then slot B, so the order is the player's choice: a
  chorus into a reverb is a different thing from a reverb into a chorus.

  Two slots and not three is what bounds the cost. Measured against the peak
  load the firmware actually reports on the device, the engine alone sits at
  21 %, and the most expensive pair -- chorus with reverb -- takes it to about
  35 %. A third simultaneous effect would have bought very little and cost the
  headroom that keeps the audio glitch-free while the display is being pushed
  out.

  There is exactly one instance of each effect, which is deliberate: it halves
  the memory a two-slot design would otherwise need, and it makes the same
  effect appearing in both slots impossible rather than merely discouraged.

  The Model D has no effects at all. Every factory preset but one leaves both
  slots empty, so the dry instrument remains what the firmware sounds like;
  "Shine On" is the exception, because a delay is part of that sound rather
  than a decoration on it.
*/

#ifndef MOOG_FX_H
#define MOOG_FX_H

#include "moog_defs.h"
#include "moog_dsp.h"

/* What a slot is set to. The order is the order of the rotary switch. */
enum MoogFxKind {
    MOOG_FX_OFF = 0,
    MOOG_FX_CHORUS,
    MOOG_FX_DELAY,
    MOOG_FX_REVERB,
    MOOG_FX_KIND_COUNT
};

/* ------------------------------------------------------------------------ */
/* Chorus                                                                    */
/* ------------------------------------------------------------------------ */
class MoogChorus
{
public:
    void  init(float sampleRate);
    void  reset();
    void  setRate(float v);      /* 0..1 */
    void  setDepth(float v);
    void  setMix(float v);
    void  setFeedback(float v);
    void  process(float* l, float* r, int n);

private:
    static const int kLen = (int) (MOOG_CHORUS_MAX_MS * 0.001f * SAMPLING_RATE) + 4;

    float buf_[MOOG_CHORUS_LINES][kLen] = {};
    int   write_ = 0;
    float phase_[MOOG_CHORUS_LINES] = {};
    float inc_   = 0.0f;
    float depth_ = 0.5f;
    float mix_   = 0.5f;
    float fb_    = 0.0f;
    float sr_    = (float) SAMPLING_RATE;
};

/* ------------------------------------------------------------------------ */
/* Delay                                                                     */
/* ------------------------------------------------------------------------ */
class MoogDelay
{
public:
    void  init(float sampleRate);
    void  reset();
    void  setTime(float v);      /* 0..1 */
    void  setFeedback(float v);
    void  setMix(float v);
    void  setTone(float v);
    void  process(float* l, float* r, int n);

private:
    static const int kLen = (int) (MOOG_DELAY_MAX_MS * 0.001f * SAMPLING_RATE) + 4;

    float    buf_[kLen] = {};
    int      write_ = 0;
    float    time_  = 0.0f;     /* samples, smoothed */
    float    target_= 0.0f;
    float    fb_    = 0.35f;
    float    mix_   = 0.30f;
    MoogLPF1 tone_;
    float    sr_    = (float) SAMPLING_RATE;
};

/* ------------------------------------------------------------------------ */
/* Reverb                                                                    */
/* ------------------------------------------------------------------------ */
class MoogReverb
{
public:
    void  init(float sampleRate);
    void  reset();
    void  setSize(float v);      /* 0..1 */
    void  setDamping(float v);
    void  setMix(float v);
    void  setWidth(float v);
    void  process(float* l, float* r, int n);

private:
    /* Freeverb tunings, in samples at 44.1 kHz. */
    static const int kComb[MOOG_REVERB_COMBS];
    static const int kAllpass[MOOG_REVERB_ALLPASS];
    static const int kCombTotal;
    static const int kAllpassTotal;

    /* One flat block per channel rather than twenty-four separate arrays:
     * the offsets are fixed, and a single block is easier on the linker and
     * on the cache than two dozen scattered ones. */
    static const int kCombLen    = 11024 + MOOG_REVERB_COMBS   * MOOG_REVERB_SPREAD;
    static const int kAllpassLen =  1563 + MOOG_REVERB_ALLPASS * MOOG_REVERB_SPREAD;

    float combBuf_[2][kCombLen]       = {};
    float allpassBuf_[2][kAllpassLen] = {};

    int   combOff_[2][MOOG_REVERB_COMBS]      = {};
    int   combLen_[2][MOOG_REVERB_COMBS]      = {};
    int   combPos_[2][MOOG_REVERB_COMBS]      = {};
    float combLp_ [2][MOOG_REVERB_COMBS]      = {};

    int   apOff_[2][MOOG_REVERB_ALLPASS]      = {};
    int   apLen_[2][MOOG_REVERB_ALLPASS]      = {};
    int   apPos_[2][MOOG_REVERB_ALLPASS]      = {};

    float feedback_ = 0.84f;
    float damp_     = 0.35f;
    float mix_      = 0.30f;
    float width_    = 1.0f;
    float sr_       = (float) SAMPLING_RATE;
};

/* ------------------------------------------------------------------------ */
/* The section                                                               */
/* ------------------------------------------------------------------------ */
class MoogFx
{
public:
    void init(float sampleRate);
    void reset();

    /* Takes the parameter IDs of the effects section; anything else is
     * ignored, so the caller can pass every parameter through. Returns true
     * if the id belonged here. */
    bool setParameter(int id, float value);

    /* In place, stereo. */
    void process(float* l, float* r, int n);

    /* True when both slots are empty -- the caller can then skip the call
     * entirely. */
    bool isBypassed() const
    {
        return slot_[0] == MOOG_FX_OFF && slot_[1] == MOOG_FX_OFF;
    }

private:
    void runSlot(int kind, float* l, float* r, int n);

    MoogChorus chorus_;
    MoogDelay  delay_;
    MoogReverb reverb_;

    int slot_[2] = { MOOG_FX_OFF, MOOG_FX_OFF };
};

#endif /* MOOG_FX_H */
