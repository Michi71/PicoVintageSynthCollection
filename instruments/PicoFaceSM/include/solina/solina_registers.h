/*
  solina_registers.h -- Gate Output Circuit, Formant Circuit, Bass Circuit,
                        Register Circuit

  Original (schematic, sheets 015.0214 and 015.0212):

      The gate outputs take two paths:

        Gate Output Circuit   -> VIOLA (8')   / VIOLIN (4')
          One RC network per keyboard group (10K in series, 10K to ground,
          C = 5n6 / 10n / 22n / 47n / ...), then TR4 and a shaping network
          47n-22K-1n-18K.

        Formant Circuit TR5   -> TRUMPET (8') / HORN (4')
          A network of 120n/47n/100K/220R around TR5, tuned distinctly lower
          and without the treble lift -- hence the hollow, brass-like tone.

        Bass Circuit          -> CELLO (8')   / CONTRA BASS (16')
          Low-Tone Selection -> clipper (709) -> Bass Sustain Voltage Circuit
          -> low-pass TR1.

  Model:
      As in the original the filters sit *behind* the summing bus rather than
      per note -- but per keyboard group, because the original has its own RC
      network for each group. That reproduces the keyboard split of the
      timbre at a fraction of the CPU cost of one filter set per voice.

      The corner frequencies are set relative to the centre frequency of the
      respective group. The ratios come from string-machine (StringFilters,
      which works per note rather than per group); they were tuned against
      the original there. The values can be trimmed through setTone() and
      setFormant().
*/

#ifndef SOLINA_REGISTERS_H
#define SOLINA_REGISTERS_H

#include "solina_defs.h"
#include "solina_dsp.h"

class SolinaRegisters
{
public:
    void init(float sampleRate);
    void reset();

    /* Front panel register switches */
    void setViola(bool on)      { viola_ = on; }
    void setViolin(bool on)     { violin_ = on; }
    void setTrumpet(bool on)    { trumpet_ = on; }
    void setHorn(bool on)       { horn_ = on; }
    void setCello(bool on)      { cello_ = on; }
    void setContrabass(bool on) { contrabass_ = on; }

    void setBassVolume(float v) { bassVolume_ = v; }

    /*
     * Tuning, each given in semitones relative to the group centre.
     * Defaults from string-machine, plugins/string-machine/StringMachineShared.cpp
     */
    void setTone(float lowpassSemis, float highpassSemis,
                 float shelfSemis, float shelfDb);
    void setFormant(float lowpassSemis);
    void setShaper(float amount);

    /*
     * One block.
     *   bus8/bus4  summing busses, one per keyboard group
     *   bass8/16   Bass Circuit
     *   out        Register Circuit, mono sum
     */
    void process(const float bus8[SOLINA_NGROUPS][SOLINA_BLOCK],
                 const float bus4[SOLINA_NGROUPS][SOLINA_BLOCK],
                 const float* bass8, const float* bass16,
                 float* out, int count);

private:
    void updateCutoffs();

    struct GroupFilters {
        /* Gate Output Circuit: low-pass, high-pass, treble lift */
        SolinaLPF1   stringLp8, stringLp4;
        SolinaHPF1   stringHp8, stringHp4;
        SolinaBiquad stringShelf8, stringShelf4;
        /* Formant Circuit: low-pass only */
        SolinaLPF1   brassLp8, brassLp4;
    };

    float samplerate_ = 44100.0f;

    GroupFilters grp_[SOLINA_NGROUPS];
    SolinaShaper shaper_;

    /* Bass Circuit */
    SolinaLPF1   bassLp_;
    SolinaDCBlock bassDc_;

    bool viola_ = true, violin_ = false;
    bool trumpet_ = false, horn_ = false;
    bool cello_ = false, contrabass_ = false;
    float bassVolume_ = 0.8f;

    /*
     * Level matching between the registers.
     *
     * In the original the resistors at the register switch take care of this
     * (100K at H8/H12/H9 in the Formant Circuit, the network at the Bass
     * Circuit). Without the correction the formant branch sits about 14 dB
     * and the bass branch about 8 dB above the string branch, because
     * neither has a high-pass. Measured on a four-note chord, see README.
     */
    static constexpr float kGainString = 1.00f;
    static constexpr float kGainBrass  = 0.19f;   /* -14.4 dB */
    static constexpr float kGainBass   = 0.38f;   /*  -8.4 dB */

    float toneLpSemis_ = 5.2f;
    float toneHpSemis_ = 12.2f;
    float shelfSemis_  = 24.8f;
    float shelfDb_     = 6.0f;
    float formantSemis_ = 16.4f;
};

#endif /* SOLINA_REGISTERS_H */
