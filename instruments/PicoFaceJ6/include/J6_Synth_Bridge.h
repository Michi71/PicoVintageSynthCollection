// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  J6_Synth_Bridge.h -- wiring the Juno engine to the Pico audio subsystem

  The bridge is deliberately thin: the engine already renders in blocks of
  float and brings its own output stage with a soft limiter. What is left here
  is the conversion to the int32 stereo format of the I2S buffer and the load
  measurement.

  The split follows the master project PicoFaceRD: the producer runs in the
  main loop on core 0 and the DMA IRQ stays microscopic. Core 1 is free: six
  voices with chorus measure P48 at the sample rate, which leaves room for USB,
  MIDI and the display. At 2x oversampling it would be P72, and then the voices
  would want splitting over both cores the way PicoFaceRD does.
*/

#ifndef J6_SYNTH_BRIDGE_H
#define J6_SYNTH_BRIDGE_H

#include <cstdint>
#include "juno/juno.h"

class J6_Synth_Bridge
{
public:
    void init();

    /* Fills one I2S buffer (stereo, int32 interleaved). */
    void fill_buffer_i32(int32_t* out, int length);

    /* --- MIDI ---------------------------------------------------------- */
    void noteOn(uint8_t note, uint8_t vel)
    {
        noteOnCount_++;
        juno_.noteOn(note, vel);
    }

    void noteOff(uint8_t note)      { juno_.noteOff(note); }
    void allNotesOff()              { juno_.stopVoices(); }
    void pitchBend(uint16_t b14)    { juno_.setPitchBend((int32_t) b14); }

    /* Controllers that are not panel parameters: sustain and the channel mode
     * messages. The engine knows the difference between all-sound-off (stop
     * now) and all-notes-off (release), so it is left to decide. */
    void controlChange(uint8_t cc, uint8_t value)
    {
        juno_.processMidiController(cc, value);
    }

    /* --- Panel --------------------------------------------------------- */
    void setParameter(int32_t index, float value) { juno_.setParameter(index, value); }
    float parameter(int32_t index) const          { return juno_.getParameter(index); }

    void setProgram(int32_t p)      { juno_.setProgram(p); }
    int32_t program() const         { return juno_.getProgram(); }
    int32_t programCount() const    { return juno_.getProgramCount(); }
    void programName(char* dst) const { juno_.getProgramName(dst); }

    void setVolume(uint8_t v)       { juno_.setVolume(v); }
    void setMasterVolume(float v)   { juno_.setMasterVolume(v); }

    uint32_t currentSampleRate() const { return SAMPLING_RATE; }

    /* --- Diagnostics (display footer) ---------------------------------- */
    int      cpuLoadPeakPercent() const { return cpuPeak_; }
    void     resetCpuPeak()             { cpuPeak_ = 0; }
    uint32_t noteOnCount() const        { return noteOnCount_; }

    /* How many of the six cards are sounding. Worth having on screen for an
     * instrument with a hard limit of six: it makes voice stealing something
     * you can see rather than something you wonder about. */
    int      activeVoices() const       { return juno_.activeVoices(); }

private:
    Juno     juno_;
    uint32_t noteOnCount_ = 0;
    int      cpuPeak_ = 0;
};

#endif /* J6_SYNTH_BRIDGE_H */
