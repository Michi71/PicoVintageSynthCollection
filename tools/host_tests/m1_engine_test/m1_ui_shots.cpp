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

static M1_Controller* g_c = nullptr;

// One gesture, delivered the way the core does: encoder turns in one tick; a
// press as a tick with the button down and one with it up; a long press as
// down, the long-press tick, and up.
static void act(int sel = 0, int a = 0, int b = 0, bool press = false, bool lng = false) {
    auto tick = [](InputState s) { g_ms += 30; s.nowMs = g_ms; g_c->input(s); };
    InputState s{};
    s.encoderDelta[0] = (int8_t)sel; s.encoderDelta[1] = (int8_t)a; s.encoderDelta[2] = (int8_t)b;
    tick(s);
    if (press || lng) {
        InputState d{};
        d.buttonDown[0] = true; d.buttonPressed[0] = true;
        tick(d);
        if (lng) { InputState l{}; l.buttonDown[0] = true; l.buttonLongPress[0] = true; tick(l); }
        tick(InputState{});
    }
    g_ms += 600;
}
#define in(...) act(__VA_ARGS__)

static void shot(M1_Controller& c, const char* name) {
    uishot::begin();
    c.draw(uishot::display(), g_ms);
    uishot::save(g_dir, name);
}

int main(int argc, char** argv) {
    int fails = 0;
    if (argc > 1) g_dir = argv[1];
    static M1_Bridge bridge;
    bridge.init();
    m1::setNameSources(m1rom::kMultisoundNames, m1rom::kDrumNames, bridge.memory().image().programs);
    static M1_Controller c(bridge);
    g_c = &c;
    static M1_SysEx sx(c);
    c.setSysEx(&sx);
    M1SettingsV1 s{};
    c.exportSettings(s);
    c.importSettings(s);
    in(0, 1);                 shot(c, "01_prog_piano16");
    in(0, 0, 1);              shot(c, "02_combi_mode");
    in(0, 0, -1);
    in(0, 0, 0, false, true); shot(c, "03_menu");
    in(0, 0, 0, true);        shot(c, "04_edit_name");
    in(0, 0, 0, true);        shot(c, "05_edit_pages");
    in(1);
    in(0, 0, 0, true);        shot(c, "06_edit_osc_basic");
    in(0, 0, 0, true); in(6); in(0, 0, 0, true);
    shot(c, "07_edit_vdf1_eg");
    in(0, 3, -2);             shot(c, "08_vdf1_eg_edited");
    in(0, 0, 0, true); in(20); in(0, 0, 0, true);
    shot(c, "09_edit_effects");
    in(4);                    shot(c, "10_edit_effects_2");
    in(0, 0, 0, true); in(30); in(0, 0, 0, true);
    shot(c, "11_edit_fx1_params");
    in(0, 0, 0, false, true);
    in(0, 0, 0, false, true); in(1); in(0, 0, 0, true);
    shot(c, "12_write");
    in(0, 0, 0, false, true);          // a long press cancels: nothing may be written
    if (memcmp(bridge.memory().program(1), m1rom::kPrograms + 143, 143) != 0) {
        printf("FAIL: a long press in Write wrote the program\n"); fails++;
    }
    in(0, 0, 0, false, true); in(3); in(0, 0, 0, true);
    shot(c, "13_global");
    if (c.m1Mode() != 4) { printf("FAIL: not in Global (mode %d)\n", c.m1Mode()); fails++; }
    in(0, 0, 0, false, true);          // a long press leaves Global
    if (c.m1Mode() != 2) { printf("FAIL: long press did not leave Global (mode %d)\n", c.m1Mode()); fails++; }
    in(0, 0, 0, false, true); in(4); in(0, 0, 0, true);
    in(1);                    shot(c, "14_drumkit");
    in(0, 0, 0, false, true);
    in(0, 0, 0, false, true); in(5); in(0, 0, 0, true);
    shot(c, "15_send_menu");
    in(0, 0, 0, false, true);
    in(0, 0, 1);
    in(0, 0, 0, false, true); in(0, 0, 0, true);
    in(0, 0, 0, true); in(2); in(0, 0, 0, true);
    shot(c, "16_combi_timbre1");
    in(0, 0, 0, false, true);
    in(6);                    shot(c, "17_diag");
    printf("%s\n", fails ? "navigation FAILED" : "navigation ok");
    return fails ? 1 : 0;
}
