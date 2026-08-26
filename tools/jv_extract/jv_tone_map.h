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
//   MANUAL      the JV-880 owner's manual names the parameter and its range,
//               but the packing into this byte is still inferred.
//
// The manual (section 10, "Parameter address map", printed pages 10-38..10-39)
// lists the SysEx view of a tone: 116 addresses, 0x00..0x73, five of which are
// nibble pairs (wave number, both LFO delays, pan, tone delay time). That is
// the same parameter set as the 84 bytes below, only unpacked -- so it names
// every field here and gives its range, which is why several entries that the
// probe could only call PARTIAL now have a documented meaning. Where the two
// disagree the measurement wins: the manual describes the panel, the ROM byte
// is what the chip reads. Both are noted when they differ.
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
// by sending the mod wheel while sweeping the code; the manual then confirmed
// the whole table and closed the gaps -- it lists the destination range as
// 0..12 with exactly these names, so there is nothing above 12 to find, and the
// codes the probe could not move were simply ones the base patch left idle
// (dest 6 needs a non-zero LFO2 pitch depth, 11/12 need a running LFO).
#define JV_MOD_DEST_OFF        0
#define JV_MOD_DEST_PITCH      1   // 19.05 cents per sensitivity unit, +-1 octave
#define JV_MOD_DEST_CUTOFF     2
#define JV_MOD_DEST_RESONANCE  3
#define JV_MOD_DEST_LEVEL      4
#define JV_MOD_DEST_PITCH_LFO1 5   // scales LFO1's pitch depth, 22 cents/unit
#define JV_MOD_DEST_PITCH_LFO2 6
#define JV_MOD_DEST_TVF_LFO1   7
#define JV_MOD_DEST_TVF_LFO2   8
#define JV_MOD_DEST_TVA_LFO1   9
#define JV_MOD_DEST_TVA_LFO2  10
#define JV_MOD_DEST_LFO1_RATE 11
#define JV_MOD_DEST_LFO2_RATE 12
// Sensitivity. The probe found 0..63 acting as a positive amount and stopping
// there; the manual gives the panel range as -63..+63. The LFO depths had the
// same shape and turned out to be signed over the whole byte, so the high half
// is much more likely to be the negative side than an inert region -- unmeasured
// either way, and the engine still treats it as a magnitude.
#define JV_MOD_SENS_MAX    63

