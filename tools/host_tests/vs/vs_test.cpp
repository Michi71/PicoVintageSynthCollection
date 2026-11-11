// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// vs_test.cpp - PicoFaceVS on the host: the program format, the parameter
// list, the flash store and the real VS_Controller, driven with the encoder
// events the panel makes. Its screens are written out as PBM.
//
//   ../ui/build_ui_kit.sh    (once, builds the u8g2 archive)
//   ./build_vs.sh && ./vs_test out && ../ui/to_png.sh out

#include "ui_shot.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "VS_Controller.h"
#include "provs/engine.h"
#include "vs_params.h"
#include "vs_store.h"

using picoface::ui::Button;
using picoface::ui::Encoder;
using picoface::ui::InputState;
using provs::Engine;

// ---- SysEx output, captured ------------------------------------------------
static std::vector<uint8_t> g_tx;
static size_t g_room = 1024;
size_t vs_sysex_room() { return g_room; }
void vs_sysex_write(const uint8_t* data, size_t len) { g_tx.insert(g_tx.end(), data, data + len); }

namespace {

int g_failed = 0;
#define CHECK(cond, ...) \
    do { if (!(cond)) { ++g_failed; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                        std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

const char* g_dir = ".";
uint32_t g_now = 1000;

struct Panel {
    Engine engine;
    vs::Store store;
    VS_Controller c{engine, store};

    void tick(const InputState& in) {
        c.poll(in.nowMs);
        c.input(in);
    }
    InputState idle() {
        InputState in{};
        g_now += 20;
        in.nowMs = g_now;
        return in;
    }
    void turn(Encoder e, int steps) {
        InputState in = idle();
        in.encoderDelta[(int)e] = (int8_t)steps;
        tick(in);
    }
    void click() {                       // SEL down, then up: a short press
        InputState in = idle();
        in.buttonDown[(int)Button::Sel] = in.buttonPressed[(int)Button::Sel] = true;
        tick(in);
        tick(idle());
    }
    void hold() {                        // SEL held until the long press fires
        InputState in = idle();
        in.buttonDown[(int)Button::Sel] = in.buttonPressed[(int)Button::Sel] = true;
        tick(in);
        in = idle();
        in.buttonDown[(int)Button::Sel] = in.buttonLongPress[(int)Button::Sel] = true;
        tick(in);
        tick(idle());
    }
    void wait(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 20) tick(idle());
    }
    void shot(const char* name) {
        c.draw(uishot::display(), g_now);
        uishot::save(g_dir, name);
    }
    float render(int frames) {           // returns the peak
        static float buf[2 * 256];
        float peak = 0.f;
        while (frames > 0) {
            const int n = frames > 256 ? 256 : frames;
            engine.render(buf, n);
            for (int i = 0; i < 2 * n; ++i) peak = std::fmax(peak, std::fabs(buf[i]));
            frames -= n;
        }
        return peak;
    }
};

uint32_t g_rng = 12345;
uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

void randomProgram(uint8_t* f) {
    for (int i = 0; i < Engine::kVsFields; ++i)
        f[i] = (uint8_t)(rnd() & ((1u << Engine::vsFieldBits(i)) - 1u));
}

// ---- the program format ----------------------------------------------------
void testFormat() {
    int bits = 0;
    for (int i = 0; i < Engine::kVsFields; ++i) bits += Engine::vsFieldBits(i);
    CHECK(bits == 647, "width table sums to %d bits, not 647", bits);

    for (int round = 0; round < 200; ++round) {
        uint8_t f[Engine::kVsFields], g[Engine::kVsFields], rec[Engine::kVsProgramBytes];
        randomProgram(f);
        Engine::packVsProgram(f, rec);
        Engine::unpackVsProgram(rec, g);
        CHECK(memcmp(f, g, sizeof f) == 0, "pack/unpack round %d", round);

        uint8_t msg[Engine::kVsDumpBytes];
        const int slot = (int)(rnd() % 100);
        CHECK(Engine::dumpVsProgram(f, slot, msg) == (size_t)Engine::kVsDumpBytes, "dump size");
        CHECK(Engine::loadVsSysex(msg, sizeof msg) == 1, "single dump not accepted");
        CHECK(memcmp(f, Engine::vsProgram(slot), sizeof f) == 0, "dump round %d, slot %d", round, slot);
    }
    Engine::resetVsBank();
    CHECK(Engine::presetCount() == 1, "bank after reset");
}

// ---- the parameter list ----------------------------------------------------
void testParams() {
    bool used[Engine::kVsFields] = {false};
    int blanks = 0;
    for (int i = 0; i < vs::paramCount(); ++i) {
        const vs::ParamDef& p = vs::param(i);
        if (p.kind == vs::Kind::Blank) { ++blanks; continue; }
        CHECK(p.field < Engine::kVsFields, "param %d: field %d", i, p.field);
        const int bits = Engine::vsFieldBits(p.field);
        used[p.field] = true;
        switch (p.kind) {
        case vs::Kind::Signed7: CHECK(bits == 7, "%s: field %d has %d bits", p.name, p.field, bits); break;
        case vs::Kind::Signed8: CHECK(bits == 8, "%s: field %d has %d bits", p.name, p.field, bits); break;
        case vs::Kind::Bit:     CHECK(p.bit < (1 << bits), "%s: mask %d in %d bits", p.name, p.bit, bits); break;
        default:                CHECK(p.max < (1 << bits), "%s: max %d in %d bits", p.name, p.max, bits); break;
        }
        uint8_t f[Engine::kVsFields] = {0};
        char lo[16], hi[16];
        float norm = 0.f;
        vs::paramStep(p, f, -1000);
        vs::paramFormat(p, f, lo, sizeof lo, &norm);
        CHECK(lo[0] != 0, "%s: empty text at minimum", p.name);
        CHECK(norm <= 0.001f, "%s: norm %f at minimum", p.name, norm);
        CHECK(vs::paramStep(p, f, 1000), "%s: does not move", p.name);
        vs::paramFormat(p, f, hi, sizeof hi, &norm);
        CHECK(strcmp(lo, hi) != 0, "%s: same text at both ends (%s)", p.name, lo);
        CHECK(norm < 0.f || norm > 0.999f, "%s: norm %f at maximum", p.name, norm);
        CHECK(f[p.field] < (1 << bits), "%s: value %d overflows %d bits", p.name, f[p.field], bits);
        CHECK(!vs::paramStep(p, f, 1), "%s: steps past its maximum", p.name);
    }
    for (int g = 0; g < vs::groupCount(); ++g) {
        CHECK((vs::groupFirst(g) & 1) == 0, "group %s starts on an odd index", vs::groupName(g));
        CHECK(vs::groupOf(vs::groupFirst(g)) == g, "groupOf/groupFirst disagree at %s", vs::groupName(g));
    }
    int unused = 0;
    for (int i = 0; i < Engine::kVsFields; ++i) if (!used[i]) ++unused;
    std::printf("params: %d entries (%d blank) in %d groups, %d of %d fields not editable\n",
                vs::paramCount(), blanks, vs::groupCount(), unused, Engine::kVsFields);
}

// ---- the panel -------------------------------------------------------------
void testPanel() {
    memset(vs_store_sim_flash, 0xFF, sizeof vs_store_sim_flash);
    Engine::resetVsBank();
    Panel p;
    p.engine.init(44100.0);
    p.c.init(nullptr, 0);
    p.shot("vs_01_program");

    // A note sounds.
    p.engine.noteOn(60, 100);
    CHECK(p.render(8000) > 0.01f, "silent note");
    p.engine.noteOff(60);
    p.engine.allNotesOff();

    // The play pages.
    p.turn(Encoder::Sel, 1);  p.shot("vs_02_level");
    p.turn(Encoder::ParamA, -10);
    p.turn(Encoder::ParamB, 2);
    p.turn(Encoder::Sel, 1);  p.turn(Encoder::ParamA, 1);  p.shot("vs_03_arp");
    p.turn(Encoder::Sel, 1);  p.shot("vs_04_arp_chord");
    p.turn(Encoder::Sel, 1);  p.shot("vs_05_diag");
    p.turn(Encoder::Sel, 1);
    CHECK(p.c.page() == VS_Controller::Page::Program, "pages do not wrap");

    // The arpeggiator, switched on above, plays a held key by itself.
    p.engine.noteOn(60, 100);
    p.engine.noteOn(64, 100);
    CHECK(p.render(44100) > 0.01f, "arpeggiator silent");
    p.engine.noteOff(60);
    p.engine.noteOff(64);
    p.turn(Encoder::Sel, 2);  p.turn(Encoder::ParamA, -1);  p.turn(Encoder::Sel, -2);
    p.engine.allNotesOff();
    p.render(44100 * 3);
    CHECK(p.engine.activeVoices() == 0, "%d voices still sounding", p.engine.activeVoices());

    // Menu -> Edit Program: change wave A.
    p.hold();                 p.shot("vs_06_menu");
    p.click();                p.shot("vs_07_edit_osc");
    const uint8_t waveBefore = p.c.editBuffer()[0];
    p.turn(Encoder::ParamA, 5);
    CHECK(p.c.edited(), "edit not noticed");
    CHECK(p.c.editBuffer()[0] == waveBefore + 5, "wave A %d", p.c.editBuffer()[0]);
    p.click();                p.shot("vs_08_groups");
    p.turn(Encoder::Sel, 4);  p.click();   p.shot("vs_09_edit_mix_env");
    p.turn(Encoder::Sel, 3);  p.turn(Encoder::ParamA, -20);  p.shot("vs_10_edit_mix_point");
    p.hold();                 p.shot("vs_11_program_edited");
    CHECK(Engine::vsProgram(0)[0] == waveBefore, "edit reached the bank before Write");

    // Menu -> Write Program, to slot 5.
    uint8_t edited[Engine::kVsFields];
    memcpy(edited, p.c.editBuffer(), sizeof edited);
    p.hold();  p.turn(Encoder::Sel, 1);  p.click();
    p.turn(Encoder::ParamA, 5);  p.shot("vs_12_write");
    p.click();                   p.shot("vs_13_written");
    CHECK(Engine::presetCount() == 6, "bank holds %d programs after Write", Engine::presetCount());
    CHECK(memcmp(Engine::vsProgram(5), edited, sizeof edited) == 0, "written program differs");
    CHECK(!p.c.edited(), "still marked edited after Write");

    // The store goes to flash once things have been quiet, and comes back.
    CHECK(p.store.dirty(), "store not dirty after Write");
    p.wait(2000);
    CHECK(!p.store.dirty(), "store not written");
    Engine::resetVsBank();
    vs::Store again;
    CHECK(again.load(), "stored bank not found");
    CHECK(Engine::presetCount() == 6, "restored bank holds %d programs", Engine::presetCount());
    CHECK(memcmp(Engine::vsProgram(5), edited, sizeof edited) == 0, "restored program differs");
    vs_store_sim_flash[100] ^= 1;
    CHECK(!again.load(), "corrupt store accepted");
    vs_store_sim_flash[100] ^= 1;

    // Menu -> Send SysEx -> This Program.
    g_tx.clear();
    p.hold();  p.turn(Encoder::Sel, 3);  p.click();  p.shot("vs_14_send");
    p.click();
    p.wait(100);
    CHECK(g_tx.size() == (size_t)Engine::kVsDumpBytes, "program dump is %zu bytes", g_tx.size());
    CHECK(g_tx.size() > 4 && g_tx[0] == 0xF0 && g_tx[3] == 5 && g_tx.back() == 0xF7, "program dump frame");

    // Menu -> Send SysEx -> VS Bank, then read it back into an empty bank.
    g_tx.clear();
    p.hold();  p.turn(Encoder::Sel, 3);  p.click();  p.turn(Encoder::Sel, 1);  p.click();
    p.wait(3000);
    CHECK(g_tx.size() == 16405, "bank dump is %zu bytes", g_tx.size());
    CHECK(!p.c.sending(), "bank dump never ends");
    Engine::resetVsBank();
    CHECK(Engine::loadVsSysex(g_tx.data(), g_tx.size()) == 100, "bank dump not accepted");
    CHECK(memcmp(Engine::vsProgram(5), edited, sizeof edited) == 0, "program differs after bank dump");

    // A dump coming in is taken, shown and stored.
    uint8_t f[Engine::kVsFields], msg[Engine::kVsDumpBytes];
    randomProgram(f);
    Engine::dumpVsProgram(f, 7, msg);
    p.c.sysExMessage(msg, sizeof msg);
    CHECK(memcmp(Engine::vsProgram(7), f, sizeof f) == 0, "received program differs");
    CHECK(p.store.dirty(), "store not dirty after a received dump");
    p.shot("vs_15_received");

    // Factory reset, with nothing embedded: back to the init program.
    p.wait(2000);
    p.hold();  p.turn(Encoder::Sel, 4);  p.click();  p.shot("vs_16_reset_confirm");
    p.click();
    CHECK(Engine::presetCount() == 1, "bank holds %d programs after reset", Engine::presetCount());

    // Settings survive a round trip.
    VsSettingsV2 s{}, t{};
    p.c.exportSettings(s);
    Panel q;
    q.engine.init(44100.0);
    q.c.init(nullptr, 0);
    q.c.importSettings(s);
    q.c.exportSettings(t);
    CHECK(memcmp(&s, &t, sizeof s) == 0, "settings round trip");
    CHECK(s.volume == 60 && s.fx == 2, "settings: volume %d fx %d", s.volume, s.fx);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    testFormat();
    testParams();
    testPanel();
    if (g_failed) {
        std::printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("all PicoFaceVS checks passed\n");
    return 0;
}
