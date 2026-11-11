// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/pvm_map.h — Patchformat des Behringer Pro VS Mini (64 Bytes)
//
//  Quelle: Engine_ApplyPatchParameters (FUN_240132f4, Firmware v1.15) und
//  die Menuetexte der Firmware. Grenzen und Vorgaben stammen aus dem Code.
//
//  KONFIDENZ: [FW] aus dem Firmware-Code, [UI] aus den Menuetexten,
//             [ANN] Annahme der Engine
//
//  BYTE   BEDEUTUNG                                        KONFIDENZ
//   0-3   Welle A-D (< 128)                                [FW]
//   4-11  je OSZ Coarse (< 25, 12 = Kammerton), Fine (< 101) [FW]
//  12     Filter-Cutoff 0..99 (/99)                        [FW]
//  13     Resonanz 0..99                                   [FW]
//  14-17  Filter-Envelope Attack, Decay, Sustain, Release  [FW]+[UI] "ATT DEC SUS REL"
//  18-21  Amp-Envelope Attack, Decay, Sustain, Release     [FW]+[UI]
//  22     Filter-Env-Betrag 0..99 (/100)                   [FW]+[UI] "FIL ENV AMOUNT"
//  23-42  Mix-Envelope: 5 Punkte x Pegel A,B,C,D in %      [FW]+[UI] "MIX ENV POINT"
//  43-46  Mix-Envelope-Zeiten R1..R4                       [FW]+[UI] "R1 R2 R3 R4"
//  47     Bit 0-2 Mix-Env-Loop (0..6), Bit 4-7 Repeat (1..7, 8 = endlos) [FW]+[UI]
//  48     Bit 0-2 LFO-1-Form, Bit 5-7 LFO-2-Form (0..4)    [FW]
//         (Dreieck, Rechteck, Saegezahn, Rampe, S&H - Reihenfolge wie beim VS)
//  49-50  LFO 1 Rate, Betrag                               [FW]
//  51-52  LFO 2 Rate, Betrag                               [FW]
//  53-54  FX Rate, Depth                                   [FW] v2.0.3
//  55-62  Name
//
//  Was die Firmware damit rechnet:
//   * Tonhoehe = Note + Coarse - 12 + Fine/100 + LFO 2          [FW]
//   * LFO 1 wirkt auf den Cutoff: fc * (1 - (Form*0,5+0,5) * Betrag) [FW]
//   * LFO 2 wirkt auf die Tonhoehe: Form * Betrag/100 Halbtoene [FW]
//   * ADSR: Zeit aus der Phasenschritt-Tabelle im QSPI, Verlauf nach
//     Kurventabelle (gen/pvm_data.h)                            [FW]
//   * Mix-Envelope: linear, R * 40 Aufrufe je Abschnitt          [FW]
//   * LFO-Rate in Hertz: (Wert*2,01+1)/10 = 0,1 .. 20 Hz; die Firmware
//     teilt durch ihre gemessene Aufrufrate.                      [FW]
//   * Mix-Zeiten zaehlen in Aufrufen des Audio-Callbacks; angenommen
//     ist ein Aufruf je 20 Samples bei 48 kHz (2400/s).           [ANN]
//   * Die ADSR-Zeiten sind mit 48000 Schritten je Sekunde umgerechnet,
//     Begruendung in gen/pvm_data.h.                              [ANN]
//   * Loop-Arten wie beim Original (Quick Start Guide); Repeat 1..7,
//     8 = endlos (FUN_24012ca4)                                   [FW]
// ============================================================================
#pragma once
#include <cmath>

#include "provs/engine.h"
#include "provs/preset_map.h"
#include "provs/gen/pvm_data.h"

