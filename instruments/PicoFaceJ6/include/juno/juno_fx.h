// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_fx.h -- the chorus

  Two MN3009 bucket-brigade lines, one per channel, clocked by MN3101s, driven
  from one triangle oscillator with the right-hand modulation inverted so the
  two channels sit half a cycle apart. This is what thickens a Juno, and it is
  not optional equipment -- almost every factory patch has it on.

  Rates and delay range are measurements off a real instrument (Juno60), and
  they caught an error in the service notes: the sheet labels the three
  settings 0.5 / 0.83 / 1 Hz, but the third is really 9.75 Hz. Building from the
  printed figure would have made the I+II setting ten times too slow.

      I      0.513 Hz    1.66 .. 5.35 ms   stereo
      II     0.863 Hz    1.66 .. 5.35 ms   stereo
      I+II   9.75 Hz     3.30 .. 3.70 ms   mono

  I+II is mono because both lines get the same modulation. What comes out is a
  vibrato rather than a chorus, which is why the manual reaches for a Leslie to
  describe it.

  An MN3009 has 256 stages, so its delay is 256/(2*fclk) and the measured range
  puts the clock between roughly 24 and 77 kHz. The line's own Nyquist limit
  therefore never drops below about 12 kHz, which at a 44.1 kHz sample rate is
  high enough that modelling the delay-dependent loss buys very little; a fixed
  reconstruction filter stands in for it. The 12 dB low-pass ahead of the line
  is modelled, because that one is inside the feedback-free path and audibly
  rounds the sawtooth -- something the Juno60 analysis noticed before anyone
  looked at the schematic.

  There is no compander around the lines in the schematic, so a loud patch
  distorts them. That is faithful, and it is why the patch's VCA level sits
  before this stage rather than after it.
*/

#ifndef JUNO_FX_H
#define JUNO_FX_H

#include "juno_defs.h"
#include "juno_dsp.h"

enum JunoChorusMode {
    JUNO_CH_OFF = 0,
    JUNO_CH_I,
    JUNO_CH_II,
    JUNO_CH_III,        /* I+II, mono */
    JUNO_CH_COUNT
};

class JunoChorus
{
public:
    void init(float sampleRate);
    void reset();
    void setMode(int mode);

    bool isOff() const { return mode_ == JUNO_CH_OFF; }

    /* Mono in (both channels carry the same signal), stereo out, in place. */
    void process(float* l, float* r, int n);

private:
    static const int kLen =
        (int) (JUNO_CHORUS_BUF_MS * 0.001f * SAMPLING_RATE) + 4;

    float    buf_[2][kLen] = {};
    int      write_ = 0;
    float    phase_ = 0.0f;
    float    inc_   = 0.0f;
    float    minMs_ = JUNO_CHORUS_MIN_MS;
    float    maxMs_ = JUNO_CHORUS_MAX_MS;
    bool     mono_  = false;
    int      mode_  = JUNO_CH_OFF;
    JunoLPF1 inLp_[2];      /* the 12 dB low-pass ahead of the lines */
    JunoLPF1 outLp_[2];     /* reconstruction after them             */
    float    sr_    = (float) SAMPLING_RATE;
};

#endif /* JUNO_FX_H */
