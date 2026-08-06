// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Bridge.h -- owns the engine and adapts it to the core's audio contract.
//
// The engine renders float stereo; the core wants one int32 word per frame with
// the two 16-bit channels packed. Master volume, soft clipping and the ROM view
// live here so the engine itself stays free of firmware concerns and keeps
// building on the host.
//
// Unlike PicoFaceRD this instrument runs entirely on core0: the measured cost is
// roughly 55 M cycles/s at full polyphony, and sample decoding is sequential per
// voice, so there is no reason to spend core1 or to raise the clock.

#ifndef JV_BRIDGE_H
#define JV_BRIDGE_H

#include <cstdint>

#include "jv_engine/jv_engine.h"

class JV_Bridge {
public:
    void init();

    void fillBufferI32(int32_t* out, int frames);

    void noteOn(uint8_t note, uint8_t vel)  { engine_.noteOn(note, vel); }
    void noteOff(uint8_t note)              { engine_.noteOff(note); }
    void allNotesOff()                      { engine_.allNotesOff(); }
    void modWheel(uint8_t v)                { engine_.modWheel(v); }
    void aftertouch(uint8_t v)              { engine_.aftertouch(v); }
    void expression(uint8_t v)              { engine_.expression(v); }

    bool selectPatch(int bank, int index);
    const char* patchName() const { return engine_.patchName(); }

    void setVolume(uint8_t percent);          // 0..100
    void setVoiceLimit(int n)                 { engine_.setVoiceLimit(n); }
    int  voiceLimit() const                   { return engine_.voiceLimit(); }
    void setMasterTune(int cents);            // -50..+50
    void setPitchBend(int16_t bend);          // -8192..8191, range from the patch

    int  activeVoices() const { return engine_.activeVoices(); }
    uint32_t sampleRate() const { return kSampleRate; }

private:
    static constexpr uint32_t kSampleRate = 32000;   // the JV-880's native rate
    static constexpr int kBlock = 64;

    void updatePitch();
    void updateBend();

    jv::Engine engine_;
    float gain_ = 0.8f;
    int   tuneCents_ = 0;
    int16_t bend_ = 0;
    float bendRatio_ = 1.0f;
    float bufL_[kBlock]{}, bufR_[kBlock]{};
};

#endif // JV_BRIDGE_H
