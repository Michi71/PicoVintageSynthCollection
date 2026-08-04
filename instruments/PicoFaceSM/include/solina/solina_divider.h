/*
  solina_divider.h -- Master Oscillator Circuit + Divider Circuit

  Original (schematic, sheet 015.0212):
      Master Oscillator TR2/SAA1004 with a tuning trimmer generates the top
      octave (12 semitones). Nine SAJ110 chips halve that down into the lower
      octaves. The "Sawtooth Circuits" behind them turn the square waves into
      sawtooths.

  Model:
      Twelve phase accumulators, one per pitch class, run at the frequency of
      the *lowest* octave. Every higher octave is produced by left-shifting
      the accumulator -- bit for bit the same thing as a divider chain, only
      viewed backwards, and it guarantees that all octaves of one pitch class
      stay phase-locked.

      This is exactly what the Solina sound lives on: there is no beating
      whatsoever between two held notes. All movement comes from the ensemble.
*/

#ifndef SOLINA_DIVIDER_H
#define SOLINA_DIVIDER_H

#include "solina_defs.h"
#include "solina_dsp.h"

class SolinaDivider
{
public:
    void init(float sampleRate)
    {
        samplerate_ = sampleRate;
        setTune(0.0f);
        reset();
    }

    void reset()
    {
        /* All dividers start in phase -- as they do after power-up, when the
         * divider chain runs out of a counter reset. */
        for (int pc = 0; pc < 12; ++pc)
            acc_[pc] = 0;
    }

    /* Master oscillator tuning, in semitones */
    void setTune(float semitones)
    {
        tune_ = semitones;
        const float ratio = powf(2.0f, semitones / 12.0f);

        for (int pc = 0; pc < 12; ++pc)
        {
            /* Frequency of MIDI note pc (lowest octave, C-1..B-1) */
            const float f = SOLINA_NOTE0_HZ * powf(2.0f, ((float) pc) / 12.0f)
                            * ratio;
            stepLow_[pc] = f / samplerate_;
            inc_[pc] = (uint32_t) (stepLow_[pc] * 4294967296.0f + 0.5f);
        }
    }

    float tune() const { return tune_; }

    /* One sample step of the whole divider chain */
    inline void tick()
    {
        for (int pc = 0; pc < 12; ++pc)
            acc_[pc] += inc_[pc];
    }

    /* Phase 0..1 of the given MIDI note */
    inline float phase(int note) const
    {
        const int pc  = note - 12 * (note / 12);
        const int oct = note / 12;
        return (float) (acc_[pc] << oct) * (1.0f / 4294967296.0f);
    }

    /* Step size (periods per sample) of the given MIDI note */
    inline float step(int note) const
    {
        const int pc  = note - 12 * (note / 12);
        const int oct = note / 12;
        return stepLow_[pc] * (float) (1u << oct);
    }

private:
    float    samplerate_ = 44100.0f;
    float    tune_ = 0.0f;
    uint32_t acc_[12] = {};
    uint32_t inc_[12] = {};
    float    stepLow_[12] = {};
};

#endif /* SOLINA_DIVIDER_H */
