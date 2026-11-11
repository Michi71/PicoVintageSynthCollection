// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/pvm_map.h — patch format of the Behringer Pro VS Mini (64 bytes)
//
//  Source: Engine_ApplyPatchParameters (FUN_240132f4, firmware v1.15) and
//  the firmware's menu texts. Limits and defaults are taken from the code.
//
//  CONFIDENCE: [FW] from the firmware code, [UI] from the menu texts,
//              [ANN] assumption of the engine
//
//  BYTE   MEANING                                          CONFIDENCE
//   0-3   Wave A-D (< 128)                                 [FW]
//   4-11  per OSC Coarse (< 25, 12 = concert pitch), Fine (< 101) [FW]
//  12     Filter cutoff 0..99 (/99)                        [FW]
//  13     Resonance 0..99                                  [FW]
//  14-17  Filter envelope attack, decay, sustain, release  [FW]+[UI] "ATT DEC SUS REL"
//  18-21  Amp envelope attack, decay, sustain, release     [FW]+[UI]
//  22     Filter env amount 0..99 (/100)                   [FW]+[UI] "FIL ENV AMOUNT"
//  23-42  Mix envelope: 5 points x levels A,B,C,D in %     [FW]+[UI] "MIX ENV POINT"
//  43-46  Mix envelope times R1..R4                        [FW]+[UI] "R1 R2 R3 R4"
//  47     bits 0-2 mix env loop (0..6), bits 4-7 repeat (1..7, 8 = endless) [FW]+[UI]
//  48     bits 0-2 LFO 1 shape, 5-7 LFO 2 shape (0..4)     [FW]
//         (triangle, square, sawtooth, ramp, S&H - same order as on the VS)
//  49-50  LFO 1 rate, amount                               [FW]
//  51-52  LFO 2 rate, amount                               [FW]
//  53-54  FX rate, depth                                   [FW] v2.0.3
//  55-62  Name
//
//  What the firmware computes from it:
//   * Pitch = note + Coarse - 12 + Fine/100 + LFO 2             [FW]
//   * LFO 1 acts on the cutoff: fc * (1 - (shape*0.5+0.5) * amount) [FW]
//   * LFO 2 acts on the pitch: shape * amount/100 semitones     [FW]
//   * ADSR: time from the phase-step table in the QSPI, contour following
//     the curve table (gen/pvm_data.h)                          [FW]
//   * Mix envelope: linear, R * 40 calls per segment             [FW]
//   * LFO rate in hertz: (value*2.01+1)/10 = 0.1 .. 20 Hz; the firmware
//     divides by its measured call rate.                          [FW]
//   * Mix times count in calls of the audio callback; assumed is one
//     call per 20 samples at 48 kHz (2400/s).                     [ANN]
//   * The ADSR times are converted with 48000 steps per second,
//     rationale in gen/pvm_data.h.                                [ANN]
//   * Loop types as on the original (Quick Start Guide); Repeat 1..7,
//     8 = endless (FUN_24012ca4)                                  [FW]
// ============================================================================
#pragma once
#include <cmath>

#include "provs/engine.h"
#include "provs/preset_map.h"
#include "provs/gen/pvm_data.h"

