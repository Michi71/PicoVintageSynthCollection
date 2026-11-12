// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/fw_tables.h - time base of the firmware and the tables from the ROM
//
//  The tables themselves are not in the repository: tools/gen_data.py reads
//  them from the ROM of the original Prophet VS and writes
//  provs/gen/rom_tables.h.
// ============================================================================
#pragma once
#include <cstdint>

#include "provs/gen/rom_tables.h"

namespace provs {

// Timer interrupt (schematic SD2400-3): 16 MHz crystal, divider HC4020, last
// stage (/16384) = 976.5625 Hz -> HC74 -> IPL1. Period 1.024 ms.
constexpr float kFwInterruptSeconds = 16384.0f / 16.0e6f;
// Tick of the modulation ISR (@0x7B6): every 4th interrupt = 4.096 ms. This
// agrees with the double delay (7 bits of ticks, owner's manual "0-500 ms")
// and the arpeggiator (owner's manual 0.4-70 steps/s).
constexpr float kFwTickSeconds = 4.0f * kFwInterruptSeconds;

// From rom_tables.h:
//   kEnvSegmentTicks  envelope rates 1-3: segment duration in ticks (@0x6920,
//                     routine @0x47EE; step = level difference / ticks, @0x482C)
//   kEnvReleaseCoef   rates 4/4A: per tick v = L4 + (v - L4) * k/32768 (@0x6A20,
//                     routine @0x4810, applied @0x0A6A)
//   kLfoRateStep      LFO rate -> phase step per tick (@0x6790, @0x42B0). The
//                     phase is 16 bits; triangle/square/random reverse at the
//                     edge (period 2*65536/step ticks), sawtooth/ramp wrap
//                     around (65536/step)
//   kVelocityCurve    velocity 0..127 -> modulation value (@0x6690, @0x5AE4)
//   kGlideRate        glide 1..99 -> value/64 cents per tick (@0x6858, @0x41AE)
//   kNameChars        character set of the program names (@0x1A2F)

}  // namespace provs
