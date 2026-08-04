// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_fx.cpp -- chorus, delay and reverb
*/

#include "moog/moog_fx.h"
#include "moog/moog_params.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Chorus                                                                    */
/*                                                                           */
/* Three lines around a base delay of 14 ms, their low frequency oscillators  */
/* a third of a cycle apart. Three rather than one is what separates a chorus */
/* from a vibrato: the beating between the lines is the effect.               */
/* ------------------------------------------------------------------------ */
void MoogChorus::init(float sampleRate)
{
    sr_ = sampleRate;
    reset();
    setRate(0.35f);
    setDepth(0.5f);
    setMix(0.5f);
    setFeedback(0.0f);
}

void MoogChorus::reset()
{
    memset(buf_, 0, sizeof(buf_));
    write_ = 0;
    for (int k = 0; k < MOOG_CHORUS_LINES; ++k)
        phase_[k] = (float) k / (float) MOOG_CHORUS_LINES;
}

void MoogChorus::setRate(float v)
{
    const float hz = MOOG_CHORUS_HZ_MIN +
                     moogClamp(v, 0.0f, 1.0f) *
                     (MOOG_CHORUS_HZ_MAX - MOOG_CHORUS_HZ_MIN);
    inc_ = hz / sr_;
}

void MoogChorus::setDepth(float v)    { depth_ = moogClamp(v, 0.0f, 1.0f); }
void MoogChorus::setMix(float v)      { mix_   = moogClamp(v, 0.0f, 1.0f); }
void MoogChorus::setFeedback(float v) { fb_    = moogClamp(v, 0.0f, 1.0f) * 0.70f; }

void MoogChorus::process(float* l, float* r, int n)
{
    const float base  = MOOG_CHORUS_BASE_MS  * 0.001f * sr_;
    const float swing = MOOG_CHORUS_SWING_MS * 0.001f * sr_ * depth_;

    for (int i = 0; i < n; ++i) {
        /* The voice is mono, so the two channels carry the same signal on the
         * way in. The stereo image is made here. */
        const float in = 0.5f * (l[i] + r[i]);

        float tap[MOOG_CHORUS_LINES];

        for (int k = 0; k < MOOG_CHORUS_LINES; ++k) {
            phase_[k] += inc_;
            if (phase_[k] >= 1.0f) phase_[k] -= 1.0f;

            float d = base + swing * sinf(phase_[k] * 6.2831853f);
            d = moogClamp(d, 1.0f, (float) (kLen - 2));

            const int   di = (int) d;
            const float fr = d - (float) di;

            int a = write_ - di;      while (a < 0) a += kLen;
            int b = a - 1;            if (b < 0)    b += kLen;

            tap[k] = buf_[k][a] + (buf_[k][b] - buf_[k][a]) * fr;
            buf_[k][write_] = in + tap[k] * fb_;
        }

        if (++write_ >= kLen) write_ = 0;

        /* Lines 1 and 3 to the sides, line 2 to both: the same arrangement
         * the ensemble of the Solina uses to get width out of a mono source
         * without the middle dropping out. */
        const float wetL = tap[0] * 0.6f + tap[1] * 0.4f;
        const float wetR = tap[2] * 0.6f + tap[1] * 0.4f;

        l[i] = in * (1.0f - mix_ * 0.5f) + wetL * mix_;
        r[i] = in * (1.0f - mix_ * 0.5f) + wetR * mix_;
    }
}

/* ------------------------------------------------------------------------ */
/* Delay                                                                     */
/*                                                                           */
/* One mono line, two taps. The tone control sits inside the feedback loop,   */
/* not across the output, so each repeat comes back darker than the one       */
/* before it -- which is what a tape echo does and what makes a long feedback */
/* setting decay into something soft instead of piling up.                    */
/* ------------------------------------------------------------------------ */
void MoogDelay::init(float sampleRate)
{
    sr_ = sampleRate;
    reset();
    setTime(0.45f);
    setFeedback(0.35f);
    setMix(0.30f);
    setTone(0.60f);
}

void MoogDelay::reset()
{
    memset(buf_, 0, sizeof(buf_));
    write_ = 0;
    tone_.reset();
    time_ = target_;
}

void MoogDelay::setTime(float v)
{
    const float ms = MOOG_DELAY_MIN_MS +
                     moogClamp(v, 0.0f, 1.0f) *
                     (MOOG_DELAY_MAX_MS - MOOG_DELAY_MIN_MS);
    target_ = moogClamp(ms * 0.001f * sr_, 2.0f, (float) (kLen - 2));
}

void MoogDelay::setFeedback(float v)
{
    fb_ = moogClamp(v, 0.0f, 1.0f) * MOOG_DELAY_FB_MAX;
}

void MoogDelay::setMix(float v) { mix_ = moogClamp(v, 0.0f, 1.0f); }

