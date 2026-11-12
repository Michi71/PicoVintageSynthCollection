// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/engine.h — Prophet VS engine: original architecture (waves, vector
//  mix, 5-point envelopes, LFOs, modulation matrix, analog filter stage)
//  + band-limited mip waves and effects (chorus, ensemble, VS chorus).
//
//  "Translation, not emulation": native C++17, portable (RP2350-capable),
//  block-based, no OS dependencies.
//
//  Architecture per voice (as in the original):
//    4 x wavetable OSC (wave 0..127, coarse 0..24 semitones, fine 0..99 cents)
//    -> vector mix (diamond A/B/C/D) with mix envelope (5 points X/Y)
//    -> 4-pole low-pass (cutoff, resonance, filter envelope)
//    -> VCA with amp envelope -> pan
//    -> sum -> effects (provs::Fx) -> output
//
//  Pitch as in the firmware (routine @0x5D12): everything in whole cents,
//    cent = (Note - 27)*100 + Coarse*100 + Fine
//  and from that exponentially (table @0x8000, 1200 entries per octave).
//  The engine anchors this to A4 = 440 Hz at coarse 0 / fine 0.
//
//  Modulation as in the firmware's timer ISR (@0x8DC ff.): two global
//  LFOs and the sources velocity, keyboard, filter envelope and mod wheel
//  are routed via destination bit masks to pitch, filter, mix, pan, level and
//  the LFOs themselves. The scalings are given at the fields.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstdint>

#include "provs/fx.h"
#include "provs/config.h"
#include "provs/gen/wavetables.h"

