// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// md_ui_shots.cpp - the real PicoFaceMD front panel, rendered to PBM.
//
// Not a mock-up: this walks the *unmodified* MD_Controller through its
// sections with the same encoder calls the panel makes, and hands what it
// reports to the same shared kit the firmware draws with. What comes out is
// what the OLED shows, pixel for pixel - the kit draws its knob pointer from a
// table rather than from sinf/cosf so that the two cannot drift apart.
//
//   ../ui/build_ui_kit.sh    (once, builds the u8g2 archive)
//   ./build_md_ui.sh && ./md_ui_shots out && ../ui/to_png.sh out

#include "ui_shot.h"

#include "picoface/ui_kit.h"

#include "MD_Controller.h"
#include "MD_Midi.h"

#include <cstdio>
#include <cstring>

namespace kit = picoface::ui::kit;
using picoface::ui::Display;

namespace {

const char* g_dir = ".";

// Exactly the draw() of MD_Instrument. Kept side by side with it on purpose: if the two drift, the
// screens stop being evidence.
void draw(MD_Controller& c)
{
    Display& d = uishot::display();
    uishot::begin();

    // Same branch as MD_Instrument::draw(), with the numbers a host has.
    if (c.isDiagPage()) {
        static const char* const kRows[] = {
            "CPU peak 31%", "Underruns 0", "IPC dropped 0", "Notes 142" };
        kit::diagnostics(d, c.title(), kRows, 4);
        return;
    }

    kit::header(d, c.title(), c.pageIndex(), c.pageTotal());

    if (c.viewKind() == MD_VIEW_LIST) {
        static char        rows[4][22];
        static const char* ptr[4];
        const int n = c.listCount(), cur = c.listCursor();
        int top = cur - 1;
        if (top > n - 4) top = n - 4;
        if (top < 0)     top = 0;
        int shown = 0;
        for (int i = 0; i < 4 && top + i < n; ++i) {
            c.listEntry(top + i, rows[i], sizeof(rows[i]));
            ptr[i] = rows[i];
            ++shown;
        }
        kit::list(d, ptr, shown, cur - top);
    } else {
        char va[20], vb[20];
        c.paramAText(va, sizeof(va));
        c.paramBText(vb, sizeof(vb));
        kit::panelDuo(d, { c.paramAName(), va, c.paramANorm() },
                         { c.paramBName(), vb, c.paramBNorm() });
    }

}

// Walks the panel from a freshly constructed controller: section list, cursor
// on section 0. Starting fresh every time rather than navigating back is what
// makes this deterministic - onSelectButton() toggles, and PRESET is itself a
// list, so "press until we are out" depends on where we were.
void goTo(MD_Controller& c, int section, int page)
{
    for (int i = 0; i < section; ++i) c.onEncoder1(1);
    c.onSelectButton();
    for (int i = 0; i < page; ++i) c.onEncoder1(1);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1) g_dir = argv[1];
    std::printf("[md_ui_shots] writing to %s\n", g_dir);

    MD_Midi midi;

    // The section list, where the panel starts.
    { MD_Controller c(midi); draw(c); uishot::save(g_dir, "md_sections"); }

    // PRESET (section 0) is a list of its own.
    { MD_Controller c(midi); goTo(c, 0, 0); draw(c);
      uishot::save(g_dir, "md_presets"); }

    // OSCILLATOR / OSC 1: a range and a waveform, both stepped - two list
    // markers, no knobs.
    { MD_Controller c(midi); goTo(c, 2, 0); draw(c);
      uishot::save(g_dir, "md_osc1"); }

    // MODIFIERS / MOD FILTER: cutoff and emphasis, both swept - the page the
    // knobs were added for.
    { MD_Controller c(midi); goTo(c, 4, 0); draw(c);
      uishot::save(g_dir, "md_filter"); }

    // The same page after turning both encoders, so the pointers have moved
    // off the preset values.
    { MD_Controller c(midi); goTo(c, 4, 0);
      c.onEncoder2(25); c.onEncoder3(-40); draw(c);
      uishot::save(g_dir, "md_filter_turned"); }

    // SYSTEM / SYS DIAG: the developer's page, section 8, second page.
    { MD_Controller c(midi); goTo(c, 8, 1); draw(c);
      uishot::save(g_dir, "md_diag"); }

    // MIXER / MIX COLOUR: one value and an empty half.
    { MD_Controller c(midi); goTo(c, 3, 4); draw(c);
      uishot::save(g_dir, "md_half_empty"); }

    std::printf("[ok]\n");
    return 0;
}
