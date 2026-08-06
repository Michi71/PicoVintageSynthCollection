// PicoFaceJV sound engine -- a native re-implementation of the JV-880 voice
// architecture. It is NOT an emulation of the original hardware: it reads the
// same ROM data (multisample table, sample table, patch parameters) and runs
// its own pitch / TVF / TVA chain, calibrated against the reference emulator.
//
// Layout constants come from jv_tone_map.h, physical units from
// jv_calibration.h -- both in tools/jv_extract/, which also documents how they
// were measured.
//
// The engine never owns ROM data. On the RP2350 the wave ROM lives in flash and
// is read through XIP; on the host it is read into a buffer. Sample decoding is
// strictly sequential per voice (DPCM cannot be addressed at random), which is
// what keeps the flash access pattern cache-friendly.
#ifndef JV_ENGINE_H
#define JV_ENGINE_H

#include <stddef.h>
#include <stdint.h>

namespace jv {

// Descrambled 4 MiB wave ROM plus the 256 KiB firmware ROM holding the tables.
struct RomView {
    const uint8_t* wave;   // waverom1 followed by waverom2, descrambled
    size_t         waveLen;
    const uint8_t* rom2;
    size_t         rom2Len;
};

struct Sample {
    uint32_t start, loop, end;   // 24-bit ROM addresses
    uint8_t  rootKey;
    uint16_t tune;               // 0x400 = neutral
    uint8_t  level;
};

enum : int {
    kMaxVoices  = 24,   // the original is 28; this is the Pico budget
    kControlDiv = 32,   // filter coefficients update at this rate, like the chip
};

class Engine {
public:
    bool init(const RomView& rom, uint32_t sampleRate);

    // A patch is 362 bytes in the layout of JvPatch. Either point at one inside
    // rom2 (selectPatch) or supply an edited copy (setPatch, which copies).
    bool selectPatch(int bank, int index);   // bank 0=User, 1=A, 2=B
    void setPatch(const uint8_t* patch362);

    // Modulation-matrix sources. Values are 0..127 and persist across notes.
    void modWheel(uint8_t v)     { srcMod_ = v; }
    void aftertouch(uint8_t v)   { srcAft_ = v; }
    void expression(uint8_t v)   { srcExp_ = v; }

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void allNotesOff();

    // Overwrites the buffers; does not accumulate.
    void render(float* left, float* right, int frames);

    int  activeVoices() const;

    // Runtime polyphony cap, 1..kMaxVoices. Lowering it does not cut sounding
    // voices; the allocator steals the oldest once the cap is reached.
    void setVoiceLimit(int n);
    int  voiceLimit() const { return voiceLimit_; }
    const char* patchName() const { return reinterpret_cast<const char*>(patch_); }

    // Pitch trim, applied to every voice. Used by the host A/B harness to
    // calibrate against the reference emulator; 1.0 = no correction.
    void setPitchTrim(float ratio) { pitchTrim_ = ratio; }

private:
    // Segment shape is direction-dependent, as measured: rising segments ramp
    // linearly in amplitude, falling ones decay linearly in dB. The time is the
    // segment's DURATION, not a rate -- at T1=80 the attack takes ~3.4 s whether
    // the target is level 64, 96 or 127. Treating it as a rate made every
    // segment whose target equals the current level collapse to nothing, which
    // is what made held pads stutter.
    struct Env {
        float level, target, slope, decay;
        float remaining;    // samples left in the current segment
        int   stage;        // 0..2 attack chain, 3 sustain, 4 release, 5 idle
        bool  rising;
        bool  segmentValid;
        const uint8_t* times;    // 4 bytes: T1 T2 T3 T4
        const uint8_t* levels;   // 3 bytes: L1 L2 L3 (the release target is 0)
        uint32_t sr;
        bool  logLevels;   // TVA maps levels in dB, TVF near-linearly
        void  begin(const uint8_t* t, const uint8_t* l, uint32_t sampleRate, bool logLevels);
        void  release();
        float tick();
        bool  idle() const { return stage >= 5; }
    };

