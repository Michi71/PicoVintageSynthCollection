/*
  solina_phaser.h -- Phaser

  NOTE: not in the original. The 1974 ARP Solina has no phaser -- its
  schematic knows only the three modulator circuits of the ensemble.

  The Behringer remake gained one; its manual (doc/BE071AAM8US1.pdf) lists
  under "Modulation Section":

      Buttons  Modulation, phaser
      Controls Color, rate

  and on the back panel a "Phaser in" and a "Phaser out" jack, so there the
  phaser sits as an insert point behind the ensemble. It is wired the same
  way here: behind the modulator circuits, ahead of the Output Amplifier and
  the Correction Filter.

  Structure: six first-order all-pass sections per channel whose corner
  frequency an LFO sweeps between 200 Hz and 1600 Hz, plus a feedback path
  ("Color") and a fixed mix of half dry, half wet. The right channel runs
  90 degrees offset so the movement stays in the stereo image.

  The all-pass coefficients are updated once per block rather than per
  sample -- at LFO rates below 10 Hz that is ample, and it saves a tangent
  at the sample rate.
*/

#ifndef SOLINA_PHASER_H
#define SOLINA_PHASER_H

#include "solina_defs.h"
#include "solina_dsp.h"

#define SOLINA_PHASER_STAGES 6

class SolinaPhaser
{
public:
    void init(float sampleRate);
    void reset();

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const     { return enabled_; }

    void setRate(float hz);      /* SOLINA_PHASER_HZ_MIN .. MAX */
    void setColor(float c);      /* 0..1 -> feedback            */

    /* Processes the stereo bus in place. */
    void process(float* l, float* r, int count);

private:
    struct Channel {
        float x1[SOLINA_PHASER_STAGES];
        float y1[SOLINA_PHASER_STAGES];
        float fb;      /* last output, used for the feedback     */
        float phase;   /* LFO phase 0..1                         */
    };

    inline float runStages(Channel& c, float in, float a1) const;

    float samplerate_ = 44100.0f;
    bool  enabled_ = false;

    float inc_ = 0.0f;        /* LFO step per sample */
    float feedback_ = 0.0f;

    Channel ch_[2] = {};
};

#endif /* SOLINA_PHASER_H */
