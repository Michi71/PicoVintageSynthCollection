// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_sysex_test -- the PicoFaceM1 SysEx layer on the host, no ROMs needed.
//
//   m1_sysex_test [preload.syx]
//
// Synthetic dumps in the M1 format are fed in random-sized pieces (as USB and
// DIN deliver them) and must land byte for byte; requests must answer with
// dumps that decode back to the memory; short, protected and foreign
// messages must leave the memory alone. With the factory preload given, the
// real Korg ALL DATA file is loaded as a last check.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "M1_SysEx.h"
#include "m1_memory.h"

static std::vector<uint8_t> g_out;
size_t m1_sysex_room() { return 64; }
void m1_sysex_write(const uint8_t* d, size_t n) { g_out.insert(g_out.end(), d, d + n); }

struct Host : M1_SysExHost {
    m1::Memory mem;
    uint8_t prog[143] = {0}, combi[124] = {0};
    int mode = 2, progLoads = 0, memLoads = 0;
    int m1Mode() const override { return mode; }
    void setM1Mode(int m) override { mode = m; }
    uint8_t* programEdit() override { return prog; }
    uint8_t* combiEdit() override { return combi; }
    void programEditLoaded() override { ++progLoads; }
    void combiEditLoaded() override {}
    bool writeProgram(int n) override { memcpy(mem.program(n), prog, 143); return true; }
    bool writeCombi(int n) override { memcpy(mem.combi(n), combi, 124); return true; }
    void memoryLoaded() override { ++memLoads; }
    bool parameterChange(int, int, int) override { return true; }
    int globalChannel() const override { return 16; }
    m1::Memory& memory() override { return mem; }
};

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<uint8_t> pack(const uint8_t* d, size_t n) {
    std::vector<uint8_t> o;
    for (size_t i = 0; i < n; i += 7) {
        uint8_t msb = 0;
        const size_t k = n - i < 7 ? n - i : 7;
        for (size_t j = 0; j < k; ++j) msb |= (uint8_t)((d[i + j] >> 7) << j);
        o.push_back(msb);
        for (size_t j = 0; j < k; ++j) o.push_back(d[i + j] & 0x7F);
    }
    return o;
}

static std::vector<uint8_t> msg(uint8_t func, std::vector<uint8_t> pre, const uint8_t* d, size_t n) {
    std::vector<uint8_t> m = {0xF0, 0x42, 0x30, 0x19, func};
    m.insert(m.end(), pre.begin(), pre.end());
    if (d) { auto p = pack(d, n); m.insert(m.end(), p.begin(), p.end()); }
    m.push_back(0xF7);
    return m;
}

static void feed(M1_SysEx& sx, const std::vector<uint8_t>& m) {
    size_t i = 0;
    while (i < m.size()) {
        size_t k = 1 + (size_t)(rand() % 3);           // 1..3 bytes, as the USB parser hands them
        if (rand() % 50 == 0) k = 64;
        if (k > m.size() - i) k = m.size() - i;
        sx.feed(&m[i], k);
        i += k;
    }
    for (int p = 0; p < 2000; ++p) sx.poll();          // drain replies
}

static int lastReply() {
    for (size_t i = g_out.size(); i >= 6; --i)
        if (g_out[i - 1] == 0xF7 && g_out[i - 6] == 0xF0) return g_out[i - 2];
    return -1;
}

// Decode one dump from the captured output (header 5 + pre + groups + F7).
static std::vector<uint8_t> unpackFrom(const std::vector<uint8_t>& o, size_t pre) {
    std::vector<uint8_t> d;
    size_t i = 5 + pre;
    while (i < o.size() && o[i] != 0xF7) {
        const uint8_t msb = o[i++];
        for (int j = 0; j < 7 && i < o.size() && o[i] != 0xF7; ++j) d.push_back((uint8_t)(o[i++] | (((msb >> j) & 1) << 7)));
    }
    return d;
}

