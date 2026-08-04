// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from giulioz/rdpiano and MAME; copyright is shared with their authors.
// See instruments/PicoFaceRD/README.md.

#pragma once

#include <cstdint>

extern const uint8_t rd_program_rom[0x2000];
extern const uint32_t rd_phase_exp_table[0x10000];
extern const uint16_t rd_samples_exp_table[0x8000];

extern const uint8_t mks20a_params_rom[0x20000];

extern const uint16_t mks20a_samples_exp[0x20000];
extern const uint8_t mks20a_samples_exp_sign[0x20000];
extern const uint16_t mks20a_samples_delta[0x20000];
extern const uint8_t mks20a_samples_delta_sign[0x20000];

extern const uint16_t mks20b_samples_exp[0x20000];
extern const uint8_t mks20b_samples_exp_sign[0x20000];
extern const uint16_t mks20b_samples_delta[0x20000];
extern const uint8_t mks20b_samples_delta_sign[0x20000];

extern const uint8_t mk80_params_rom[0x20000];

extern const uint16_t mk80_samples_exp[0x20000];
extern const uint8_t mk80_samples_exp_sign[0x20000];
extern const uint16_t mk80_samples_delta[0x20000];
extern const uint8_t mk80_samples_delta_sign[0x20000];

struct RdSampleEntry {
    uint16_t exp;        // exponent value
    uint16_t delta;      // delta value
    uint8_t  exp_sign;   // exponent sign (0/1)
    uint8_t  delta_sign; // delta sign (0/1)
    uint16_t _pad;       // padding for 8-byte alignment
}; // 8 bytes - one XIP burst per part instead of 4 scattered reads
static_assert(sizeof(RdSampleEntry) == 8, "RdSampleEntry must be exactly 8 bytes for XIP burst access");

extern const RdSampleEntry rd_samples_ilv_a[0x20000];
extern const RdSampleEntry rd_samples_ilv_b[0x20000];
extern const RdSampleEntry rd_samples_ilv_m[0x20000];

// 4-byte packed sample banks (bits[13:0]=exp, [14]=exp_sign, [23:15]=delta, [24]=delta_sign).
// Provably lossless (exp <= 0x3FFF, delta <= 0x1FF verified across all banks); two
// entries per 8-byte XIP line halve the dominant flash-miss stream.
extern const uint32_t rd_samples_pk4_a[0x20000];
extern const uint32_t rd_samples_pk4_b[0x20000];
extern const uint32_t rd_samples_pk4_m[0x20000];
