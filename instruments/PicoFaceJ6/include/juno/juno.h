// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno.h -- Roland Juno-60, top level

  Assembles what the panel is divided into:

      JunoDco      the digitally clocked oscillator, one per voice
      JunoFilter   the IR3109 low-pass, one per voice
      JunoHpf      the switched high-pass -- one, after the sum
      JunoEnv      the contour generator, one per voice
      JunoLfo      the low-frequency oscillator -- one, for the instrument
      JunoVoice    a voice card
      JunoChorus   the two bucket-brigade lines

  The division of labour follows the instrument: what a Juno has six of lives
  in JunoVoice, and what it has one of lives here. That is not tidiness for its
  own sake -- the high-pass filter and the chorus really are single circuits
  after the voices are summed, and building them per voice would both cost six
  times as much and sound wrong.

  The public interface follows the rest of the family so the bridge and the
  host test see the shape of object they always have.
*/

#ifndef JUNO_H
#define JUNO_H

#include "juno_defs.h"
#include "juno_params.h"
#include "juno_presets.h"
#include "juno_voice.h"
#include "juno_fx.h"
#include "juno_arp.h"

class Juno
{
public:
    Juno();

    void setSampleRate(float sampleRate);

    /* Renders I2S_BUFFER_WORDS frames; argument order as in the rest of the
     * family: (r, l). */
    void process(int16_t* outputs_r, int16_t* outputs_l);
    void processFloat(float* out_l, float* out_r, int frames);

    /* --- MIDI ----------------------------------------------------------- */
    void noteOn(int32_t note, int32_t velocity);
    void noteOff(int32_t note);
    bool processMidiController(uint8_t cc, uint8_t value);
    void setPitchBend(int32_t bend14);      /* 0..16383, centre 8192 */

    void resetVoices();
    void stopVoices();
    void resetControllers();

    void    setVolume(uint8_t value);       /* 0..127 -> the master */
    uint8_t getVolume() const;

    /* Master volume. An instrument setting rather than part of a patch, so it
     * lives in the second half of the parameter list -- see JUNO_PARAM_COUNT
     * in juno_params.h. */
    void  setMasterVolume(float v) { setParameter(JUNO_MASTER, v); }
    float masterVolume() const     { return master_; }

    /* --- Patches --------------------------------------------------------- */
    int32_t getProgramCount() const { return JUNO_NPROGRAMS; }
    int32_t getProgram() const      { return curProgram_; }
    void    setProgram(int32_t program);
    void    getProgramName(char* name) const;

    /* --- Parameters, each 0..1 ------------------------------------------- */
    /* Everything, patch parameters and instrument settings together. */
    int32_t getParameterCount() const { return JUNO_TOTAL_COUNT; }
    void    setParameter(int32_t index, float value);
    float   getParameter(int32_t index) const
    {
        return (index >= 0 && index < JUNO_TOTAL_COUNT) ? p_[index] : 0.0f;
    }
    void    getParameterName(int32_t index, char* text) const;
    void    getParameterDisplay(int32_t index, char* text) const;

    /* --- Diagnostics ----------------------------------------------------- */
    int activeVoices() const;

private:
    void applyParameter(int id);
    void applyAll();
    int  allocate(int note);
    void arpNoteOn(int note);
    void arpNoteOff(int note);

    JunoVoice       voice_[JUNO_VOICES];
    JunoVoiceParams vp_;
    JunoLfo         lfo_;
    JunoHpf         hpf_;
    JunoChorus      chorus_;
    JunoArp         arp_;

    /* Output stage, after the chorus. */
    JunoDCBlock  dcL_, dcR_;
    JunoBiquadLP dec_[3];      /* decimation, on the summed voices */
    JunoNoise    hiss_;

    float p_[JUNO_TOTAL_COUNT] = {};

    /* Cached values that are not simply a copy of a parameter. */
    float volume_    = 0.5f;   /* the patch's VCA level */
    float master_    = 1.0f;   /* the instrument's, not the patch's */
    float bendNorm_  = 0.0f;
    float bendRange_ = 2.0f;
    float tuneSemis_ = 0.0f;
    int   transpose_ = 0;
    bool  lfoAuto_   = true;
    bool  sustain_   = false;
    bool  arpOn_     = false;
    bool  hold_      = false;

    /* Which voice took which note, and in what order, for the stealing rule
     * and for round-robin assignment. */
    uint32_t stamp_ = 0;
    uint32_t taken_[JUNO_VOICES] = {};
    bool     pedalHeld_[JUNO_VOICES] = {};
    int      nextVoice_ = 0;

    float   samplerate_ = (float) SAMPLING_RATE;
    int32_t curProgram_ = 0;
};

#endif /* JUNO_H */
