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
    void setPitchBendSemis(float semis);   // wheel position x bender range
    int bendRangeSemis() const { return bendRange_; }
    // RPN 0 data entry (EPROM 0x4E72): overwrites the patch's bender range
    // until the next patch change, exactly like the firmware's FE04/FE0C.
    void setBendRange(int semis);

    void setModWheel(float w);             // CC1, the D-50 lever's near kin   // reaches sounding notes
    void setAftertouch(float a);           // channel pressure, 0..1; survives patch changes like the wheel
    void setPortamentoSwitch(bool on);     // CC65: overrides the patch's switch
    void setPortamentoTime(int percent);   // CC5, 0..100
    int voiceLimit() const { return voiceLimit_; }
    // The reverb and chorus balance in force, in the D-50's own 0..100.
    // They follow the patch on every change, so the panel always starts
    // from what the patch itself asks for.
    int reverbBalance() const { return reverb_; }
    // Reverb Type, the D-50's 32 rooms/halls/delays/gates (patch data,
    // panel numbering 1..32).
    void setReverbType(int t);
    int reverbType() const;
    int chorusBalance() const { return chorus_; }
    // What the governor actually allows in notes. A whole-mode patch runs
    // one tone, so a note costs one voice instead of two and the same
    // silicon carries twice as many -- which is exactly the D-50's own
    // 16-against-8 polyphony (bank driver 0x8003: all sixteen slots go to
    // the upper tone when the key mode is whole).
    int noteLimit() const { return wholeMode_ ? 2 * voiceLimit_ : voiceLimit_; }
    // Tails the CPU governor retired in the last second. A RATE, not a
    // total: a running total only ever climbs, so it says nothing about
    // whether the machine is coping right now. Zero means the render has
    // room; a handful means dense passages are being trimmed at their
    // quietest end; tens mean the patch is living at the limit.
    uint32_t shedRate() const { return shedRate_; }
    uint32_t shedTotal() const { return shedTotal_; }

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
    bool wholeMode_ = false;        // set from the patch's key mode
    int shedHoldoff_ = 0;           // blocks since the last governor shed
    uint32_t shedTotal_ = 0;        // tails retired for the CPU, since boot
    uint32_t shedWindow_ = 0;       // ... in the second being counted
    uint32_t shedRate_ = 0;         // ... in the second before that
    uint32_t blockCount_ = 0;       // blocks into the current second
    // CC65/CC5 state, kept so a patch change restores its own setting and a
    // CC5 arriving before CC65 still lands when the switch does.
    bool portaSwitch_ = false;
    int portaTime_ = 0;
    int bendRange_ = 2;             // bender range in semitones, pb[26]
    int activeVoices_ = 0;
    uint32_t noteOnTotal_ = 0;
    int cpuPeak_ = 0;
    int outPeak_ = 0;
    float baseVolume_ = 1.0f;
    int patchReverbBal_ = 30;      // what the patch itself asks for, 0..100
    int patchChorusBal_ = 50;
    uint8_t held_[128] = {};
};

#endif // D5_BRIDGE_H
