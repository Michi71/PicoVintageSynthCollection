// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Display.h -- page renderer in the collection's house style: inverted
// header bar with title and page name, two body lines, diagnostics footer.

#ifndef JV_DISPLAY_H
#define JV_DISPLAY_H

#include "picoface/ui.h"

struct JvUiModel {
    char title[16];
    char page[10];
    char lineA[24];
    char lineB[24];
    char footer[28];
};

void jv_display_page(picoface::ui::Display& d, const JvUiModel& m);

#endif // JV_DISPLAY_H