#pragma pack(push, 1)
typedef struct {
    uint8_t flags;              // +00 VERIFIED  bit7 = tone on/off; bits0-1 wave group
    uint8_t waveNumber;         // +01 VERIFIED  index into the multisample table
    uint8_t fxmConfig;          // +02 VERIFIED FXM switch (bit 7) + depth 0..15 (panel 1..16).
                                //     Frequency cross modulation: a fixed
                                //     125 Hz square wave on the playback rate,
                                //     2.17 % per panel step. Measured -- see
                                //     jv_calibration.h.
    uint8_t velocityRangeLow;   // +03 DISPROVEN  the manual calls this a velocity
                                //     window's lower bound. It is not one, or not one the
                                //     machine gates on: forced to 127 in a patch written
                                //     into the reference's temporary patch area -- which
                                //     under that reading silences the tone below velocity
                                //     127 -- its output is unchanged at every velocity.
    uint8_t velocityRangeUp;    // +04 DISPROVEN  likewise, forced to 30 and nothing moves.
                                //     The engine gated on these two until 26.08.2026 and
                                //     that silenced six bank-A basses (St Fretless, House
                                //     Bass, Thumpin Bass, Pick Bass, Wonder Bass, Yowza
                                //     Bass) below velocity 61, where every one of their
                                //     tones carries +03 = 61. Whatever these bytes are,
                                //     do not gate on them.
                                //     NOTE: the machine DOES gate somewhere -- the user
                                //     patch "Dist Line" is silent on the reference at
                                //     velocity 20 and sounds here, at -57 dBFS. Whatever
                                //     does that is still unfound.
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
                            //     bits3-5 offset, bit6 key sync, bit7 fade polarity IN/OUT.
                            //     The offset is a FIVE-valued field (-100/-50/0/+50/+100) and
                            //     neutral at index 2, not 0 -- which is why sweeping bit 5 alone
                            //     looked inert and why the depth tables and the offset scale had
                            //     been cancelling each other unnoticed. It shifts the waveform
                            //     rather than resizing it, until at +-100 it sits wholly above or
                            //     below the centre. Fade OUT runs the LFO at full depth from
                            //     note-on and fades it away. The three-bit width and the fade
                            //     bit are independently corroborated by the parameter table in
                            //     charlesvestal/schwung-jv880; see README.md.
    uint8_t lfo1Rate;           // +24 VERIFIED 0.122 Hz * 2^(v/18.20), whole range
    uint8_t lfo1Delay;          // +25 VERIFIED 32.3 ms * 2^(v/12.0) before the LFO starts.
                                //     MANUAL: the panel has a KEY-OFF setting past 127 that
                                //     holds the LFO off until the note is released.
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
                                //     MANUAL: pan keyfollow is one of 15 steps -100..+100 about
                                //     C4; random pitch is one of 16 cent amounts
                                //     0/5/10/20/30/40/50/70/100/200/300/400/500/600/800/1200,
                                //     redrawn per note.
    uint8_t tvpKFtvaTimeKF;     // +40 MANUAL  packed keyfollows. Pitch keyfollow is 0..15
                                //     (-100..+200, normal is +100 = one octave per twelve keys)
                                //     and a tone set away from +100 does not track the keyboard
                                //     at all -- the engine assumes +100 for every tone.
    uint8_t tvpVelocity;        // +41 MANUAL  P-ENV velocity level sense, -63..+63
    uint8_t tvpT1T4Velocity;    // +42 MANUAL  P-ENV velocity on/off time sense, two 15-step fields
    uint8_t tvpEnvDepth;        // +43 VERIFIED pitch env depth; saturates above ~16
                                //     MANUAL: -12..+12 semitones, centred at 64
    uint8_t tvpEnvTime1;        // +44 VERIFIED acts on pitch
    uint8_t tvpEnvLevel1;       // +45 MANUAL  P-ENV levels are bipolar, -63..+63 about 64,
    uint8_t tvpEnvTime2;        // +46 VERIFIED acts on pitch     unlike the TVF and TVA levels
    uint8_t tvpEnvLevel2;       // +47 MANUAL  bipolar
    uint8_t tvpEnvTime3;        // +48 VERIFIED acts on pitch
    uint8_t tvpEnvLevel3;       // +49 MANUAL  bipolar
    uint8_t tvpEnvTime4;        // +50 MANUAL  release segment of the pitch envelope
    uint8_t tvpEnvLevel4;       // +51 MANUAL  bipolar. The whole pitch envelope is unimplemented:
                                //     the engine reads none of +40..+51.
    uint8_t tvfCutoff;          // +52 VERIFIED cutoff, 431 Hz * 2^(v/17.93); needs +55 != OFF
    uint8_t tvfResonance;       // +53 VERIFIED resonance: level and brightness rise together
    uint8_t tvfTimeKFKeyfollow; // +54 PARTIAL reacts; period-2 alternation confirms it is packed
                                //     MANUAL: the two fields are cutoff keyfollow (0..15,
                                //     -100..+200 about C4) and TVF-ENV time keyfollow (0..14,
                                //     -100..+100, shortening T2..T4 as the note rises). Cutoff
                                //     keyfollow is the one that matters: without it every tone
                                //     filters at the same absolute frequency across the keyboard.
    uint8_t tvfVeloCurveLpfHpf; // +55 VERIFIED bits3-4 = filter mode: 0 OFF, 8 LPF, 16 HPF, 24 OFF
                                //     MANUAL: the byte also holds the TVF-ENV velocity curve
                                //     (one of seven shapes, linear through hard knee) and the
                                //     resonance mode SOFT/HARD.
    uint8_t tvfVelocity;        // +56 PARTIAL reacts on brightness in steps
                                //     MANUAL: TVF-ENV velocity level sense, -63..+63. Positive
                                //     raises the envelope level with velocity, negative inverts.
    uint8_t tvfT1T4Velocity;    // +57 MANUAL  TVF-ENV velocity on/off time sense, two 15-step
                                //     fields, -100..+100 on T1 (note-on) and T4 (note-off)
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
    uint8_t tvaPan;             // +68 VERIFIED  0 = left, 64 = centre, 127 = right; constant-gain.
                                //     128 alternates between two positions note to note; the
                                //     manual calls that setting RND and describes it as a random
                                //     stereo position per note, so the alternation is what the
                                //     chip actually does with a nominally random parameter.
    uint8_t tvaDelayTime;       // +69 VERIFIED delay: attack time rises to 79x, then past the measuring window
                                //     MANUAL: past 127 sits KEY-OFF, which starts the tone when
                                //     the key is released regardless of the mode.
    uint8_t tvaTimeKFDelayKF;   // +70 PARTIAL reacts; alternating pattern confirms it is packed
                                //     MANUAL: the TVA-side keyfollows are level keyfollow
                                //     (0..14, -100..+100) and TVA-ENV time keyfollow (0..14);
                                //     which nibble is which is not established.
    uint8_t tvaDelayModeVeloCrv;// +71 MANUAL  tone delay mode 0..2 plus the TVA-ENV velocity curve
                                //     (seven shapes). NORMAL delays the tone and lets it sound
                                //     after release; HOLD drops it if the key is released first;
                                //     PLAY-MATE takes the gap between the last two note-ons as
                                //     the delay, up to two seconds.
    uint8_t tvaVelocity;        // +72 VERIFIED  bipolar, steps at bit 6. Measured as a pure
                                //     attenuation: at velocity 127 every setting gives the same
                                //     level, lower velocities are pulled down further the larger
                                //     the value. MANUAL: the panel range is -63..+63 and negative
                                //     values make HARDER playing quieter -- so the upper half of
                                //     the byte is probably the inverted side rather than inert,
                                //     the same trap the LFO depths sprang. Unmeasured.
    uint8_t tvaT1T4Velocity;    // +73 MANUAL  TVA-ENV velocity on/off time sense, two 15-step
                                //     fields: T1 scaled by note-on velocity, T4 by release velocity
    uint8_t tvaEnvTime1;        // +74 VERIFIED  attack, 67.6 ms * 2^(v/14.06); linear in amplitude
    uint8_t tvaEnvLevel1;       // +75 VERIFIED level of stage 1
    uint8_t tvaEnvTime2;        // +76 VERIFIED decay time of stage 2
    uint8_t tvaEnvLevel2;       // +77 VERIFIED level of stage 2
    uint8_t tvaEnvTime3;        // +78 UNVERIFIED
    uint8_t tvaEnvLevel3;       // +79 VERIFIED  sustain level; 0 = silent
    uint8_t tvaEnvTime4;        // +80 VERIFIED  release, 56.7 ms * 2^(v/13.98) to -40 dB
    uint8_t drySend;            // +81 VERIFIED  raises level, shortens tail. MANUAL names this
                                //     "output dry level", 0..127, and it scales the direct path
                                //     alone -- the sends are separate, so a tone can be softer
                                //     dry than wet. The engine does not read it, which biases
                                //     every effect-heavy tone upward relative to the hardware.
    uint8_t reverbSend;         // +82 VERIFIED  lengthens tail. MANUAL: reverb send level 0..127
    uint8_t chorusSend;         // +83 VERIFIED  lengthens tail. MANUAL: chorus send level 0..127
} JvTone;