namespace provs {

constexpr int kMaxVoices = 8;   // as in the original
constexpr int kNoiseWave = 127; // wave 127 = noise (chip flag in the original)
constexpr int kControlSamples = 32;   // recompute modulation every 32 samples

// Filter characteristic of the original (schematic SD2400-4, data sheet CEM3379):
//   Vpin = CV * kVcfSpanMv - kVcfOffsetMv,  fc = kVcfHzAt0 * exp(Vpin / kVcfVtMv)
// CV 0..1 is the DAC voltage 0..5.2 V; it is applied through 20k + 1k to the
// frequency input, with 1k to ground and 75k to -8 V. The chip computes
// fc = Gm / (2 pi C), Gm = 200 uS * exp(Vpin / VT), C = 33 nF.
// VT is the thermal voltage of the control characteristic: 28.5 mV at 20 deg C
// (data sheet); it grows with the absolute temperature and is not compensated
// in the VS (1 kOhm metal film instead of a tempco resistor). With 30.9 mV
// (about 45 deg C inside the unit) all three statements of the owner's manual
// hold: eleven octaves of control range, roughly 9 units per octave and exact
// key tracking at a keyboard amount near 50.
constexpr float kVcfSpanMv = 233.4f;
constexpr float kVcfOffsetMv = 100.5f;
constexpr float kVcfHzAt0 = 964.6f;
constexpr float kVcfVtMv = 30.9f;

// ---------------------------------------------------------------------------
// VS envelope (owner's manual ch. 8): starts at L0, runs linearly
// L0->L1->L2->L3, holds at L3 as long as the key is pressed and then
// falls exponentially to L4.
// Loop as in the firmware (@0x4756, @0x0A0C):
//   0      no loop
//   1..3   at point 3 jump back to point 0/1/2, then forward again
//   4..6   alternates between point 0/1/2 and point 3 (backward with the same
//          segment times)
// repeat: 0 = endless, n = n repetitions, then hold.
struct Env5 {
    float level[5] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    float time[3]  = {0.008f, 0.008f, 0.008f};   // segment durations in seconds
    float releaseTau = 0.0f;                     // time constant, 0 = immediate
    float releaseTauAlt = -1.0f;                 // rate 4A (alternate release), < 0 = none
    // > 0: segments do not run linearly but approach their target like
    // 1 - exp(-curve * t/T) (curve shape of the Behringer envelopes)
    float curve = 0.0f;
    int loop = 0, repeat = 0;
};

struct OscParams {
    int   wave = 32;         // 0..127 (original wave number; 32 = sine)
    int   coarse = 0;        // 0..24 semitones upward
    int   fine = 0;          // 0..99 cents upward
    float level = 1.0f;
    // Edit Waveform mode (owner's manual ch. 13): the wave runs at this
    // multiple of the pitch, 1..32. Programs always use 1.
    int   harmonic = 1;
};

// Destination bits of the LFOs (order of the firmware tables @0x5F6 / @0x61E)
enum LfoDest : uint16_t {
    kLfoFreqA = 1 << 0, kLfoFreqB = 1 << 1, kLfoFreqC = 1 << 2, kLfoFreqD = 1 << 3,
    kLfoFilter = 1 << 4,       // +-1/4 of the cutoff scale at amount 99
    kLfoMixAC = 1 << 5,        // mix X, full scale
    kLfoMixBD = 1 << 6,        // mix Y, full scale
    kLfoOtherRate = 1 << 7,    // rate of the respective other LFO
    kLfoOtherAmount = 1 << 8,  // amount of the respective other LFO
    kLfoPan = 1 << 9,
};
enum LfoShape { kLfoTriangle = 0, kLfoSquare, kLfoSaw, kLfoRamp, kLfoRandom };

struct LfoParams {
    int shape = kLfoTriangle;
    int rate = 70;             // 0..99, table kLfoRateStep
    float hz = 0.0f;           // > 0: triangle frequency directly, instead of the table
    int amount = 0;            // 0..99; 99 = +-256 cents on the pitch
    uint16_t dest = 0;         // LfoDest bits
};

// Destination bits of the remaining sources (firmware @0x5AE0 ff., @0x45CA, @0x45F8)
enum : uint8_t {
    kVelFilterEnv = 1, kVelMixAC = 2, kVelMixBD = 4, kVelAmp = 8, kVelPan = 16,
    kKeyFilter = 1, kKeyMixAC = 2, kKeyMixBD = 4, kKeyPan = 8,
    kFenvFreq = 1, kFenvPan = 2,
    kWheelLfo1 = 1, kWheelLfo2 = 2, kWheelChorus = 4,
};
// Destination bits of the pressure source (firmware table @0x6E6)
enum PressDest : uint16_t {
    kPressFreqA = 1 << 0, kPressFreqB = 1 << 1, kPressFreqC = 1 << 2, kPressFreqD = 1 << 3,
    kPressFilter = 1 << 4, kPressMixAC = 1 << 5, kPressMixBD = 1 << 6,
    kPressLfo1Rate = 1 << 7, kPressLfo1Amount = 1 << 8,
    kPressLfo2Rate = 1 << 9, kPressLfo2Amount = 1 << 10,
    kPressVolume = 1 << 11, kPressPan = 1 << 12,
    kPressChorusRate = 1 << 13, kPressChorusDepth = 1 << 14,
};

enum KeyMode { kKeyNormal = 0, kKeySplit = 1, kKeyDouble = 2 };
enum ArpMode { kArpNormal = 0, kArpAutoLatch = 1, kArpExtend = 2 };
enum ArpScan { kArpUp = 0, kArpDown, kArpUpDown, kArpAssign, kArpReverse, kArpRandom };

struct Params {
    OscParams osc[4] = {{32}, {33}, {34}, {35}};   // A B C D (init patch of the firmware)
    Env5 ampEnv;
    Env5 filterEnv = {{0, 0, 0, 0, 0}};
    // Mix envelope: 5 points in joystick coordinates [-1,1] (0 = center).
    // A is at X = -1, C at X = +1, B at Y = -1, D at Y = +1.
    Env5 mixEnvX = {{0, 0, 0, 0, 0}};
    Env5 mixEnvY = {{0, 0, 0, 0, 0}};
    float mixAmount = 1.0f;  // depth of the mix envelope (1 = as programmed)
    float mixX = 0.0f, mixY = 0.0f;   // joystick offset (live)
    float cutoff = 1.0f;     // 0..1, linear in semitones (like the CV in the original)
    float resonance = 0.0f;  // 0..1
    float envAmount = 0.0f;  // filter envelope depth 0..1
    float ampLevel = 1.0f;   // base level of the VCA 0..1 (firmware field 69)

    LfoParams lfo[2];
    // Signed sources: amount -99..+99, destinations as a bit mask
    int velAmount = 0;   uint8_t velDest = 0;
    int keyAmount = 0;   uint8_t keyDest = 0;    // relative to note 66
    int fenvAmount = 0;  uint8_t fenvDest = 0;
    int pressAmount = 0; uint16_t pressDest = 0;  // PressDest bits
    uint8_t wheelDest = kWheelLfo1;
    float pan[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};   // -1 .. +1 per voice

    int glide = 0;           // 0 = off, 1..99 via kGlideRate
    int unison = 0;          // 0 = off, n = unison with n cents offset per voice

