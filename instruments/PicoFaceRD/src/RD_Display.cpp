// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// RD_Display.cpp -- see RD_Display.h. Only the boot splash is left here.
#include "RD_Display.h"
#include "rd_logo.h"

void rd_display_splash(u8g2_t* u) {
    u8g2_ClearBuffer(u);
    u8g2_SetBitmapMode(u, false);
    u8g2_DrawXBMP(u, 0, 0, rd_logoWidth, rd_logoHeight, rd_logo);
    // Blocking send is fine here: audio engine is not running yet.
    u8g2_SendBuffer(u);
}
