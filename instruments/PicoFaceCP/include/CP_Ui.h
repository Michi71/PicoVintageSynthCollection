// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CP_Ui.h
//
// Front panel and menu tree of PicoFaceCP as a state machine, driven one
// InputState at a time from uiTick().
//
//   - The SELECTOR encoder pages through the eight panel screens.
//   - A SHORT press of the selector cycles the effect mode on the TREM, CHO
//     and DLY screens (Off -> A -> B), a LONG press opens the main menu.
//   - The PARAM A and PARAM B encoders set the two on-screen values, their
//     switches reset that value to a sensible default.
//
// Replaces the blocking pico_UserInterfaceFrontPanel() and its program-select
// screen, which owned the encoders on core1 and never returned. The cached
// panel values below were locals of that function; they still exist because
// an edit must show up on screen immediately, while the ring only reaches the
// engine at the next rendered block. refresh() picks up whatever changed
// behind the panel's back - MIDI, a preset, a SysEx parameter change.

#ifndef CP_UI_H
#define CP_UI_H

#include <cstdint>

#include "picoface/ui.h"
#include "picoface/list_view.h"

class mdaEPiano;
class RefaceCpChain;
class RefaceMidi;

class CP_Ui {
public:
    CP_Ui(mdaEPiano& ep, RefaceCpChain& fx, RefaceMidi& midi);

    // One pass: consume input, redraw when needed. Never blocks.
    void tick(picoface::ui::Display& d, const picoface::ui::InputState& in);

    // Unconditional redraw of the current screen, for the first frame.
    void drawNow(picoface::ui::Display& d);

private:
    enum class Screen : uint8_t { Panel, Menu, MenuSystem, About, Preset };

    // The eight panel screens, in selector order.
    enum Page : uint8_t {
        PG_VOLOCT = 0, PG_VOICE, PG_TREM, PG_CHO, PG_DLY, PG_REV, PG_VPARAM, PG_SYSTEM, PG_COUNT
    };

    void go(Screen next, uint32_t nowMs);
    void refresh();                 // reload the cached values from engine and FX
    void tickPanel(const picoface::ui::InputState& in, bool selReleased);
    void cycleMode();               // selector short press on TREM / CHO / DLY
    void resetA();                  // PARAM A switch
    void resetB();                  // PARAM B switch
    void editA(int8_t delta);
    void editB(int8_t delta);

    void draw(picoface::ui::Display& d);
    void drawPanel(picoface::ui::Display& d);
    void drawPreset(picoface::ui::Display& d) const;
    void drawAbout(picoface::ui::Display& d) const;

    mdaEPiano&             ep_;
    RefaceCpChain&         fx_;
    RefaceMidi&            midi_;
    picoface::ui::ListView list_;

    Screen   screen_     = Screen::Panel;
    Page     page_       = PG_VOLOCT;
    uint8_t  sysCursor_  = 0;

    // cached panel values
    int      instr_ = 0, nInstr_ = 1;
    int      twM_ = 0, cpM_ = 0, dlyM_ = 0, oct_ = 0;
    float    vol_ = 0.0f, drv_ = 0.0f;
    float    twD_ = 0.0f, twR_ = 0.0f, cpD_ = 0.0f, cpS_ = 0.0f;
    float    dlyD_ = 0.0f, dlyT_ = 0.0f, rev_ = 0.0f, preGain_ = 0.0f;
    int      vpIdx_ = 0;
    float    vpVal_ = 0.0f;
    uint8_t  midiCh_ = 0;
    uint8_t  presetIdx_ = 0;

    bool     selWasDown_  = false;
    bool     selConsumed_ = false;
    bool     dirty_       = true;
    uint32_t lastDrawMs_  = 0;
    uint32_t lastInputMs_ = 0;
};

#endif // CP_UI_H
