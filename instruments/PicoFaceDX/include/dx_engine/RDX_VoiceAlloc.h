// RDX_VoiceAlloc.h
#pragma once
#include "RDX_Voice.h"
#include "RDX_Types.h"
#include "RDX_State.h"
#include "dx_engine_config.h"
#include "ram_hot.h"

class RDX_VoiceAllocator {
public:
    inline __attribute__((always_inline)) int RAM_HOT(findVoice)(RDX_Voice* voices, int count, uint8_t note, uint8_t vel, uint8_t mode) {
        switch (mode) {
            case RDX_MODE_MONO_FULL: case RDX_MODE_MONO_LEGATO:
                pushNote(note); monoActive_ = true; return 0;
            case RDX_MODE_POLY: default: break;
        }
        for (int i = 0; i < count; ++i) { if (!voices[i].isActive()) { return i; } }
        float minScore = 1e9f; int victim = 0;
        for (int i = 0; i < count; ++i) { float s = voices[i].calcScore(); if (s < minScore) { minScore = s; victim = i; } }
        voices[victim].setJustAllocated(); return victim;
    }
    inline __attribute__((always_inline)) void RAM_HOT(noteOff)(RDX_Voice* voices, int, uint8_t note, uint8_t mode) {
        switch (mode) {
            case RDX_MODE_MONO_FULL: case RDX_MODE_MONO_LEGATO:
                popNote(note);
                if (stackSize_ == 0) { monoActive_ = false; voices[0].noteOff(); }
                else { uint8_t prev = stack_[stackSize_ - 1]; voices[0].noteOn(prev, 100); }
                return;
            case RDX_MODE_POLY: default: {
                for (int i = 0; i < MAX_VOICES; ++i) {
                    RDX_Voice& v = voices[i];
                    if (v.note() == note && v.isActive()) {
                        v.setHeld(false);
                        if (ctl_.sustain) { v.setSustained(true); }
                        else { if (!v.isHeld()) { v.setSustained(false); v.noteOff(); } }
                    }
                }
                return;
            }
        }
    }
    inline void allSoundOff(RDX_Voice* voices, int count) {
        for (int i = 0; i < count; ++i) { voices[i].setHeld(false); voices[i].setSustained(false); voices[i].noteOff(); }
        clearStack(); monoActive_ = false;
    }
    inline void allNotesOff(RDX_Voice* voices, int count) {
        for (int i = 0; i < count; ++i) {
            if (!ctl_.sustain) { voices[i].setSustained(false); voices[i].setHeld(false); voices[i].noteOff(); }
            else if (!voices[i].isHeld()) { voices[i].setSustained(true); voices[i].noteOff(); }
        }
        clearStack(); monoActive_ = false;
    }
    inline bool legatoPending() const { return legatoPending_; }
    inline uint8_t monoNote() const { return monoNote_; }
    inline void clearStack() { stackSize_ = 0; }
private:
    RDX_Controls& ctl_ = RDX_State::getState().controls;
    static constexpr int MAX_STACK = 12;
    uint8_t stack_[MAX_STACK]; uint8_t stackSize_ = 0;
    uint8_t monoNote_ = 0; bool monoActive_ = false; bool legatoPending_ = false;
    inline void pushNote(uint8_t note) { if (stackSize_ < MAX_STACK) stack_[stackSize_++] = note; monoNote_ = note; }
    inline void popNote(uint8_t note) {
        for (int i = 0; i < stackSize_; ++i) { if (stack_[i] == note) { for (int j = i; j < stackSize_ - 1; ++j) stack_[j] = stack_[j + 1]; --stackSize_; break; } }
    }
};
