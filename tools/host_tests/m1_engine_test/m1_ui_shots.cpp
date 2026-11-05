// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_ui_shots -- the PicoFaceM1 panel rendered to PBM on the host: the
// unmodified controller, bridge and engine against the real u8g2, driven by
// synthetic encoder input. Needs a configured ROM conversion (gendir).
#include "ui_shot.h"

#include <cstdio>
#include <cstring>

#include "M1_Bridge.h"
#include "M1_Controller.h"
#include "M1_SysEx.h"
#include "m1_params.h"
#include "m1_rom_tables.h"

// The PCM is not needed for screens.
extern "C" {
extern const uint8_t m1_pcm_blob[16];
extern const uint8_t m1_pcm_blob_end[1];
const uint8_t m1_pcm_blob[16] = {0};
const uint8_t m1_pcm_blob_end[1] = {0};
}
size_t m1_sysex_room() { return 64; }
void m1_sysex_write(const uint8_t*, size_t) {}

using picoface::ui::InputState;
static const char* g_dir = ".";
static uint32_t g_ms = 1000;

static InputState in(int sel = 0, int a = 0, int b = 0, bool press = false, bool lng = false) {
    InputState s{};
    s.encoderDelta[0] = (int8_t)sel; s.encoderDelta[1] = (int8_t)a; s.encoderDelta[2] = (int8_t)b;
    s.buttonPressed[0] = press; s.buttonLongPress[0] = lng;
    g_ms += 600;
    s.nowMs = g_ms;
    return s;
}

static void shot(M1_Controller& c, const char* name) {
    uishot::begin();
    c.draw(uishot::display(), g_ms);
    uishot::save(g_dir, name);
}

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    static M1_Bridge bridge;
    bridge.init();
    m1::setNameSources(m1rom::kMultisoundNames, m1rom::kDrumNames, bridge.memory().image().programs);
    static M1_Controller c(bridge);
    static M1_SysEx sx(c);
    c.setSysEx(&sx);
    M1SettingsV1 s{};
    c.exportSettings(s);
    c.importSettings(s);
    c.input(in(0, 1));                 shot(c, "01_prog_piano16");
    c.input(in(0, 0, 1));              shot(c, "02_combi_mode");
    c.input(in(0, 0, -1));
    c.input(in(0, 0, 0, false, true)); shot(c, "03_menu");
    c.input(in(0, 0, 0, true));        shot(c, "04_edit_name");
    c.input(in(0, 0, 0, true));        shot(c, "05_edit_pages");
    c.input(in(1));
    c.input(in(0, 0, 0, true));        shot(c, "06_edit_osc_basic");
    c.input(in(0, 0, 0, true)); c.input(in(6)); c.input(in(0, 0, 0, true));
    shot(c, "07_edit_vdf1_eg");
    c.input(in(0, 3, -2));             shot(c, "08_vdf1_eg_edited");
    c.input(in(0, 0, 0, true)); c.input(in(20)); c.input(in(0, 0, 0, true));
    shot(c, "09_edit_effects");
    c.input(in(4));                    shot(c, "10_edit_effects_2");
    c.input(in(0, 0, 0, true)); c.input(in(30)); c.input(in(0, 0, 0, true));
    shot(c, "11_edit_fx1_params");
    c.input(in(0, 0, 0, false, true));
    c.input(in(0, 0, 0, false, true)); c.input(in(1)); c.input(in(0, 0, 0, true));
    shot(c, "12_write");
    c.input(in(0, 0, 0, false, true));
    c.input(in(0, 0, 0, false, true)); c.input(in(3)); c.input(in(0, 0, 0, true));
    shot(c, "13_global");
    c.input(in(0, 0, 0, false, true));
    c.input(in(0, 0, 0, false, true)); c.input(in(4)); c.input(in(0, 0, 0, true));
    c.input(in(1));                    shot(c, "14_drumkit");
    c.input(in(0, 0, 0, false, true));
    c.input(in(0, 0, 0, false, true)); c.input(in(5)); c.input(in(0, 0, 0, true));
    shot(c, "15_send_menu");
    c.input(in(0, 0, 0, false, true));
    c.input(in(0, 0, 1));
    c.input(in(0, 0, 0, false, true)); c.input(in(0, 0, 0, true));
    c.input(in(0, 0, 0, true)); c.input(in(2)); c.input(in(0, 0, 0, true));
    shot(c, "16_combi_timbre1");
    c.input(in(0, 0, 0, false, true));
    c.input(in(6));                    shot(c, "17_diag");
    return 0;
}