// Patch common. Of these the engine reads only `level` and `pan`; the rest are
// listed with their manual ranges so it is clear what is being left on the table.
typedef struct {
    char    name[12];
    uint8_t revChorConfig;      // bits0-3 reverb type, 4-5 chorus type, 7 velocity
                                //     reverb type 0..7 = ROOM1, ROOM2, STAGE1, STAGE2, HALL1,
                                //     HALL2, DELAY, PAN-DLY; chorus type 0..2. Bit 7 is the
                                //     patch velocity switch, which disables velocity response.
    uint8_t reverbLevel;
    uint8_t reverbTime;         //     also the delay time when the type is DELAY / PAN-DLY
    uint8_t reverbFeedback;     //     only meaningful for DELAY / PAN-DLY
    uint8_t chorusLevel;        // bit7 chorus mode: MIX, or route the chorus through the reverb
    uint8_t chorusDepth;
    uint8_t chorusRate;
    uint8_t chorusFeedback;
    uint8_t analogFeel;         //     0..127, the drift that keeps unison tones from phase-locking
    uint8_t level;              //     0..127, its own curve -- see JV_PATCH_LEVEL_DB
    uint8_t pan;                //     L64..0..63R
    uint8_t bendRange;          //     bend down, -48..0 semitones
    uint8_t flags;              // bits0-3 bend up (0..+12), 4 porta mode, 5 solo legato,
                                //     6 porta sw, 7 key assign POLY/SOLO. The bridge hardwires
                                //     +-2 semitones and always plays poly, so lead patches
                                //     written as SOLO with portamento come out wrong.
    uint8_t portamentoTime;     // bit7 porta type TIME/RATE
    JvTone  tones[4];
} JvPatch;
#pragma pack(pop)

#endif // JV_TONE_MAP_H
