// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// DX_Ui.cpp - front panel and menu tree of PicoFaceDX, see DX_Ui.h.

#include "DX_Ui.h"

#include <cstdio>

#include "u8g2.h"

#include "DX_Controller.h"
#include "DX_GUI.h"
#include "DX_Synth_Bridge.h"
#include "audio_i2s.h"   // g_i2s_underrun_count
#include "ipc.h"
#include "midi_reface.h"
#include "presets.h"

using picoface::ui::Button;
using picoface::ui::Display;
using picoface::ui::Encoder;
using picoface::ui::InputState;
using picoface::ui::ListView;

extern "C" void ui_set_master_volume(int vol);
extern "C" int  ui_get_master_volume(void);

namespace {

const char* const kMenuEntries[]   = {"Presets", "System", "<< BACK"};
const char* const kSystemEntries[] = {"Master Vol", "About", "CPU Load", "<< BACK"};

// The preset list is built once from the factory table: ListView holds a
// pointer to the array and never copies it, so it has to outlive the view.
// The old blocking list took one newline-joined 512-byte string instead.
const char* const* presetEntries()
{
    static const char* entries[DX_NPRESETS + 1];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < DX_NPRESETS; ++i) entries[i] = dxPresets[i].name;
        entries[DX_NPRESETS] = "<< BACK";
        built = true;
    }
    return entries;
}

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
constexpr const char* kAboutName = "PicoFaceDX";
#endif

#ifdef PICOFACE_VERSION
constexpr const char* kAboutVersion = PICOFACE_VERSION;
#else
constexpr const char* kAboutVersion = "0.1";
#endif

} // namespace

DX_Ui::DX_Ui(DX_Controller& controller, DX_Synth_Bridge& bridge, RefaceMidi& midi)
    : controller_(controller), bridge_(bridge), midi_(midi) {}

void DX_Ui::go(Screen next, uint32_t nowMs)
{
    screen_      = next;
    dirty_       = true;
    lastInputMs_ = nowMs;

    if (next == Screen::Menu) {
        list_.open(kMenuEntries, 3);
    } else if (next == Screen::Presets) {
        // Opens on the preset that is currently loaded, as the blocking list did.
        list_.open(presetEntries(), DX_NPRESETS + 1, preset_get_current());
    } else if (next == Screen::System) {
        list_.open(kSystemEntries, 4, sysCursor_);
    }
}

void DX_Ui::drawNow(Display& d)
{
    draw(d);
    dirty_ = false;
}

void DX_Ui::tick(Display& d, const InputState& in)
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
        break;
    }

    case Screen::Menu: {
        const int sel = list_.update(in);
        if (sel == 0)      go(Screen::Presets, in.nowMs);
        else if (sel == 1) go(Screen::System, in.nowMs);
        else if (sel == 2) go(Screen::Panel, in.nowMs);
        break;
    }

    case Screen::Presets: {
        const int sel = list_.update(in);
        if (sel >= 0 && sel < DX_NPRESETS) {
            preset_set_current((uint8_t) sel);
            preset_stage((uint8_t) sel);
            midi_.txProgram(sel);
            // Straight back to the panel: the point of picking a preset is to
            // play it.
            go(Screen::Panel, in.nowMs);
        } else if (sel == DX_NPRESETS) {
            go(Screen::Menu, in.nowMs);
        }
        break;
    }

    case Screen::System: {
        const int sel = list_.update(in);
        // Remember only the three screens, not "<< BACK".
        if (sel >= 0 && sel <= 2) sysCursor_ = (uint8_t) sel;
        if (sel == 0)      go(Screen::MasterVol, in.nowMs);
        else if (sel == 1) go(Screen::About, in.nowMs);
        else if (sel == 2) go(Screen::CpuLoad, in.nowMs);
        else if (sel == 3) go(Screen::Menu, in.nowMs);
        break;
    }

    case Screen::MasterVol: {
        // Every step goes to the engine immediately (ring -> next block), so
        // the level can be set by ear while notes are sounding.
        const int8_t d = in.delta(Encoder::Sel);
        if (d) ui_set_master_volume(ui_get_master_volume() + d);   // clamps to 0..100 internally
        if (in.pressed(Button::Sel)) go(Screen::System, in.nowMs);
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
    // exactly as the blocking selection list used to. Master Vol, About and
    // CPU Load are left out on purpose - setting a level by ear and watching
    // the load counters both take longer than five seconds.
    if ((screen_ == Screen::Menu || screen_ == Screen::Presets || screen_ == Screen::System) &&
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

void DX_Ui::draw(Display& d)
{
    switch (screen_) {
    case Screen::Panel:
        // dxDrawScreen() only paints; clearing and pushing are ours.
        d.clear();
        dxDrawScreen(d.raw(), controller_);
        break;
    case Screen::Menu:
        list_.draw(d, "MENU");
        break;
    case Screen::Presets:
        list_.draw(d, "PRESETS");
        break;
    case Screen::System:
        list_.draw(d, "SYSTEM");
        break;
    case Screen::MasterVol:
        drawMasterVol(d);
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

void DX_Ui::drawMasterVol(Display& d) const
{
    u8g2_t* u = d.raw();
    char buf[16];
    const int vol = ui_get_master_volume();

    d.clear();
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawStr(u, 4, 14, "MASTER VOL");
    u8g2_DrawHLine(u, 0, 18, 128);

    snprintf(buf, sizeof(buf), "%d %%", vol);
    u8g2_DrawStr(u, 4, 38, buf);

    // Level bar: 100 units mapped onto the 100 px between the frame edges.
    u8g2_DrawFrame(u, 12, 44, 104, 10);
    if (vol > 0) u8g2_DrawBox(u, 14, 46, (u8g2_uint_t) vol, 6);

    u8g2_SetFont(u, u8g2_font_6x10_tf);
    u8g2_DrawStr(u, 4, 62, "Turn=set  Press=OK");
}

void DX_Ui::drawAbout(Display& d) const
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

void DX_Ui::drawCpuLoad(Display& d) const
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
    // Every underrun is one block of silence substituted for missing audio,
    // i.e. an audible click. Non-zero after normal playing means the producer
    // is not keeping up.
    snprintf(buf, sizeof(buf), "URN:  %lu", (unsigned long) g_i2s_underrun_count);
    u8g2_DrawStr(u, 4, 52, buf);
    // Dropped IPC packets: a non-zero value means the ring overflowed between
    // two rendered blocks.
    snprintf(buf, sizeof(buf), "DRP:  %lu", (unsigned long) dx_ipc_dropped);
    u8g2_DrawStr(u, 4, 63, buf);
}