namespace provs {

inline float pvmTime(int v) { return kPvmEnvSeconds[v < 100 ? v : 99]; }

// ADSR der Firmware (FUN_240130d4): jeder Abschnitt laeuft in seiner Zeit
// entlang der Kurventabelle vom Startwert zum Ziel.
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

// r: die 64 Bytes eines Patches
inline void mapPvmPatch(const uint8_t* r, Params& p) {
    for (int i = 0; i < 4; ++i) {
        p.osc[i].wave = r[i] < 128 ? r[i] : 0;
        // Firmware: Tonhoehe = Note + Coarse - 12 (Engine_ProcessEnvLfo);
        // die -12 traegt p.transpose
        p.osc[i].coarse = r[4 + 2 * i] < 25 ? r[4 + 2 * i] : 12;
        p.osc[i].fine = r[5 + 2 * i] < 100 ? r[5 + 2 * i] : 0;
        p.osc[i].level = 1.0f;
    }
    float cutoff = level99(r[12]);
    p.resonance = level99(r[13]);
    p.filterEnv = pvmAdsr(r + 14);
    p.ampEnv = pvmAdsr(r + 18);
    p.envAmount = r[22] < 100 ? float(r[22]) / 100.0f : 0.0f;

    // Mix-Envelope: Pegel A,B,C,D in Prozent -> Joystick-Koordinaten.
    // Mit u = C+D und w = A+D gilt x = C - A, y = D - B.
    for (int k = 0; k < 5; ++k) {
        const uint8_t* q = r + 23 + 4 * k;
        float a = q[0], b = q[1], c = q[2], d = q[3];
        const float sum = a + b + c + d;
        if (sum <= 0.0f) { a = b = c = d = 25.0f; }
        const float n = sum > 0.0f ? sum : 100.0f;
        p.mixEnvX.level[k] = (c - a) / n;
        p.mixEnvY.level[k] = (d - b) / n;
    }
    // Zeiten (FUN_24012294): R * 4000/100 Aufrufe, linear von Punkt zu Punkt
    auto mixTime = [](int v) {
        const float t = float(v < 100 ? v : 25) * 40.0f / kPvmCallsPerSecond;
        return t > 0.0f ? t : 1.0f / kPvmCallsPerSecond;
    };
    for (int k = 0; k < 3; ++k)
        p.mixEnvX.time[k] = p.mixEnvY.time[k] = mixTime(r[43 + k]);
    // das Release der Engine ist exponentiell: Zeitkonstante = Dauer / 3
    p.mixEnvX.releaseTau = p.mixEnvY.releaseTau = mixTime(r[46]) / 3.0f;
    p.mixEnvX.curve = p.mixEnvY.curve = 0.0f;
    // Loop wie beim Original (Quick Start Guide: off, 0->3, 1->3, 2->3,
    // 0<->3, 1<->3, 2<->3). Repeat (FUN_24012ca4): 1..7 Wiederholungen,
    // 8 = endlos, 0 = keine. Die Engine zaehlt wie das Original (0 = endlos).
    int loop = r[47] & 7, repeat = r[47] >> 4;
    if (loop >= 7) loop = 0;
    if (repeat > 8) repeat = 1;
    if (repeat == 0) loop = 0;
    p.mixEnvX.loop = p.mixEnvY.loop = loop;
    p.mixEnvX.repeat = p.mixEnvY.repeat = repeat == 8 ? 0 : repeat;
    p.mixAmount = 1.0f;
    p.mixX = p.mixY = 0.0f;

    // LFOs. LFO 1 zieht den Cutoff nach unten:
    // fc * (1 - (Form*0,5+0,5) * Betrag); hier als Schwingung um die Mitte
    // dieses Bereichs (LFO-Ziel Filter = +-1/4 der Skala bei Betrag 99).
    // LFO 2 liegt in Halbtoenen auf der Tonhoehe aller Oszillatoren
    // (Form * Betrag/100), bei der Engine entspricht Betrag 99 +-256 Cent.
    p.lfo[0] = p.lfo[1] = LfoParams{};
    const int s1 = r[48] & 7, s2 = r[48] >> 5;
    p.lfo[0].shape = s1 < 5 ? s1 : 0;
    p.lfo[1].shape = s2 < 5 ? s2 : 0;
    // Rate in Hertz: (Wert * 2,01 + 1) / 10, also 0,1 .. 20 Hz. Die Firmware
    // rechnet daraus den Phasenschritt Hz * 1024 / Aufrufrate.
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

    // Nicht im Patch: Velocity und Key-Tracking (beim Mini global). [ANN]
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
