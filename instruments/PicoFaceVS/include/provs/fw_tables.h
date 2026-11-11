// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/fw_tables.h - Zeitbasis der Firmware und die Tabellen aus dem ROM
//
//  Die Tabellen selbst stehen nicht im Repository: tools/gen_data.py liest
//  sie aus dem ROM des Original-Prophet-VS und schreibt provs/gen/rom_tables.h.
// ============================================================================
#pragma once
#include <cstdint>

#include "provs/gen/rom_tables.h"

namespace provs {

// Timer-Interrupt (Schaltplan SD2400-3): 16-MHz-Quarz, Teiler HC4020, letzte
// Stufe (/16384) = 976,5625 Hz -> HC74 -> IPL1. Periode 1,024 ms.
constexpr float kFwInterruptSeconds = 16384.0f / 16.0e6f;
// Tick der Modulations-ISR (@0x7B6): jeder 4. Interrupt = 4,096 ms. Dazu
// passen das Double-Delay (7 Bit Ticks, Handbuch "0-500 ms") und der
// Arpeggiator (Handbuch 0,4-70 Schritte/s).
constexpr float kFwTickSeconds = 4.0f * kFwInterruptSeconds;

// Aus rom_tables.h:
//   kEnvSegmentTicks  Envelope-Raten 1-3: Segmentdauer in Ticks (@0x6920,
//                     Routine @0x47EE; Schritt = Pegeldifferenz / Ticks, @0x482C)
//   kEnvReleaseCoef   Raten 4/4A: je Tick v = L4 + (v - L4) * k/32768 (@0x6A20,
//                     Routine @0x4810, Anwendung @0x0A6A)
//   kLfoRateStep      LFO-Rate -> Phasenschritt je Tick (@0x6790, @0x42B0). Die
//                     Phase ist 16 Bit; Dreieck/Rechteck/Random kehren am Rand
//                     um (Periode 2*65536/Schritt Ticks), Saegezahn/Rampe laufen
//                     ueber (65536/Schritt)
//   kVelocityCurve    Velocity 0..127 -> Modulationswert (@0x6690, @0x5AE4)
//   kGlideRate        Glide 1..99 -> Wert/64 Cent je Tick (@0x6858, @0x41AE)
//   kNameChars        Zeichensatz der Programmnamen (@0x1A2F)

}  // namespace provs
