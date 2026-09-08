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
    kit::footer(d, "J6  1-12 Brass  ch1  P42%");
    shot("j6_vcf");
}

// --- MD: a value that steps through a list next to one that sweeps ---------
void mdOsc()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "OSC 1", 1, 4);
    kit::panelDuo(d, {"Range", "16'", 0.33f}, {"Wave", "Saw"});
    kit::footer(d, "MD  Fat Bass  ch1  N3");
    shot("md_osc1");
}

// --- Both ends of the sweep, which is what the pointer has to make obvious --
void knobEnds()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "OUTPUT", 0, 1);
    kit::panelDuo(d, {"Volume", "0", 0.0f}, {"Tune", "100", 1.0f});
    kit::footer(d, "both ends of the sweep");
    shot("knob_ends");
}

// --- An encoder with nothing on it: half the pages have one ----------------
void halfEmpty()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "HPF", 3, 5);
    kit::panelDuo(d, {"Freq", "3", 0.75f}, {});
    kit::footer(d, "J6  1-12 Brass  ch1");
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
    kit::footer(d, "J6  1-12 Brass  ch1  P42%");
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

    kit::footer(d, "DX  E.Piano 1  ch1  4op");
    shot("custom_body");
}

// --- A page whose value is a name: D5, JV and RD all have one -------------
void namePage()
{
    uishot::begin();
    Display& d = uishot::display();
    kit::header(d, "Patch", 0, 9);
    kit::panelName(d, "2-37 Nightfall", "S+S Ring", {"Voices", "8", 0.5f});
    kit::footer(d, "P41 B39 U0 A6/16 N142");
    shot("panel_name");
}

// --- The About screen, once instead of four times -------------------------
void about()
{
    uishot::begin();
    kit::about(uishot::display(), "PicoFaceOB", "1.8.0-4-ga39504b",
               "Press any button");
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
    about();

    std::printf("[ok]\n");
    return 0;
}