    // Keyboard mode (evaluated in the main layer only). With split and
    // double a second layer (Engine::setLayer2) plays on voices
    // 5-8, the main layer on 1-4.
    int keyMode = kKeyNormal;
    int splitPoint = 60;       // split: keys below it play layer 2
    int doubleDetune = 0;      // double: layer 2 higher by 0..31 cents
    float doubleDelay = 0.0f;  // double: layer 2 later by this many seconds

    // Arpeggiator (firmware fields 86-93; owner's manual ch. 5)
    int arpRate = 70;        // 1..99: clock period (106 - rate) ms; 0 = stopped
    int arpMode = kArpNormal;
    int arpScan = kArpUp;
    int arpOctaves = 0;      // 0..3 additional octaves upward
    int arpRepeats = 0;      // 0..3 additional repeats per note
    bool arpVelocity = true; // use the played velocity (otherwise fixed 80)
    // Split programs only: 0 off, 1 arpeggiate right of the split point,
    // 2 left of it, 3 dual - both sides, as two separate sequences
    int arpSplit = 0;
    bool arpPoly = true;     // false: mono voicing, one voice per sequence
    // Top C (note 96) counts as a rest; with an arpeggiator split the lowest
    // C (note 36) as well
    bool arpRests = false;
    // Layer (field 94): with a latched sequence, keys held after that are
    // arpeggiated as a second sequence beside it (overdub)
    bool arpLayer = false;

    // Chord mode of the Pro VS Mini: 0 = off, 1..8 = chord set (kChordSets);
    // each key plays three notes. Only with the Behringer data (PROVS_HAS_PVM).
    int chordSet = 0;

    int transpose = 0;       // semitones (Behringer: coarse 12 = concert pitch -> -12)
    float tuneCents = 0.0f;  // master tune
    float bendRange = 2.0f;  // semitones
    float gain = 0.7f;
    Fx::Type fxType = Fx::kOff;
    float fxRate = 0.4f, fxDepth = 0.5f;
    float fxRateHz = 0.0f;   // > 0: chorus frequency directly (original programs)
    uint8_t chorusOn = 3;    // Fx::kVsChorus: bit 0 right, bit 1 left (field 66)
};

// ---------------------------------------------------------------------------
class Engine {
public:
    void init(double sampleRate);
    void setParams(const Params& p);
    const Params& params() const { return pNext_; }
    // Second layer for split/double (the "linked program"). Globally, the
    // values of the main layer always apply: keyboard mode, pan per voice,
    // joystick, tune, bend, gain, FX.
    void setLayer2(const Params& p);
    const Params& layer2() const { return p2Next_; }

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();
    // Controllers are kept per voice, so that MIDI mode 4 (one channel per
    // voice) and two channels for the halves of a split can address them
    // separately; 'voices' is a bit mask, bit 0 = voice 1. The LFOs and the
    // chorus of a layer follow its first voice.
    static constexpr unsigned kAllVoices = (1u << kMaxVoices) - 1u;
    void pitchBend(float amount, unsigned voices = kAllVoices);     // -1..+1 (times bendRange)
    void setModWheel(float amount, unsigned voices = kAllVoices);   // 0..1 -> LFO amount
    void setPressure(float amount, unsigned voices = kAllVoices);   // 0..1 (Channel Pressure)
    // Joystick offset over MIDI, added to Params::mixX / mixY
    void setJoystick(int axis, float amount, unsigned voices = kAllVoices);
    // The voices of a layer as a mask (all of them without split or double)
    unsigned layerVoices(int layer) const;

    // A key for one layer only, over the whole keyboard and past the
    // arpeggiator - MIDI mode 3 with separate channels for the two halves.
    void layerOn(int layer, int note, int velocity);
    void layerOff(int layer, int note);
    // One voice played directly - MIDI mode 4. A note while the voice still
    // holds one changes its pitch without restarting the envelopes (legato).
    void voiceOn(int voice, int note, int velocity);
    void voiceOff(int voice, int note);

    // Overflow (owner's manual ch. 14): with all voices held, a further key
    // is not played by stealing one but handed out, for a second instrument.
    void setOverflow(bool on) { overflow_ = on; }
    // Hands out the arpeggiator's notes as well (the VS's arpeggiator channel).
    void setArpOut(bool on) { arpOut_ = on; }
    // Notes to be sent on: from the arpeggiator or as overflow. Filled while
    // rendering and playing, emptied by the caller.
    struct NoteOut { uint8_t arp; uint8_t on; uint8_t note; uint8_t velocity; };
    bool popNoteOut(NoteOut& out);
    // Alternate-release footswitch: release runs at rate 4A instead of rate 4
    void setAltRelease(bool on) { altRelease_ = on; }
    void setHold(bool on) { hold_ = on; if (!on) releaseHeld(); }

