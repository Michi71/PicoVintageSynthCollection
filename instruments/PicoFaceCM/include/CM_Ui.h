// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CM_Ui.h -- how a page of the controller gets onto the screen, with the
// shared UI kit. In a header so that the firmware and the host test that
// renders the panel to frames draw with the same code.
//
// Covers the six pages that show values. The display page and the diagnostics
// page show what only the engine knows; the instrument draws those itself.

#ifndef CM_UI_H
#define CM_UI_H

#include "picoface/ui_kit.h"

#include "CM_Controller.h"

// Draws the controller's current page. Returns true while the timbre name on
// the part page is scrolling; the caller then keeps calling kit::marqueeTick().
inline bool cmDrawPage(picoface::ui::Display& d, const CM_Controller& c, uint32_t nowMs) {
    namespace kit = picoface::ui::kit;
    const CM_Controller::Value a = c.valueA();
    const CM_Controller::Value b = c.valueB();
    bool scrolling = false;

    d.clear();
    kit::header(d, c.pageName(), c.pageIndex(), c.pageTotal());
    if (c.isPartPage()) {
        // The part number stays put on the left, the timbre's name takes the
        // rest of the width; program and MIDI channel sit under it.
        char number[4], name[16], sub[24];
        c.partNumber(number, sizeof number);
        c.partName(name, sizeof name);
        c.partSub(sub, sizeof sub);
        scrolling = kit::panelName(d, number, name, sub, {b.name, b.text, b.norm}, nowMs);
    } else {
        kit::panelDuo(d, {a.name, a.text, a.norm}, {b.name, b.text, b.norm});
    }
    d.flush();
    return scrolling;
}

#endif // CM_UI_H
