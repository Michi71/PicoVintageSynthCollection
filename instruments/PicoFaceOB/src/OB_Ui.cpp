// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// OB_Ui.cpp - see OB_Ui.h.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#include "OB_Ui.h"

#include <cstdio>

#include "u8g2.h"

#include "picoface/ui_kit.h"

#include "OB_Engine.h"
#include "ob_ipc.h"
#include "ob_presets.h"

using picoface::ui::Button;
using picoface::ui::Display;
using picoface::ui::Encoder;
using picoface::ui::InputState;

namespace {

const char* const kMenuEntries[]   = {"Presets", "System", "<< BACK"};
const char* const kSystemEntries[] = {"About", "CPU Load", "<< BACK"};

constexpr uint32_t kMenuIdleMs  = 5000;
constexpr uint32_t kMinRedrawMs = 33;
constexpr uint32_t kMaxRedrawMs = 500;

// One detent = one percent of a continuous parameter.
constexpr float kStep = 0.01f;

// ListView wants arrays of pointers. Categories are filled once, the entry
// list is refilled whenever a category is opened; both end in "<< BACK".
const char* kCatNames[OB_NPRESET_CATS + 1] = {};
const char* kEntryNames[OB_MAX_CAT_PRESETS + 1] = {};

uint8_t fillCatNames()
{
    for (int i = 0; i < OB_NPRESET_CATS; ++i)
    {
        kCatNames[i] = obPresetCategories[i].name;
    }
    kCatNames[OB_NPRESET_CATS] = "<< BACK";
    return (uint8_t)(OB_NPRESET_CATS + 1);
}

uint8_t fillEntryNames(uint8_t cat)
{
    const ObPresetCategory& c = obPresetCategories[cat];
    for (int i = 0; i < c.count; ++i)
    {
        kEntryNames[i] = obPresets[c.first + i].name;
    }
    kEntryNames[c.count] = "<< BACK";
    return (uint8_t)(c.count + 1);
}

// The 15 Xpander pole-mix responses, in Filter.h's table order.
const char* const kXpanderModeNames[15] = {
    "LP4", "LP3", "LP2", "LP1", "HP3", "HP2", "HP1", "BP4",
    "BP2", "N2",  "PH3", "H2+L1", "H3+L1", "N2+L1", "P3+L1"};

#ifdef PICOFACE_INSTRUMENT_NAME
constexpr const char* kAboutName = PICOFACE_INSTRUMENT_NAME;
#else
constexpr const char* kAboutName = "PicoFaceOB";
#endif

#ifdef PICOFACE_VERSION
constexpr const char* kAboutVersion = PICOFACE_VERSION;
#else
constexpr const char* kAboutVersion = "0.1";
#endif

} // namespace

OB_Ui::OB_Ui(OB_Engine& engine) : engine_(engine) {}

void OB_Ui::go(Screen next, uint32_t nowMs)
{
    screen_      = next;
    dirty_       = true;
    lastInputMs_ = nowMs;

    if (next == Screen::Menu)
    {
        list_.open(kMenuEntries, 3);
    }
    else if (next == Screen::System)
    {
        list_.open(kSystemEntries, 3, sysCursor_);
    }
    else if (next == Screen::CpuLoad)
    {
        if (resetPeak_)
        {
            resetPeak_(resetCtx_);
        }
    }
    else if (next == Screen::Presets)
    {
        list_.open(kCatNames, fillCatNames(), catCursor_);
    }
    else if (next == Screen::PresetList)
    {
        list_.open(kEntryNames, fillEntryNames(catCursor_), entryCursor_);
    }
}

void OB_Ui::edit(uint8_t paramId, int8_t delta)
{
    if (paramId >= OB_PARAM_COUNT)
    {
        return;
    }

    const ObParamDesc& d = obParams[paramId];
    float v = engine_.getParam(paramId);

    if (d.steps >= 2)
    {
        // Discrete parameter: step through its positions.
        const int n = d.steps;
        int idx = (int)(v * (n - 1) + 0.5f) + (delta > 0 ? 1 : -1);
        if (idx < 0) idx = 0;
        if (idx > n - 1) idx = n - 1;
        v = (float)idx / (float)(n - 1);
    }
    else
    {
        v += delta * kStep;
    }

    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;

    // Through the ring, so the value is applied at a block boundary rather
    // than in the middle of one - and the same path a MIDI CC takes.
    ipc_send_ob_param(paramId, v);
}

