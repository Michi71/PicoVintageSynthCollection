// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// Paints into the u8g2 back buffer only. The main loop streams the buffer out
// in staged half-tile rows while audio renders, so this must never send it.

#include "JV_Display.h"

#include "u8g2.h"

void jv_display_page(picoface::ui::Display& d, const JvUiModel& m) {
    u8g2_t* u = d.raw();
    u8g2_ClearBuffer(u);

    u8g2_SetFontPosBaseline(u);
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    const int asc = u8g2_GetAscent(u);
    const int desc = u8g2_GetDescent(u);

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawBox(u, 0, 0, 128, asc - desc);
    u8g2_SetDrawColor(u, 0);
    if (m.title[0]) u8g2_DrawStr(u, 2, asc, m.title);
    if (m.page[0]) {
        const int pw = u8g2_GetStrWidth(u, m.page);
        u8g2_DrawStr(u, 126 - pw, asc, m.page);
    }
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawHLine(u, 0, asc - desc, 128);

    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    if (m.lineA[0]) u8g2_DrawStr(u, 4, 32, m.lineA);
    if (m.lineB[0]) u8g2_DrawStr(u, 4, 48, m.lineB);

    u8g2_SetFont(u, u8g2_font_6x10_tf);
    if (m.footer[0]) u8g2_DrawStr(u, 4, 62, m.footer);

    // Leave the state deterministic for the next pass.
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
}
