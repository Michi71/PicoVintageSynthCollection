// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// vs_test.cpp - PicoFaceVS on the host: the program format, the parameter
// list, the arpeggiator's two sequences, the user waves, the dumps in both
// directions, the flash store and the real VS_Controller, driven with the
// encoder events the panel makes. Its screens are written out as PBM.
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
constexpr int kN = Engine::kWaveSamples;

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
    void press(Button b) {
        InputState in = idle();
        in.buttonDown[(int)b] = in.buttonPressed[(int)b] = true;
        tick(in);
        tick(idle());
    }
    void click() { press(Button::Sel); }     // SEL down, then up: a short press
    void hold() {                            // SEL held until the long press fires
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
    float render(int frames) {               // returns the peak
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
    void sysex(const std::vector<uint8_t>& m) { c.sysExMessage(m.data(), m.size()); }
};

uint32_t g_rng = 12345;
uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

void randomProgram(uint8_t* f) {
    for (int i = 0; i < Engine::kVsFields; ++i)
        f[i] = (uint8_t)(rnd() & ((1u << Engine::vsFieldBits(i)) - 1u));
}

void randomWave(int16_t* w) {
    for (int n = 0; n < kN; ++n) w[n] = (int16_t)((int)(rnd() % 4096) - 2048);
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

// ---- the arpeggiator's two sequences (@0x6326) -----------------------------
void testArp() {
    Engine e;
    e.init(44100.0);
    provs::Params p = e.params();
    static float buf[2 * 256];
    auto run = [&](int frames) {             // returns the peak
        float pk = 0.f;
        for (; frames > 0; frames -= 256) {
            e.render(buf, 256);
            for (float v : buf) pk = std::fmax(pk, std::fabs(v));
        }
        return pk;
    };
    auto set = [&](int keyMode, int split, bool layer, int mode) {
        p.keyMode = keyMode; p.splitPoint = 60; p.arpSplit = split; p.arpLayer = layer; p.arpMode = mode;
        e.setParams(p);
        e.setLayer2(p);
        e.setArpLatch(false);
        e.setArp(false);
        e.allNotesOff();
        e.setArp(true);
    };

    // One sequence; a released key is forgotten.
    set(provs::kKeyNormal, 0, false, provs::kArpNormal);
    e.noteOn(60, 100); e.noteOn(64, 100);
    CHECK(e.arpKeyCount(0) == 2 && e.arpKeyCount(1) == 0, "plain: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    e.noteOff(64);
    CHECK(e.arpKeyCount(0) == 1, "released key stays");

    // Latch freezes it; with Layer off further keys are played, not arpeggiated.
    e.noteOn(64, 100);
    e.setArpLatch(true);
    CHECK(e.arpLatched(), "not latched");
    e.noteOff(60); e.noteOff(64);
    CHECK(e.arpKeyCount(0) == 2, "latched keys lost on release");
    e.noteOn(72, 100);
    CHECK(e.arpKeyCount(0) == 2 && e.arpKeyCount(1) == 0, "key entered a latched sequence");
    run(4410);
    e.noteOff(72);

    // Layer on: the same key is an overdub in the second sequence, while held.
    p.arpLayer = true;
    e.setParams(p);
    e.noteOn(72, 100); e.noteOn(76, 100);
    CHECK(e.arpKeyCount(0) == 2 && e.arpKeyCount(1) == 2, "overdub: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    run(44100);
    e.noteOff(72); e.noteOff(76);
    CHECK(e.arpKeyCount(1) == 0, "overdub keys stay after release in normal mode");
    CHECK(e.arpKeyCount(0) == 2, "first sequence disturbed by the overdub");

    // Unlatch: what is no longer held goes.
    e.setArpLatch(false);
    CHECK(e.arpKeyCount(0) == 0 && !e.arpLatched(), "unlatch keeps %d keys", e.arpKeyCount(0));

    // Latch set with nothing held waits for a chord and takes it on release.
    e.setArpLatch(true);
    CHECK(!e.arpLatched(), "latched with nothing to latch");
    e.noteOn(60, 100); e.noteOn(67, 100);
    e.noteOff(60);
    CHECK(e.arpKeyCount(0) == 2 && !e.arpLatched(), "armed latch: %d keys", e.arpKeyCount(0));
    e.noteOff(67);
    CHECK(e.arpKeyCount(0) == 2 && e.arpLatched(), "chord not latched on release");
    CHECK(run(44100) > 0.01f, "latched arpeggio silent");

    // Split programs: Right, Left, Dual.
    set(provs::kKeySplit, 1, false, provs::kArpNormal);
    e.noteOn(48, 100); e.noteOn(72, 100);
    CHECK(e.arpKeyCount(0) == 1 && e.arpKeyCount(1) == 0, "split right: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    set(provs::kKeySplit, 2, false, provs::kArpNormal);
    e.noteOn(48, 100); e.noteOn(50, 100); e.noteOn(72, 100);
    CHECK(e.arpKeyCount(0) == 2 && e.arpKeyCount(1) == 0, "split left: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    set(provs::kKeySplit, 3, false, provs::kArpNormal);
    e.noteOn(48, 100); e.noteOn(50, 100); e.noteOn(72, 100);
    CHECK(e.arpKeyCount(0) == 1 && e.arpKeyCount(1) == 2, "dual: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    CHECK(run(44100) > 0.01f, "dual arpeggio silent");
    // Dual, latched, Layer on: both sides are in use, nothing can be overdubbed.
    e.setArpLatch(true);
    p.arpLayer = true;
    e.setParams(p);
    e.noteOn(74, 100); e.noteOn(40, 100);
    CHECK(e.arpKeyCount(0) == 1 && e.arpKeyCount(1) == 2, "dual overdub: %d/%d", e.arpKeyCount(0), e.arpKeyCount(1));
    e.noteOff(74); e.noteOff(40);

    // Auto-latch: keys add up while one is held, a new chord starts over.
    set(provs::kKeyNormal, 0, false, provs::kArpAutoLatch);
    e.noteOn(60, 100); e.noteOn(64, 100); e.noteOff(60); e.noteOn(67, 100);
    CHECK(e.arpKeyCount(0) == 3, "auto-latch holds %d keys", e.arpKeyCount(0));
    e.noteOff(64); e.noteOff(67);
    CHECK(e.arpKeyCount(0) == 3, "auto-latch forgot on release");
    e.noteOn(72, 100);
    CHECK(e.arpKeyCount(0) == 1, "auto-latch did not start over: %d", e.arpKeyCount(0));
    e.noteOff(72);

    // Mono voicing: one voice per sequence, and nothing left hanging at the end.
    set(provs::kKeyNormal, 0, false, provs::kArpNormal);
    p.arpPoly = false;
    e.setParams(p);
    e.noteOn(60, 100); e.noteOn(64, 100); e.noteOn(67, 100);
    CHECK(run(44100) > 0.01f, "mono arpeggio silent");
    e.noteOff(60); e.noteOff(64); e.noteOff(67);
    e.setArp(false);
    run(44100 * 2);
    CHECK(e.activeVoices() == 0, "%d voices hang after the arpeggiator", e.activeVoices());
}

// ---- user waves ------------------------------------------------------------
void testWaves() {
    Engine::clearUserWaves();

    // The 192-byte record.
    for (int round = 0; round < 50; ++round) {
        int16_t w[kN], v[kN];
        uint8_t rec[Engine::kWaveRecordBytes];
        randomWave(w);
        Engine::packWave(w, rec);
        Engine::unpackWave(rec, v);
        CHECK(memcmp(w, v, sizeof w) == 0, "wave record round %d", round);
    }

    // The mix (@0x7222). At corner A it is wave A; harmonic 2 reads every
    // second sample; four equal waves in the center give the wave back.
    int16_t sine[kN], out[kN];
    Engine::waveSamples(32, sine);
    int peak = 0;
    for (int n = 0; n < kN; ++n) peak = std::max(peak, std::abs((int)sine[n]));
    CHECK(peak > 1800 && peak < 2100, "wave 32 peaks at %d", peak);
    const int same[4] = {32, 32, 32, 32}, one[4] = {1, 1, 1, 1}, two[4] = {2, 1, 1, 1};
    const int mixed[4] = {32, 40, 50, 60};
    Engine::mixWave(mixed, one, -1.f, 0.f, out);
    for (int n = 0; n < kN; ++n) CHECK(std::abs(out[n] - sine[n]) <= 1, "corner A, sample %d: %d/%d", n, out[n], sine[n]);
    Engine::mixWave(same, two, -1.f, 0.f, out);
    for (int n = 0; n < kN; ++n) CHECK(std::abs(out[n] - sine[(2 * n) & 127]) <= 1, "harmonic 2, sample %d", n);
    Engine::mixWave(same, one, 0.f, 0.f, out);
    for (int n = 0; n < kN; ++n) CHECK(std::abs(out[n] - sine[n]) <= 2, "center, sample %d: %d/%d", n, out[n], sine[n]);
    // The four corners are the four waves - scaled down to twelve bits where
    // a reconstructed wave overshoots them.
    int16_t src[kN];
    const float corner[4][2] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
    for (int k = 0; k < 4; ++k) {
        Engine::waveSamples(mixed[k], src);
        int top = 0;
        for (int n = 0; n < kN; ++n) top = std::max(top, std::abs((int)src[n]));
        const double fit = top > 2047 ? 2047.0 / top : 1.0;
        Engine::mixWave(mixed, one, corner[k][0], corner[k][1], out);
        double worst = 0;
        int most = 0;
        for (int n = 0; n < kN; ++n) {
            worst = std::max(worst, std::fabs(out[n] - src[n] * fit));
            most = std::max(most, std::abs((int)out[n]));
        }
        CHECK(worst <= 2.0 && most <= 2048, "corner %c is off by %.1f, peak %d of %d", 'A' + k, worst, most, top);
    }

    // A written slot: level 0 is the wave, the others lose what is above
    // their harmonic and nothing below it.
    int16_t w[kN];
    randomWave(w);
    CHECK(Engine::userWaveMask() == 0, "slots written before any was");
    Engine::setUserWave(5, w);
    CHECK(Engine::userWaveMask() == (1u << 5), "mask %x", Engine::userWaveMask());
    CHECK(Engine::buildUserWaves(10), "ten harmonics finish a wave");
    while (Engine::buildUserWaves(8)) {}
    for (int n = 0; n < kN; ++n) CHECK(Engine::userWaveTable(5, 0)[n] == w[n] * 8, "level 0, sample %d", n);
    const int limit[4] = {64, 31, 15, 7};
    for (int level = 1; level < 4; ++level) {
        const int16_t* t = Engine::userWaveTable(5, level);
        double worstKept = 0, worstCut = 0;
        for (int h = 0; h <= 63; ++h) {
            double re = 0, im = 0, rre = 0, rim = 0;
            for (int n = 0; n < kN; ++n) {
                const double a = 6.283185307179586 * h * n / kN;
                re += t[n] / 8.0 * std::cos(a);  im += t[n] / 8.0 * std::sin(a);
                rre += w[n] * std::cos(a);       rim += w[n] * std::sin(a);
            }
            const double diff = std::hypot(re - rre, im - rim) / kN, mag = std::hypot(re, im) / kN;
            if (h <= limit[level]) worstKept = std::max(worstKept, diff); else worstCut = std::max(worstCut, mag);
        }
        CHECK(worstKept < 0.5 && worstCut < 0.5, "level %d: kept off by %.2f, cut leaves %.2f", level, worstKept, worstCut);
    }

    // It sounds, and an empty slot does not (this build has no Mini waves).
    Engine e;
    e.init(44100.0);
    static float buf[2 * 256];
    auto peakOf = [&](int wave) {
        provs::Params p = e.params();
        for (provs::OscParams& o : p.osc) o.wave = wave;
        e.setParams(p);
        e.allNotesOff();
        e.noteOn(60, 100);
        float pk = 0.f;
        for (int k = 0; k < 40; ++k) {
            e.render(buf, 256);
            for (float v : buf) pk = std::fmax(pk, std::fabs(v));
        }
        e.noteOff(60);
        return pk;
    };
    CHECK(peakOf(5) > 0.01f, "user wave 5 silent");
    CHECK(peakOf(6) < 0.0001f, "empty slot 6 sounds");
    Engine::clearUserWave(5);
    CHECK(peakOf(5) < 0.0001f, "cleared slot sounds");
}

// One sample dump packet of 60 words, as a sender makes it.
std::vector<uint8_t> sdsPacket(int packet, const int16_t* wave, bool zeroChecksum) {
    std::vector<uint8_t> m = {0xF0, 0x7E, 0x00, 0x02, (uint8_t)packet};
    for (int k = 0; k < 60; ++k) {
        const int word = packet * 60 + k;
        const int bits = word < kN ? (wave[word] & 0xFFF) : 0;
        m.push_back((uint8_t)(bits >> 5));
        m.push_back((uint8_t)((bits & 0x1F) << 2));
    }
    uint8_t sum = 0;
    for (size_t i = 1; i < m.size(); ++i) sum ^= m[i];
    m.push_back(zeroChecksum ? 0 : (sum & 0x7F));
    m.push_back(0xF7);
    return m;
}

// ---- the panel -------------------------------------------------------------
void testPanel() {
    memset(vs_store_sim_flash, 0xFF, sizeof vs_store_sim_flash);
    Engine::resetVsBank();
    Engine::clearUserWaves();
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
    p.turn(Encoder::Sel, 1);  p.shot("vs_03_joystick");
    p.turn(Encoder::ParamA, 8);          // right, toward C
    p.turn(Encoder::ParamB, 4);          // up, toward B
    CHECK(std::fabs(p.engine.params().mixX - 0.5f) < 1e-6f && std::fabs(p.engine.params().mixY + 0.25f) < 1e-6f,
          "joystick at %f, %f", p.engine.params().mixX, p.engine.params().mixY);
    p.shot("vs_04_joystick_moved");
    // ... and it is heard: the same note at corner A and at corner C is a
    // different sound (the init program has four different waves).
    auto tone = [&](std::vector<float>& x) {
        static float b[2 * 256];
        p.engine.allNotesOff();
        p.engine.noteOn(60, 100);
        for (int k = 0; k < 40; ++k) {
            p.engine.render(b, 256);
            for (int i = 0; i < 256; ++i) x.push_back(b[2 * i]);
        }
        p.engine.noteOff(60);
        p.engine.allNotesOff();
    };
    std::vector<float> atA, atC;
    p.turn(Encoder::ParamA, -24);  p.turn(Encoder::ParamB, -4);  tone(atA);
    p.turn(Encoder::ParamA, 32);   tone(atC);
    double diff = 0, ref = 0;
    for (size_t n = 0; n < atA.size(); ++n) { diff += std::fabs(atA[n] - atC[n]); ref += std::fabs(atA[n]); }
    CHECK(ref > 0 && diff > 0.3 * ref, "joystick A and C sound alike: difference %.3f of %.3f", diff, ref);
    p.press(Button::ParamA);             // back to the center
    CHECK(p.engine.params().mixX == 0.f && p.engine.params().mixY == 0.f, "joystick not centered");
    p.turn(Encoder::Sel, 1);  p.turn(Encoder::ParamA, 1);  p.shot("vs_05_arp");
    p.turn(Encoder::Sel, 1);  p.shot("vs_06_arp_chord");
    p.turn(Encoder::Sel, 1);  p.shot("vs_07_midi");
    // Mode 1 takes any channel; mode 3 only the right one.
    CHECK(p.c.noteOn(9, 60, 100) && p.c.noteOff(9, 60), "omni refuses a channel");
    p.turn(Encoder::ParamA, 1);  p.turn(Encoder::ParamB, 2);
    CHECK(p.c.noteOn(2, 60, 100) && p.c.noteOff(2, 60) && !p.c.noteOn(0, 60, 100) && !p.c.noteOn(3, 60, 100),
          "mode 3 does not select channel 3");
    // Mode 4: eight channels from the right one, a voice each; a second note
    // on a channel moves the voice (legato), the channel below is global.
    p.turn(Encoder::ParamA, 1);
    p.engine.allNotesOff();
    CHECK(p.c.noteOn(2, 60, 100) && p.c.noteOn(2, 64, 100) && p.c.noteOn(9, 67, 100), "mode 4 refuses its channels");
    p.render(256);
    CHECK(p.engine.activeVoices() == 2, "mode 4: %d voices for two channels", p.engine.activeVoices());
    CHECK(!p.c.noteOn(10, 60, 100) && !p.c.noteOn(1, 60, 100), "mode 4 plays outside its channels");
    p.c.noteOff(2, 60);                  // the first note of the legato line: nothing ends
    p.c.noteOff(2, 64);  p.c.noteOff(9, 67);
    p.engine.allNotesOff();
    p.turn(Encoder::ParamA, -2);  p.turn(Encoder::ParamB, -2);
    CHECK(p.c.noteOn(0, 60, 100) && p.c.noteOff(0, 60), "not back at mode 1");
    p.engine.allNotesOff();
    p.turn(Encoder::Sel, 1);  p.shot("vs_08_diag");
    p.turn(Encoder::Sel, 1);
    CHECK(p.c.page() == VS_Controller::Page::Program, "pages do not wrap");

    // The arpeggiator, switched on above, plays a held key by itself.
    p.engine.noteOn(60, 100);
    p.engine.noteOn(64, 100);
    CHECK(p.render(44100) > 0.01f, "arpeggiator silent");
    p.engine.noteOff(60);
    p.engine.noteOff(64);
    // The Layer switch on the ARP page is the program's parameter.
    p.turn(Encoder::Sel, 3);  p.turn(Encoder::ParamB, 1);
    CHECK(p.c.editBuffer()[94] == 1 && p.c.edited() && p.engine.params().arpLayer, "Layer not switched on");
    p.turn(Encoder::ParamB, -1);
    p.turn(Encoder::ParamA, -1);  p.turn(Encoder::Sel, -3);
    p.engine.allNotesOff();
    p.render(44100 * 3);
    CHECK(p.engine.activeVoices() == 0, "%d voices still sounding", p.engine.activeVoices());
    p.turn(Encoder::ParamA, 1);  p.turn(Encoder::ParamA, -1);     // select again: drops the edit

    // Menu -> Edit Program: change wave A.
    p.hold();                 p.shot("vs_09_menu");
    p.click();                p.shot("vs_10_edit_osc");
    const uint8_t waveBefore = p.c.editBuffer()[0];
    p.turn(Encoder::ParamA, 5);
    CHECK(p.c.edited(), "edit not noticed");
    CHECK(p.c.editBuffer()[0] == waveBefore + 5, "wave A %d", p.c.editBuffer()[0]);
    p.click();                p.shot("vs_11_groups");
    p.turn(Encoder::Sel, 4);  p.click();   p.shot("vs_12_edit_mix_env");
    p.turn(Encoder::Sel, 3);  p.turn(Encoder::ParamA, -20);  p.shot("vs_13_edit_mix_point");
    p.hold();                 p.shot("vs_14_program_edited");
    CHECK(Engine::vsProgram(0)[0] == waveBefore, "edit reached the bank before Write");

    // Menu -> Write Program, to slot 5.
    uint8_t edited[Engine::kVsFields];
    memcpy(edited, p.c.editBuffer(), sizeof edited);
    p.hold();  p.turn(Encoder::Sel, 1);  p.click();
    p.turn(Encoder::ParamA, 5);  p.shot("vs_15_write");
    p.click();                   p.shot("vs_16_written");
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
    vs_store_sim_flash[7 * 4096 + 100] ^= 1;
    CHECK(!again.load(), "corrupt store accepted");
    vs_store_sim_flash[7 * 4096 + 100] ^= 1;

    // Menu -> Edit Waveform: wave B a fifth harmonic, the mix moved, stored as wave 7.
    p.hold();  p.turn(Encoder::Sel, 3);  p.click();   p.shot("vs_17_wave_a");
    p.engine.noteOn(60, 100);
    CHECK(p.render(8000) > 0.01f, "Edit Waveform program silent");
    p.engine.noteOff(60);
    p.turn(Encoder::Sel, 1);  p.turn(Encoder::ParamA, 8);  p.turn(Encoder::ParamB, 3);   p.shot("vs_18_wave_b");
    p.turn(Encoder::Sel, 3);  p.turn(Encoder::ParamA, -4);  p.turn(Encoder::ParamB, 4);  p.shot("vs_19_wave_mix");
    p.turn(Encoder::Sel, 1);  p.turn(Encoder::ParamA, 7);   p.shot("vs_20_wave_store");
    p.click();                                              p.shot("vs_21_wave_stored");
    CHECK(Engine::userWaveMask() == (1u << 7), "wave 7 not stored: mask %x", Engine::userWaveMask());
    int16_t made[kN], expect[kN];
    Engine::userWave(7, made);
    const int wv[4] = {32, 40, 32, 32}, hm[4] = {1, 5, 3, 4};
    Engine::mixWave(wv, hm, -0.25f, -0.25f, expect);
    CHECK(memcmp(made, expect, sizeof made) == 0, "stored wave is not the mix");
    p.hold();
    CHECK(std::fabs(p.engine.params().mixX) < 1e-6f && p.engine.params().osc[1].harmonic == 1,
          "the program did not come back after Edit Waveform");

    // The waves go to flash too, and come back.
    p.wait(2000);
    CHECK(!p.store.dirty(), "waves not written");
    Engine::clearUserWaves();
    CHECK(again.loadWaves(), "stored waves not found");
    int16_t back[kN];
    CHECK(Engine::userWave(7, back) && memcmp(back, made, sizeof made) == 0, "restored wave differs");
    CHECK(Engine::userWaveMask() == (1u << 7), "restored mask %x", Engine::userWaveMask());

    // Menu -> Send SysEx -> This Program.
    g_tx.clear();
    p.hold();  p.turn(Encoder::Sel, 6);  p.click();  p.shot("vs_22_send");
    p.click();
    p.wait(100);
    CHECK(g_tx.size() == (size_t)Engine::kVsDumpBytes, "program dump is %zu bytes", g_tx.size());
    CHECK(g_tx.size() > 4 && g_tx[0] == 0xF0 && g_tx[3] == 5 && g_tx.back() == 0xF7, "program dump frame");

    // Menu -> Send SysEx -> VS Bank, then read it back into an empty bank.
    g_tx.clear();
    p.hold();  p.turn(Encoder::Sel, 6);  p.click();  p.turn(Encoder::Sel, 1);  p.click();
    p.wait(3000);
    CHECK(g_tx.size() == 16405, "bank dump is %zu bytes", g_tx.size());
    CHECK(!p.c.sending(), "bank dump never ends");
    Engine::resetVsBank();
    CHECK(Engine::loadVsSysex(g_tx.data(), g_tx.size()) == 100, "bank dump not accepted");
    CHECK(memcmp(Engine::vsProgram(5), edited, sizeof edited) == 0, "program differs after bank dump");

    // Dump requests (F0 01 00 nn F7): a program, all programs, all waves.
    g_tx.clear();
    p.sysex({0xF0, 0x01, 0x00, 0x05, 0xF7});
    p.wait(100);
    uint8_t want[Engine::kVsDumpBytes];
    Engine::dumpVsProgram(Engine::vsProgram(5), 5, want);
    CHECK(g_tx.size() == sizeof want && memcmp(g_tx.data(), want, sizeof want) == 0, "program request: %zu bytes", g_tx.size());
    g_tx.clear();
    p.sysex({0xF0, 0x01, 0x00, 0x64, 0xF7});
    p.wait(3000);
    CHECK(g_tx.size() == 16405 && g_tx[3] == 0x64, "bank request: %zu bytes", g_tx.size());
    g_tx.clear();
    p.sysex({0xF0, 0x01, 0x00, 0x7F, 0xF7});
    p.wait(3000);
    CHECK(g_tx.size() == 12293 && g_tx[3] == 0x7F && g_tx.back() == 0xF7, "wave request: %zu bytes", g_tx.size());

    // That dump, sent back into empty slots, is the same waves again.
    const std::vector<uint8_t> waves = g_tx;
    Engine::clearUserWaves();
    p.sysex(waves);
    CHECK(Engine::userWaveMask() == (1u << 7), "wave dump read back: mask %x", Engine::userWaveMask());
    CHECK(Engine::userWave(7, back) && memcmp(back, made, sizeof made) == 0, "wave differs after its dump");

    // Sample dump standard, coming in: header and three packets for wave 9,
    // the second with the 00 checksum a VS sends.
    int16_t sent[kN];
    randomWave(sent);
    g_tx.clear();
    p.sysex({0xF0, 0x7E, 0x00, 0x01, 0x09, 0x00, 0x0C, 0, 0, 0, 0x7F, 0x01, 0, 0, 0, 0, 0x7F, 0x01, 0, 0, 0xF7});
    for (int k = 0; k < 3; ++k) p.sysex(sdsPacket(k, sent, k == 1));
    CHECK(Engine::userWave(9, back) && memcmp(back, sent, sizeof sent) == 0, "sample dump: wave differs");
    // acknowledged: the header, two packets; then cancelled, as the VS does
    const uint8_t hs[] = {0xF0, 0x7E, 0x00, 0x7F, 0x00, 0xF7, 0xF0, 0x7E, 0x00, 0x7F, 0x00, 0xF7,
                          0xF0, 0x7E, 0x00, 0x7F, 0x01, 0xF7, 0xF0, 0x7E, 0x00, 0x7D, 0x02, 0xF7};
    CHECK(g_tx.size() == sizeof hs && memcmp(g_tx.data(), hs, sizeof hs) == 0, "sample dump handshake: %zu bytes", g_tx.size());
    // a packet with a wrong checksum is refused
    g_tx.clear();
    p.sysex({0xF0, 0x7E, 0x00, 0x01, 0x0A, 0x00, 0x0C, 0, 0, 0, 0x7F, 0x01, 0, 0, 0, 0, 0x7F, 0x01, 0, 0, 0xF7});
    std::vector<uint8_t> bad = sdsPacket(0, sent, false);
    bad[bad.size() - 2] ^= 0x55;
    p.sysex(bad);
    CHECK(g_tx.size() == 12 && g_tx[9] == 0x7E, "bad packet not refused");
    CHECK(!(Engine::userWaveMask() & (1u << 10)), "bad packet stored");

    // ... and going out on request: header, three packets, the wave in them.
    g_tx.clear();
    p.sysex({0xF0, 0x7E, 0x00, 0x03, 0x09, 0x00, 0xF7});
    p.wait(2500);
    CHECK(g_tx.size() == 21 + 3 * 127, "sample dump out: %zu bytes", g_tx.size());
    if (g_tx.size() == 21 + 3 * 127) {
        CHECK(g_tx[3] == 0x01 && g_tx[4] == 0x09 && g_tx[6] == 0x0C && g_tx[20] == 0xF7, "sample dump header");
        for (int k = 0; k < 3; ++k) {
            const std::vector<uint8_t> pk = sdsPacket(k, sent, false);
            CHECK(memcmp(g_tx.data() + 21 + 127 * k, pk.data(), 127) == 0, "sample dump packet %d", k);
        }
    }
    CHECK(!p.c.sending(), "sample dump never ends");
    // A ROM wave goes out too, as on the VS.
    g_tx.clear();
    p.sysex({0xF0, 0x7E, 0x00, 0x03, 40, 0x00, 0xF7});
    p.wait(2500);
    CHECK(g_tx.size() == 21 + 3 * 127 && g_tx[4] == 40, "ROM wave dump: %zu bytes", g_tx.size());

    // Parameters over MIDI: off after power-up; then cutoff (field 12) to
    // the middle of the slider, and LFO 1 -> filter (switch 04) on.
    p.c.controlChange(0, 98, 12);  p.c.controlChange(0, 99, 0);  p.c.controlChange(0, 6, 64);
    const uint8_t cutoff = p.c.editBuffer()[12];
    p.hold();  p.turn(Encoder::Sel, 4);  p.click();   p.shot("vs_25_midi_options");
    p.turn(Encoder::Sel, 1);  p.turn(Encoder::ParamA, 3);  p.turn(Encoder::ParamB, 1);   p.shot("vs_26_midi_params");
    p.c.controlChange(0, 98, 12);  p.c.controlChange(0, 99, 0);
    p.c.controlChange(0, 38, 0);   p.c.controlChange(0, 6, 64);
    CHECK(p.c.editBuffer()[12] == 50 && cutoff != 50, "cutoff over MIDI: %d (was %d)", p.c.editBuffer()[12], cutoff);
    p.c.controlChange(0, 98, 0x04);  p.c.controlChange(0, 99, 1);  p.c.controlChange(0, 6, 127);
    CHECK(p.c.editBuffer()[103] & 0x10, "LFO 1 -> filter switch not set over MIDI");
    p.c.controlChange(0, 98, 99);  p.c.controlChange(0, 99, 0);  p.c.controlChange(0, 6, 0);
    CHECK(p.c.editBuffer()[99] == (uint8_t)-99, "velocity amount at the slider's bottom: %d", (int8_t)p.c.editBuffer()[99]);
    // ... and going out, when a parameter is turned on the panel.
    p.hold();                            // leave MIDI options
    p.hold();  p.click();                // Edit Program, where it was left
    g_tx.clear();
    p.turn(Encoder::ParamA, 3);
    CHECK(g_tx.size() == 12 && g_tx[0] == 0xB0 && g_tx[1] == 98 && g_tx[4] == 99 && g_tx[7] == 38 && g_tx[10] == 6,
          "parameter out: %zu bytes", g_tx.size());
    if (g_tx.size() == 12) {             // what was sent, taken as a receiver takes it, is the edit
        const vs::ParamDef* def = vs::paramByMidi((g_tx[5] << 7) | g_tx[2]);
        uint8_t copy[Engine::kVsFields];
        memcpy(copy, p.c.editBuffer(), sizeof copy);
        CHECK(def != nullptr, "parameter out: unknown number");
        if (def) {
            copy[def->field] ^= 0x15;
            vs::paramSetSlider(*def, copy, (g_tx[11] << 1) | (g_tx[8] >> 6));
            CHECK(memcmp(copy, p.c.editBuffer(), sizeof copy) == 0, "parameter out does not decode to the edit");
        }
    }
    p.hold();

    // Overflow (switched on above): the ninth held key goes out instead of stealing a voice.
    p.engine.allNotesOff();
    p.wait(40);
    g_tx.clear();
    for (int k = 0; k < 9; ++k) p.c.noteOn(0, (uint8_t)(48 + k), 100);
    p.render(256);
    p.wait(40);
    const uint8_t over[] = {0x90, 56, 100};
    CHECK(p.engine.activeVoices() == 8 && g_tx.size() == 3 && memcmp(g_tx.data(), over, 3) == 0,
          "overflow: %d voices, %zu bytes out", p.engine.activeVoices(), g_tx.size());
    for (int k = 0; k < 9; ++k) p.c.noteOff(0, (uint8_t)(48 + k));
    p.wait(40);
    CHECK(g_tx.size() == 6 && g_tx[3] == 0x80 && g_tx[4] == 56, "overflow note not ended: %zu bytes", g_tx.size());
    p.engine.allNotesOff();

    // The arpeggiator's notes on its channel (set to 5 here), and a latched
    // arpeggio transposed from the keyboard.
    p.hold();  p.turn(Encoder::Sel, 4);  p.click();  p.turn(Encoder::ParamB, 5);  p.hold();
    p.turn(Encoder::Sel, 3);  p.turn(Encoder::ParamA, 1);        // ARP page: on
    p.c.noteOn(0, 60, 100);
    g_tx.clear();
    p.render(22050);  p.wait(40);
    CHECK(g_tx.size() >= 6 && g_tx[0] == 0x94 && g_tx[1] == 60, "arpeggiator out: %zu bytes", g_tx.size());
    p.press(Button::ParamA);             // transpose: the next key
    CHECK(p.c.noteOn(0, 67, 100), "transpose key refused");
    p.c.noteOff(0, 67);
    g_tx.clear();
    p.render(22050);  p.wait(40);
    bool up = false;
    for (size_t k = 0; k + 2 < g_tx.size(); k += 3) up |= g_tx[k] == 0x94 && g_tx[k + 1] == 67;
    CHECK(up, "arpeggio not transposed by a fifth");
    p.press(Button::ParamB);
    p.c.noteOff(0, 60);
    p.turn(Encoder::ParamA, -1);  p.turn(Encoder::Sel, -3);
    p.engine.allNotesOff();
    p.wait(40);

    // Cartridge: save programs and waves, wipe the internal memory, load them back.
    p.hold();  p.turn(Encoder::Sel, 5);  p.click();   p.shot("vs_27_cartridge");
    p.click();                                                    // Save Programs
    p.hold();  p.turn(Encoder::Sel, 5);  p.click();  p.turn(Encoder::Sel, 1);  p.click();   // Save Waves
    CHECK(p.store.cartridgePresent(), "no cartridge after saving");
    uint8_t five[Engine::kVsFields];
    memcpy(five, Engine::vsProgram(5), sizeof five);
    Engine::resetVsBank();
    Engine::clearUserWaves();
    p.hold();  p.turn(Encoder::Sel, 5);  p.click();  p.turn(Encoder::Sel, 2);  p.click();   // Load Programs
    p.hold();  p.turn(Encoder::Sel, 5);  p.click();  p.turn(Encoder::Sel, 3);  p.click();   // Load Waves
    CHECK(Engine::presetCount() == 100 && memcmp(Engine::vsProgram(5), five, sizeof five) == 0, "programs not back from the cartridge");
    CHECK(Engine::userWave(9, back) && memcmp(back, sent, sizeof sent) == 0, "waves not back from the cartridge");
    CHECK(p.store.dirty(), "loaded memory not marked for storing");

    // A program dump coming in is taken, shown and stored.
    uint8_t f[Engine::kVsFields], msg[Engine::kVsDumpBytes];
    randomProgram(f);
    Engine::dumpVsProgram(f, 7, msg);
    p.c.sysExMessage(msg, sizeof msg);
    CHECK(memcmp(Engine::vsProgram(7), f, sizeof f) == 0, "received program differs");
    CHECK(p.store.dirty(), "store not dirty after a received dump");
    p.shot("vs_23_received");

    // Factory reset, with nothing embedded: back to the init program, no user waves.
    p.wait(2000);
    p.hold();  p.turn(Encoder::Sel, 7);  p.click();  p.shot("vs_24_reset_confirm");
    p.click();
    CHECK(Engine::presetCount() == 1, "bank holds %d programs after reset", Engine::presetCount());
    CHECK(Engine::userWaveMask() == 0, "user waves survive the reset");

    // Settings survive a round trip.
    VsSettingsV4 s{}, t{};
    p.c.exportSettings(s);
    Panel q;
    q.engine.init(44100.0);
    q.c.init(nullptr, 0);
    q.c.importSettings(s);
    q.c.exportSettings(t);
    CHECK(memcmp(&s, &t, sizeof s) == 0, "settings round trip");
    CHECK(s.volume == 60 && s.fx == 2, "settings: volume %d fx %d", s.volume, s.fx);
    CHECK(s.waveNo[1] == 40 && s.waveHarm[1] == 5 && s.waveX == -16 && s.waveY == -16, "settings: Edit Waveform state");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    testFormat();
    testParams();
    testArp();
    testWaves();
    testPanel();
    if (g_failed) {
        std::printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("all PicoFaceVS checks passed\n");
    return 0;
}