    // Topology-preserving 2-pole state-variable filter. The obvious Chamberlin
    // form was tried first and is only accurate to about sr/6: at 32 kHz it
    // already erred by a third of an octave at cutoff parameter 56 and stopped
    // filtering altogether by 64, which is squarely inside the range the
    // modulation matrix reaches.
    struct Filter {
        float ic1, ic2;
        void  reset() { ic1 = ic2 = 0.0f; }
        float run(float in, float g, float k, int mode);
    };

    // One LFO. Runs at control rate, not per sample. `out` is UNIPOLAR: 1.0 is
    // the unmodulated value and 0.0 is full modulation, because both measured
    // destinations move one-sided downward from the set value.
    struct Lfo {
        float    phase;       // 0..1
        float    inc;         // phase per control tick
        int      wave;
        uint32_t rng;
        float    held;        // sample-and-hold value
        float    ramp;        // delay/fade envelope, 0..1
        float    delayTicks;  // remaining delay, in control ticks
        float    fadeInc;     // ramp increment per control tick
        float    out;
        void begin(const uint8_t* p, uint32_t sr, int ctlDiv, uint32_t seed, float freePhase);
        void tick();
    };

    struct Voice {
        bool     active;
        uint8_t  note, velocity, tone;
        uint32_t addr;        // current integer ROM address
        uint32_t phase;       // Q16 fractional position
        uint32_t inc;         // Q16 increment, refreshed at control rate
        uint32_t baseInc;     // before pitch modulation
        int32_t  ref;         // DPCM accumulator (20 bit)
        int32_t  refAtLoop;   // accumulator when the loop point was first passed
        bool     loopSeen;    // ... and whether that has happened yet
        int32_t  s0, s1;      // last two decoded samples, for interpolation
        Sample   smp;
        Env      tva, tvf;
        Filter   filt;
        uint8_t  tvaT[4], tvaL[3], tvfT[4], tvfL[3];
        float    gainL, gainR;
        float    cutoffBase;  // parameter units, 0..127
        float    envDepth;    // bipolar TVF envelope depth, parameter units
        float    resonance;   // 0..127
        float    resMod;      // matrix contribution to it
        int      filtMode;
        float    coefF, coefQ; // filter coefficients, refreshed at control rate
        int      ctlPhase;
        Lfo      lfo[2];
        // Modulation matrix, flattened: three sources of four slots each, in
        // source order mod / aftertouch / expression.
        int8_t   matSens[12];
        uint8_t  matDest[12];
        float    lfoPitchDepth[2], lfoTvfDepth[2], lfoTvaDepth[2];  // cents / params / dB
        float    lfoGain;      // TVA modulation, linear, refreshed at control rate
        float    cutoffMod;    // TVF modulation, parameter units
        uint32_t age;
    };

    int  allocVoice();
    void startVoice(Voice& v, int toneIndex, uint8_t note, uint8_t vel);
    bool sampleFor(int waveNumber, uint8_t note, Sample& out) const;
    int32_t decodeStep(Voice& v) const;
    void updateFilterCoeffs(Voice& v);
    void updateModulation(Voice& v);

    // Free-running LFO phases, advanced whenever any voice sounds. A voice
    // whose key-sync bit is clear adopts these at note-on instead of zero.
    float    freePhase_[2] = {0.0f, 0.0f};
    float    freeInc_[2] = {0.0f, 0.0f};
    RomView  rom_{};
    uint32_t sr_ = 32000;
    float    pitchTrim_ = 1.0f;
    uint8_t  srcMod_ = 0, srcAft_ = 0, srcExp_ = 0;
    const uint8_t* patch_ = nullptr;
    uint8_t  patchCopy_[362]{};
    Voice    voices_[kMaxVoices]{};
    uint32_t ageCounter_ = 0;
    int      voiceLimit_ = kMaxVoices;
};

} // namespace jv

#endif // JV_ENGINE_H
