// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/preset_map.h — Feldmapping der Original-Prophet-VS-Programme
//
//  Ein Programm sind 82 Bytes, bitgepackt (MSB-first) in 116 Felder nach der
//  Breitentabelle der Firmware @0x48F6. Direkt hinter der Tabelle (@0x496C)
//  liegt der Init-Patch der Firmware; er ist der Schluessel fuer Nullpunkte.
//
//  KONFIDENZ:
//   [FW]   aus Firmware-Code/-Daten gelesen
//   [MAN]  Handbuch (Kap. 6: Coarse = Halbtoene ueber Kammerton, Fine = Cent;
//          Kap. 8: Envelope startet auf Pegel 0, Raten 1-4 und 4A, haelt auf
//          Punkt 3, Release exponentiell, Amp-Pegel 4 ist immer 0)
//   [STAT] aus Statistik ueber die 100 Factory-Programme + Init-Patch
//   [ANN]  Annahme der Engine (nicht aus der Firmware belegt)
//
//  FELD    BITS  BEDEUTUNG                                   KONFIDENZ
//   0-3     7    Welle A-D (Init 32,33,34,35)                [FW] Init-Patch, Upload @0x43A6
//   4-7     5    Coarse A-D, 0..24 Halbtoene aufwaerts       [FW] @0x5D2E: *100, Init 0
//   8-11    7    Fine A-D, 0..99 Cent aufwaerts              [FW] @0x5D3A: addiert, Init 0
//  12       7    Filter-Cutoff 0..99 (lineare CV, Tab @0x6590) [FW] @0x5DFA
//  13       7    Resonanz                                    [STAT]
//  14       7    Filter-Envelope-Betrag                      [STAT]
//  15-16    3    LFO-1/2-Form: 0 Dreieck, 1 Rechteck, 2 Saege, [FW] @0x42D8, ISR @0x936
//                3 Rampe, 4 Random
//  17-18    7    LFO-1/2-Rate, Tabelle @0x6790               [FW] @0x42B0
//  19-23    7    Amp-Env: Raten 1,2,3,4,4A                   [FW] @0x465C
//  24-27    7    Amp-Env: Pegel 0..3 (Pegel 4 = 0)           [FW] @0x4652, clr.l @0x465A
//  28-29    3    Amp-Env Loop/Repeat                         [FW] @0x463A
//  30-34    7    Filter-Env: Raten 1,2,3,4,4A                [FW] @0x46B4
//  35-39    7    Filter-Env: Pegel 0..4                      [FW] @0x46A8
//  40-41    3    Filter-Env Loop/Repeat                      [FW] @0x4690
//  42-46    7    Mix-Env: Raten 1,2,3,4,4A                   [FW] @0x4710
//  47-56    7    Mix-Env: 5 Punkte (X,Y), 7 Bit signed       [FW] Sign-Extension im Lader
//  57-58    3    Mix-Env Loop/Repeat                         [STAT]
//  59       2    Tastaturmodus: 0 normal, 1 Split, 2 Double   [FW] @0x121E
//  60       7    Splitpunkt (Note)                            [FW] @0x126E
//  61       7    verknuepftes Programm fuer Split/Double      [FW] @0x4212
//  62       5    Detune des zweiten Programms im Double-Modus [FW] @0x4064
//  63       7    Delay des zweiten Programms im Double-Modus  [FW] @0x1252, @0x54A4
//                (Handbuch: 0-500 ms)
//  64       3    Unisono: 0 aus, n = Versatz n Cent je Stimme [FW] @0x1284, @0x4068
//  65       7    Glide, Tabelle @0x6858                       [FW] @0x41AE
//  66       2    Chorus an: Bit 0 rechts, Bit 1 links         [FW] @0x3D88, Schaltplan U320
//  67-68    7    Chorus Rate / Depth (Original-Chorus)       [FW] @0x4288
//  69       7    VCA-Grundpegel                              [FW] @0x4144, ISR @0xCD2
//  70-77    7    Pan je Stimme 1-8, signed                   [FW] @0x4278, ISR @0xD9C
//  78-85    5    Programmname, 8 Zeichen (A-Z, 1-5, Leerzeichen;  [FW] Zeichensatz @0x1A2F,
//                Init-Patch "BASIC")                            Init-Patch
//  86       7    Arpeggiator-Rate 1..99                       [FW] @0x5F26
//  87       2    Arp-Modus: 0 normal, 1 Auto-Latch, 2 Extend  [FW] @0x5F70
//  88       3    Arp-Scan: Up, Down, Up/Down, Assign,         [FW] @0x5F7E, @0x60A8 ff.
//                Reverse, Random
//  89       2    Arp-Oktaven (0..3 zusaetzlich)               [FW] @0x6134
//  90       2    Arp-Repeats (0..3 zusaetzlich)               [FW] @0x62AC
//  91       2    Arp-Split: Off, Right, Left, Dual            [MAN] (Dual wie Off)
//  92       1    Arp-Voicing: 0 Mono, 1 Poly                  [FW] @0x5FC6
//  93       1    Arp-Velocity an/aus                          [MAN]
//  94       1    Arp-Layer                                    [MAN] - nicht umgesetzt
//  95       1    Arp-Rests                                    [MAN]
//  96-97    7    LFO-1/2-Betrag 0..99                        [FW] @0x44BC
//  98       8    Pressure-Betrag, signed                     [FW] @0x44E4
//  99       8    Velocity-Betrag, signed                     [FW] @0x44F6
//  100      8    Keyboard-Betrag, signed (Bezug Note 66)     [FW] @0x4508
//  101      8    Filter-Env-Modulationsbetrag, signed        [FW] @0x451A
//  102-103 2+8   LFO-1-Ziele (10 Bit)                        [FW] @0x456A, Tab @0x5F6
//  104-105 2+8   LFO-2-Ziele (10 Bit)                        [FW] @0x458E, Tab @0x61E
//                  Bit 0-3 Freq A-D, 4 Filter, 5 Mix A-C, 6 Mix B-D,
//                  7 Rate des anderen LFO, 8 Betrag des anderen LFO, 9 Pan
//  106-107 7+8   Pressure-Ziele (15 Bit)                     [FW] @0x45B2, Tab @0x6E6
//                  Bit 0-3 Freq A-D, 4 Filter, 5 Mix A-C, 6 Mix B-D,
//                  7 LFO-1-Rate, 8 LFO-1-Betrag, 9 LFO-2-Rate, 10 LFO-2-Betrag,
//                  11 Volume, 12 Pan, 13 Chorus-Rate, 14 Chorus-Depth
//  109      5    Velocity-Ziele: Bit 0 Filter-Env-Tiefe,     [FW] @0x5B5A ff.
//                  1 Mix A-C, 2 Mix B-D, 3 VCA-Pegel, 4 Pan
//  111      4    Keyboard-Ziele: Bit 0 Filter, 1 Mix A-C,    [FW] @0x5B26 ff.
//                  2 Mix B-D, 3 Pan
//  113      2    Filter-Env-Ziele: Bit 0 Freq (alle 4), 1 Pan [FW] @0x45CA
//  115      3    Mod-Wheel-Ziele: Bit 0 LFO-1-Betrag,        [FW] @0x45F8, Tab @0x75E
//                  1 LFO-2-Betrag, 2 Chorus-Depth
//
//  Skalierung der Modulation (ISR @0x974 ff.): LFO-Ausgang = Form * Betrag;
//  auf Mix und Pan in voller Skala, auf Filter und LFO-Betrag /4, auf Tonhoehe
//  und LFO-Rate /128 (Betrag 99 = +-256 Cent). Filter-Env auf Tonhoehe /32
//  (+-1024 Cent). Velocity laeuft ueber die Kurve @0x6690.
//  Pressure (@0x3B58): volle Skala auf Volume, Pan und LFO-Betrag, /4 auf
//  Filter, Mix und LFO-Rate, /64 auf die Tonhoehe (+-512 Cent).
//  Mod-Wheel (@0x51A4): MIDI-Wert 127 = voller LFO-Betrag.
//
//  Loop/Repeat (Felder 28/29, 40/41, 57/58): Loop 0 aus, 1-3 Sprung von
//  Punkt 3 zurueck auf Punkt 0/1/2, 4-6 Pendeln zwischen Punkt 0/1/2 und 3;
//  Repeat 0 = endlos, sonst Anzahl Wiederholungen.   [FW] @0x4756, @0x0A0C
//
//  Nicht umgesetzt: beim Arpeggiator Layer (Overdub) und Split "Dual";
//  getrennte Chorus-Werte je Schicht bei
//  Split/Double; die analoge Chorus-Schaltung selbst (ersetzt durch den
//  Behringer-Chorus).
//  Pegel 0..99 laufen ueber die lineare Tabelle @0x6590 (331 je Schritt).
// ============================================================================
#pragma once
#include <cmath>
#include <cstdint>

