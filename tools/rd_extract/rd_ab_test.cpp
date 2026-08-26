// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// rd_ab_test.cpp -- A/B comparison: RdNewEngine against the reference emulator.
//
//   rd_ab_test <romdir> <packdir> <patch> <note> <vel> [wav-prefix]
//
// The reference is giulioz/rdpiano in its original state. That used to be an
// adapted copy of it, and the difference is not cosmetic: the adaptation grew
// a hand-written per-patch voicing table (gain, brightness, saturation, noise)
// that the original does not have and that is live in its output path. A
// regression measured against it would be measuring the adaptation.
//
// The original takes ROM images rather than compiled-in tables and has no
// loadPatch, so patch selection is done the way its firmware does it: mount
// the patch's parameter block with loadSounds, then hand the sound CPU the
// panel bytes 0x31 0x30. The three tables below are the mapping, and they are
// the same ones rd_make_packs.py uses.
#include "rd_new_engine.h"
#include "mcu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>

static const u8 SET[16]     = {0,0,0,1,1,1,1,1,2,2,2,2,2,2,2,2};
static const int RATE[16]   = {20000,20000,20000,32000,32000,20000,20000,32000,
                               20000,20000,20000,32000,20000,20000,32000,20000};
static const size_t OFF[16] = {
    0x000000,0x008000,0x010000,0x018000,0x003c20,0x00ab50,0x014260,0x01bef0,
    0x000020,0x008000,0x010000,0x018000,0x002c00,0x00b1f0,0x012910,0x0199f0};
static const char* SAMPLES[3][3] = {
    {"mks20_15179738.BIN","mks20_15179737.BIN","mks20_15179736.BIN"},
    {"mks20_15179741.BIN","mks20_15179740.BIN","mks20_15179739.BIN"},
    {"MK80_IC5.bin","MK80_IC6.bin","MK80_IC7.bin"}};
static const char* PARAMS[3] = {"mks20_15179757.BIN","mks20_15179757.BIN","MK80_IC18.bin"};

static void w16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void w32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }

static void writeWav(const char* path, const std::vector<int32_t>& buf, uint32_t rate) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write %s\n", path); return; }
    uint32_t dataSize = (uint32_t)(buf.size() * 2);
    fwrite("RIFF", 1, 4, f); w32(f, 36 + dataSize); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(f, 16); w16(f, 1); w16(f, 1);
    w32(f, rate); w32(f, rate * 2); w16(f, 2); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, dataSize);
    for (int32_t s : buf) {
        int32_t v = s; if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        w16(f, (uint16_t)(int16_t)v);
    }
    fclose(f);
}

