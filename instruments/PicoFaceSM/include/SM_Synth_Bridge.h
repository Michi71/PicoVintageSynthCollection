/*
  SM_Synth_Bridge.h -- wiring the Solina engine to the Pico audio subsystem

  The bridge is deliberately thin: the engine already renders in blocks of
  float and brings its own soft limiter. What is left here is the conversion
  to the int32 stereo format of the I2S buffer, the sustain pedal and the
  load measurement.

  The split follows the master project PicoFaceRD: the producer runs in the
  main loop on core 0 and the DMA IRQ stays microscopic. The Solina needs no
  worker on core 1 -- on the host the engine costs 0.33 % of one M4 core with
  ten keys held.
*/

#ifndef SM_SYNTH_BRIDGE_H
#define SM_SYNTH_BRIDGE_H

#include <cstdint>
#include "solina/solina.h"

class SM_Synth_Bridge
{
public:
    void init();

    /* Fills one I2S buffer (stereo, int32 interleaved). */
    void fill_buffer_i32(int32_t* out, int length);

    /* --- MIDI ---------------------------------------------------------- */
    void noteOn(uint8_t note, uint8_t vel)
    {
        noteOnCount_++;
        solina_.noteOn(note, vel);
    }

    void noteOff(uint8_t note)      { solina_.noteOff(note); }
    void sustain(uint8_t value)     { solina_.processMidiController(0x40, value); }
    void allNotesOff()              { solina_.stopVoices(); }
    void pitchBend(uint16_t b14)    { solina_.setPitchBend((int32_t) b14); }

    /* --- Panel --------------------------------------------------------- */
    void setParameter(int32_t index, float value) { solina_.setParameter(index, value); }
    float parameter(int32_t index) const          { return solina_.getParameter(index); }

    void setProgram(int32_t p)      { solina_.setProgram(p); }
    int32_t program() const         { return solina_.getProgram(); }
    int32_t programCount() const    { return solina_.getProgramCount(); }
    void programName(char* dst) const { solina_.getProgramName(dst); }

    void setVolume(uint8_t v)       { solina_.setVolume(v); }

    uint32_t currentSampleRate() const { return SAMPLING_RATE; }

    /* --- Diagnostics (display footer) ---------------------------------- */
    int      cpuLoadPeakPercent() const { return cpuPeak_; }
    void     resetCpuPeak()             { cpuPeak_ = 0; }
    uint32_t noteOnCount() const        { return noteOnCount_; }

private:
    Solina   solina_;
    uint32_t noteOnCount_ = 0;
    int      cpuPeak_ = 0;
};

#endif /* SM_SYNTH_BRIDGE_H */
