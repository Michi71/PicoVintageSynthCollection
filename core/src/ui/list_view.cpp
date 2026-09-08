// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// list_view.cpp - implementation of picoface::ui::ListView (module ui_menu).
//
// Holds the cursor; the drawing belongs to the module ui_kit, which every
// instrument that asks for ui_menu therefore has to request as well. Keeping
// them apart is deliberate: this file is a cursor over an array, the kit is
// how a panel is painted, and instruments exist that want one without the
// other.

#include "picoface/list_view.h"

#include "picoface/ui_kit.h"

namespace picoface {
namespace ui {

void ListView::open(const char* const* entries, uint8_t count, uint8_t cursor)
{
    entries_ = entries;
    count_   = count;
    if (count == 0) {
        cursor_ = 0;
    } else {
        cursor_ = (cursor < count) ? cursor : static_cast<uint8_t>(count - 1);
    }
}

int ListView::update(const InputState& in)
{
    if (count_ == 0) {
        return kNone;
    }

    const int8_t d = in.delta(Encoder::Sel);
    if (d != 0) {
        int c = static_cast<int>(cursor_) + d;
        if (c < 0) {
            c = 0;
        }
        if (c > count_ - 1) {
            c = count_ - 1;
        }
        cursor_ = static_cast<uint8_t>(c);
    }

    if (in.pressed(Button::Sel)) {
        return static_cast<int>(cursor_);
    }
    return kNone;
}

void ListView::draw(Display& d, const char* title) const
{
    // Drawn by the shared kit since the panels were unified: the same header
    // bar and the same four rows an instrument's own lists use, so opening a
    // menu no longer changes the look of the screen it came from.
    d.clear();
    kit::header(d, title);

    if (count_ == 0) {
        return;
    }

    // The kit's list window is its own; hand it the whole array and the cursor
    // and let it decide what is on screen.
    kit::list(d, entries_, count_, cursor_);
}

} // namespace ui
} // namespace picoface
