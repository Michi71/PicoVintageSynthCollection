// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// M1_Bridge.h -- owns the engine and adapts it to the core's audio contract.
//
// The engine renders float stereo at the M1's own 31.25 kHz; the core wants
// two int32 words per frame. Master volume, soft clipping, the ROM view and the
// RAM the effect section and the interpolation kernel live in are here, so the
// engine stays free of firmware concerns and builds on the host.

#ifndef M1_BRIDGE_H
#define M1_BRIDGE_H

#include <cstdint>

#include "m1_engine/m1_engine.h"
#include "m1_memory.h"

class M1_Bridge {
public:
    void init();

    void fillBufferI32(int32_t* out, int frames);

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) { engine_.noteOn(ch, note, vel); }
    void noteOff(uint8_t ch, uint8_t note)             { engine_.noteOff(ch, note); }
    void allNotesOff()                                 { engine_.allNotesOff(); }
    void allSoundOff()                                 { engine_.allSoundOff(); }
    void damper(uint8_t ch, bool down)                 { engine_.damper(ch, down); }
    void pitchBend(uint8_t ch, int16_t b)              { engine_.pitchBend(ch, b); }
    void modWheel(uint8_t ch, uint8_t v)               { engine_.modWheel(ch, v); }
    void modWheelNeg(uint8_t ch, uint8_t v)            { engine_.modWheelNeg(ch, v); }
    void aftertouch(uint8_t ch, uint8_t v)             { engine_.aftertouch(ch, v); }
    void setRotaryFast(bool f)                         { engine_.setRotaryFast(f); }

    // The engine plays from the controller's edit buffers.
    void playProgram(const uint8_t* p)        { engine_.playProgram(p); }
    void playCombi(const uint8_t* c)          { engine_.playCombi(c); }
    void refreshEffects(const uint8_t* block) { engine_.refreshEffects(block); }
    void rebuildKits()                        { engine_.rebuildKits(); }
    m1::Memory& memory();

    void setVolume(uint8_t percent);          // 0..100, the front-panel knob
    void setMidiVolume(uint8_t v);            // CC7
    void setMasterTune(int cents)             { engine_.setMasterTune(cents); }
    void setFxOn(bool on)                     { engine_.setBypassFx(!on); }
    void setVoiceLimit(int n)                 { engine_.setVoiceLimit(n); }
    int  voiceLimit() const                   { return engine_.voiceLimit(); }
    int  activeVoices() const                 { return engine_.activeVoices(); }

    float cpuLoadPercent() const     { return cpuLoad_; }
    float cpuLoadPeakPercent() const { return cpuPeak_; }
    uint32_t sampleRate() const { return m1::kSampleRate; }

private:
    static constexpr int kBlock = m1::kRenderBlock;

    m1::Engine engine_;
    float gain_ = 0.8f;
    float midiGain_ = 1.0f;
    float cpuLoad_ = 0.0f, cpuPeak_ = 0.0f;
    float bufL_[kBlock]{}, bufR_[kBlock]{};
};

#endif // M1_BRIDGE_H
