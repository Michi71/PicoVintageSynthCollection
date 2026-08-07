// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Program change is not what this test covers; the two entry points exist only
// so midi_reface.cpp links.
#pragma once
#include <stdint.h>
#define DX_NPRESETS 32
void preset_set_current(uint8_t);
void preset_stage(uint8_t);