    // Arpeggiator: when switched on, noteOn/noteOff are routed through it. The
    // options are in Params; clocks/step is global as in the original
    // (factory setting 6).
    void setArp(bool on);
    bool arp() const { return arpOn_; }
    void setArpClocksPerStep(int clocks) { arpClocksPerStep_ = clocks < 1 ? 1 : clocks; }
    // Latch (@0x2FC4): freezes the sequences as they are. Keys played after
    // that sound normally - or, with the Layer option on, are arpeggiated as
    // a second sequence over the first. Switched on while nothing is held it
    // waits for the next keys instead and latches them once they are all
    // released: a panel without a latch button cannot be pressed while a
    // chord is held. Switching it off drops what is no longer held.
    void setArpLatch(bool on);
    bool arpLatched() const { return arpLatched_; }
    // Transpose of the arpeggiator notes in semitones (in the original: key
    // relative to middle C while Latch/Transpose is held)
    void setArpTranspose(int semitones) { arpTranspose_ = semitones; }
    // External clock: when true the arpeggiator runs only via arpTick()
    // (e.g. MIDI clock, 24 per quarter note)
    void setArpExternalClock(bool on) { arpExternal_ = on; }
    void arpTick() { if (arpOn_ && arpExternal_ && arpAny()) arpClock(); }
    // Keys a sequence holds: 0 = the first, 1 = the overdub or the left half
    // of a dual split (for tests and displays)
    int arpKeyCount(int sequence) const { return arp_[sequence & 1].count; }

    // Render an audio block (interleaved stereo)
    void render(float* out, int frames);

    int activeVoices() const { return active_; }

    // --- Programs ------------------------------------------------------------
    // The program bank lives in RAM and is shared by all Engine instances.
    // After startup it contains only the init patch of the firmware;
    // programs arrive at run time from the user's SysEx data. Do not load
    // concurrently with render()/setPreset() from another thread.
    //
    // loadVsSysex: SysEx dump of the Prophet VS, also several messages
    // in a row. A bank dump (F0 01 0A 64, 100 programs) replaces the bank,
    // a single program (F0 01 0A nn) fills slot nn. Returns: number of
    // programs accepted, 0 = nothing valid (bank stays unchanged).
    static int loadVsSysex(const uint8_t* data, size_t size);
    static void resetVsBank();               // back to the init patch
    static int presetCount();                // programs in the bank
    void setPreset(int index);
    // Write the program name (8 characters) into buf[9]
    static void presetName(int index, char* buf);

    // --- Program access for an editor and for storage -----------------------
    // A program is 116 fields, one byte each (see preset_map.h), packed
    // MSB-first into 82 bytes by the firmware's width table; a dump sends
    // those as 164 nibbles.
    static constexpr int kVsFields = 116;
    static constexpr int kVsProgramBytes = 82;
    static constexpr int kVsDumpBytes = 4 + 2 * kVsProgramBytes + 1;   // F0 01 0A nn .. F7
    static int vsFieldBits(int field);
    // The fields of a program in the bank (the init program for an index
    // outside it).
    static const uint8_t* vsProgram(int index);
    // Writes a program into slot 0..99; each field is masked to its width.
    // The bank grows to include the slot.
    static bool storeVsProgram(int index, const uint8_t* fields);
    // Number of programs the bank counts, 1..100 (for restoring a stored bank).
    static void setVsBankCount(int count);
    // Plays a program from an edit buffer instead of from the bank. The
    // linked program of a split or double still comes from the bank.
    void setVsProgram(const uint8_t* fields);
    static void packVsProgram(const uint8_t* fields, uint8_t* bytes);
    static void unpackVsProgram(const uint8_t* bytes, uint8_t* fields);
    // Single-program dump (F0 01 0A nn, 164 nibbles, F7) into out[kVsDumpBytes].
    // Returns kVsDumpBytes. A bank dump is F0 01 0A 64, the nibbles of all
    // 100 programs (dumpVsNibbles) and one F7.
    static size_t dumpVsProgram(const uint8_t* fields, int slot, uint8_t* out);
    static void dumpVsNibbles(const uint8_t* fields, uint8_t* out164);