namespace provs {

inline float pvmTime(int v) { return kPvmEnvSeconds[v < 100 ? v : 99]; }

// ADSR of the firmware (FUN_240130d4): each segment runs, within its time,
// along the curve table from the start value to the target.
inline Env5 pvmAdsr(const uint8_t* b) {
    Env5 e;
    const float sustain = level99(b[2]);
    e.level[0] = 0.0f;
    e.level[1] = 1.0f;
    e.level[2] = sustain;
    e.level[3] = sustain;
    e.level[4] = 0.0f;
    e.time[0] = pvmTime(b[0]);
    e.time[1] = pvmTime(b[1]);
    e.time[2] = 0.001f;
    e.releaseTau = pvmTime(b[3]) / kPvmEnvCurveK;
    e.curve = kPvmEnvCurveK;
    return e;
}

// r: the 64 bytes of a patch
inline void mapPvmPatch(const uint8_t* r, Params& p) {
    for (int i = 0; i < 4; ++i) {
        p.osc[i].wave = r[i] < 128 ? r[i] : 0;
        // Firmware: pitch = note + Coarse - 12 (Engine_ProcessEnvLfo);
        // the -12 is carried by p.transpose
        p.osc[i].coarse = r[4 + 2 * i] < 25 ? r[4 + 2 * i] : 12;
        p.osc[i].fine = r[5 + 2 * i] < 100 ? r[5 + 2 * i] : 0;
        p.osc[i].level = 1.0f;
    }
    float cutoff = level99(r[12]);
    p.resonance = level99(r[13]);
    p.filterEnv = pvmAdsr(r + 14);
    p.ampEnv = pvmAdsr(r + 18);
    p.envAmount = r[22] < 100 ? float(r[22]) / 100.0f : 0.0f;

    // Mix envelope: levels A,B,C,D in percent -> joystick coordinates.
    // With u = C+D and w = A+D, x = C - A and y = D - B hold.
    for (int k = 0; k < 5; ++k) {
        const uint8_t* q = r + 23 + 4 * k;
        float a = q[0], b = q[1], c = q[2], d = q[3];
        const float sum = a + b + c + d;
        if (sum <= 0.0f) { a = b = c = d = 25.0f; }
        const float n = sum > 0.0f ? sum : 100.0f;
        p.mixEnvX.level[k] = (c - a) / n;
        p.mixEnvY.level[k] = (d - b) / n;
    }
    // Times (FUN_24012294): R * 4000/100 calls, linear from point to point
    auto mixTime = [](int v) {
        const float t = float(v < 100 ? v : 25) * 40.0f / kPvmCallsPerSecond;
        return t > 0.0f ? t : 1.0f / kPvmCallsPerSecond;
    };
    for (int k = 0; k < 3; ++k)
        p.mixEnvX.time[k] = p.mixEnvY.time[k] = mixTime(r[43 + k]);
    // the engine's release is exponential: time constant = duration / 3
    p.mixEnvX.releaseTau = p.mixEnvY.releaseTau = mixTime(r[46]) / 3.0f;
    p.mixEnvX.curve = p.mixEnvY.curve = 0.0f;
    // Loop as on the original (Quick Start Guide: off, 0->3, 1->3, 2->3,
    // 0<->3, 1<->3, 2<->3). Repeat (FUN_24012ca4): 1..7 repetitions,
    // 8 = endless, 0 = none. The engine counts like the original (0 = endless).
    int loop = r[47] & 7, repeat = r[47] >> 4;
    if (loop >= 7) loop = 0;
    if (repeat > 8) repeat = 1;
    if (repeat == 0) loop = 0;
    p.mixEnvX.loop = p.mixEnvY.loop = loop;
    p.mixEnvX.repeat = p.mixEnvY.repeat = repeat == 8 ? 0 : repeat;
    p.mixAmount = 1.0f;
    p.mixX = p.mixY = 0.0f;

    // LFOs. LFO 1 pulls the cutoff downward:
    // fc * (1 - (shape*0.5+0.5) * amount); here as an oscillation around the
    // centre of that range (LFO destination filter = +-1/4 of the scale at
    // amount 99). LFO 2 is applied in semitones to the pitch of all oscillators
    // (shape * amount/100); in the engine amount 99 corresponds to +-256 cents.
    p.lfo[0] = p.lfo[1] = LfoParams{};
    const int s1 = r[48] & 7, s2 = r[48] >> 5;
    p.lfo[0].shape = s1 < 5 ? s1 : 0;
    p.lfo[1].shape = s2 < 5 ? s2 : 0;
    // Rate in hertz: (value * 2.01 + 1) / 10, i.e. 0.1 .. 20 Hz. From this
    // the firmware computes the phase step Hz * 1024 / call rate.
    auto lfoHz = [](int v) { return (float(v < 100 ? v : 0) * 2.01f + 1.0f) / 10.0f; };
    p.lfo[0].hz = lfoHz(r[49]);
    p.lfo[1].hz = lfoHz(r[51]);
    const float depth = (r[50] < 100 ? float(r[50]) / 100.0f : 0.0f) * cutoff;
    cutoff -= 0.5f * depth;
    int amount = int(std::lround(2.0f * depth * 99.0f));
    p.lfo[0].amount = amount > 99 ? 99 : amount;
    p.lfo[0].dest = amount > 0 ? kLfoFilter : 0;
    p.cutoff = cutoff;
    const int vib = int(std::lround(float(r[52] < 100 ? r[52] : 0) * 99.0f / 256.0f));
    p.lfo[1].amount = vib;
    p.lfo[1].dest = vib > 0 ? (kLfoFreqA | kLfoFreqB | kLfoFreqC | kLfoFreqD) : 0;

    p.fxRateHz = 0.0f;
    p.fxRate = r[53] < 100 ? float(r[53]) / 99.0f : 0.0f;
    p.fxDepth = r[54] < 100 ? float(r[54]) / 99.0f : 0.0f;

    // Not in the patch: velocity and key tracking (global on the Mini). [ANN]
    p.ampLevel = 0.5f;
    p.velAmount = 50;  p.velDest = kVelAmp;
    p.keyAmount = 25;  p.keyDest = kKeyFilter;
    p.fenvAmount = 0;  p.fenvDest = 0;
    p.pressAmount = 0; p.pressDest = 0;
    p.wheelDest = 0;
    p.glide = 0;       p.unison = 0;
    p.transpose = -12;
    p.arpRate = 70; p.arpMode = kArpNormal; p.arpScan = kArpUp;
    p.arpOctaves = 0; p.arpRepeats = 0; p.arpVelocity = true;
    p.arpSplit = 0; p.arpPoly = true; p.arpRests = false;
    p.keyMode = kKeyNormal;
    for (float& x : p.pan) x = 0.0f;
}

}  // namespace provs