void MoogDelay::setTone(float v)
{
    tone_.setCutoff(MOOG_DELAY_TONE_MIN *
                    moogExp2f(moogClamp(v, 0.0f, 1.0f) *
                              log2f(MOOG_DELAY_TONE_MAX / MOOG_DELAY_TONE_MIN)),
                    sr_);
}

void MoogDelay::process(float* l, float* r, int n)
{
    /* Glide the read position rather than jumping it. Moving a delay tap
     * instantly puts a click in the repeats; at this rate a large change
     * takes about a fifth of a second and sounds like a tape speeding up,
     * which is the right kind of wrong. */
    const float glide = 1.0f - expf(-1.0f / (0.2f * sr_));

    for (int i = 0; i < n; ++i) {
        time_ += (target_ - time_) * glide;

        const float in = 0.5f * (l[i] + r[i]);

        float dl = moogClamp(time_, 1.0f, (float) (kLen - 2));
        float dr = moogClamp(time_ * MOOG_DELAY_SPREAD, 1.0f, (float) (kLen - 2));

        const int   il = (int) dl, ir = (int) dr;
        const float fl = dl - (float) il, fr = dr - (float) ir;

        int al = write_ - il; while (al < 0) al += kLen;
        int bl = al - 1;      if (bl < 0)    bl += kLen;
        int ar = write_ - ir; while (ar < 0) ar += kLen;
        int br = ar - 1;      if (br < 0)    br += kLen;

        const float wetL = buf_[al] + (buf_[bl] - buf_[al]) * fl;
        const float wetR = buf_[ar] + (buf_[br] - buf_[ar]) * fr;

        buf_[write_] = in + tone_.process(wetL) * fb_;
        if (++write_ >= kLen) write_ = 0;

        l[i] = in + wetL * mix_;
        r[i] = in + wetR * mix_;
    }
}

/* ------------------------------------------------------------------------ */
/* Reverb                                                                    */
/*                                                                           */
/* Eight comb filters in parallel into four all-passes in series, per         */
/* channel, with the right channel's delays offset by a few samples so the    */
/* two do not correlate. Schroeder's arrangement by way of Freeverb; the      */
/* tunings are the usual ones.                                                */
/* ------------------------------------------------------------------------ */
const int MoogReverb::kComb[MOOG_REVERB_COMBS] =
    { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
const int MoogReverb::kAllpass[MOOG_REVERB_ALLPASS] =
    { 556, 441, 341, 225 };

void MoogReverb::init(float sampleRate)
{
    sr_ = sampleRate;

    /* The tunings are in samples at 44.1 kHz. The buffers are sized for that,
     * so a different rate scales the lengths used rather than the storage. */
    const float scale = sr_ / 44100.0f;

    for (int ch = 0; ch < 2; ++ch) {
        const int spread = ch ? MOOG_REVERB_SPREAD : 0;

        int off = 0;
        for (int i = 0; i < MOOG_REVERB_COMBS; ++i) {
            int len = (int) ((float) (kComb[i] + spread) * scale);
            if (len < 8) len = 8;
            if (off + len > kCombLen) len = kCombLen - off;
            combOff_[ch][i] = off;
            combLen_[ch][i] = len;
            off += len;
        }

        off = 0;
        for (int i = 0; i < MOOG_REVERB_ALLPASS; ++i) {
            int len = (int) ((float) (kAllpass[i] + spread) * scale);
            if (len < 8) len = 8;
            if (off + len > kAllpassLen) len = kAllpassLen - off;
            apOff_[ch][i] = off;
            apLen_[ch][i] = len;
            off += len;
        }
    }

    reset();
    setSize(0.6f);
    setDamping(0.35f);
    setMix(0.30f);
    setWidth(1.0f);
}

void MoogReverb::reset()
{
    memset(combBuf_,    0, sizeof(combBuf_));
    memset(allpassBuf_, 0, sizeof(allpassBuf_));
    memset(combPos_,    0, sizeof(combPos_));
    memset(apPos_,      0, sizeof(apPos_));
    memset(combLp_,     0, sizeof(combLp_));
}

void MoogReverb::setSize(float v)
{
    feedback_ = MOOG_REVERB_FB_MIN +
                moogClamp(v, 0.0f, 1.0f) * (MOOG_REVERB_FB_MAX - MOOG_REVERB_FB_MIN);
}

void MoogReverb::setDamping(float v) { damp_  = moogClamp(v, 0.0f, 1.0f) * 0.85f; }
void MoogReverb::setMix(float v)     { mix_   = moogClamp(v, 0.0f, 1.0f); }
void MoogReverb::setWidth(float v)   { width_ = moogClamp(v, 0.0f, 1.0f); }

void MoogReverb::process(float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i) {
        /* Scaled well down on the way in: eight combs in parallel sum to a
         * lot of gain, and the tail has to have room to sit under the dry
         * signal rather than on top of it. */
        const float in = (l[i] + r[i]) * 0.015f;

        float wet[2];

        for (int ch = 0; ch < 2; ++ch) {
            float acc = 0.0f;

            for (int c = 0; c < MOOG_REVERB_COMBS; ++c) {
                float* b   = combBuf_[ch] + combOff_[ch][c];
                const int len = combLen_[ch][c];
                int&  pos  = combPos_[ch][c];

                const float y = b[pos];
                combLp_[ch][c] = y * (1.0f - damp_) + combLp_[ch][c] * damp_;
                b[pos] = in + combLp_[ch][c] * feedback_;
                if (++pos >= len) pos = 0;
                acc += y;
            }

            for (int a = 0; a < MOOG_REVERB_ALLPASS; ++a) {
                float* b   = allpassBuf_[ch] + apOff_[ch][a];
                const int len = apLen_[ch][a];
                int&  pos  = apPos_[ch][a];

                const float y = b[pos];
                b[pos] = acc + y * 0.5f;
                if (++pos >= len) pos = 0;
                acc = y - acc;
            }
            wet[ch] = acc;
        }

        /* Width folds the two tails back together: at 0 both channels get the
         * same one and the reverb is mono, at 1 they stay apart. */
        const float mid  = (wet[0] + wet[1]) * 0.5f;
        const float side = (wet[0] - wet[1]) * 0.5f * width_;

        l[i] += (mid + side) * mix_;
        r[i] += (mid - side) * mix_;
    }
}

