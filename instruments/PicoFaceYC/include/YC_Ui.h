// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// YC_Ui.h
//
// Front panel and menu tree of PicoFaceYC as a state machine, driven one
// InputState at a time from uiTick().
//
//   - The SELECTOR encoder pages through the YC pages (see YcPage).
//   - The PARAM A and PARAM B encoders edit the two values of the current page.
//   - Pressing PARAM A toggles Percussion On/Off while on the PERCUSSION page.
//   - A LONG press of the selector opens the System main menu.
//
// Replaces src/pico_frontpanel.cpp, whose pico_UserInterfaceFrontPanel() never
// returned: it *was* the core1 loop. Every screen here draws once per tick and
// returns, so the audio producer on core0 keeps running with a menu open.

#ifndef YC_UI_H
#define YC_UI_H

#include <cstdint>

#include "picoface/ui.h"
#include "picoface/list_view.h"

class YC_Controller;
class YC_Synth_Bridge;

class YC_Ui {
public:
    YC_Ui(YC_Controller& controller, YC_Synth_Bridge& bridge);

    // One pass: consume input, redraw when needed. Never blocks.
    void tick(picoface::ui::Display& d, const picoface::ui::InputState& in);

    // Unconditional redraw of the current screen, for the first frame.
    void drawNow(picoface::ui::Display& d);

private:
    enum class Screen : uint8_t { Panel, Menu, System, About, CpuLoad };

    void go(Screen next, uint32_t nowMs);
    void draw(picoface::ui::Display& d);
    void drawAbout(picoface::ui::Display& d) const;
    void drawCpuLoad(picoface::ui::Display& d) const;

    YC_Controller&           controller_;
    YC_Synth_Bridge&         bridge_;
    picoface::ui::ListView   list_;
    Screen                   screen_      = Screen::Panel;
    // Cursor the System list returns to, so leaving About or CPU Load lands
    // back on the entry it was opened from.
    uint8_t                  sysCursor_   = 0;
    bool                     dirty_       = true;
    uint32_t                 lastDrawMs_  = 0;
    uint32_t                 lastInputMs_ = 0;
};

#endif // YC_UI_H
