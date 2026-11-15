// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// mt_ui_shots.cpp -- the PicoFaceMT tape pages as PBM frames, one per pair of
// bank entries, drawn through the same ui_kit calls the instrument makes. The
// point is the tape names: the value cell is 50 pixels wide, and a name that
// does not fit shows here rather than on the hardware.
//
//   mt_ui_shots <outdir>
#include "ui_shot.h"
#include "picoface/ui_kit.h"
#include "mello/bank.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mt_ui_shots <outdir>\n"); return 1; }
    namespace kit = picoface::ui::kit;
    picoface::ui::Display& d = uishot::display();
    for (int i = 0; i < mello::kBankSize; i += 2) {
        const int j = i + 1 < mello::kBankSize ? i + 1 : i;
        uishot::begin();
        kit::header(d, "Tapes A B", 1, 10);
        kit::panelDuo(d, {"Tape A", mello::kBank[i].label, -1.0f}, {"Tape B", mello::kBank[j].label, -1.0f});
        char name[32];
        std::snprintf(name, sizeof name, "mt_tapes_%02d", i);
        uishot::save(argv[1], name);
    }
    uishot::begin();
    kit::header(d, "Tape", 3, 10);
    kit::panelDuo(d, {"Limit", "Off", -1.0f}, {"Rewind", "600ms", 0.3f});
    uishot::save(argv[1], "mt_tape");
    // The longest values beside a knob.
    uishot::begin();
    kit::header(d, "Keys", 6, 10);
    kit::panelDuo(d, {"Attack", "500ms", 1.0f}, {"Release", "3.0 s", 1.0f});
    uishot::save(argv[1], "mt_keys");
    uishot::begin();
    kit::header(d, "Tune", 8, 10);
    kit::panelDuo(d, {"Pitch", "+200", 1.0f}, {"Bend", "12 st", 1.0f});
    uishot::save(argv[1], "mt_tune");
    return 0;
}
