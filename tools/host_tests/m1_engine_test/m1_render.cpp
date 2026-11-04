// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_render -- host renders of the PicoFaceM1 engine, for listening and for
// the comparison against the Python reference (compare_reference.py).
//
//   m1_render <gendir> prog  <n> <note> <vel> <gate_s> <total_s> <out.wav> [nofx|ref]
//   m1_render <gendir> combi <n> <note> <vel> <gate_s> <total_s> <out.wav> [nofx]
//   m1_render <gendir> chord <prog|combi> <n> <out.wav>     a C major chord, 4 s
//   m1_render <gendir> fxblock <50 hex digits> <in.wav> <out.wav>
//            run a 25-byte effect block over a stereo 16-bit WAV (in on
//            buses A/B, C/D silent)
//   m1_render <gendir> fxcheck                                every effect type at
//            its extreme parameters: delay RAM layout fits, output finite
//
// <gendir> is the configure step's PicoFaceM1_rom directory (m1_pcm.bin and
// m1_rom_tables.h); build.sh compiles against the header found there.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "m1_engine/m1_engine.h"
#include "m1_rom_tables.h"

static std::vector<int16_t> g_pcm;
static int16_t g_fx[65536];
static uint8_t g_kernel[sizeof(m1rom::kKernel)];

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t n = (uint32_t)l.size(), rate = m1::kSampleRate;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + n * 4); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(n * 4);
    for (uint32_t i = 0; i < n; ++i) {
        for (float x : {l[i], r[i]}) {
            int v = (int)lrintf(x * 32767.0f);
            v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
            u16((uint16_t)(int16_t)v);
        }
    }
    fclose(f);
}

static m1::Engine* makeEngine(const char* gendir) {
    std::string p = std::string(gendir) + "/m1_pcm.bin";
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { perror(p.c_str()); exit(1); }
    g_pcm.resize(0x200000);
    if (fread(g_pcm.data(), 2, g_pcm.size(), f) != g_pcm.size()) { fprintf(stderr, "short pcm\n"); exit(1); }
    fclose(f);
    memcpy(g_kernel, m1rom::kKernel, sizeof g_kernel);
    m1::RomTables rom{};
    rom.pcm = g_pcm.data(); rom.pcmWords = (uint32_t)g_pcm.size();
    rom.ms = m1rom::kMultisounds; rom.splits = m1rom::kSplits;
    rom.drums = m1rom::kDrums; rom.nDrums = m1rom::kDrumCount;
    rom.kernel = g_kernel; rom.vdf1k = m1rom::kVdf1k; rom.vdf2k = m1rom::kVdf2k;
    rom.egRise = m1rom::kEgRise; rom.egFall = m1rom::kEgFall; rom.velCurve = m1rom::kVelCurve;
    rom.mgFreq = m1rom::kMgFreq; rom.mgDelay = m1rom::kMgDelay;
    rom.mgIntP = m1rom::kMgIntP; rom.mgIntF = m1rom::kMgIntF;
    rom.pitchS = m1rom::kPitchS; rom.pitchF = m1rom::kPitchF; rom.panReg = m1rom::kPanReg; rom.pan101 = m1rom::kPan101;
    rom.delayStart = m1rom::kDelayStart; rom.fx = &m1rom::kFx;
    rom.programs = m1rom::kPrograms; rom.combis = m1rom::kCombis; rom.kits = m1rom::kKits;
    auto* e = new m1::Engine();
    e->init(rom, g_fx);
    return e;
}

