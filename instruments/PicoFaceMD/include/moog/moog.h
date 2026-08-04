/*
  moog.h -- Minimoog Model D, top level

  Assembles what the front panel is divided into:

      MoogOsc      Oscillator Bank, three of them
      MoogNoise    Noise Source, white and pink
      MoogLadder   Modifiers: the 24 dB/oct transistor ladder
      MoogEnv      the two contour generators
      MoogVoice    Controllers, Mixer, keyboard logic and the output stage

  The public interface follows Solina and mdaEPiano (PicoFaceSM, PicoFaceCP)
  so that wiring it to the RP2350 stays mechanical -- the bridge and the host
  test see the same shape of object they always have.
*/

#ifndef MOOG_H
#define MOOG_H

#include "moog_defs.h"
#include "moog_params.h"
#include "moog_presets.h"
#include "moog_voice.h"
#include "moog_fx.h"

class Moog
{
public:
    Moog();

    void setSampleRate(float sampleRate);

    /* Renders I2S_BUFFER_WORDS frames; argument order as in mdaEPiano: (r, l).
     * The Model D is mono, so both channels carry the same signal. */
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

    void    setVolume(uint8_t value);       /* 0..127 -- writes MOOG_VOLUME */
    uint8_t getVolume() const;

    /* --- Programs -------------------------------------------------------- */
    int32_t getProgramCount() const { return MOOG_NPROGRAMS; }
    int32_t getProgram() const      { return curProgram_; }
    void    setProgram(int32_t program);
    void    getProgramName(char* name) const;

    /* --- Parameters, each 0..1 ------------------------------------------- */
    int32_t getParameterCount() const { return MOOG_PARAM_COUNT; }
    void    setParameter(int32_t index, float value);
    float   getParameter(int32_t index) const;
    void    getParameterName(int32_t index, char* text) const;
    void    getParameterDisplay(int32_t index, char* text) const;

private:
    MoogVoice voice_;

    /* Behind the output stage, and the only thing in the path that makes the
     * instrument stereo -- the voice is mono, as the original is. */
    MoogFx    fx_;

    float     samplerate_ = (float) SAMPLING_RATE;
    int32_t   curProgram_ = 0;

    /* The voice renders mono; the split into two channels happens on the way
     * into the I2S buffer. */
    float mono_[MOOG_BLOCK];
};

#endif /* MOOG_H */