void OB_Ui::tick(Display& d, const InputState& in)
{
    const bool anyInput = in.delta(Encoder::Sel) || in.delta(Encoder::ParamA) ||
                          in.delta(Encoder::ParamB) || in.pressed(Button::Sel) ||
                          in.pressed(Button::ParamA) || in.pressed(Button::ParamB);
    if (anyInput)
    {
        dirty_       = true;
        lastInputMs_ = in.nowMs;
    }

    switch (screen_)
    {
    case Screen::Panel:
    {
        if (in.longPress(Button::Sel))
        {
            go(Screen::Menu, in.nowMs);
            break;
        }
        const int8_t dSel = in.delta(Encoder::Sel);
        if (dSel != 0)
        {
            int p = (int)page_ + (dSel > 0 ? 1 : -1);
            p = (p + kPageCount) % kPageCount;
            page_ = (uint8_t)p;
        }
        const int8_t dA = in.delta(Encoder::ParamA);
        const int8_t dB = in.delta(Encoder::ParamB);
        if (dA) edit((uint8_t)(page_ * 2), dA);
        if (dB) edit((uint8_t)(page_ * 2 + 1), dB);
        break;
    }

    case Screen::Menu:
    {
        const int sel = list_.update(in);
        if (sel == 0)      go(Screen::Presets, in.nowMs);
        else if (sel == 1) go(Screen::System, in.nowMs);
        else if (sel == 2) go(Screen::Panel, in.nowMs);
        break;
    }

    case Screen::Presets:
    {
        const int sel = list_.update(in);
        if (sel == OB_NPRESET_CATS)
        {
            go(Screen::Menu, in.nowMs);
        }
        else if (sel >= 0)
        {
            if ((uint8_t) sel != catCursor_)
            {
                catCursor_   = (uint8_t) sel;
                entryCursor_ = 0;
            }
            go(Screen::PresetList, in.nowMs);
        }
        break;
    }

    case Screen::PresetList:
    {
        const int sel = list_.update(in);
        const ObPresetCategory& c = obPresetCategories[catCursor_];
        if (sel == c.count)
        {
            go(Screen::Presets, in.nowMs);
        }
        else if (sel >= 0)
        {
            entryCursor_ = (uint8_t) sel;
            // Straight to the engine rather than through the ring: a preset
            // is the whole parameter set at once and would fill it.
            engine_.applyPreset(c.first + sel);
            go(Screen::Panel, in.nowMs);
        }
        break;
    }

    case Screen::System:
    {
        const int sel = list_.update(in);
        if (sel == 0 || sel == 1) sysCursor_ = (uint8_t)sel;
        if (sel == 0)      go(Screen::About, in.nowMs);
        else if (sel == 1) go(Screen::CpuLoad, in.nowMs);
        else if (sel == 2) go(Screen::Menu, in.nowMs);
        break;
    }

    case Screen::About:
    case Screen::CpuLoad:
        if (in.pressed(Button::Sel) || in.pressed(Button::ParamA) || in.pressed(Button::ParamB))
        {
            go(Screen::System, in.nowMs);
        }
        break;
    }

    if ((screen_ == Screen::Menu || screen_ == Screen::System || screen_ == Screen::Presets ||
         screen_ == Screen::PresetList) &&
        (in.nowMs - lastInputMs_) > kMenuIdleMs)
    {
        go(Screen::Panel, in.nowMs);
    }

    if ((dirty_ && (in.nowMs - lastDrawMs_) >= kMinRedrawMs) ||
        (in.nowMs - lastDrawMs_) >= kMaxRedrawMs)
    {
        draw(d);
        dirty_      = false;
        lastDrawMs_ = in.nowMs;
    }
}

void OB_Ui::drawNow(Display& d)
{
    draw(d);
    dirty_ = false;
}

void OB_Ui::draw(Display& d)
{
    switch (screen_)
    {
    case Screen::Panel:   drawPanel(d);            break;
    case Screen::Menu:    list_.draw(d, "MENU");   break;
    case Screen::Presets: list_.draw(d, "PRESET"); break;
    // the category name as title, so you know where you are
    case Screen::PresetList: list_.draw(d, obPresetCategories[catCursor_].name); break;
    case Screen::System:  list_.draw(d, "SYSTEM"); break;
    case Screen::About:   drawAbout(d);            break;
    case Screen::CpuLoad: drawCpuLoad(d);          break;
    }
    d.flush();
}

