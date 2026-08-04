// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// OB_Ui.h - front panel and menu tree of PicoFaceOB.
//
// 49 parameters on three encoders: the selector pages through them two at a
// time, PARAM A and PARAM B edit the two on screen. A long press of the
// selector opens the menu; presets sit behind a category level (18 categories,
// several hundred patches). Same state machine shape as YC_Ui and CP_Ui.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#ifndef OB_UI_H
#define OB_UI_H

#include <cstdint>

#include "picoface/ui.h"
#include "picoface/list_view.h"

#include "ob_params.h"

class OB_Engine;

class OB_Ui
{
  public:
    explicit OB_Ui(OB_Engine& engine);

    void tick(picoface::ui::Display& d, const picoface::ui::InputState& in);
    void drawNow(picoface::ui::Display& d);

    // The adapter feeds these in for the CPU Load screen. resetPeak is a
    // callback into the adapter: the peak is a maximum since boot, and the
    // very first block after power-on runs with cold caches, so one cold
    // outlier would otherwise sit on the screen forever. Entering the screen
    // restarts the measurement.
    void setLoad(float now, float peak) { load_ = now; loadPeak_ = peak; }
    void setPeakReset(void (*fn)(void*), void* ctx) { resetPeak_ = fn; resetCtx_ = ctx; }

  private:
    // Presets is the category list, PresetList the entries of one category -
    // a flat list stopped working at 400+ factory patches.
    enum class Screen : uint8_t { Panel, Menu, Presets, PresetList, System, About, CpuLoad };

    static constexpr uint8_t kPageCount = (OB_PARAM_COUNT + 1) / 2;

    void go(Screen next, uint32_t nowMs);
    void edit(uint8_t paramId, int8_t delta);
    void draw(picoface::ui::Display& d);
    void drawPanel(picoface::ui::Display& d);
    void drawAbout(picoface::ui::Display& d) const;
    void drawCpuLoad(picoface::ui::Display& d) const;
    void valueText(uint8_t paramId, char* buf, size_t len) const;

    OB_Engine&             engine_;
    picoface::ui::ListView list_;

    Screen   screen_      = Screen::Panel;
    uint8_t  page_        = 0;
    uint8_t  sysCursor_   = 0;
    uint8_t  catCursor_   = 0;  // last category browsed
    uint8_t  entryCursor_ = 0;  // last preset inside it
    bool     dirty_       = true;
    uint32_t lastDrawMs_  = 0;
    uint32_t lastInputMs_ = 0;
    float    load_        = 0.f;
    float    loadPeak_    = 0.f;
    void   (*resetPeak_)(void*) = nullptr;
    void*    resetCtx_    = nullptr;
};

#endif // OB_UI_H
