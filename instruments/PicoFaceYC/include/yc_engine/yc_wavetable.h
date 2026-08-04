// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71



// include/yc_engine/yc_wavetable.h
#pragma once
#include <cstring>
#include "yc_core.h"
#include "yc_lut_data.h"

inline float yc_wavetable_ram[YC_WAVETABLE_SIZE];

static inline void yc_wavetable_select(uint8_t wave) {
    if (wave >= YC_NUM_WAVE_TYPES) wave = 0;
    std::memcpy(yc_wavetable_ram, yc_wavetables[wave], sizeof(float) * YC_WAVETABLE_SIZE);
}

