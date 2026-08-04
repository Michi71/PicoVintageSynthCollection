// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// YC_Ui.cpp - front panel and menu tree of PicoFaceYC, see YC_Ui.h.

#include "YC_Ui.h"

#include <cstdio>

#include "hardware/structs/watchdog.h"
#include "u8g2.h"

#include "YC_Controller.h"
#include "YC_GUI.h"
#include "YC_Synth_Bridge.h"
#include "ipc.h"

using picoface::ui::Button;
using picoface::ui::Display;
using picoface::ui::Encoder;
using picoface::ui::InputState;
using picoface::ui::ListView;

namespace {

const char* const kMenuEntries[]   = {"System", "<< BACK"};
const char* const kSystemEntries[] = {"About", "CPU Load", "<< BACK"};

// A menu screen left alone this long falls back to the front panel. The
// blocking selection list had the same 5 s escape hatch.
constexpr uint32_t kMenuIdleMs = 5000;

// Redraw at most every 33 ms after an edit, and at least twice a second so the
// CPU Load screen keeps counting.
constexpr uint32_t kMinRedrawMs = 33;
constexpr uint32_t kMaxRedrawMs = 500;

#ifdef PICOFACE_INSTRUMENT_NAME
constexpr const char* kAboutName = PICOFACE_INSTRUMENT_NAME;
#else
constexpr const char* kAboutName = "PicoFaceYC";
#endif

#ifdef PICOFACE_VERSION
constexpr const char* kAboutVersion = PICOFACE_VERSION;
#else
constexpr const char* kAboutVersion = "0.1";
#endif

} // namespace

YC_Ui::YC_Ui(YC_Controller& controller, YC_Synth_Bridge& bridge)
    : controller_(controller), bridge_(bridge) {}

void YC_Ui::go(Screen next, uint32_t nowMs)
{
    screen_      = next;
    dirty_       = true;
    lastInputMs_ = nowMs;

    if (next == Screen::Menu) {
        list_.open(kMenuEntries, 2);
    } else if (next == Screen::System) {
        list_.open(kSystemEntries, 3, sysCursor_);
    }
}

void YC_Ui::drawNow(Display& d)
{
    draw(d);
    dirty_ = false;
}

void YC_Ui::tick(Display& d, const InputState& in)
{
    const bool anyInput = in.delta(Encoder::Sel) || in.delta(Encoder::ParamA) ||
                          in.delta(Encoder::ParamB) || in.pressed(Button::Sel) ||
                          in.pressed(Button::ParamA) || in.pressed(Button::ParamB);
    if (anyInput) {
        dirty_       = true;
        lastInputMs_ = in.nowMs;
    }

    switch (screen_) {
    case Screen::Panel: {
        // The long press arrives while the button is still down; its rising
        // edge fired on an earlier tick, so opening the menu cannot swallow
        // a selection in the list that is about to appear.
        if (in.longPress(Button::Sel)) {
            go(Screen::Menu, in.nowMs);
            break;
        }
        const int8_t dSel = in.delta(Encoder::Sel);
        const int8_t dA   = in.delta(Encoder::ParamA);
        const int8_t dB   = in.delta(Encoder::ParamB);
        if (dSel) controller_.onEncoder1(dSel);
        if (dA)   controller_.onEncoder2(dA);
        if (dB)   controller_.onEncoder3(dB);
        if (in.pressed(Button::ParamA)) controller_.onButtonA();
        break;
    }

    case Screen::Menu: {
        const int sel = list_.update(in);
        if (sel == 0)      go(Screen::System, in.nowMs);
        else if (sel == 1) go(Screen::Panel, in.nowMs);
        break;
    }

    case Screen::System: {
        const int sel = list_.update(in);
        // Remember only the two screens, not "<< BACK".
        if (sel == 0 || sel == 1) sysCursor_ = (uint8_t) sel;
        if (sel == 0)      go(Screen::About, in.nowMs);
        else if (sel == 1) go(Screen::CpuLoad, in.nowMs);
        else if (sel == 2) go(Screen::Menu, in.nowMs);
        break;
    }

    case Screen::About:
    case Screen::CpuLoad:
        // Any button returns, as the "Press any button" hint promises.
        if (in.pressed(Button::Sel) || in.pressed(Button::ParamA) || in.pressed(Button::ParamB)) {
            go(Screen::System, in.nowMs);
        }
        break;
    }

    // Never strand the user in a list: fall back to the panel after a while,
    // exactly as the blocking selection list used to. About and CPU Load are
    // left out on purpose - watching the load counters for a minute is the
    // point of that screen.
    if ((screen_ == Screen::Menu || screen_ == Screen::System) &&
        (in.nowMs - lastInputMs_) > kMenuIdleMs) {
        go(Screen::Panel, in.nowMs);
    }

    if ((dirty_ && (in.nowMs - lastDrawMs_) >= kMinRedrawMs) ||
        (in.nowMs - lastDrawMs_) >= kMaxRedrawMs) {
        draw(d);
        dirty_      = false;
        lastDrawMs_ = in.nowMs;
    }
}

void YC_Ui::draw(Display& d)
{
    switch (screen_) {
    case Screen::Panel:
        // ycDrawScreen() only paints; clearing and pushing are ours.
        d.clear();
        ycDrawScreen(d.raw(), controller_);
        break;
    case Screen::Menu:
        list_.draw(d, "MENU");
        break;
    case Screen::System:
        list_.draw(d, "SYSTEM");
        break;
    case Screen::About:
        drawAbout(d);
        break;
    case Screen::CpuLoad:
        drawCpuLoad(d);
        break;
    }

    // Arms the incremental push; the core loop sends the buffer out in half
    // tile rows.
    d.flush();
}

void YC_Ui::drawAbout(Display& d) const
{
    u8g2_t* u = d.raw();

    d.clear();
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawStr(u, 4, 14, "ABOUT");
    u8g2_DrawHLine(u, 0, 18, 128);
    u8g2_DrawStr(u, 4, 36, kAboutName);
    u8g2_DrawStr(u, 4, 52, kAboutVersion);
    u8g2_SetFont(u, u8g2_font_6x10_tf);
    u8g2_DrawStr(u, 4, 62, "Press any button");
}

void YC_Ui::drawCpuLoad(Display& d) const
{
    u8g2_t* u = d.raw();
    char buf[32];

    d.clear();
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawStr(u, 4, 14, "CPU LOAD");
    u8g2_DrawHLine(u, 0, 18, 128);

    u8g2_SetFont(u, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "Now:  %d %%", (int) bridge_.cpuLoadPercent());
    u8g2_DrawStr(u, 4, 30, buf);
    snprintf(buf, sizeof(buf), "Peak: %d %%", (int) bridge_.cpuLoadPeakPercent());
    u8g2_DrawStr(u, 4, 41, buf);
    snprintf(buf, sizeof(buf), "WDR:  %lu", (unsigned long) watchdog_hw->scratch[0]);
    u8g2_DrawStr(u, 4, 52, buf);
    // Dropped IPC packets: a non-zero value means the ring overflowed between
    // two rendered blocks.
    snprintf(buf, sizeof(buf), "DRP:  %lu", (unsigned long) yc_ipc_dropped);
    u8g2_DrawStr(u, 4, 63, buf);
}
