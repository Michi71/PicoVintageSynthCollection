// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// What the engine reads out of flash, and nothing more.
//
// All five regions live in one blob built at configure time from a local ROM
// set (tools/rd_extract/rd_make_rom.sh) and pulled in by the generated
// rd_rom_blob.S, which names them at their offsets. No ROM data is in this
// repository; see instruments/PicoFaceRD/README.md.
//
// The decoded exponent/delta arrays, the parameter ROMs and the program ROM
// used to be declared here as well. Only the reference emulator ever wanted
// them, and the reference emulator is not part of the firmware.

#pragma once

#include <cstdint>

// The chip's two arithmetic tables. Computed rather than read: they come out of
// the sound chip's own constructor and depend on no ROM.
extern const uint32_t rd_phase_exp_table[0x10000];
extern const uint16_t rd_samples_exp_table[0x8000];

// 4-byte packed sample banks (bits[13:0]=exp, [14]=exp_sign, [23:15]=delta,
// [24]=delta_sign). Provably lossless -- exp <= 0x3FFF and delta <= 0x1FF hold
// across all three banks -- and two entries per 8-byte XIP line halve the
// dominant flash-miss stream. a and b are the two MKS-20 sample sets, m is the
// MK-80's.
extern const uint32_t rd_samples_pk4_a[0x20000];
extern const uint32_t rd_samples_pk4_b[0x20000];
extern const uint32_t rd_samples_pk4_m[0x20000];
