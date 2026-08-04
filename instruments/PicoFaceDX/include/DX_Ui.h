// DX_Ui.h
//
// Front panel and menu tree of PicoFaceDX as a state machine, driven one
// InputState at a time from uiTick().
//
//   - The SELECTOR encoder pages through the DX pages (OP1..OP4/LFO/ALGO/FX1/FX2).
//   - The PARAM A and PARAM B encoders edit the two values of the current page.
//   - A LONG press of the selector opens the main menu (Presets / System).
//
// Replaces src/pico_frontpanel.cpp, whose pico_UserInterfaceFrontPanel() never
// returned: it *was* the core1 loop, and every menu screen inside it spun in
// its own loop pumping USB by hand. Every screen here draws once per tick and
// returns, so the audio producer on core0 keeps running with a menu open.

#ifndef DX_UI_H
#define DX_UI_H

#include <cstdint>

#include "picoface/ui.h"
#include "picoface/list_view.h"

class DX_Controller;
class DX_Synth_Bridge;
class RefaceMidi;

class DX_Ui {
public:
    DX_Ui(DX_Controller& controller, DX_Synth_Bridge& bridge, RefaceMidi& midi);

    // One pass: consume input, redraw when needed. Never blocks.
    void tick(picoface::ui::Display& d, const picoface::ui::InputState& in);

    // Unconditional redraw of the current screen, for the first frame.
    void drawNow(picoface::ui::Display& d);

private:
    enum class Screen : uint8_t { Panel, Menu, Presets, System, MasterVol, About, CpuLoad };

    void go(Screen next, uint32_t nowMs);
    void draw(picoface::ui::Display& d);
    void drawMasterVol(picoface::ui::Display& d) const;
    void drawAbout(picoface::ui::Display& d) const;
    void drawCpuLoad(picoface::ui::Display& d) const;

    DX_Controller&           controller_;
    DX_Synth_Bridge&         bridge_;
    RefaceMidi&              midi_;
    picoface::ui::ListView   list_;
    Screen                   screen_      = Screen::Panel;
    // Cursor the System list returns to, so leaving Master Vol, About or CPU
    // Load lands back on the entry it was opened from.
    uint8_t                  sysCursor_   = 0;
    bool                     dirty_       = true;
    uint32_t                 lastDrawMs_  = 0;
    uint32_t                 lastInputMs_ = 0;
};

#endif // DX_UI_H
