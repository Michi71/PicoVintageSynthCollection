// Host harness for the PicoFaceJV engine: loads a ROM set, renders a patch and
// writes a WAV, so the engine can be listened to and A/B'd against the dumps
// tools/jv_extract/jv_probe produces from the reference emulator.
//
//   c++ -O2 -std=c++17 -Iinstruments/PicoFaceJV/include -Itools/jv_extract \
//       -o jv_engine_test tools/host_tests/jv_engine_test/jv_engine_test.cpp \
//       instruments/PicoFaceJV/src/jv_engine/jv_engine.cpp -lm
//   ./jv_engine_test <romdir> [bank] [patch] [note] [vel] [out.wav]
#include "jv_engine/jv_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int SR = 32000;

static const int ADDR_PERM[20] = {2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19};
static const int DATA_PERM[8]  = {2, 0, 4, 5, 7, 6, 3, 1};

static bool readFile(const std::string& p, std::vector<uint8_t>& out, size_t want) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", p.c_str()); return false; }
    out.resize(want);
    size_t got = fread(out.data(), 1, want, f);
    fclose(f);
    if (got != want) { fprintf(stderr, "%s: short read\n", p.c_str()); return false; }
    return true;
}

static void descramble(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    uint8_t dmap[256];
    for (int v = 0; v < 256; v++) {
        int d = 0;
        for (int j = 0; j < 8; j++) if (v & (1 << DATA_PERM[j])) d |= 1 << j;
        dmap[v] = (uint8_t)d;
    }
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        size_t a = i & ~(size_t)0xFFFFF;
        for (int j = 0; j < 20; j++) if (i & (1u << j)) a |= (size_t)1 << ADDR_PERM[j];
        dst[i] = dmap[src[a]];
    }
}

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r) {
    size_t n = l.size();
    std::vector<int16_t> il(n * 2);
    for (size_t i = 0; i < n; i++) {
        auto cv = [](float v) {
            int s = (int)lrintf(v * 32767.0f);
            return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        };
        il[2 * i] = cv(l[i]);
        il[2 * i + 1] = cv(r[i]);
    }
    uint32_t data = (uint32_t)(il.size() * 2), riff = 36 + data, rate = SR, brate = SR * 4;
    uint16_t ch = 2, bits = 16, fmt = 1, align = 4;
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t sz = 16; fwrite(&sz, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(il.data(), 2, il.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: jv_engine_test <romdir> [bank 0-2] [patch 0-63] "
                "[--hold s] [--tail s] "
                        "[note] [vel] [out.wav]\n");
        return 1;
    }
    std::string dir = argv[1];
    int bank = argc > 2 ? atoi(argv[2]) : 1;
    int pidx = argc > 3 ? atoi(argv[3]) : 0;
    int note = argc > 4 ? atoi(argv[4]) : 60;
    int vel  = argc > 5 ? atoi(argv[5]) : 100;
    const char* out = argc > 6 ? argv[6] : "jv_engine_test.wav";

    // --set tone:offset:value patches the tone bytes before the note, mirroring
    // jv_probe's "#base" lines so both sides can be driven identically.
    struct Mod { int tone, off, val; };
    std::vector<Mod> mods;
    float trim = 1.0f;
    // Hold and tail in seconds, so a short burst can be rendered for effect
    // work -- an impulse response needs a note far shorter than the tail, and
    // jv_probe's JV_HOLD / JV_REL are the matching knobs on the other side.
    float holdS = 2.0f, tailS = 2.0f;
    int modWheel = -1, aftertouch = -1, expression = -1;   // applied at 1.0 s
    for (int i = 7; i < argc; i++) {
        Mod m{};
        if (!strcmp(argv[i], "--set") && i + 1 < argc &&
            sscanf(argv[++i], "%d:%d:%d", &m.tone, &m.off, &m.val) == 3)
            mods.push_back(m);
        else if (!strcmp(argv[i], "--trim") && i + 1 < argc)
            trim = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
            holdS = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--tail") && i + 1 < argc)
            tailS = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mod") && i + 1 < argc) modWheel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--aft") && i + 1 < argc) aftertouch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--expr") && i + 1 < argc) expression = atoi(argv[++i]);
    }

    std::vector<uint8_t> rom2, w1raw, w2raw, w1, w2, wave;
    if (!readFile(dir + "/jv880_rom2.bin", rom2, 0x40000) ||
        !readFile(dir + "/jv880_waverom1.bin", w1raw, 0x200000) ||
        !readFile(dir + "/jv880_waverom2.bin", w2raw, 0x200000))
        return 1;
    descramble(w1raw, w1);
    descramble(w2raw, w2);
    wave.reserve(0x400000);
    wave.insert(wave.end(), w1.begin(), w1.end());
    wave.insert(wave.end(), w2.begin(), w2.end());

    jv::RomView rv{wave.data(), wave.size(), rom2.data(), rom2.size()};
    jv::Engine eng;
    if (!eng.init(rv, SR)) { fprintf(stderr, "engine init failed\n"); return 1; }
    if (!eng.selectPatch(bank, pidx)) { fprintf(stderr, "bad patch\n"); return 1; }
    eng.setPitchTrim(trim);
    if (!mods.empty()) {
        static const uint32_t bankOff[3] = {0x008CE0, 0x010CE0, 0x018CE0};
        uint8_t p[362];
        memcpy(p, rom2.data() + bankOff[bank] + (size_t)pidx * 362, 362);
        for (const Mod& m : mods) {
            // A negative tone index addresses the patch bytes directly, the same
            // convention jv_probe uses. Without this the index arithmetic ran off
            // the front of the buffer.
            const int idx = (m.tone >= 0) ? 26 + m.tone * 84 + m.off : m.off;
            if (idx >= 0 && idx < 362) p[idx] = (uint8_t)m.val;
        }
        eng.setPatch(p);
    }

    char name[13] = {0};
    memcpy(name, eng.patchName(), 12);
    printf("patch \"%s\"  note %d vel %d\n", name, note, vel);

    const int hold = (int)(SR * holdS), tail = (int)(SR * tailS);
    std::vector<float> L(hold + tail), R(hold + tail);
    eng.noteOn((uint8_t)note, (uint8_t)vel);
    // Controllers land 1.0 s in, matching jv_probe's "#midi 1.0 ..." convention
    // so both sides can be compared window for window.
    int ctlAt = SR;
    if (ctlAt > hold) ctlAt = hold;
    eng.render(L.data(), R.data(), ctlAt);
    if (modWheel   >= 0) eng.modWheel((uint8_t)modWheel);
    if (aftertouch >= 0) eng.aftertouch((uint8_t)aftertouch);
    if (expression >= 0) eng.expression((uint8_t)expression);
    eng.render(L.data() + ctlAt, R.data() + ctlAt, hold - ctlAt);
    printf("  voices after note-on: %d\n", eng.activeVoices());
    eng.noteOff((uint8_t)note);
    eng.render(L.data() + hold, R.data() + hold, tail);

    double peak = 0, rms = 0;
    for (size_t i = 0; i < L.size(); i++) {
        double m = 0.5 * (L[i] + R[i]);
        peak = fmax(peak, fabs(m));
        rms += m * m;
    }
    rms = sqrt(rms / (double)L.size());
    printf("  peak %.4f  rms %.5f  voices at end: %d\n", peak, rms, eng.activeVoices());
    if (peak < 1e-6) fprintf(stderr, "  WARNING: engine produced silence\n");

    writeWav(out, L, R);
    printf("  wrote %s\n", out);
    return 0;
}
