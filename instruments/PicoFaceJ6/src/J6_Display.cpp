// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// J6_Display.cpp -- see J6_Display.h. Only the boot splash is left here.

#include "J6_Display.h"
#include "j6_logo.h"

void j6_display_splash(u8g2_t* u) {
    u8g2_ClearBuffer(u);
    u8g2_SetBitmapMode(u, false);
    u8g2_DrawXBMP(u, 0, 0, j6_logoWidth, j6_logoHeight, j6_logo);
    // Blocking send is fine here: audio engine is not running yet.
    u8g2_SendBuffer(u);
}