    // --- User waves ----------------------------------------------------------
    // Waves 0..31 are RAM on the original: 128 samples of 12 bits, signed. A
    // slot that has been written plays from here; one that has not plays what
    // the build put there (the Pro VS Mini's wave, or silence). Shared by all
    // Engine instances, like the program bank.
    static constexpr int kUserWaves = 32;
    static constexpr int kWaveSamples = 128;
    static constexpr int kWaveRecordBytes = 192;
    // Writes a slot. Its band-limited levels are computed by buildUserWaves();
    // until then the slot plays what it played before.
    static void setUserWave(int slot, const int16_t* samples);
    // Back to what the build put there.
    static void clearUserWave(int slot);
    static void clearUserWaves();
    static uint32_t userWaveMask();              // bit n: slot n has been written
    // The 12-bit samples of a written slot (zeros and false for one that has
    // not been).
    static bool userWave(int slot, int16_t* out);
    // Computes the band-limited levels of written slots, at most 'harmonics'
    // harmonics of work per call (31 complete one wave; each costs about a
    // thousand multiplications). Returns true while work is left, so a caller
    // can spread it over its ticks.
    static bool buildUserWaves(int harmonics);
    // The samples of any wave 0..126 on the 12-bit scale, whatever it plays
    // from. The Mini's waves may exceed 12 bits; 127 (noise) gives zeros.
    static void waveSamples(int wave, int16_t* out);
    // The mix of four waves as Edit Waveform mode stores it (@0x7222): each
    // wave read at its harmonic step, weighted by the joystick position x, y
    // (-1..+1), rounded to 12 bits. A mix that would exceed them is scaled
    // down as a whole.
    static void mixWave(const int* wave, const int* harmonic, float x, float y, int16_t* out);
    // The 192-byte record of the firmware: the upper eight bits of the 128
    // samples, then their lower four bits, two samples to a byte.
    static void packWave(const int16_t* samples, uint8_t* record);
    static void unpackWave(const uint8_t* record, int16_t* samples);
    // The table a written slot plays from: level 0 is the wave, 1..3 are
    // band-limited to 31, 15 and 7 harmonics; 128 samples, full scale 16384
    // (for tests)
    static const int16_t* userWaveTable(int slot, int level);

    // Patch bank of the Behringer Pro VS Mini (F0 00 20 32 ..., 32 patches).
    // Usable only with the optional Behringer tables (PROVS_HAS_PVM, ADSR times),
    // otherwise returns 0.
    static int loadPvmSysex(const uint8_t* data, size_t size);
    static void resetPvmBank();
    static int pvmPatchCount();
    static const char* pvmPatchName(int index);
    void setPvmPatch(int index);

    // Pitch of an oscillator in Hz without modulation (for tests / display)
    float oscHz(int note, int osc) const;

private:
    enum { kEnvHold = 6, kEnvRelease = 7, kEnvDone = 8 };
    struct EnvState {
        float v = 0.0f, inc = 0.0f, target = 0.0f, coef = 0.0f;
        int left = 0;
        int repeat = 0;
        int stage = kEnvDone;  // 0..5 segments (3..5 = reverse run), then see above
    };
    struct Lfo {
        float phase = 0.0f;    // -1 .. +1 (16-bit phase of the firmware)
        float dir = 1.0f;
        float rnd = 0.0f;
        float out = 0.0f;      // shape value times amount, -1 .. +1
    };
    struct Voice {
        bool on = false, held = false, pending = false;
        int arpSeq = -1;       // started by this arpeggiator sequence, -1 = by a key
        int index = 0;
        int layer = 0;
        int note = 60;
        float velMod = 0.0f, keyMod = 0.0f;
        float detune = 0.0f;   // unison offset in cents
        float glide = 0.0f;    // remaining distance to the target pitch in cents
        float phase[4] = {0, 0, 0, 0};
        float inc[4] = {0, 0, 0, 0};
        const int16_t* tab[4] = {nullptr, nullptr, nullptr, nullptr};
        float tabScale[4] = {0, 0, 0, 0};   // int16 -> level
        float noise[4] = {0, 0, 0, 0};
        EnvState amp, filt, mx, my;
        // computed at the control rate
        float fcoef = 1.0f, level = 0.0f, panL = 0.5f, panR = 0.5f;
        float modX = 0.0f, modY = 0.0f;
        // Step per sample until the next control tick (against staircase steps):
        // fcoef, level, panL, panR, modX, modY, inc[0..3]
        float ramp[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        float vcaCv = 0.0f;    // smoothed VCA control voltage
        float vcfCv = -1.0f;   // smoothed filter control voltage (< 0: none yet)
        float f0 = 0.0f, f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;
        float dcX = 0, dcY = 0;
        uint64_t age = 0;
    };

