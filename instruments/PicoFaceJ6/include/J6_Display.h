// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// J6_Display.h -- what is left of the instrument's own display code.
//
// The page and list renderers that used to live here (J6UiModel, J6ListModel
// and about a hundred lines of u8g2 calls) are gone: the shared kit in
// core/include/picoface/ui_kit.h draws those now, and J6_Instrument calls it
// directly. What cannot be shared stays -- the splash screen, because the logo
// is this instrument's own.

#pragma once
#include "u8g2.h"

// Boot only: draws the logo and BLOCKS on SendBuffer. Safe there and only
// there, because the audio engine has not started yet.
void j6_display_splash(u8g2_t* u);
