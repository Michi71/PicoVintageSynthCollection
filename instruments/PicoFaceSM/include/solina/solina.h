/*
  solina.h -- ARP Solina String Ensemble, top level

  Assembles the building blocks of the schematic:

      SolinaDivider    Master Oscillator Circuit + Divider Circuit
      SolinaKeyboard   Manual Circuit + Gate Circuit + Sustain Circuits
      SolinaRegisters  Gate Output Circuit + Formant Circuit + Bass Circuit
      SolinaEnsemble   Control Circuit + Modulator Circuit I/II/III
      SolinaPhaser     Phaser (not in the original, from the Behringer remake)

  The public interface follows mdaEPiano (PicoFaceCP) so that wiring it up to
  the RP2350 stays mechanical.
*/

#ifndef SOLINA_H
#define SOLINA_H

#include "solina_defs.h"
#include "solina_divider.h"
#include "solina_keyboard.h"
#include "solina_registers.h"
#include "solina_ensemble.h"
#include "solina_phaser.h"

/* ------------------------------------------------------------------------ */
/* Parameter                                                                 */
/*                                                                           */
/* The first eleven match the front panel of the original exactly           */
/* (Behringer manual: "Buttons Contrabass, cello, viola, violin, trumpet,    */
/* horn / Controls Volume bass, crescendo, sustain, volume, tune").          */
/* After those come the Control Circuit trimmers and the tuning of the       */
/* register filters, which are component values in the original.             */
/* ------------------------------------------------------------------------ */
enum SolinaParam {
    /* Tone Section */
    SOLINA_CONTRABASS = 0,  /* 16', Bass Circuit           (switch) */
    SOLINA_CELLO,           /*  8', Bass Circuit           (switch) */
    SOLINA_VIOLA,           /*  8', Gate Output Circuit    (switch) */
    SOLINA_VIOLIN,          /*  4', Gate Output Circuit    (switch) */
    SOLINA_TRUMPET,         /*  8', Formant Circuit        (switch) */
    SOLINA_HORN,            /*  4', Formant Circuit        (switch) */
    SOLINA_BASS_VOLUME,     /* Volume Bass                           */
    SOLINA_CRESCENDO,       /* attack time of the sustain circuit    */
    SOLINA_SUSTAIN,         /* decay time of the sustain circuit     */
    SOLINA_VOLUME,          /* Output Amplifier                      */
    SOLINA_TUNE,            /* Master Oscillator Tuning              */

    /* Modulation Section */
    SOLINA_ENSEMBLE,        /* Modulation on/off           (switch) */
    SOLINA_TREMOLO_RATE,    /* Control Circuit, Trimmer 71           */
    SOLINA_TREMOLO_DEPTH,
    SOLINA_CHORUS_RATE,     /* Control Circuit, Trimmer 48           */
    SOLINA_CHORUS_DEPTH,
    SOLINA_ENSEMBLE_TONE,   /* reconstruction filters behind the BBD */
    SOLINA_ENSEMBLE_WIDTH,  /* stereo width of the output mix        */
    SOLINA_PHASER,          /* Phaser on/off               (switch) */
    SOLINA_PHASER_RATE,     /* sweep speed                           */
    SOLINA_PHASER_COLOR,    /* feedback                              */

    /* Voicing (component values in the original) */
    SOLINA_TONE_LOWPASS,    /* Gate Output Circuit, low-pass         */
    SOLINA_TONE_HIGHPASS,   /* Gate Output Circuit, high-pass        */
    SOLINA_TONE_SHELF,      /* Gate Output Circuit, treble lift      */
    SOLINA_FORMANT,         /* Formant Circuit, low-pass             */
    SOLINA_SHAPER,          /* limiting of the gate circuit          */

    SOLINA_PARAM_COUNT
};

#define SOLINA_NPROGRAMS 8

struct SolinaProgram {
    char  name[24];
    float param[SOLINA_PARAM_COUNT];
};

extern const SolinaProgram solinaPrograms[SOLINA_NPROGRAMS];

class Solina
{
public:
    Solina();

    void setSampleRate(float sampleRate);

    /* Renders I2S_BUFFER_WORDS frames; order as in mdaEPiano: (r, l) */
    void process(int16_t* outputs_r, int16_t* outputs_l);
    void processFloat(float* out_l, float* out_r, int frames);

    /* MIDI */
    void noteOn(int32_t note, int32_t velocity);
    void noteOff(int32_t note);
    bool processMidiController(uint8_t cc, uint8_t value);
    void setPitchBend(int32_t bend14);

    void resetVoices();
    void stopVoices();
    void resetControllers();

    void    setVolume(uint8_t value);      /* 0..127 */
    uint8_t getVolume() const { return volume_; }

    /* Programs */
    int32_t getProgramCount() const { return SOLINA_NPROGRAMS; }
    int32_t getProgram() const      { return curProgram_; }
    void    setProgram(int32_t program);
    void    getProgramName(char* name) const;
    void    setProgramName(const char* name);

    /* Parameters, each 0..1 */
    int32_t getParameterCount() const { return SOLINA_PARAM_COUNT; }
    void    setParameter(int32_t index, float value);
    float   getParameter(int32_t index) const;
    void    getParameterName(int32_t index, char* text) const;
    void    getParameterDisplay(int32_t index, char* text) const;
    void    getParameterLabel(int32_t index, char* text) const;

    /* Transposition of incoming MIDI notes */
    void setTranspose(int semitones) { transpose_ = semitones; }

private:
    void applyParameter(int32_t index, float value);
    void applyAllParameters();
    void renderBlock(int frames);

    SolinaDivider   divider_;
    SolinaKeyboard  keyboard_;
    SolinaRegisters registers_;
    SolinaEnsemble  ensemble_;
    SolinaPhaser    phaser_;

    /* Register Circuit -> Output Amplifier */
    SolinaDCBlock   outDc_;
    SolinaLPF1      correctionL_, correctionR_;

    float   samplerate_ = (float) SAMPLING_RATE;
    uint8_t volume_ = 100;
    int     transpose_ = 0;
    float   bend_ = 0.0f;
    int32_t curProgram_ = 0;

    float   params_[SOLINA_PARAM_COUNT] = {};
    char    programName_[24] = {};

    /* Work buffers */
    float bus8_[SOLINA_NGROUPS][SOLINA_BLOCK];
    float bus4_[SOLINA_NGROUPS][SOLINA_BLOCK];
    float bass8_[SOLINA_BLOCK];
    float bass16_[SOLINA_BLOCK];
    float mono_[SOLINA_BLOCK];
    float left_[SOLINA_BLOCK];
    float right_[SOLINA_BLOCK];
};

#endif /* SOLINA_H */
