// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// SM_Display.h -- what is left of the instrument's own display code.
//
// The page renderer that used to live here (SmUiModel plus its u8g2 calls) is
// gone: the shared kit in core/include/picoface/ui_kit.h draws that now, and
// SM_Instrument calls it directly. The splash stays, because the logo is this
// instrument's own.

#pragma once
#include "u8g2.h"

// Boot only: draws the logo and BLOCKS on SendBuffer. Safe there and only
// there, because the audio engine has not started yet.
void sm_display_splash(u8g2_t* u);
