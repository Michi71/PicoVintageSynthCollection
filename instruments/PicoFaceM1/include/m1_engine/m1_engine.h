// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_engine.h -- PicoFaceM1 sound engine: a native re-implementation of the
// Korg M1's voice architecture over the original PCM data.
//
// It is not a chip emulation. It reads the M1's own tables (multisounds,
// interpolation kernel, VDF coefficients, EG and pitch tables, effect tables,
// all from the user's ROM files at configure time) and runs the signal chain
// the analysis worked out of the OS: TG with the IC30 8-tap kernel, two VDF
// one-pole stages, VDA, software EGs updated every 20.8 ms with linear ramps in
// between, voice pan to the four buses A/B/C/D, and the two-effect section.
//
// Like PicoFaceJV, every voice reads its sample strictly forward, so the PCM
// in flash is streamed rather than addressed at random.
#ifndef M1_ENGINE_H
#define M1_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "m1_engine/m1_fx.h"

namespace m1 {

enum : int {
    kVoices       = 16,     // TG channels; a Double program takes two per note
    kTimbres      = 8,
    kPrograms     = 100,
    kCombis       = 100,
    kProgLen      = 143,
    kCombiLen     = 124,
    kKitLen       = 210,    // 30 instruments x 7 bytes
    kUpdate       = 650,    // samples between EG/pitch updates per voice (20.8 ms)
    kRenderBlock  = 64,
};

constexpr uint32_t kSampleRate = 31250;   // 32 MHz / 1024

struct Split {              // one keyboard zone of a multisound
    uint32_t start, loop, end;   // absolute word addresses into the PCM
    uint8_t  top;                // highest key of the zone
    uint8_t  pan;                // 0..13, always 5 in ROM multisounds
    uint8_t  root;               // root key
    uint8_t  flags;              // bit 0 loop on, bit 1 NT (root := played key)
    int8_t   tune;               // cents
    int8_t   level;              // x 0x14A on 0x8000
    int8_t   cutoff;             // x 0x52 on the cutoff register
};

struct Multisound {
    uint16_t first;              // index into splits[]
    uint8_t  count;
    uint8_t  dwgs;
    char     name[11];
};

struct DrumSound {               // one entry of the OS drum-sound list
    uint32_t start, loop, end;
    uint8_t  root;
    uint8_t  flags;              // bit 0 loop on
    int8_t   tune, level, cutoff;
};

// Everything the engine reads from ROM. Produced by tools/m1_extract.
struct RomTables {
    const int16_t*    pcm;            // 2 M words
    uint32_t          pcmWords;
    const Multisound* ms;             // 100
    const Split*      splits;
    const DrumSound*  drums;          // drum-sound list (index 1..nDrums)
    int               nDrums;
    const uint8_t*    kernel;         // IC30: 4096 phases x 8 bytes
    const uint16_t*   vdf1k;          // 4096, Q13
    const uint16_t*   vdf2k;          // 4096, Q13
    const uint16_t*   egRise;         // 256
    const uint16_t*   egFall;         // 256
    const uint16_t*   velCurve;       // 128
    const uint16_t*   mgFreq;         // 100
    const uint16_t*   panReg;         // 14, DS:0854
    const uint16_t*   pan101;         // 101, DS:86EA
    const uint16_t*   delayStart;     // 100, DS:776E, in 1.3 ms ticks
    const FxTables*   fx;
    const uint8_t*    programs;       // 100 x 143
    const uint8_t*    combis;         // 100 x 124
    const uint8_t*    kits;           // 4 x 210
};

// Segment envelope (analysis 6.2): level moves linearly between break points,
// segment length from the firmware's rise/fall rate tables. Levels are in
// firmware units (parameter x 0x14A).
struct Eg {
    int32_t level = 0, src = 0, target = 0;
    float   n = 0;            // updates in this segment
    int     k = 0;
    int     seg = 0, nseg = 0;
    bool    active = false;
    int16_t segTime[4], segLevel[4];
    int16_t relTime = 0, relLevel = 0;
    bool    useFall = true;
    float   timeScale = 1.0f;
};

class Engine {
public:
    bool init(const RomTables& rom, int16_t* fxMem /* 65536 words */);

