// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// SM_Display.cpp -- see SM_Display.h. Only the boot splash is left here.
#include "SM_Display.h"
#include "sm_logo.h"

void sm_display_splash(u8g2_t* u) {
    u8g2_ClearBuffer(u);
    u8g2_SetBitmapMode(u, false);
    u8g2_DrawXBMP(u, 0, 0, sm_logoWidth, sm_logoHeight, sm_logo);
    // Blocking send is fine here: audio engine is not running yet.
    u8g2_SendBuffer(u);
}
