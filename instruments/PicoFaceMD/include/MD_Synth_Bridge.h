/*
  MD_Synth_Bridge.h -- wiring the Moog engine to the Pico audio subsystem

  The bridge is deliberately thin: the engine already renders in blocks of
  float and brings its own output stage with a soft limiter. What is left here
  is the conversion to the int32 stereo format of the I2S buffer and the load
  measurement.

  The split follows the master project PicoFaceRD: the producer runs in the
  main loop on core 0 and the DMA IRQ stays microscopic. The Model D needs no
  worker on core 1 -- it is one voice, and on the host the engine costs about
  three quarters of what the Solina does with ten keys held.
*/

#ifndef MD_SYNTH_BRIDGE_H
#define MD_SYNTH_BRIDGE_H

#include <cstdint>
#include "moog/moog.h"

class MD_Synth_Bridge
{
public:
    void init();

    /* Fills one I2S buffer (stereo, int32 interleaved). */
    void fill_buffer_i32(int32_t* out, int length);

    /* --- MIDI ---------------------------------------------------------- */
    void noteOn(uint8_t note, uint8_t vel)
    {
        noteOnCount_++;
        moog_.noteOn(note, vel);
    }

    void noteOff(uint8_t note)      { moog_.noteOff(note); }
    void allNotesOff()              { moog_.stopVoices(); }
    void pitchBend(uint16_t b14)    { moog_.setPitchBend((int32_t) b14); }

    /* Controllers that are not panel parameters: sustain and the channel mode
     * messages. The engine knows the difference between all-sound-off (stop
     * now) and all-notes-off (release), so it is left to decide. */
    void controlChange(uint8_t cc, uint8_t value)
    {
        moog_.processMidiController(cc, value);
    }

    /* --- Panel --------------------------------------------------------- */
    void setParameter(int32_t index, float value) { moog_.setParameter(index, value); }
    float parameter(int32_t index) const          { return moog_.getParameter(index); }

    void setProgram(int32_t p)      { moog_.setProgram(p); }
    int32_t program() const         { return moog_.getProgram(); }
    int32_t programCount() const    { return moog_.getProgramCount(); }
    void programName(char* dst) const { moog_.getProgramName(dst); }

    void setVolume(uint8_t v)       { moog_.setVolume(v); }

    uint32_t currentSampleRate() const { return SAMPLING_RATE; }

    /* --- Diagnostics (display footer) ---------------------------------- */
    int      cpuLoadPeakPercent() const { return cpuPeak_; }
    void     resetCpuPeak()             { cpuPeak_ = 0; }
    uint32_t noteOnCount() const        { return noteOnCount_; }

private:
    Moog     moog_;
    uint32_t noteOnCount_ = 0;
    int      cpuPeak_ = 0;
};

#endif /* MD_SYNTH_BRIDGE_H */
