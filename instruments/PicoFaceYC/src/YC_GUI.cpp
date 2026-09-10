// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// YC_GUI.cpp -- the front panel pages, laid out by the shared kit.
//
// This file used to place every string itself. Now it only says what is on the
// page: a name, the value formatted the way the YC prints it, and where the
// value sits on its own range. The ranges are the ones YC_Controller clamps to
// -- drawbars 0..6 as on the instrument, the two 0..127 MIDI-scale controls,
// the short lists.

#include "YC_GUI.h"

#include <cstdio>

#include "picoface/ui_kit.h"

namespace kit = picoface::ui::kit;
using picoface::ui::Display;

namespace {

constexpr float kDrawbar = 6.0f;    // footage registers, as on the panel
constexpr float kMidi    = 127.0f;  // volume, distortion, reverb

// A drawbar: the number is what the player reads, the pointer is what they see.
kit::Param bar(const char* name, char* buf, size_t n, int value, float full)
{
    std::snprintf(buf, n, "%d", value);
    return { name, buf, (full > 0.0f) ? value / full : -1.0f };
}

} // namespace

void ycDrawScreen(Display& d, YC_Controller& controller)
{
    static const char* const kWaveNames[5]    = {"H", "V", "F", "A", "Y"};
    static const char* const kRotarySpeeds[4] = {"OFF", "STOP", "SLOW", "FAST"};

    const yc_engine_state_t& s = controller.state();
    char va[16], vb[16];

    // Whether percussion is on at all decides what the two values on that
    // page do, and there is no third value slot for it: it goes into the
    // title, where the eye lands first.
    const char* title = controller.pageName();
    if (controller.currentPage() == YcPage::PERCUSSION) {
        title = (s.perc_on != 0) ? "PERC ON" : "PERC OFF";
    }
    kit::header(d, title, (int) controller.currentPage(), (int) YcPage::COUNT);

    switch (controller.currentPage()) {
    case YcPage::VOLUME:
        kit::panelDuo(d, bar("Volume", va, sizeof va, s.volume, kMidi), {});
        break;

    case YcPage::WAVE_OCTAVE:
        std::snprintf(va, sizeof va, "%s", kWaveNames[s.wave]);
        std::snprintf(vb, sizeof vb, "%+d", (int) s.octave);
        // The waveform steps through five voicings and the octave through
        // five positions; neither is a sweep.
        kit::panelDuo(d, { "Wave", va }, { "Octave", vb });
        break;

    case YcPage::FOOT_16_513:
        kit::panelDuo(d, bar("16'",   va, sizeof va, s.footage[0], kDrawbar),
                         bar("5 1/3", vb, sizeof vb, s.footage[1], kDrawbar));
        break;

    case YcPage::FOOT_8_4:
        kit::panelDuo(d, bar("8'", va, sizeof va, s.footage[2], kDrawbar),
                         bar("4'", vb, sizeof vb, s.footage[3], kDrawbar));
        break;

    case YcPage::FOOT_223_2:
        kit::panelDuo(d, bar("2 2/3", va, sizeof va, s.footage[4], kDrawbar),
                         bar("2'",    vb, sizeof vb, s.footage[5], kDrawbar));
        break;

    case YcPage::FOOT_135_113:
        kit::panelDuo(d, bar("1 3/5", va, sizeof va, s.footage[6], kDrawbar),
                         bar("1 1/3", vb, sizeof vb, s.footage[7], kDrawbar));
        break;

    case YcPage::FOOT_1:
        kit::panelDuo(d, bar("1'", va, sizeof va, s.footage[8], kDrawbar), {});
        break;

    case YcPage::PERCUSSION:
        std::snprintf(va, sizeof va, "%s", s.perc_type == 0 ? "A" : "B");
        std::snprintf(vb, sizeof vb, "%d", s.perc_length);
        kit::panelDuo(d, { "Type", va },
                         { "Length", vb, s.perc_length / 4.0f });
        break;

    case YcPage::VIBCHO:
        std::snprintf(va, sizeof va, "%s", s.vibcho_select == 0 ? "Vibrato" : "Chorus");
        std::snprintf(vb, sizeof vb, "%d", s.vibcho_depth);
        kit::panelDuo(d, { "Mode", va },
                         { "Depth", vb, s.vibcho_depth / 4.0f });
        break;

    case YcPage::ROTARY:
        std::snprintf(va, sizeof va, "%s", kRotarySpeeds[s.rotary_speed]);
        kit::panelDuo(d, { "Speed", va }, {});
        break;

    case YcPage::EFFECT:
        kit::panelDuo(d, bar("Dist",   va, sizeof va, s.distortion, kMidi),
                         bar("Reverb", vb, sizeof vb, s.reverb,     kMidi));
        break;

    case YcPage::COUNT:
    default:
        break;
    }
}