    // Program mode: one timbre playing a program, buses A/B at 5:5.
    void selectProgram(int index);
    // Combination mode: eight timbres.
    void selectCombi(int index);
    bool combiMode() const { return combiMode_; }
    const char* name() const { return name_; }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void noteOff(uint8_t ch, uint8_t note);
    void allNotesOff();
    void allSoundOff();
    void damper(uint8_t ch, bool down);
    void pitchBend(uint8_t ch, int16_t bend);     // -8192..8191
    void modWheel(uint8_t ch, uint8_t v);          // joystick +Y
    void modWheelNeg(uint8_t ch, uint8_t v);       // joystick -Y (CC 2)
    void aftertouch(uint8_t ch, uint8_t v);
    void setRotaryFast(bool f) { fx_.setRotaryFast(f); }
    void setMasterTune(int cents) { tuneCents_ = cents; }
    void setVoiceLimit(int n) { voiceLimit_ = n < 1 ? 1 : (n > kVoices ? kVoices : n); }
    int  voiceLimit() const { return voiceLimit_; }
    int  activeVoices() const;

    // Stereo out = M1 outputs 1/2. Overwrites.
    void render(float* left, float* right, int frames);

    // Testing hooks: render without the effect section, and the Python
    // reference's simplified voice (no velocity/keyboard/MG terms).
    void setBypassFx(bool b) { bypassFx_ = b; }
    void setReferenceMode(bool b) { refMode_ = b; }

private:
    struct OscParams;
    struct Timbre {
        const uint8_t* prog = nullptr;    // 143-byte program
        int     level = 99;               // combi timbre level
        int     transpose = 0, detune = 0;
        int     pan = 5;                  // 0..13, bit 7 INS
        uint8_t keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
        uint8_t midiCh = 0;
        bool    on = false;
        bool    damper = false;
        // mono memory: current + two previous notes
        uint8_t monoNotes[3] = {0xFF, 0xFF, 0xFF};
        uint8_t monoVel[3] = {0, 0, 0};
        uint32_t mgPhase[2] = {0, 0};     // free-running MG per timbre (no key sync)
    };
    struct Voice {
        bool     active = false;
        uint8_t  status = 0;              // 0x80 held, 3 sustained by damper, 1 release, 0 free
        uint8_t  timbre = 8, osc = 0, note = 0, vel = 0;
        int      pitchNote = 0;           // note for pitch (after transpose)
        const int16_t* base = nullptr;    // PCM at sample start
        // 20.12 offset from the sample start. Signed: DWGS waves loop back to
        // before their start address. The longest M1 sample is 56 K words.
        int32_t  pos = 0;
        int32_t  step = 0;                // 4.12
        uint32_t endRel = 0, loopLen = 0; // in words, relative to start
        bool     loop = false;
        int      root128 = 0;
        int      splitCutoff = 0, splitLevel = 0;
        float    panA = 0, panB = 0, panC = 0, panD = 0;
        // filter / amp
        float    y1 = 0, y2 = 0;
        float    cut = 0, cutInc = 0;     // register value, ramped
        float    amp = 0, ampInc = 0;
        bool     first = true;
        int      untilUpdate = 0;
        int      delayTicks = 0;          // OSC2 delay start, in samples
        Eg       pitchEg, vdfEg, vdaEg;
        uint32_t mgPhase[2] = {0, 0};
        int      mgDelay[2] = {0, 0};
        bool     releasing = false;
        bool     oscDone = false;         // one-shot ran out: filters ring out to the next update
        uint32_t age = 0;
    };

    void startVoice(int v, int t, int osc, uint8_t note, uint8_t vel, int pitchNote);
    void releaseVoice(int v);
    void killVoice(int v);
    int  takeChannel();
    void queueToFront(int v);
    void queueBeforeHeld(int v);
    void queueToBack(int v);
    void update(Voice& v);
    void timbreNoteOn(int t, uint8_t note, uint8_t vel);
    void timbreNoteOff(int t, uint8_t note);
    void configureTimbre(int t, const uint8_t* prog);
    void renderVoice(Voice& v, int frames);
    float egStep(Eg& e);
    void  egStart(Eg& e, int startLevel, const int16_t* times, const int16_t* levels, int n,
                  int relTime, int relLevel, bool useFall, float timeScale);
    void  egRelease(Eg& e);
    float segUpdates(int timeParam, bool rising, float scale) const;

    RomTables rom_{};
    EffectSection fx_;
    Timbre tim_[kTimbres];
    Voice voice_[kVoices];
    uint8_t queue_[kVoices];
    bool combiMode_ = false;
    bool bypassFx_ = false;
    bool refMode_ = false;
    char name_[11] = {0};
    int tuneCents_ = 0;
    int voiceLimit_ = kVoices;
    int16_t bend_[16] = {0};
    uint8_t mod_[16] = {0}, modNeg_[16] = {0}, aft_[16] = {0};
    uint32_t ageCounter_ = 0;
    float busA_[kRenderBlock], busB_[kRenderBlock], busC_[kRenderBlock], busD_[kRenderBlock];
};

} // namespace m1

#endif // M1_ENGINE_H