int main(int argc, char** argv) {
    srand(1);
    static uint8_t fG[21], fK[840], fC[12400], fP[14300];
    for (size_t i = 0; i < sizeof fP; ++i) fP[i] = (uint8_t)(i * 7);
    for (size_t i = 0; i < sizeof fC; ++i) fC[i] = (uint8_t)(i * 3);
    static Host host;
    host.mem.init(fG, fK, fC, fP);
    M1_SysEx sx(host);

    // 1. ALL PROGRAM PARAMETER DUMP (4C), as presetpatch.com sends it
    std::vector<uint8_t> bank(14300);
    for (size_t i = 0; i < bank.size(); ++i) bank[i] = (uint8_t)(255 - (i * 13) % 256);
    auto m4c = msg(0x4C, {0x00}, bank.data(), bank.size());
    CHECK(m4c.size() == 16350, "4C message is %zu bytes, the manual says 16350 incl. header", m4c.size());
    g_out.clear();
    feed(sx, m4c);
    CHECK(memcmp(host.mem.image().programs, bank.data(), bank.size()) == 0, "4C did not land");
    CHECK(lastReply() == 0x23, "4C reply %02X", lastReply());
    CHECK(host.mem.dirty(), "memory not marked for storing");

    // 2. request it back (1C) and compare
    g_out.clear();
    feed(sx, msg(0x1C, {0x00}, nullptr, 0));
    for (int p = 0; p < 20000 && sx.sending(); ++p) sx.poll();
    CHECK(g_out.size() == 16350, "4C answer is %zu bytes", g_out.size());
    auto back = unpackFrom(g_out, 1);
    CHECK(back.size() >= 14300 && memcmp(back.data(), bank.data(), 14300) == 0, "4C answer does not decode to memory");

    // 3. program dump into the edit buffer (40), and back (10)
    uint8_t one[143];
    for (int i = 0; i < 143; ++i) one[i] = (uint8_t)(i * 31 + 5);
    g_out.clear();
    feed(sx, msg(0x40, {}, one, 143));
    CHECK(memcmp(host.prog, one, 143) == 0 && host.progLoads == 1, "40 did not land in the edit buffer");
    g_out.clear();
    feed(sx, msg(0x10, {}, nullptr, 0));
    for (int p = 0; p < 2000 && sx.sending(); ++p) sx.poll();
    auto b40 = unpackFrom(g_out, 0);
    CHECK(g_out.size() == 5 + 164 + 1 && b40.size() >= 143 && memcmp(b40.data(), one, 143) == 0, "10 answer wrong (%zu)", g_out.size());

    // 4. short dump: format error, memory unchanged
    std::vector<uint8_t> before(host.mem.image().programs, host.mem.image().programs + 14300);
    auto bad = msg(0x4C, {0x00}, bank.data(), 14000);
    g_out.clear();
    feed(sx, bad);
    CHECK(lastReply() == 0x26, "short 4C reply %02X", lastReply());
    CHECK(memcmp(host.mem.image().programs, before.data(), 14300) == 0, "short 4C changed memory");

    // 5. protected: load error
    host.mem.protect = true;
    std::vector<uint8_t> cb(12400, 0x55);
    g_out.clear();
    feed(sx, msg(0x4D, {0x00}, cb.data(), cb.size()));
    CHECK(lastReply() == 0x24, "protected 4D reply %02X", lastReply());
    host.mem.protect = false;
    feed(sx, msg(0x4D, {0x00}, cb.data(), cb.size()));
    CHECK(memcmp(host.mem.image().combis, cb.data(), cb.size()) == 0, "4D did not land");

    // 6. card bank: refused
    g_out.clear();
    feed(sx, msg(0x4C, {0x01}, bank.data(), bank.size()));
    CHECK(lastReply() == 0x24, "card 4C reply %02X", lastReply());

    // 7. another manufacturer, and a message cut by a status byte
    std::vector<uint8_t> roland = {0xF0, 0x41, 0x10, 0x14, 0x12, 0x00, 0x00, 0x00, 0x01, 0x7F, 0xF7};
    g_out.clear();
    feed(sx, roland);
    CHECK(g_out.empty(), "answered a Roland message");
    auto cut = msg(0x4C, {0x00}, bank.data(), bank.size());
    cut.insert(cut.begin() + 100, 0x90);
    g_out.clear();
    feed(sx, cut);
    CHECK(g_out.empty(), "acted on an interrupted dump");

    // 8. device inquiry
    g_out.clear();
    feed(sx, {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7});
    CHECK(g_out.size() == 15 && g_out[5] == 0x42 && g_out[6] == 0x19, "inquiry reply");

    // 9. write request (11) and global dump (51)
    g_out.clear();
    feed(sx, msg(0x11, {0x00, 42}, nullptr, 0));
    CHECK(lastReply() == 0x21 && memcmp(host.mem.program(42), one, 143) == 0, "11 write");
    std::vector<uint8_t> gl(861);
    for (size_t i = 0; i < gl.size(); ++i) gl[i] = (uint8_t)(i & 0x7F);
    auto m51 = msg(0x51, {0x00}, gl.data(), gl.size());
    CHECK(m51.size() == 5 + 1 + 984 + 1, "51 is %zu bytes", m51.size());
    feed(sx, m51);
    CHECK(memcmp(host.mem.image().global, gl.data(), gl.size()) == 0, "51 did not land");

    // 10. the real factory preload (ALL DATA, 50)
    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 2; }
        std::vector<uint8_t> syx;
        int c;
        while ((c = fgetc(f)) != EOF) syx.push_back((uint8_t)c);
        fclose(f);
        g_out.clear();
        feed(sx, syx);
        CHECK(lastReply() == 0x23, "preload reply %02X", lastReply());
        const m1::MemoryImage& im = host.mem.image();
        char name[11] = {0};
        memcpy(name, im.programs, 10);
        CHECK(strncmp(name, "Universe", 8) == 0, "first program is '%s'", name);
        memcpy(name, im.combis, 10);
        CHECK(strncmp(name, "FilmScore", 9) == 0, "first combination is '%s'", name);
        printf("preload: %zu bytes loaded, program 0 '%.10s', combination 0 '%.10s'\n", syx.size(),
               (const char*)im.programs, (const char*)im.combis);
    }

    printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "all SysEx checks passed", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
