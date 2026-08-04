// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_fx.cpp -- the chorus
*/

#include "juno/juno_fx.h"

#include <math.h>
#include <string.h>

void JunoChorus::init(float sampleRate)
{
    sr_ = sampleRate;
    for (int c = 0; c < 2; ++c) {
        inLp_[c].setCutoff(JUNO_BBD_INPUT_LP_HZ, sampleRate);
        /* Reconstruction after the line. The output filters on the schematic
         * are built from 820 pF and 0.0018 uF; this stands in for them and for
         * the line's own bandwidth, which never drops below about 12 kHz. */
        outLp_[c].setCutoff(11000.0f, sampleRate);
    }
    reset();
    setMode(JUNO_CH_OFF);
}

void JunoChorus::reset()
{
    memset(buf_, 0, sizeof(buf_));
    write_ = 0;
    phase_ = 0.0f;
    for (int c = 0; c < 2; ++c) { inLp_[c].reset(); outLp_[c].reset(); }
}

void JunoChorus::setMode(int mode)
{
    mode_ = (mode < 0) ? 0 : (mode >= JUNO_CH_COUNT ? JUNO_CH_COUNT - 1 : mode);

    float hz;
    switch (mode_) {
        case JUNO_CH_I:
            hz = JUNO_CHORUS_I_HZ;
            minMs_ = JUNO_CHORUS_MIN_MS; maxMs_ = JUNO_CHORUS_MAX_MS;
            mono_ = false;
            break;
        case JUNO_CH_II:
            hz = JUNO_CHORUS_II_HZ;
            minMs_ = JUNO_CHORUS_MIN_MS; maxMs_ = JUNO_CHORUS_MAX_MS;
            mono_ = false;
            break;
        case JUNO_CH_III:
            /* Both lines modulated together, and over a much narrower range --
             * measured 3.30 to 3.70 ms. The result is a vibrato. */
            hz = JUNO_CHORUS_III_HZ;
            minMs_ = 3.30f; maxMs_ = 3.70f;
            mono_ = true;
            break;
        default:
            hz = 0.0f;
            mono_ = false;
            break;
    }
    inc_ = hz / sr_;
}

void JunoChorus::process(float* l, float* r, int n)
{
    if (mode_ == JUNO_CH_OFF) return;

    const float minS = minMs_ * 0.001f * sr_;
    const float spanS = (maxMs_ - minMs_) * 0.001f * sr_;

    for (int i = 0; i < n; ++i) {
        /* The voices arrive summed and mono, as they do at the chorus board's
         * single signal input. */
        const float in = 0.5f * (l[i] + r[i]);

        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        /* Triangle, running 0 .. 1 .. 0 over one cycle. */
        const float modL = 1.0f - 2.0f * fabsf(phase_ - 0.5f);

        /* Right-hand modulation inverted, so the two channels sit half a cycle
         * apart -- except in I+II, where both lines share it and the output is
         * mono. */
        const float modR = mono_ ? modL : (1.0f - modL);

        float dL = minS + spanS * modL;
        float dR = minS + spanS * modR;
        dL = junoClamp(dL, 1.0f, (float) (kLen - 2));
        dR = junoClamp(dR, 1.0f, (float) (kLen - 2));

        const int   iL = (int) dL, iR = (int) dR;
        const float fL = dL - (float) iL, fR = dR - (float) iR;

        int aL = write_ - iL; while (aL < 0) aL += kLen;
        int bL = aL - 1;      if (bL < 0)    bL += kLen;
        int aR = write_ - iR; while (aR < 0) aR += kLen;
        int bR = aR - 1;      if (bR < 0)    bR += kLen;

        float wetL = buf_[0][aL] + (buf_[0][bL] - buf_[0][aL]) * fL;
        float wetR = buf_[1][aR] + (buf_[1][bR] - buf_[1][aR]) * fR;

        wetL = outLp_[0].process(wetL);
        wetR = outLp_[1].process(wetR);

        /* Written after reading, so the shortest delay is one sample rather
         * than zero. The low-pass ahead of the line is what rounds the
         * sawtooth. */
        buf_[0][write_] = inLp_[0].process(in);
        buf_[1][write_] = buf_[0][write_];
        if (++write_ >= kLen) write_ = 0;

        /* Dry and delayed at equal weight, which is how the board sums its
         * direct path against the two lines. */
        l[i] = in * 0.7f + wetL * 0.7f;
        r[i] = in * 0.7f + wetR * 0.7f;
    }
}
