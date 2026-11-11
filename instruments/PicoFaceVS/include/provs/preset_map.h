// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/preset_map.h — field mapping of the original Prophet VS programs
//
//  A program is 82 bytes, bit-packed (MSB-first) into 116 fields according to
//  the firmware's width table @0x48F6. Directly after the table (@0x496C)
//  lies the firmware's init patch; it is the key to the zero points.
//
//  CONFIDENCE:
//   [FW]   read from firmware code/data
//   [MAN]  owner's manual (ch. 6: Coarse = semitones above concert pitch,
//          Fine = cents; ch. 8: envelope starts at level 0, rates 1-4 and 4A,
//          holds at point 3, release exponential, amp level 4 is always 0)
//   [STAT] from statistics over the 100 factory programs + init patch
//   [ANN]  assumption of the engine (not backed by the firmware)
//
//  FIELD   BITS  MEANING                                     CONFIDENCE
//   0-3     7    Wave A-D (init 32,33,34,35)                 [FW] init patch, upload @0x43A6
//   4-7     5    Coarse A-D, 0..24 semitones upward          [FW] @0x5D2E: *100, init 0
//   8-11    7    Fine A-D, 0..99 cents upward                [FW] @0x5D3A: added, init 0
//  12       7    Filter cutoff 0..99 (linear CV, table @0x6590) [FW] @0x5DFA
//  13       7    Resonance                                   [STAT]
//  14       7    Filter envelope amount                      [STAT]
//  15-16    3    LFO 1/2 shape: 0 triangle, 1 square,        [FW] @0x42D8, ISR @0x936
//                2 sawtooth, 3 ramp, 4 random
//  17-18    7    LFO 1/2 rate, table @0x6790                 [FW] @0x42B0
//  19-23    7    Amp env: rates 1,2,3,4,4A                   [FW] @0x465C
//  24-27    7    Amp env: levels 0..3 (level 4 = 0)          [FW] @0x4652, clr.l @0x465A
//  28-29    3    Amp env loop/repeat                         [FW] @0x463A
//  30-34    7    Filter env: rates 1,2,3,4,4A                [FW] @0x46B4
//  35-39    7    Filter env: levels 0..4                     [FW] @0x46A8
//  40-41    3    Filter env loop/repeat                      [FW] @0x4690
//  42-46    7    Mix env: rates 1,2,3,4,4A                   [FW] @0x4710
//  47-56    7    Mix env: 5 points (X,Y), 7-bit signed       [FW] sign extension in the loader
//  57-58    3    Mix env loop/repeat                         [STAT]
//  59       2    Keyboard mode: 0 normal, 1 Split, 2 Double   [FW] @0x121E
//  60       7    Split point (note)                           [FW] @0x126E
//  61       7    linked program for Split/Double              [FW] @0x4212
//  62       5    Detune of the second program in Double mode  [FW] @0x4064
//  63       7    Delay of the second program in Double mode   [FW] @0x1252, @0x54A4
//                (owner's manual: 0-500 ms)
//  64       3    Unison: 0 off, n = offset n cents per voice  [FW] @0x1284, @0x4068
//  65       7    Glide, table @0x6858                         [FW] @0x41AE
//  66       2    Chorus on: bit 0 right, bit 1 left           [FW] @0x3D88, schematic U320
//  67-68    7    Chorus rate / depth (original chorus)       [FW] @0x4288
//  69       7    VCA base level                              [FW] @0x4144, ISR @0xCD2
//  70-77    7    Pan per voice 1-8, signed                   [FW] @0x4278, ISR @0xD9C
//  78-85    5    Program name, 8 characters (A-Z, 1-5, space;     [FW] character set @0x1A2F,
//                init patch "BASIC")                            init patch
//  86       7    Arpeggiator rate 1..99                       [FW] @0x5F26
//  87       2    Arp mode: 0 normal, 1 Auto-Latch, 2 Extend   [FW] @0x5F70
//  88       3    Arp scan: Up, Down, Up/Down, Assign,         [FW] @0x5F7E, @0x60A8 ff.
//                Reverse, Random
//  89       2    Arp octaves (0..3 additional)                [FW] @0x6134
//  90       2    Arp repeats (0..3 additional)                [FW] @0x62AC
//  91       2    Arp split: Off, Right, Left, Dual            [MAN] (Dual same as Off)
//  92       1    Arp voicing: 0 Mono, 1 Poly                  [FW] @0x5FC6
//  93       1    Arp velocity on/off                          [MAN]
//  94       1    Arp layer                                    [MAN] - not implemented
//  95       1    Arp rests                                    [MAN]
//  96-97    7    LFO 1/2 amount 0..99                        [FW] @0x44BC
//  98       8    Pressure amount, signed                     [FW] @0x44E4
//  99       8    Velocity amount, signed                     [FW] @0x44F6
//  100      8    Keyboard amount, signed (reference note 66) [FW] @0x4508
//  101      8    Filter env modulation amount, signed        [FW] @0x451A
//  102-103 2+8   LFO 1 destinations (10 bits)                [FW] @0x456A, table @0x5F6
//  104-105 2+8   LFO 2 destinations (10 bits)                [FW] @0x458E, table @0x61E
//                  bits 0-3 freq A-D, 4 filter, 5 mix A-C, 6 mix B-D,
//                  7 rate of the other LFO, 8 amount of the other LFO, 9 pan
//  106-107 7+8   Pressure destinations (15 bits)             [FW] @0x45B2, table @0x6E6
//                  bits 0-3 freq A-D, 4 filter, 5 mix A-C, 6 mix B-D,
//                  7 LFO 1 rate, 8 LFO 1 amount, 9 LFO 2 rate, 10 LFO 2 amount,
//                  11 volume, 12 pan, 13 chorus rate, 14 chorus depth
//  109      5    Velocity destinations:                      [FW] @0x5B5A ff.
//                  bit 0 filter env depth, 1 mix A-C, 2 mix B-D,
//                  3 VCA level, 4 pan
//  111      4    Keyboard destinations:                      [FW] @0x5B26 ff.
//                  bit 0 filter, 1 mix A-C, 2 mix B-D, 3 pan
//  113      2    Filter env destinations:                    [FW] @0x45CA
//                  bit 0 freq (all 4), 1 pan
//  115      3    Mod wheel destinations:                     [FW] @0x45F8, table @0x75E
//                  bit 0 LFO 1 amount, 1 LFO 2 amount, 2 chorus depth
//
//  Scaling of the modulation (ISR @0x974 ff.): LFO output = shape * amount;
//  onto mix and pan at full scale, onto filter and LFO amount /4, onto pitch
//  and LFO rate /128 (amount 99 = +-256 cents). Filter env onto pitch /32
//  (+-1024 cents). Velocity runs through the curve @0x6690.
//  Pressure (@0x3B58): full scale onto volume, pan and LFO amount, /4 onto
//  filter, mix and LFO rate, /64 onto the pitch (+-512 cents).
//  Mod wheel (@0x51A4): MIDI value 127 = full LFO amount.
//
//  Loop/Repeat (fields 28/29, 40/41, 57/58): Loop 0 off, 1-3 jump from
//  point 3 back to point 0/1/2, 4-6 alternate between point 0/1/2 and 3;
//  Repeat 0 = endless, otherwise number of repetitions. [FW] @0x4756, @0x0A0C
//
//  Not implemented: for the arpeggiator, Layer (overdub) and Split "Dual";
//  separate chorus values per layer with
//  Split/Double; the analog chorus circuit itself (replaced by the
//  Behringer chorus).
//  Levels 0..99 run through the linear table @0x6590 (331 per step).
// ============================================================================
#pragma once
#include <cmath>
#include <cstdint>

