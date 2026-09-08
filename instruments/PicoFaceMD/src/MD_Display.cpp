// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// MD_Display.cpp -- see MD_Display.h. Only the boot splash is left here.

#include "MD_Display.h"
#include "md_logo.h"

void md_display_splash(u8g2_t* u) {
    u8g2_ClearBuffer(u);
    u8g2_SetBitmapMode(u, false);
    u8g2_DrawXBMP(u, 0, 0, md_logoWidth, md_logoHeight, md_logo);
    // Blocking send is fine here: audio engine is not running yet.
    u8g2_SendBuffer(u);
}
