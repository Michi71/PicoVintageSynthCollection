// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Display.h -- page renderer in the collection's house style: inverted
// header bar with title and page name, two body lines, diagnostics footer.

#ifndef D5_DISPLAY_H
#define D5_DISPLAY_H

#include "picoface/ui.h"

struct D5UiModel {
    char title[16];
    char page[10];
    char lineA[24];
    char lineB[24];
    char footer[28];
};

void d5_display_page(picoface::ui::Display& d, const D5UiModel& m);

#endif // D5_DISPLAY_H
