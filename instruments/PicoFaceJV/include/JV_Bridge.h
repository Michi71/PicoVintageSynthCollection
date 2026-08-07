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

    void setVolume(uint8_t percent);          // 0..100, the front-panel knob
    // MIDI channel volume (CC7) and pan (CC10). These sit on top of the panel
    // volume rather than replacing it, which is how a part control behaves.
    void setMidiVolume(uint8_t v);
    void setMidiPan(uint8_t v);
    // RPN 0 overrides the patch's bend range; -1 hands it back to the patch.
    void setBendRangeOverride(int semis);
    // RPN 1 and 2, fine and coarse tuning, in cents on top of the master tune.
    void setRpnTuneCents(float cents);
    void setSendScale(float rev, float cho) { engine_.setSendScale(rev, cho); }
    void setPortaTimeOverride(int t)        { engine_.setPortaTimeOverride(t); }
    void setPortaSwitchOverride(int s)      { engine_.setPortaSwitchOverride(s); }
    void setMonoOverride(int m)             { engine_.setMonoOverride(m); }

    // Incoming-velocity scaling, for playing back material that was not written
    // for the JV. The machine drops about 11 dB from velocity 127 to 64 on a
    // typical patch -- faithful, but it leaves a sequencer file sounding thin.
    // 100 % passes velocity through untouched; lower values pull it toward 127,
    // so 50 % halves the distance and 0 % makes every note full strength.
    //
    // The mapping happens before the engine sees anything, so the velocity
    // curves, the TVA and TVF sensitivities and the per-tone velocity windows
    // all act on the same value. That last one is worth knowing: compressing
    // upward will also bring in tone layers a patch reserves for hard playing.
    void    setVelocityScale(uint8_t pct) { veloScale_ = pct > 100 ? 100 : pct; }
    uint8_t velocityScale() const { return veloScale_; }
    uint8_t mapVelocity(uint8_t v) const {
        if (veloScale_ >= 100 || v >= 127) return v;
        const int m = 127 - ((127 - (int)v) * (int)veloScale_ + 50) / 100;
        return (uint8_t)(m < 1 ? 1 : m);
    }
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
    int   bendOverride_ = -1;
    float rpnCents_ = 0.0f;
    float bendRatio_ = 1.0f;
    uint8_t veloScale_ = 100;
    float midiGain_ = 1.0f;
    float panL_ = 1.0f, panR_ = 1.0f;
    float bufL_[kBlock]{}, bufR_[kBlock]{};
};

#endif // JV_BRIDGE_H