void OB_Ui::valueText(uint8_t paramId, char* buf, size_t len) const
{
    if (paramId >= OB_PARAM_COUNT)
    {
        buf[0] = 0;
        return;
    }

    const ObParamDesc& d = obParams[paramId];
    const float v = engine_.getParam(paramId);

    if (paramId == OB_BEND_RANGE)
    {
        // The two positions of the bend assembly switch, not On/Off.
        snprintf(buf, len, "%s", v > 0.5f ? "Broad" : "Narrow");
    }
    else if (paramId == OB_NOISE_COLOR)
    {
        snprintf(buf, len, "%s", v < 1.f / 3.f ? "White" : (v < 2.f / 3.f ? "Pink" : "Red"));
    }
    else if (paramId == OB_XPANDER_MODE)
    {
        int idx = (int)(v * 14.f + 0.5f);
        if (idx > 14) idx = 14;
        snprintf(buf, len, "%s", kXpanderModeNames[idx]);
    }
    else if (paramId == OB_ENV_TO_PITCH || paramId == OB_ENV_TO_PW ||
             paramId == OB_FILTER_ENV_AMT)
    {
        // bipolar: 0.5 is neutral, below inverts
        snprintf(buf, len, "%+d", (int)((v - 0.5f) * 200.f + (v >= 0.5f ? 0.5f : -0.5f)));
    }
    else if (d.steps == 2)
    {
        snprintf(buf, len, "%s", v > 0.5f ? "On" : "Off");
    }
    else if (d.steps > 2)
    {
        snprintf(buf, len, "%d/%d", (int)(v * (d.steps - 1) + 0.5f) + 1, (int)d.steps);
    }
    else
    {
        snprintf(buf, len, "%d", (int)(v * 100.f + 0.5f));
    }
}

void OB_Ui::drawPanel(Display& d)
{
    namespace kit = picoface::ui::kit;
    char va[16], vb[16];

    d.clear();
    kit::header(d, "OB-X", page_, kPageCount);

    const uint8_t idA = (uint8_t)(page_ * 2);
    const uint8_t idB = (uint8_t)(page_ * 2 + 1);

    valueText(idA, va, sizeof(va));
    kit::Param a{ obParams[idA].name, va, knobNorm(idA) };

    kit::Param b{};
    if (idB < OB_PARAM_COUNT) {
        valueText(idB, vb, sizeof(vb));
        b = { obParams[idB].name, vb, knobNorm(idB) };
    }

    kit::panelDuo(d, a, b);
    kit::footer(d, "Sel:Page  A/B:edit");
}

// Where a value sits on its dial, or negative where it has no dial: the OB-X
// panel is sliders and switches, and obParams says which is which - steps 0 is
// a slider, anything else a switch or a selector. The bipolar amounts count as
// sliders and sit at the middle when neutral, which is what the original's
// centre-detented controls did.
float OB_Ui::knobNorm(uint8_t paramId) const
{
    if (paramId >= OB_PARAM_COUNT) return -1.0f;
    if (obParams[paramId].steps != 0) return -1.0f;
    return engine_.getParam(paramId);
}

void OB_Ui::drawAbout(Display& d) const
{
    picoface::ui::kit::about(d, kAboutName, kAboutVersion, "Press any button");
}

void OB_Ui::drawCpuLoad(Display& d) const
{
    u8g2_t* u = d.raw();
    char buf[32];

    d.clear();
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawStr(u, 4, 14, "CPU LOAD");
    u8g2_DrawHLine(u, 0, 18, Display::kWidth);

    u8g2_SetFont(u, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "Now:  %d %%", (int)load_);
    u8g2_DrawStr(u, 4, 30, buf);
    snprintf(buf, sizeof(buf), "Peak: %d %%", (int)loadPeak_);
    u8g2_DrawStr(u, 4, 41, buf);
    snprintf(buf, sizeof(buf), "Voices: %d/%d", engine_.soundingVoices(), MAX_VOICES);
    u8g2_DrawStr(u, 4, 52, buf);
    snprintf(buf, sizeof(buf), "DRP:  %lu", (unsigned long)ob_ipc_dropped);
    u8g2_DrawStr(u, 4, 63, buf);
}
