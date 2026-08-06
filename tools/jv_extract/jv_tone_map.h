// JV-880 patch / tone parameter layout.
//
// Field names and the overall layout come from giulioz/jv880_juce
// (Source/dataStructures.h, by Giulio Zausa) -- see README.md for credit.
// Each field carries what is actually known about it:
//   VERIFIED    measured on the reference emulator with the differential probe
//               in this directory, and the behaviour matches the name
//   PARTIAL     measured and it does react, but the sub-field structure of the
//               byte is unresolved. These are fields whose author marked them
//               "check" himself -- the measurement confirms they are packed
//               multi-field bytes, not plain scalars. (+55 started here and is
//               now fully resolved; see jv_calibration.h.)
//   UNVERIFIED  taken from that header on trust; no measurement yet. Either the
//               probe never reached it or no base patch exercised its section.
//
// Layout is fixed-size and byte-packed; a patch is 362 bytes and holds four
// 84-byte tones after 12 bytes of name and 14 bytes of patch-common.
#ifndef JV_TONE_MAP_H
#define JV_TONE_MAP_H

#include <stdint.h>

#define JV_PATCH_SIZE   362
#define JV_TONE_OFFSET   26
#define JV_TONE_SIZE     84

// Patch banks inside jv880_rom2.bin, 64 patches each.
#define JV_BANK_USER 0x008CE0
#define JV_BANK_A    0x010CE0
#define JV_BANK_B    0x018CE0

// Multisample table: 129 entries of 60 bytes at 0x0004
//   +0  12 B name, +12 15 B split keys, +28 16 x uint16be sample index
//   A split key is the INCLUSIVE upper note of its zone; the last zone runs to
//   127 and unused sample slots are 0xFFFF. Verified by repointing zones at a
//   noise sample and sweeping notes -- see README.md.
// Sample table: 577 entries of 18 bytes at 0x1E41
//   +0 start(24), +3 loop(24), +6 end(24), +11 flag, +12 rootKey,
//   +13 tune(16be), +17 level
//   `end` is INCLUSIVE: a loop spans end-loop+1 samples.
//   rootKey is exact semitones; tune is 0.1 cent per unit, neutral at 1024.
//   The machine itself sits 9.4 cents below equal temperament.
//   All four established by patching rom2 and measuring -- see README.md.
#define JV_MULTI_TABLE   0x0004
#define JV_MULTI_COUNT   129
#define JV_MULTI_STRIDE  60
#define JV_SAMPLE_TABLE  0x1E41
#define JV_SAMPLE_COUNT  577
#define JV_SAMPLE_STRIDE 18

// Modulation matrix destination codes (low nibble of a DestAB byte). Measured
// by sending the mod wheel while sweeping the code; see README.md.
#define JV_MOD_DEST_OFF     0
#define JV_MOD_DEST_PITCH   1   // 19.05 cents per sensitivity unit, +-1 octave
#define JV_MOD_DEST_CUTOFF  2
#define JV_MOD_DEST_LEVEL   4
// 7, 8, 9, 10 are LFO-depth destinations: they make the sound oscillate at the
// LFO rate rather than shifting it. 3 and 5 have weak filter / pitch effects
// that are not pinned down; 6 and 11-15 produced nothing measurable.
#define JV_MOD_SENS_MAX    63   // signed; 64..127 disables, like the LFO depths

