// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The engine used to reach into the reference emulator's mame_utils.h for two
// things: short integer names and the attribute that keeps hot code out of
// flash. Only the second was ever ours, and the emulator is no longer part of
// this instrument, so both live here now.

#pragma once

#include <cstdint>

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;

// Hot engine code must execute from RAM on RP2350; XIP flash stalls dominate
// otherwise.
#if defined(TARGET_RP2350) || defined(PICO_BUILD)
#include "pico.h"
#define RD_HOT_FUNC(x) __not_in_flash_func(x)
#else
#define RD_HOT_FUNC(x) x
#endif