#include "provs/engine.h"
#include "provs/fw_tables.h"

namespace provs {

// 7-bit two's complement (firmware loader: sign extension)
inline float signed7(int v) { return float(v > 63 ? v - 128 : v); }
inline int signed8(int v) { return v > 127 ? v - 256 : v; }

// --- Envelope times from the firmware ----------------------------------------
// The envelopes run in the timer ISR (@0x7B6), on every 4th interrupt.
// Tables kEnvSegmentTicks and kEnvReleaseCoef: see fw_tables.h.
// Tick duration: kFwTickSeconds (fw_tables.h), 4 ms.
constexpr float kEnvTickSeconds = kFwTickSeconds;

inline float envSegmentSeconds(int rate) {
    return float(kEnvSegmentTicks[rate > 99 ? 99 : rate]) * kEnvTickSeconds;
}
// Time constant of the exponential release in seconds (0 = immediate)
inline float envReleaseTau(int rate) {
    const int k = kEnvReleaseCoef[rate > 99 ? 99 : rate];
    if (k <= 0) return 0.0f;
    return -kEnvTickSeconds / std::log(float(k) / 32768.0f);
}

inline float level99(int v) { return v >= 99 ? 1.0f : float(v) / 99.0f; }

// f: the 116 fields of a program, one byte per field
inline void mapVsPreset(const uint8_t* f, Params& p) {
    for (int i = 0; i < 4; ++i) {
        p.osc[i].wave = f[0 + i] & 127;
        p.osc[i].coarse = f[4 + i] > 24 ? 24 : f[4 + i];
        p.osc[i].fine = f[8 + i] > 99 ? 99 : f[8 + i];
        p.osc[i].level = 1.0f;
    }
    p.cutoff = level99(f[12]);
    p.resonance = level99(f[13]);
    p.envAmount = level99(f[14]);

    for (int i = 0; i < 3; ++i) {
        p.ampEnv.time[i] = envSegmentSeconds(f[19 + i]);
        p.filterEnv.time[i] = envSegmentSeconds(f[30 + i]);
        p.mixEnvX.time[i] = p.mixEnvY.time[i] = envSegmentSeconds(f[42 + i]);
    }
    p.ampEnv.releaseTau = envReleaseTau(f[22]);
    p.filterEnv.releaseTau = envReleaseTau(f[33]);
    p.mixEnvX.releaseTau = p.mixEnvY.releaseTau = envReleaseTau(f[45]);
    // Rate 4A: release while the alternate-release footswitch is held
    p.ampEnv.releaseTauAlt = envReleaseTau(f[23]);
    p.filterEnv.releaseTauAlt = envReleaseTau(f[34]);
    p.mixEnvX.releaseTauAlt = p.mixEnvY.releaseTauAlt = envReleaseTau(f[46]);
    for (int i = 0; i < 4; ++i) p.ampEnv.level[i] = level99(f[24 + i]);
    p.ampEnv.loop = f[28];      p.ampEnv.repeat = f[29];
    p.filterEnv.loop = f[40];   p.filterEnv.repeat = f[41];
    p.mixEnvX.loop = p.mixEnvY.loop = f[57];
    p.mixEnvX.repeat = p.mixEnvY.repeat = f[58];
    p.ampEnv.level[4] = 0.0f;
    for (int i = 0; i < 5; ++i) {
        p.filterEnv.level[i] = level99(f[35 + i]);
        // Firmware: value << 9 on the 16-bit scale (@0x47CA)
        p.mixEnvX.level[i] = signed7(f[47 + 2 * i]) / 64.0f;
        p.mixEnvY.level[i] = signed7(f[48 + 2 * i]) / 64.0f;
    }
    p.mixAmount = 1.0f;
    p.mixX = p.mixY = 0.0f;

    // LFOs and modulation matrix
    p.lfo[0].shape = f[15];   p.lfo[1].shape = f[16];
    p.lfo[0].rate = f[17];    p.lfo[1].rate = f[18];
    p.lfo[0].amount = f[96];  p.lfo[1].amount = f[97];
    p.lfo[0].dest = uint16_t((f[102] << 8) | f[103]);
    p.lfo[1].dest = uint16_t((f[104] << 8) | f[105]);
    p.velAmount = signed8(f[99]);    p.velDest = f[109];
    p.keyAmount = signed8(f[100]);   p.keyDest = f[111];
    p.fenvAmount = signed8(f[101]);  p.fenvDest = f[113];
    p.wheelDest = f[115];
    p.pressAmount = signed8(f[98]);
    p.pressDest = uint16_t((f[106] << 8) | f[107]);
    p.unison = f[64];
    p.glide = f[65];
    p.transpose = 0;
    p.ampEnv.curve = p.filterEnv.curve = p.mixEnvX.curve = p.mixEnvY.curve = 0.0f;
    for (LfoParams& l : p.lfo) l.hz = 0.0f;
    // Arpeggiator (fields 86-95 are copied in this order to 0xCAA6 ff.):
    // rate, mode, scan, octaves, repeats, split, voicing,
    // velocity, layer, rests
    p.arpRate = f[86];
    p.arpMode = f[87] < 3 ? f[87] : 0;
    p.arpScan = f[88] < 6 ? f[88] : 0;
    p.arpOctaves = f[89];
    p.arpRepeats = f[90];
    p.arpVelocity = f[93] != 0;
    p.arpSplit = f[91];
    p.arpPoly = f[92] != 0;
    p.arpRests = f[95] != 0;
    // Split/Double: the firmware treats mode 3 as "off" (@0x4200)
    p.keyMode = f[59] <= 2 ? f[59] : 0;
    p.splitPoint = f[60];
    p.doubleDetune = f[62];
    // counts in firmware ticks (@0x159A, counter @0x8A8); owner's manual: 0-500 ms
    p.doubleDelay = float(f[63]) * kFwTickSeconds;

    // Chorus of the original: field 66 switches the left/right chorus,
    // 67 = rate (LFO table @0x6790, @0x4288), 68 = depth. It is rendered
    // with Fx::kVsChorus, the model of the original's circuit.
    p.fxType = (f[66] & 3) ? Fx::kVsChorus : Fx::kOff;
    p.chorusOn = f[66] & 3;        // bit 0 right, bit 1 left (latch U320: D8, D9)
    p.fxRateHz = float(kLfoRateStep[f[67] > 99 ? 99 : f[67]]) / (2.0f * 65536.0f * kFwTickSeconds);
    p.fxDepth = level99(f[68]);
    p.ampLevel = level99(f[69]);
    for (int i = 0; i < kMaxVoices; ++i) p.pan[i] = signed7(f[70 + i]) / 64.0f;
}

}  // namespace provs