#pragma pack(push, 1)
typedef struct {
    uint8_t flags;              // +00 VERIFIED  bit7 = tone on/off; bits0-1 wave group
    uint8_t waveNumber;         // +01 VERIFIED  index into the multisample table
    uint8_t fxmConfig;          // +02 UNVERIFIED bit7 enable, bits1-6 depth
    uint8_t velocityRangeLow;   // +03 UNVERIFIED
    uint8_t velocityRangeUp;    // +04 UNVERIFIED
    uint8_t matrixModDestAB;    // +05 VERIFIED dest A = low nibble, B = high nibble
    uint8_t matrixModDestCD;    // +06 UNVERIFIED assumed dest C/D, same packing
    uint8_t matrixModSensA;     // +07 VERIFIED signed, +-63; 64..127 disables. Pitch dest: 19.05 cents/unit
    uint8_t matrixModSensB;     // +08 UNVERIFIED
    uint8_t matrixModSensC;     // +09 UNVERIFIED
    uint8_t matrixModSensD;     // +10 UNVERIFIED
    uint8_t matrixAftDestAB;    // +11 PARTIAL aftertouch source confirmed to reach the matrix; layout assumed
    uint8_t matrixAftDestCD;    // +12 UNVERIFIED
    uint8_t matrixAftSensA;     // +13 PARTIAL assumed same law as +07
    uint8_t matrixAftSensB;     // +14 UNVERIFIED
    uint8_t matrixAftSensC;     // +15 UNVERIFIED
    uint8_t matrixAftSensD;     // +16 UNVERIFIED
    uint8_t matrixExpDestAB;    // +17 PARTIAL expression (CC11) source confirmed; layout assumed
    uint8_t matrixExpDestCD;    // +18 UNVERIFIED
    uint8_t matrixExpSensA;     // +19 PARTIAL assumed same law as +07
    uint8_t matrixExpSensB;     // +20 UNVERIFIED
    uint8_t matrixExpSensC;     // +21 UNVERIFIED
    uint8_t matrixExpSensD;     // +22 UNVERIFIED
    uint8_t lfo1Flags;          // +23 VERIFIED bits0-2 wave (0/6/7 tri,1 sin,2 saw,3 sqr,4/5 rnd),
                            //     bits3-4 offset (shrinks the swing), bit6 key sync; bit5 unresolved
    uint8_t lfo1Rate;           // +24 VERIFIED 0.122 Hz * 2^(v/18.20), whole range
    uint8_t lfo1Delay;          // +25 VERIFIED 32.3 ms * 2^(v/12.0) before the LFO starts
    uint8_t lfo1Fade;           // +26 VERIFIED fade-in, 17.5 ms * 2^(v/10.5)
    uint8_t lfo2Flags;          // +27 VERIFIED identical to +23 in every respect probed
    uint8_t lfo2Rate;           // +28 VERIFIED same law as +24
    uint8_t lfo2Delay;          // +29 VERIFIED same law as +25
    uint8_t lfo2Fade;           // +30 VERIFIED same law as +26
    uint8_t lfo1PitchDepth;     // +31 VERIFIED depth 0..63; >=64 disables it entirely
    uint8_t lfo1TvfDepth;       // +32 VERIFIED depth 0..63; >=64 disables it entirely
    uint8_t lfo1TvaDepth;       // +33 VERIFIED depth 0..63; >=64 disables it entirely
    uint8_t lfo2PitchDepth;     // +34 VERIFIED depth 0..63; >=64 disables it entirely
    uint8_t lfo2TvfDepth;       // +35 VERIFIED depth 0..63; >=64 disables it entirely
    uint8_t lfo2TvaDepth;       // +36 VERIFIED depth 0..63; >=64 disables it entirely
    int8_t  pitchCoarse;        // +37 VERIFIED  signed; sweeping it unsigned aliases wildly
    int8_t  pitchFine;          // +38 VERIFIED fine tune; gentle monotonic pitch change
    uint8_t tvaPanKFpitchRandom;// +39 VERIFIED  two nibbles: high = pan keyfollow, low = random pitch
    uint8_t tvpKFtvaTimeKF;     // +40 UNVERIFIED
    uint8_t tvpVelocity;        // +41 UNVERIFIED
    uint8_t tvpT1T4Velocity;    // +42 UNVERIFIED
    uint8_t tvpEnvDepth;        // +43 VERIFIED pitch env depth; saturates above ~16
    uint8_t tvpEnvTime1;        // +44 VERIFIED acts on pitch
    uint8_t tvpEnvLevel1;       // +45 UNVERIFIED
    uint8_t tvpEnvTime2;        // +46 VERIFIED acts on pitch
    uint8_t tvpEnvLevel2;       // +47 UNVERIFIED
    uint8_t tvpEnvTime3;        // +48 VERIFIED acts on pitch
    uint8_t tvpEnvLevel3;       // +49 UNVERIFIED
    uint8_t tvpEnvTime4;        // +50 UNVERIFIED
    uint8_t tvpEnvLevel4;       // +51 UNVERIFIED
    uint8_t tvfCutoff;          // +52 VERIFIED cutoff, 431 Hz * 2^(v/17.93); needs +55 != OFF
    uint8_t tvfResonance;       // +53 VERIFIED resonance: level and brightness rise together
    uint8_t tvfTimeKFKeyfollow; // +54 PARTIAL reacts; period-2 alternation confirms it is packed
    uint8_t tvfVeloCurveLpfHpf; // +55 VERIFIED bits3-4 = filter mode: 0 OFF, 8 LPF, 16 HPF, 24 OFF
    uint8_t tvfVelocity;        // +56 PARTIAL reacts on brightness in steps
    uint8_t tvfT1T4Velocity;    // +57 UNVERIFIED
    uint8_t tvfEnvDepth;        // +58 VERIFIED bipolar around 64; strong brightness effect
    uint8_t tvfEnvTime1;        // +59 VERIFIED acts on brightness
    uint8_t tvfEnvLevel1;       // +60 VERIFIED acts on brightness
    uint8_t tvfEnvTime2;        // +61 VERIFIED acts on brightness
    uint8_t tvfEnvLevel2;       // +62 VERIFIED acts on brightness
    uint8_t tvfEnvTime3;        // +63 VERIFIED acts on brightness
    uint8_t tvfEnvLevel3;       // +64 VERIFIED acts on brightness
    uint8_t tvfEnvTime4;        // +65 UNVERIFIED
    uint8_t tvfEnvLevel4;       // +66 UNVERIFIED
    uint8_t tvaLevel;           // +67 VERIFIED  0 = silent; see JV_TVA_LEVEL_DB
    uint8_t tvaPan;             // +68 VERIFIED  0 = left, 64 = centre, 127 = right; constant-gain
    uint8_t tvaDelayTime;       // +69 VERIFIED delay: attack time rises to 79x, then past the measuring window
    uint8_t tvaTimeKFDelayKF;   // +70 PARTIAL reacts; alternating pattern confirms it is packed
    uint8_t tvaDelayModeVeloCrv;// +71 UNVERIFIED
    uint8_t tvaVelocity;        // +72 VERIFIED  bipolar, steps at bit 6
    uint8_t tvaT1T4Velocity;    // +73 UNVERIFIED upstream marks this "check again"
    uint8_t tvaEnvTime1;        // +74 VERIFIED  attack, 67.6 ms * 2^(v/14.06); linear in amplitude
    uint8_t tvaEnvLevel1;       // +75 VERIFIED level of stage 1
    uint8_t tvaEnvTime2;        // +76 VERIFIED decay time of stage 2
    uint8_t tvaEnvLevel2;       // +77 VERIFIED level of stage 2
    uint8_t tvaEnvTime3;        // +78 UNVERIFIED
    uint8_t tvaEnvLevel3;       // +79 VERIFIED  sustain level; 0 = silent
    uint8_t tvaEnvTime4;        // +80 VERIFIED  release, 56.7 ms * 2^(v/13.98) to -40 dB
    uint8_t drySend;            // +81 VERIFIED  raises level, shortens tail
    uint8_t reverbSend;         // +82 VERIFIED  lengthens tail
    uint8_t chorusSend;         // +83 VERIFIED  lengthens tail
} JvTone;

typedef struct {
    char    name[12];
    uint8_t revChorConfig;      // bits0-3 reverb type, 4-5 chorus type, 7 velocity
    uint8_t reverbLevel;
    uint8_t reverbTime;
    uint8_t reverbFeedback;
    uint8_t chorusLevel;        // bit7 chorus mode
    uint8_t chorusDepth;
    uint8_t chorusRate;
    uint8_t chorusFeedback;
    uint8_t analogFeel;
    uint8_t level;
    uint8_t pan;
    uint8_t bendRange;
    uint8_t flags;              // bits0-3 bend up, 4 porta mode, 5 solo legato, 6 porta sw, 7 key assign
    uint8_t portamentoTime;     // bit7 porta type
    JvTone  tones[4];
} JvPatch;
#pragma pack(pop)

#endif // JV_TONE_MAP_H