    void envSegment(EnvState& e, const Env5& p, int stage) const;
    void envStart(EnvState& e, const Env5& p) const;
    void envRelease(EnvState& e, const Env5& p) const;
    void envTick(EnvState& e, const Env5& p) const;

    struct Delayed { int samples; int note; int velocity; bool on; };

    const Params& lp(int layer) const { return layer ? p2_ : p_; }
    void applyParams();
    void lfoAdvance(int samples, int layer);
    void voiceControl(Voice& v, bool snap = false);
    int pickVoice(int lo, int hi);
    void startVoice(int index, int note, int velocity, float detune, int layer);
    void layerRange(int layer, int& lo, int& hi) const;
    void layerNoteOn(int layer, int note, int velocity);
    void layerNoteOff(int layer, int note);
    void unisonUpdate(int layer, int velocity);
    void queueDelayed(int note, int velocity, bool on);
    void keyOn(int note, int velocity);
    void keyOff(int note);
    void inputOn(int note, int velocity);
    void inputOff(int note);
    struct ArpSeq;
    bool arpAny() const { return arp_[0].count > 0 || arp_[1].count > 0; }
    int arpRoute(int note) const;
    void arpSeqReset(ArpSeq& q);
    void arpLatchNow();
    void arpClock();
    void arpStepSeq(int s);
    void arpGateOff(int s);
    bool arpOwns(int voice) const;
    int keysDown(int lo, int hi) const;
    void startRelease(Voice& v);
    void releaseHeld();
    void countActive();

    float sr_ = 44100.0f;
    Params p_, pNext_;
    Params p2_, p2Next_;       // second layer
    bool paramsDirty_ = true;
    Voice voices_[kMaxVoices];
    Lfo lfo_[2][2];            // [layer][LFO]
    int ctl_ = 0;
    int active_ = 0;
    float bend_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    float wheel_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    float pressure_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    float joyX_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    float joyY_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool overflow_ = false, arpOut_ = false;
    uint32_t overflowed_[4] = {0, 0, 0, 0};    // notes handed out as overflow
    static constexpr int kNoteOutSize = 32;
    NoteOut noteOut_[kNoteOutSize];
    int noteOutHead_ = 0, noteOutCount_ = 0;
    void pushNoteOut(bool arp, bool on, int note, int velocity);
    bool allHeld(int lo, int hi) const;
    // per voice: last played pitch (for glide), survives the Voice reset
    float lastCents_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool played_[kMaxVoices] = {false, false, false, false, false, false, false, false};
    // Unison: held keys, the lowest one sounds
    int heldNotes_[2][16];
    int heldCount_[2] = {0, 0};
    int unisonNote_[2] = {-1, -1};
    // Arpeggiator: two sequences as in the firmware (@0xFFFFB828 and
    // @0xFFFFBA26), stepped together. The first takes the keys; the second
    // is the overdub of the Layer option, or the left half of a dual split.
    struct ArpKey { int note; int velocity; };
    static constexpr int kArpMaxKeys = 64;
    struct ArpSeq {
        ArpKey keys[kArpMaxKeys];
        int count = 0;
        int mode = kArpNormal;     // the arpeggiator mode it was recorded in
        bool latched = false;      // held by the latch (flag at 0x1F0)
        int pos = -1, oct = 0, dir = 1, repeatLeft = 0;
        int note = -1;             // the note it sounds
        bool sounding = false;
    };
    ArpSeq arp_[2];
    uint32_t keyDown_[4] = {0, 0, 0, 0};   // keys physically held, one bit per note
    bool arpOn_ = false;
    bool arpLatchWanted_ = false, arpLatched_ = false, arpArmed_ = false;
    int arpClocksPerStep_ = 6;
    int arpClock_ = 0;             // clocks until the next step
    float arpSamples_ = 0.0f;      // samples until the next clock
    bool arpExternal_ = false;
    int arpSeqNow_ = -1;           // the sequence whose note is being started or ended
    int arpTranspose_ = 0;
    // Double with delay: delayed notes of the second layer
    Delayed delayed_[32];
    int delayedCount_ = 0;
    bool hold_ = false;
    bool altRelease_ = false;
    uint32_t rng_ = 0x2545F491u;
    Fx fx_;
    uint64_t clock_ = 0;
};

}  // namespace provs
