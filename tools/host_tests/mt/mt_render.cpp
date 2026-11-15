// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Host renderer for the PicoFaceMT engine: plays a fixed test score and writes
// a 16-bit mono WAV. Used to listen to the engine without flashing, and by
// run_all.sh, which fails on a silent or runaway render (exit code 2).
//
//   mt_engine_test out.wav [--rate 44100] [--score demo|scale|hold|tracks|poly|long]
//                        [--a N --b N --c N] [--track 0..1] [--set name=value ...]
//   mt_engine_test --list
#include "mello/engine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace mello;

static const char* const kParamNames[kNumParams] = {
    "volume", "tone", "pitch", "track", "attack", "release",
    "velocity", "wow", "flutter", "drive", "keydrag", "tapelimit"};

struct Ev { double t; int type; int a; float b; };   // 0 on, 1 off, 2 param
static std::vector<Ev> score;
static void note(double t, double dur, int n) { score.push_back({t, 0, n, 100}); score.push_back({t + dur, 1, n, 0}); }
static void par(double t, int p, float v) { score.push_back({t, 2, p, v}); }

static double buildScore(const char* name) {
    double t = 0.0;
    auto scale = [&] { for (int n = 43; n <= 77; ++n) { note(t, 0.38, n); t += 0.4; } t += 1.0; };
    auto hold  = [&] { for (int n : {45, 57, 69, 76}) { note(t, 5.0, n); t += 5.7; } };
    auto chords = [&] {
        for (auto ch : {std::vector<int>{48, 55, 60, 64}, {50, 57, 62, 65}, {43, 55, 59, 62, 67}}) {
            for (int n : ch) note(t, 4.0, n);
            t += 4.3;
        }
    };
    if (!strcmp(name, "scale")) scale();
    else if (!strcmp(name, "hold")) hold();
    else if (!strcmp(name, "tracks")) {            // same chord on A, A+B, B, B+C, C
        for (float sel : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            par(t, kTrack, sel);
            for (int n : {48, 55, 60, 64}) note(t + 0.05, 3.0, n);
            t += 3.6;
        }
    } else if (!strcmp(name, "long")) {            // one note held past the tape length
        note(0.0, 11.0, 60);
        t = 11.5;
    } else if (!strcmp(name, "poly")) {            // worst case: every voice busy
        for (int n = 43; n < 43 + kMaxVoices; ++n) note(0.0, 4.0, n);
        t = 4.5;
    } else { scale(); hold(); chords(); }
    return t + 1.0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < kBankSize; ++i) printf("%2d  %s (%d zones)\n", i, kBank[i].name, kBank[i].numZones);
        printf("params:");
        for (const char* p : kParamNames) printf(" %s", p);
        printf("\n");
        return 0;
    }
    if (argc < 2) { fprintf(stderr, "usage: mt_engine_test out.wav [options], see mt_render.cpp\n"); return 1; }
    int rate = 44100;
    const char* scoreName = "demo";
    Engine eng;
    std::vector<std::pair<int, float>> sets;
    int inst[kNumTracks] = {-1, -1, -1};
    for (int i = 2; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!strcmp(argv[i], "--rate")) rate = atoi(next());
        else if (!strcmp(argv[i], "--score")) scoreName = next();
        else if (!strcmp(argv[i], "--a")) inst[0] = atoi(next());
        else if (!strcmp(argv[i], "--b")) inst[1] = atoi(next());
        else if (!strcmp(argv[i], "--c")) inst[2] = atoi(next());
        else if (!strcmp(argv[i], "--track")) sets.push_back({kTrack, float(atof(next()))});
        else if (!strcmp(argv[i], "--set")) {
            const char* kv = next();
            const char* eq = strchr(kv, '=');
            bool found = false;
            for (int p = 0; eq && p < kNumParams; ++p)
                if (!strncmp(kv, kParamNames[p], size_t(eq - kv)) && kParamNames[p][eq - kv] == 0) {
                    sets.push_back({p, float(atof(eq + 1))});
                    found = true;
                }
            if (!found) { fprintf(stderr, "unknown parameter: %s\n", kv); return 1; }
        } else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    eng.init(float(rate));
    for (int t = 0; t < kNumTracks; ++t) if (inst[t] >= 0) eng.setTrackInstrument(t, inst[t]);
    for (auto& s : sets) eng.setParam(Param(s.first), s.second);

    double len = buildScore(scoreName);
    std::stable_sort(score.begin(), score.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    size_t total = size_t(len * rate), ev = 0;
    std::vector<float> buf(total);
    int peakVoices = 0;
    auto t0 = std::chrono::steady_clock::now();
    const size_t block = 64;
    for (size_t pos = 0; pos < total; pos += block) {
        while (ev < score.size() && score[ev].t * rate <= double(pos)) {
            const Ev& e = score[ev++];
            if (e.type == 0) eng.noteOn(uint8_t(e.a), uint8_t(e.b));
            else if (e.type == 1) eng.noteOff(uint8_t(e.a));
            else eng.setParam(Param(e.a), e.b);
        }
        size_t n = total - pos < block ? total - pos : block;
        eng.process(&buf[pos], int(n));
        if (eng.activeVoices() > peakVoices) peakVoices = eng.activeVoices();
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    float peak = 0.0f;
    bool bad = false;
    std::vector<int16_t> pcm(total);
    for (size_t i = 0; i < total; ++i) {
        float x = buf[i];
        if (!(x == x) || x > 4.0f || x < -4.0f) bad = true;
        if (x > peak) peak = x;
        if (-x > peak) peak = -x;
        x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
        pcm[i] = int16_t(x * 32767.0f);
    }
    if (peak < 0.01f) bad = true;                 // silent
    FILE* f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t bytes = uint32_t(total * 2), u32;
    uint16_t u16;
    fwrite("RIFF", 1, 4, f); u32 = 36 + bytes; fwrite(&u32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u32 = 16; fwrite(&u32, 4, 1, f);
    u16 = 1; fwrite(&u16, 2, 1, f); fwrite(&u16, 2, 1, f);
    u32 = uint32_t(rate); fwrite(&u32, 4, 1, f); u32 = uint32_t(rate) * 2; fwrite(&u32, 4, 1, f);
    u16 = 2; fwrite(&u16, 2, 1, f); u16 = 16; fwrite(&u16, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(pcm.data(), 2, total, f);
    fclose(f);
    printf("%s: %.1f s, peak %.3f, max voices %d, %.0fx realtime%s\n", argv[1], len, double(peak),
           peakVoices, len / secs, bad ? "  ** silent, NaN or runaway output **" : "");
    return bad ? 2 : 0;
}