// Renders `total` samples; noteOff for `notes` at sample `gate`.
static void run(m1::Engine& e, const std::vector<int>& notes, int vel, long gate, long total,
                std::vector<float>& L, std::vector<float>& R) {
    L.assign(total, 0); R.assign(total, 0);
    for (int n : notes) e.noteOn(0, (uint8_t)n, (uint8_t)vel);
    long i = 0;
    while (i < total) {
        long n = total - i;
        if (i < gate && i + n > gate) n = gate - i;
        e.render(&L[i], &R[i], (int)n);
        i += n;
        if (i == gate) for (int k : notes) e.noteOff(0, (uint8_t)k);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "see the header of m1_render.cpp\n"); return 2; }
    m1::Engine& e = *makeEngine(argv[1]);
    const std::string cmd = argv[2];
    std::vector<float> L, R;
    if (cmd == "prog" || cmd == "combi") {
        if (argc < 9) return 2;
        const int idx = atoi(argv[3]), note = atoi(argv[4]), vel = atoi(argv[5]);
        const long gate = (long)(atof(argv[6]) * m1::kSampleRate);
        const long total = (long)(atof(argv[7]) * m1::kSampleRate);
        const std::string opt = argc > 9 ? argv[9] : "";
        e.setBypassFx(opt == "nofx" || opt == "ref");
        e.setReferenceMode(opt == "ref");
        if (cmd == "prog") e.selectProgram(idx); else e.selectCombi(idx);
        run(e, {note}, vel, gate, total, L, R);
        writeWav(argv[8], L, R);
        float pk = 0;
        for (size_t i = 0; i < L.size(); ++i) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
        printf("%s %d '%s' note %d: peak %.4f\n", cmd.c_str(), idx, e.name(), note, pk);
        return 0;
    }
    if (cmd == "chord") {
        if (argc < 6) return 2;
        const bool combi = std::string(argv[3]) == "combi";
        const int idx = atoi(argv[4]);
        if (combi) e.selectCombi(idx); else e.selectProgram(idx);
        run(e, {48, 60, 64, 67}, 100, 3 * m1::kSampleRate, 6 * m1::kSampleRate, L, R);
        writeWav(argv[5], L, R);
        float pk = 0;
        for (size_t i = 0; i < L.size(); ++i) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
        printf("%s %d '%s': peak %.4f\n", combi ? "combi" : "prog", idx, e.name(), pk);
        return 0;
    }
    if (cmd == "fxblock") {
        if (argc < 6) return 2;
        uint8_t blk[25];
        for (int i = 0; i < 25; ++i) { unsigned v; sscanf(argv[3] + 2 * i, "%2x", &v); blk[i] = (uint8_t)v; }
        FILE* f = fopen(argv[4], "rb");
        if (!f) { perror(argv[4]); return 1; }
        fseek(f, 44, SEEK_SET);
        std::vector<int16_t> in;
        int16_t buf[2];
        while (fread(buf, 2, 2, f) == 2) { in.push_back(buf[0]); in.push_back(buf[1]); }
        fclose(f);
        const size_t n = in.size() / 2;
        std::vector<float> a(n), b(n), z(n, 0.0f);
        for (size_t i = 0; i < n; ++i) { a[i] = in[2 * i] / 32768.0f; b[i] = in[2 * i + 1] / 32768.0f; }
        m1::EffectSection fx;
        fx.init(&m1rom::kFx, m1rom::kPan101, g_fx);
        fx.configure(blk);
        L.assign(n, 0); R.assign(n, 0);
        for (size_t i = 0; i < n; i += 64) {
            const int m = (int)(n - i < 64 ? n - i : 64);
            fx.process(&a[i], &b[i], &z[i], &z[i], &L[i], &R[i], m);
        }
        writeWav(argv[5], L, R);
        return 0;
    }
    if (cmd == "fxcheck") {
        // Every type in both slots at the extremes of its time parameters,
        // driven by noise; the output has to stay finite and bounded.
        int bad = 0;
        e.selectProgram(0);
        std::vector<float> a(64), b(64), c(64), d(64), o1(64), o2(64);
        uint32_t rnd = 1;
        for (int t = 0; t < 33; ++t) {
            for (int ext = 0; ext < 2; ++ext) {
                uint8_t blk[25] = {0};
                blk[0] = (uint8_t)t; blk[1] = (uint8_t)t;
                blk[2] = blk[3] = blk[4] = blk[5] = 50;
                blk[6] = 101; blk[7] = 1;
                blk[8] = 0x0F | (ext ? 0x10 : 0);
                for (int k = 0; k < 2; ++k) {
                    uint8_t* p = blk + 9 + 8 * k;
                    for (int j = 0; j < 8; ++j) p[j] = ext ? 99 : 0;
                    if (ext) { p[0] = 0xF4; p[1] = 1; p[4] = 0xF4; p[5] = 1; }   // 500 ms where a word
                }
                m1::EffectSection fx;
                fx.init(&m1rom::kFx, m1rom::kPan101, g_fx);
                fx.configure(blk);
                float pk = 0;
                for (int blkn = 0; blkn < 31250 * 2 / 64; ++blkn) {
                    for (int i = 0; i < 64; ++i) {
                        rnd = rnd * 1664525u + 1013904223u;
                        const float x = ((int)(rnd >> 16) - 32768) / 65536.0f;
                        a[i] = b[i] = c[i] = d[i] = x;
                    }
                    fx.process(a.data(), b.data(), c.data(), d.data(), o1.data(), o2.data(), 64);
                    for (int i = 0; i < 64; ++i) {
                        if (!std::isfinite(o1[i]) || !std::isfinite(o2[i])) { pk = 1e9f; break; }
                        pk = fmaxf(pk, fmaxf(fabsf(o1[i]), fabsf(o2[i])));
                    }
                }
                const bool ok = pk < 64.0f && !fx.overflow();
                if (!ok) { ++bad; printf("type %2d ext %d: peak %g overflow %d  FAIL\n", t, ext, pk, fx.overflow()); }
            }
        }
        printf("fxcheck: %d failures\n", bad);
        return bad ? 1 : 0;
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