static std::vector<u8> slurp(const std::string& path, size_t want) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "rd_ab_test: cannot open %s\n", path.c_str()); exit(2); }
    std::vector<u8> v(want);
    size_t n = fread(v.data(), 1, want, f);
    fclose(f);
    if (n != want) { fprintf(stderr, "rd_ab_test: %s is short\n", path.c_str()); exit(2); }
    return v;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <romdir> <packdir> <patch> <note> <vel> "
                        "[wav-prefix]\n", argv[0]);
        return 1;
    }
    std::string romdir = argv[1], packdir = argv[2];
    int patch = atoi(argv[3]), note = atoi(argv[4]), vel = atoi(argv[5]);
    const char* prefix = argc > 6 ? argv[6] : nullptr;
    if (patch < 0 || patch > 15) { fprintf(stderr, "patch out of range\n"); return 1; }

    const u8 set = SET[patch];
    auto ic5 = slurp(romdir + "/" + SAMPLES[set][0], 0x20000);
    auto ic6 = slurp(romdir + "/" + SAMPLES[set][1], 0x20000);
    auto ic7 = slurp(romdir + "/" + SAMPLES[set][2], 0x20000);
    auto prm = slurp(romdir + "/" + std::string(PARAMS[set]), 0x20000);
    auto prg = slurp(romdir + "/RD200_B.bin", 0x2000);

    char pp[512];
    snprintf(pp, sizeof(pp), "%s/pack_p%d.rdp", packdir.c_str(), patch);
    FILE* pf = fopen(pp, "rb");
    if (!pf) { fprintf(stderr, "rd_ab_test: cannot open %s\n", pp); return 2; }
    fseek(pf, 0, SEEK_END); long psz = ftell(pf); fseek(pf, 0, SEEK_SET);
    std::vector<uint8_t> pack((size_t)psz);
    if ((long)fread(pack.data(), 1, (size_t)psz, pf) != psz) { fclose(pf); return 2; }
    fclose(pf);

    RdNewEngine eng;
    if (!eng.loadPack(pack.data(), pack.size())) {
        fprintf(stderr, "loadPack failed\n"); return 1; }

    Mcu mcu(ic5.data(), ic6.data(), ic7.data(), prg.data(), prm.data());
    mcu.reset();
    mcu.commands_queue.push(0x30); mcu.commands_queue.push(0xE0);
    mcu.commands_queue.push(0x00); mcu.commands_queue.push(0x00);
    const bool r32 = RATE[patch] == 32000;
    for (int i = 0; i < 1024; i++) mcu.generate_next_sample(r32);
    mcu.loadSounds(ic5.data(), ic6.data(), ic7.data(), prm.data(), OFF[patch]);
    mcu.commands_queue.push(0x31); mcu.commands_queue.push(0x30);
    const uint32_t rate = (uint32_t)RATE[patch];

    // Fresh-boot quirk: the firmware kills a high FIRST note after ~70 samples.
    // A quiet primer note settles the allocator; its tail is negligible at vel 1.
    mcu.sendMidiCmd(0x90, 60, 1);
    for (size_t i = 0; i < (size_t)(0.05 * rate); ++i) mcu.generate_next_sample(r32);
    mcu.sendMidiCmd(0x80, 60, 0);
    for (size_t i = 0; i < (size_t)(1.0 * rate); ++i) mcu.generate_next_sample(r32);

    const size_t onSamples = (size_t)(2.0 * rate), offSamples = (size_t)(1.5 * rate);
    std::vector<int32_t> bufRef, bufNew;
    bufRef.reserve(onSamples + offSamples); bufNew.reserve(onSamples + offSamples);

    mcu.sendMidiCmd(0x90, (u8)note, (u8)vel);
    for (size_t i = 0; i < onSamples; i++) bufRef.push_back(mcu.generate_next_sample(r32));
    mcu.sendMidiCmd(0x80, (u8)note, 0);
    for (size_t i = 0; i < offSamples; i++) bufRef.push_back(mcu.generate_next_sample(r32));

    eng.noteOn((uint8_t)note, (uint8_t)vel);
    for (size_t i = 0; i < onSamples; i++) bufNew.push_back(eng.renderSample());
    eng.noteOff((uint8_t)note);
    for (size_t i = 0; i < offSamples; i++) bufNew.push_back(eng.renderSample());

    // Lag-scan cross-correlation (onset alignment is hypersensitive: a few
    // samples of shift flip r at high frequencies).
    size_t start = (size_t)(0.1 * rate);
    size_t L = (size_t)(1.5 * rate);
    if (start + L > std::min(bufRef.size(), bufNew.size()))
        L = std::min(bufRef.size(), bufNew.size()) - start;

    double bestR = -2.0; long bestLag = 0;
    for (long lag = -300; lag <= 300; ++lag) {
        double sAB = 0, sAA = 0, sBB = 0;
        for (size_t i = start; i < start + L; i += 2) {
            long j = (long)i + lag;
            if (j < 0 || (size_t)j >= bufNew.size()) continue;
            double a = bufRef[i], b = bufNew[(size_t)j];
            sAB += a * b; sAA += a * a; sBB += b * b;
        }
        if (sAA > 0 && sBB > 0) {
            double r = sAB / std::sqrt(sAA * sBB);
            if (r > bestR) { bestR = r; bestLag = lag; }
        }
    }
    double sumSqRef = 0, sumSqNew = 0;
    for (size_t i = start; i < start + L; ++i) {
        sumSqRef += (double)bufRef[i] * bufRef[i];
        sumSqNew += (double)bufNew[i] * bufNew[i];
    }
    double rmsRef = std::sqrt(sumSqRef / L), rmsNew = std::sqrt(sumSqNew / L);

    printf("bestLag=%ld r=%.6f rmsRef=%.1f rmsNew=%.1f rmsRatio=%.4f activeVoices=%d\n",
           bestLag, bestR, rmsRef, rmsNew, rmsRef > 0 ? rmsNew / rmsRef : 0.0,
           eng.activeVoices());

    if (prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s_ref.wav", prefix); writeWav(path, bufRef, rate);
        snprintf(path, sizeof(path), "%s_new.wav", prefix); writeWav(path, bufNew, rate);
    }
    return 0;
}
