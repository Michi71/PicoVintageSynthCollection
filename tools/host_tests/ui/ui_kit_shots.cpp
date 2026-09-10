// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ui_kit_shots.cpp - renders every screen the shared kit can draw, as PBM.
//
// Two jobs. While the look is being worked on it turns an edit into a picture
// in about a second, instead of a firmware build and a flash cycle. Once the
// look is settled the same images are the regression test: the kit draws the
// pointer from a table rather than from sinf/cosf precisely so that these
// files are bit-identical to what the panel shows, and a diff against the
// stored set says what moved.
//
// The content below is real - the values are taken from the pages the
// instruments actually have - so the screens show the fit of the layout to
// this collection, not to invented text.

#include "ui_shot.h"

#include "picoface/ui_kit.h"

namespace kit = picoface::ui::kit;
using picoface::ui::Display;

namespace {

const char* g_dir = ".";

void shot(const char* name) { uishot::save(g_dir, name); }

// --- J6: the filter page, the most ordinary panel there is -----------------
void j6Filter()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "VCF", 0, 3);
    kit::panelDuo(d, {"Freq", "62", 0.62f}, {"Reso", "18", 0.18f});
    shot("j6_vcf");
}

// --- MD: a value that steps through a list next to one that sweeps ---------
void mdOsc()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "OSC 1", 1, 4);
    kit::panelDuo(d, {"Range", "16'", 0.33f}, {"Wave", "Saw"});
    shot("md_osc1");
}

// --- Both ends of the sweep, which is what the pointer has to make obvious --
void knobEnds()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "OUTPUT", 0, 1);
    kit::panelDuo(d, {"Volume", "0", 0.0f}, {"Tune", "100", 1.0f});
    shot("knob_ends");
}

// --- An encoder with nothing on it: half the pages have one ----------------
void halfEmpty()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "HPF", 3, 5);
    kit::panelDuo(d, {"Freq", "3", 0.75f}, {});
    shot("half_empty");
}

// --- The section menu, two columns ----------------------------------------
void menu()
{
    static const char* const kSections[] = {
        "PATCH", "LFO", "DCO", "HPF", "VCF", "VCA", "ENV", "CHORUS", "ARP", "SYSTEM" };
    static const char* const kVcfPages[] = { "VCF", "VCF ENV", "VCF MOD" };

    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "MENU");
    kit::listTwoCol(d, kSections, 10, 4, kVcfPages, 3, 0, false);
    shot("menu_sections");
}

// --- The same browser one level in, focus on the right --------------------
void presets()
{
    static const char* const kBanks[] = { "BANK A", "BANK B", "BANK C", "USER" };
    static const char* const kPatches[] = {
        "10 Brass Ens", "11 Strings", "12 Brass", "13 Flute", "14 Pipe Org" };

    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "PATCH");
    kit::listTwoCol(d, kBanks, 4, 0, kPatches, 5, 2, true);
    shot("menu_presets");
}

// --- An overlay over a panel that stays readable underneath ----------------
void popup()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "VCF", 0, 3);
    kit::panelDuo(d, {"Freq", "62", 0.62f}, {"Reso", "18", 0.18f});
    kit::popup(d, "PATCH", "12  Brass");
    shot("popup_patch");
}

// --- What an instrument-owned body gets: header, footer, and the box -------
// Drawn here as a plain frame with its measurements, so the space the reface
// DX algorithm diagram (or a scope, or a meter) may use is on record.
void customBody()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "ALGO", 3, 6);

    d.drawFrame(0, kit::kBodyTop, Display::kWidth, kit::kBodyHeight);
    d.setFont(u8g2_font_5x8_tf);
    d.drawTextCentered(28, "instrument draws here");
    char box[32];
    std::snprintf(box, sizeof box, "y %d..%d, %d px", kit::kBodyTop,
                  kit::kBodyBottom, kit::kBodyHeight);
    d.drawTextCentered(40, box);

    shot("custom_body");
}

// --- A page whose value is a name: D5, JV, RD and SM all have one ----------
// Three frames: a name that fits, a long one the moment it appears (held),
// and the same one 2.6 s later, scrolled. The face is the same in all three.
void namePage()
{
    Display& d = uishot::display();
    uishot::begin();
    kit::header(d, "Patch", 0, 9);
    kit::panelName(d, "2-37", "Nightfall", "S+S Ring", {"Voices", "8", 0.5f}, 0);
    shot("panel_name");

    uishot::begin();
    kit::header(d, "Patch", 0, 9);
    kit::panelName(d, "2-37", "Nightfall Dreams II", "S+S Ring", {"Voices", "8", 0.5f}, 1000);
    shot("panel_name_hold");

    // The device ticks the marquee every 40 ms between full redraws; the
    // scrolled frame is reached the same way here, through marqueeTick(), so
    // this exercises the band redraw and not just the full-page path.
    for (uint32_t t = 1040; t <= 3600; t += 40) {
        if (!kit::marqueeTick(d, t)) { std::printf("marquee stopped early at %u ms\n", (unsigned) t); break; }
    }
    shot("panel_name_scroll");
}

// --- The developer's numbers, on a screen of their own -------------------
void diag()
{
    // 6x12 holds 21 characters a row; these are the shapes the instruments use.
    static const char* const kRows[] = {
        "CPU 41%  bench 39",
        "Underruns 0",
        "Voices 6/16",
        "Notes 142  shed 0" };
    uishot::begin();
    kit::diagnostics(uishot::display(), "SYS DIAG", kRows, 4);
    shot("diagnostics");
}

// --- The About screen, once instead of four times -------------------------
void about()
{
    uishot::begin();
#ifndef PICOFACE_VERSION
#define PICOFACE_VERSION "unknown"
#endif
    kit::about(uishot::display(), "PicoFaceMD", PICOFACE_VERSION, "Press any button");
    shot("about");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1) g_dir = argv[1];
    std::printf("[ui_kit_shots] writing to %s\n", g_dir);

    j6Filter();
    mdOsc();
    knobEnds();
    halfEmpty();
    menu();
    presets();
    popup();
    customBody();
    namePage();
    diag();
    about();

    std::printf("[ok]\n");
    return 0;
}
