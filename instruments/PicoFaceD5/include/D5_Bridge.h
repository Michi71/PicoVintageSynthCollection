// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Bridge.h -- everything the rest of the firmware needs from the LA engine,
// and nothing of the engine's own vocabulary. The controller and the MIDI
// front end talk to this; only this file knows what a partial is.

#ifndef D5_BRIDGE_H
#define D5_BRIDGE_H

#include <cstdint>

#include "d5_engine/d5_patch.h"

namespace d5 { constexpr int kMaxVoicesPerTone = 8; }

class D5_Bridge {
public:
    void init();

    uint32_t sampleRate() const { return 32000; }

    // Renders into the core's buffer layout: two int32 words per frame, left
    // then right, each carrying its 16-bit sample in the upper half.
    void fillBufferI32(int32_t* out, int frames);

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void allNotesOff();

    void selectPatch(int index);
    int patch() const { return patchIndex_; }
    // The rest of the firmware counts patches through here rather than
    // reaching into the preset table: that table is the engine's business.
    int patchCount() const;
    const char* patchName() const;
    const char* structureName() const;

    void setVolume(int percent);           // 0..100
    void setMasterTune(int cents);         // -50..+50
    void setReverb(int percent);           // scales the patch's reverb balance
    void setChorus(int percent);
    void setVoiceLimit(int voices);        // per tone, 1..8
    void setPitchBendCents(float cents);
    void setModWheel(float w);             // CC1, the D-50 lever's near kin   // reaches sounding notes
    int voiceLimit() const { return voiceLimit_; }

    int activeVoices() const { return activeVoices_; }
    // Every note-on that reached this bridge since boot -- the footer shows
    // it so a stuck voice display can be told apart from notes never
    // arriving (chord of three: +3 here means delivery works, +1 means the
    // transport or parser dropped the rest).
    uint32_t noteOnTotal() const { return noteOnTotal_; }
    int cpuLoadPeakPercent() const { return cpuPeak_; }
    int outputPeakPercent() const { return outPeak_; }
    int bootBenchPercent();

private:
    void applyPatch();
    void applyLevels();

    d5::Patch patch_{};
    int patchIndex_ = 0;
    int volume_ = 80;
    int tune_ = 0;
    int reverb_ = 100;
    int chorus_ = 100;
    int voiceLimit_ = d5::kMaxVoicesPerTone;
    int activeVoices_ = 0;
    uint32_t noteOnTotal_ = 0;
    int cpuPeak_ = 0;
    int outPeak_ = 0;
    float baseVolume_ = 1.0f;
    float baseReverb_ = 0.3f;
    float baseChorus_ = 0.0f;
    uint8_t held_[128] = {};
};

#endif // D5_BRIDGE_H