#include "provs/engine.h"
#include "provs/fw_tables.h"

namespace provs {

// 7-Bit-Zweierkomplement (Lader der Firmware: Sign-Extension)
inline float signed7(int v) { return float(v > 63 ? v - 128 : v); }
inline int signed8(int v) { return v > 127 ? v - 256 : v; }

// --- Envelope-Zeiten aus der Firmware ----------------------------------------
// Die Envelopes laufen in der Timer-ISR (@0x7B6), bei jedem 4. Interrupt.
// Tabellen kEnvSegmentTicks und kEnvReleaseCoef: siehe fw_tables.h.
// Tick-Dauer: kFwTickSeconds (fw_tables.h), 4 ms.
constexpr float kEnvTickSeconds = kFwTickSeconds;

inline float envSegmentSeconds(int rate) {
    return float(kEnvSegmentTicks[rate > 99 ? 99 : rate]) * kEnvTickSeconds;
}
// Zeitkonstante des exponentiellen Release in Sekunden (0 = sofort)
inline float envReleaseTau(int rate) {
    const int k = kEnvReleaseCoef[rate > 99 ? 99 : rate];
    if (k <= 0) return 0.0f;
    return -kEnvTickSeconds / std::log(float(k) / 32768.0f);
}

inline float level99(int v) { return v >= 99 ? 1.0f : float(v) / 99.0f; }

// f: die 116 Felder eines Programms, ein Byte je Feld
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
    // Rate 4A: Release bei gehaltenem Alternate-Release-Fussschalter
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
        // Firmware: Wert << 9 auf der 16-Bit-Skala (@0x47CA)
        p.mixEnvX.level[i] = signed7(f[47 + 2 * i]) / 64.0f;
        p.mixEnvY.level[i] = signed7(f[48 + 2 * i]) / 64.0f;
    }
    p.mixAmount = 1.0f;
    p.mixX = p.mixY = 0.0f;

    // LFOs und Modulationsmatrix
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
    // Arpeggiator (Felder 86-95 werden in dieser Reihenfolge nach 0xCAA6 ff.
    // kopiert): Rate, Modus, Scan, Oktaven, Repeats, Split, Voicing,
    // Velocity, Layer, Rests
    p.arpRate = f[86];
    p.arpMode = f[87] < 3 ? f[87] : 0;
    p.arpScan = f[88] < 6 ? f[88] : 0;
    p.arpOctaves = f[89];
    p.arpRepeats = f[90];
    p.arpVelocity = f[93] != 0;
    p.arpSplit = f[91];
    p.arpPoly = f[92] != 0;
    p.arpRests = f[95] != 0;
    // Split/Double: Modus 3 behandelt die Firmware als "aus" (@0x4200)
    p.keyMode = f[59] <= 2 ? f[59] : 0;
    p.splitPoint = f[60];
    p.doubleDetune = f[62];
    // zaehlt in Firmware-Ticks (@0x159A, Zaehler @0x8A8); Handbuch: 0-500 ms
    p.doubleDelay = float(f[63]) * kFwTickSeconds;

    // Chorus des Originals: Feld 66 schaltet den linken/rechten Chorus,
    // 67 = Rate (LFO-Tabelle @0x6790, @0x4288), 68 = Depth. Abgespielt wird
    // er mit Fx::kVsChorus, dem Modell der Schaltung des Originals.
    p.fxType = (f[66] & 3) ? Fx::kVsChorus : Fx::kOff;
    p.chorusOn = f[66] & 3;        // Bit 0 rechts, Bit 1 links (Latch U320: D8, D9)
    p.fxRateHz = float(kLfoRateStep[f[67] > 99 ? 99 : f[67]]) / (2.0f * 65536.0f * kFwTickSeconds);
    p.fxDepth = level99(f[68]);
    p.ampLevel = level99(f[69]);
    for (int i = 0; i < kMaxVoices; ++i) p.pan[i] = signed7(f[70 + i]) / 64.0f;
}

}  // namespace provs
