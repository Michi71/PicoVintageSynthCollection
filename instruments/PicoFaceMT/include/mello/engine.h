// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Mellotron-style tape replay engine.
//
// A keyboard of looped tape recordings on three tracks (A/B/C) with a track
// selector that can rest between two tracks, a common capstan whose speed
// wanders (wow and flutter), and a mono output path of head filters and tape
// saturation. Everything is plain float C++17 with no heap, no exceptions and
// no double arithmetic in the audio path, so it runs on a Cortex-M33 with a
// single-precision FPU as well as on a host.
#pragma once
#include <cstdint>
#include "mello/bank.h"

#ifndef MELLO_MAX_VOICES
#define MELLO_MAX_VOICES 16
#endif

namespace mello {

constexpr int kMaxVoices = MELLO_MAX_VOICES;
constexpr int kNumTracks = 3;

// All parameters take a normalised value 0..1.
enum Param : uint8_t {
    kVolume,     // output level
    kTone,       // treble cut, 0 = dark .. 1 = open
    kPitch,      // tape speed, 0.5 = in tune, +-2 semitones
    kTrack,      // track selector: 0 = A, 0.5 = B, 1 = C, anything between blends
    kAttack,     // 1 ms .. 500 ms
    kRelease,    // 5 ms .. 3 s
    kVelocity,   // velocity sensitivity, 0 = none (as on the instrument)
    kWow,        // slow speed drift
    kFlutter,    // fast speed ripple
    kDrive,      // tape saturation
    kKeyDrag,    // pitch dip while a tape is pulled up to speed
    kTapeLimit,  // >= 0.5: a note runs out after 8 s like the real tape
    kNumParams
};

class Engine {
public:
    void init(float sampleRate);

    // Which bank instrument each track plays; takes effect on the next note.
    void setTrackInstrument(int track, int bankIndex);
    int  trackInstrument(int track) const { return trackInst_[track]; }

    void  setParam(Param p, float v);
    float param(Param p) const { return param_[p]; }

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void sustainPedal(bool down);
    void pitchBend(float bend);                       // -1..+1
    void setBendRange(float semitones) { bendRange_ = semitones; bendDirty_ = true; }
    void allNotesOff();

    // Renders `frames` mono samples, nominally within -1..+1.
    void process(float* out, int frames);
    // Same, as interleaved 16-bit stereo (both channels identical).
    void processStereo16(int16_t* out, int frames);

    int activeVoices() const;

private:
    enum Stage : uint8_t { kOff, kAttackStage, kHold, kReleaseStage };

    struct Voice {
        const int16_t* pcm;
        uint64_t phase;                 // 32.32 fixed point, in source samples
        uint64_t loopEnd, loopLen;      // same format; loopLen 0 = one-shot
        float    ratio;                 // source samples per output sample at speed 1
        float    drag;                  // remaining speed deficit after key-down
        float    env, gain, relCoef;
        uint32_t age;                   // output samples since key-down
        uint32_t serial;
        uint8_t  note, track;
        Stage    stage;
        bool     keyDown, held;         // held: key up, sustain pedal still down
    };

    struct Biquad {
        float b0, b1, b2, a1, a2, z1, z2;
        inline float tick(float x) {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void   startVoice(uint8_t note, float gain, int track);
    Voice* allocVoice();
    void   releaseVoice(Voice& v, float coef);
    void   renderVoice(Voice& v, float* mix, int n, float speed, float trackGain);
    void   updateDerived();
    float  nextSpeed(int n);
    uint32_t rnd();

    float sr_ = 44100.0f;
    float param_[kNumParams] = {};
    int   trackInst_[kNumTracks] = {};
    uint8_t zoneOf_[kNumTracks][128] = {};   // zone index + 1, 0 = no sound
    Voice voice_[kMaxVoices] = {};
    uint32_t serial_ = 0;
    bool  pedal_ = false;

    float bend_ = 0.0f, bendRange_ = 2.0f;
    bool  bendDirty_ = true, filterDirty_ = true;
    float pitchRatio_ = 1.0f;                // tune and bend
    float trackGain_[kNumTracks] = {}, trackTarget_[kNumTracks] = {};
    float attackInc_ = 1.0f, relCoef_ = 0.0f;
    float volume_ = 0.0f, volumeTarget_ = 0.0f;
    float preGain_ = 1.0f, postGain_ = 1.0f;
    float dragDepth_ = 0.0f, dragCoef_ = 0.0f;
    uint32_t tapeFrames_ = 0;

    float wowPhase_ = 0.0f, flutterPhase_ = 0.0f;
    float wowNoise_ = 0.0f, flutterNoise_ = 0.0f;
    float wowDepth_ = 0.0f, flutterDepth_ = 0.0f;
    uint32_t rng_ = 0x2545F491u;

    Biquad hp_ = {}, lp_ = {};
};

}  // namespace mello