/* ------------------------------------------------------------------------ */
/* The section                                                               */
/* ------------------------------------------------------------------------ */
void MoogFx::init(float sampleRate)
{
    chorus_.init(sampleRate);
    delay_.init(sampleRate);
    reverb_.init(sampleRate);
    slot_[0] = slot_[1] = MOOG_FX_OFF;
}

void MoogFx::reset()
{
    chorus_.reset();
    delay_.reset();
    reverb_.reset();
}

bool MoogFx::setParameter(int id, float v)
{
    switch (id) {
        case MOOG_FX_SLOT_A: slot_[0] = moogParamStep(v, MOOG_FX_KIND_COUNT); return true;
        case MOOG_FX_SLOT_B: slot_[1] = moogParamStep(v, MOOG_FX_KIND_COUNT); return true;

        case MOOG_CHORUS_RATE:  chorus_.setRate(v);     return true;
        case MOOG_CHORUS_DEPTH: chorus_.setDepth(v);    return true;
        case MOOG_CHORUS_MIX:   chorus_.setMix(v);      return true;
        case MOOG_CHORUS_FB:    chorus_.setFeedback(v); return true;

        case MOOG_DELAY_TIME:   delay_.setTime(v);      return true;
        case MOOG_DELAY_FB:     delay_.setFeedback(v);  return true;
        case MOOG_DELAY_MIX:    delay_.setMix(v);       return true;
        case MOOG_DELAY_TONE:   delay_.setTone(v);      return true;

        case MOOG_REVERB_SIZE:  reverb_.setSize(v);     return true;
        case MOOG_REVERB_DAMP:  reverb_.setDamping(v);  return true;
        case MOOG_REVERB_MIX:   reverb_.setMix(v);      return true;
        case MOOG_REVERB_WIDTH: reverb_.setWidth(v);    return true;

        default: return false;
    }
}

void MoogFx::runSlot(int kind, float* l, float* r, int n)
{
    switch (kind) {
        case MOOG_FX_CHORUS: chorus_.process(l, r, n); break;
        case MOOG_FX_DELAY:  delay_.process(l, r, n);  break;
        case MOOG_FX_REVERB: reverb_.process(l, r, n); break;
        default: break;
    }
}

void MoogFx::process(float* l, float* r, int n)
{
    /* Slot A first, then slot B. A chorus into a reverb is not the same thing
     * as a reverb into a chorus, and which one it is should be the player's
     * decision rather than an accident of the code. */
    if (slot_[0] != MOOG_FX_OFF) runSlot(slot_[0], l, r, n);

    /* One instance per effect, so a slot that repeats the other one would
     * feed a line into itself. Skipped rather than allowed. */
    if (slot_[1] != MOOG_FX_OFF && slot_[1] != slot_[0])
        runSlot(slot_[1], l, r, n);

    /* Two effects in series, each with its own wet level, can sum past full
     * scale -- measured, a delay at maximum feedback into an undamped reverb
     * peaked at 1.63. Linear below 0.70, so anything sensible is untouched;
     * beyond that it approaches 1.0 instead of hitting the hard clip in the
     * I2S conversion. */
    for (int i = 0; i < n; ++i) {
        l[i] = moogLimit(l[i]);
        r[i] = moogLimit(r[i]);
    }
}
