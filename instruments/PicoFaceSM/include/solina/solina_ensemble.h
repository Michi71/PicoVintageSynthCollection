/*
  solina_ensemble.h -- Control Circuit + Modulator Circuit I/II/III

  Original (schematic, sheet 015.0212, area "CONTROL CIRCUIT *3*"):

      TREMOLO OSCILLATOR   741, 2M2 (101) + 1M Trimmer (71), 68n (100), 56K (97)
        -> LOW-PASS FILTER 741 via 5K6/680K/680K/680K and 6K8/56n/180n/5n6
        -> PHASE SHIFT     741, 47n (83), 820K (82), 10K (79)
        -> 22K/15uF auf die Modulatorschaltungen

      CHORUS OSCILLATOR    741, 1M8 (43) + 1M Trimmer (48), 680n (46), 56K (50)
        -> LOW-PASS FILTER 741 via 8K2/1M/1M/1M and 6K8/120n/680n/1M/56n
        -> PHASE SHIFT     741, 1M5 (16), 220n (13), 33K (12)
        -> PHASE INVERTER  741, 33K (11), 10K (6), 680n (2)
        -> C1 / C2 / C3 (220R per output)

      The two oscillators therefore produce three control signals, each about
      120 degrees apart, which detune the clock frequency of the three BBD
      lines (TCA350Y) in modulator circuits I, II and III.

      Comparing the time constants confirms the ranges:
        Tremolo 2M2 x 68n  = 150 ms  -> a few Hz
        Chorus  1M8 x 680n = 1.22 s  -> below 1 Hz

  Model (after string-machine: SolinaChorus, LFO3PhaseDual, Delay3Phase):

      Two LFO rows with three phases each (0, 120, 240 degrees) are summed.
      Three delay lines at 5 ms +/- 1 ms, preceded by a three-stage
      anti-aliasing chain. The original is mono and sums the lines with a
      sign matrix; here they are combined as mid/side instead, which leaves
      the mono sum untouched but gives the stereo image a width control:

          mid = (d1 + d2 + d3) * 2/3        side = (d1 - d3) * width

      The BBD emulation from string-machine (bbd_line.cpp) is deliberately
      *not* used here: it computes two fifth-order filters with
      std::complex<double> at an internal rate of 2*185/5ms = 74 kHz per
      line. The Cortex-M33 in the RP2350 only has a single-precision FPU.
*/

#ifndef SOLINA_ENSEMBLE_H
#define SOLINA_ENSEMBLE_H

#include "solina_defs.h"
#include "solina_dsp.h"

class SolinaEnsemble
{
public:
    void init(float sampleRate);
    void reset();

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const     { return enabled_; }

    /* Control Circuit trimmers */
    void setTremoloRate(float hz);
    void setTremoloDepth(float d);   /* 0..1 */
    void setChorusRate(float hz);
    void setChorusDepth(float d);    /* 0..1 */

    /* Voicing of the reconstruction filters, 1.0 = schematic values */
    void setReconScale(float s);

    /* Stereo width: 0 = mono as in the original, 1 = maximum */
    void setWidth(float w);

    /* One block: mono in, stereo out */
    void process(const float* in, float* outL, float* outR, int count);

    /* Phase readout (the indicator lamp in the original) */
    float phase1() const { return ph1_[0]; }
    float phase2() const { return ph2_[0]; }

private:
    struct Line {
        float* buf = nullptr;
        int    size = 0;
        int    w = 0;
    };

    inline float readDelay(const Line& l, float delaySamples) const;

    float samplerate_ = 44100.0f;
    bool  enabled_ = true;

    /* Control Circuit */
    float ph1_[SOLINA_ENSEMBLE_LINES] = {};   /* tremolo row */
    float ph2_[SOLINA_ENSEMBLE_LINES] = {};   /* chorus row  */
    float inc1_ = 0.0f, inc2_ = 0.0f;
    float depth1_ = 0.5f, depth2_ = 0.5f;

    /* Modulator Circuit I/II/III */
    Line  line_[SOLINA_ENSEMBLE_LINES];
    float mem_[SOLINA_ENSEMBLE_LINES]
              [(int) (SOLINA_ENSEMBLE_MAX_MS * 0.001f * 48000.0f) + 4] = {};

    /* Anti-aliasing chain ahead of the lines */
    SolinaBiquad aa1_, aa2_, aa3_;

    /*
     * Reconstruction low-pass behind the lines.
     *
     * In the signal flow diagram each modulator circuit has its own
     * "LOW-PASS FILTER TR4-5" behind the TCA350Y -- a clocked bucket brigade
     * needs both, anti-aliasing in front and reconstruction behind. Because
     * the output matrix is linear, one filter per output channel is
     * mathematically identical to one behind each of the three lines, but it
     * costs two instead of three.
     */
    SolinaBiquad reconL1_, reconL2_, reconR1_, reconR2_;
    float reconScale_ = 1.0f;

    float width_ = 0.7f;

    float delayCenter_ = 0.0f;   /* in samples */
    float delayVar_    = 0.0f;
};

#endif /* SOLINA_ENSEMBLE_H */
