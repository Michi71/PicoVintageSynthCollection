// rd_ab_test.cpp -- A/B comparison: RdNewEngine vs Mcu reference emulator
#include "rd_new_engine.h"
#include "mcu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

static void w16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void w32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }

static void writeWav(const char* path, const std::vector<int32_t>& buf, uint32_t rate) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write %s\n", path); return; }
    uint32_t dataSize = (uint32_t)(buf.size() * 2);
    fwrite("RIFF", 1, 4, f);
    w32(f, 36 + dataSize);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(f, 16);
    w16(f, 1);
    w16(f, 1);
    w32(f, rate);
    w32(f, rate * 2);
    w16(f, 2);
    w16(f, 16);
    fwrite("data", 1, 4, f);
    w32(f, dataSize);
    for (int32_t s : buf) {
        float v = (float)s * (1.0f / 131072.0f);
        float c = 0.9f;
        v = c * std::tanh(v / c);   // softclip (applied identically to both files)
        int16_t out = (int16_t)(v * 32767.0f);
        w16(f, (uint16_t)out);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s pack.rdp patch [note=60] [vel=110] [prefix=ab]\n", argv[0]);
        return 1;
    }
    const char* packPath = argv[1];
    int patch    = argc > 2 ? std::atoi(argv[2]) : 0;
    int note     = argc > 3 ? std::atoi(argv[3]) : 60;
    int vel      = argc > 4 ? std::atoi(argv[4]) : 110;
    const char* prefix = argc > 5 ? argv[5] : "ab";

    FILE* pf = fopen(packPath, "rb");
    if (!pf) { fprintf(stderr, "Cannot open pack: %s\n", packPath); return 1; }
    fseek(pf, 0, SEEK_END);
    long sz = ftell(pf);
    fseek(pf, 0, SEEK_SET);
    std::vector<uint8_t> pack((size_t)sz);
    size_t rd = fread(pack.data(), 1, (size_t)sz, pf);
    fclose(pf);
    if ((long)rd != sz) { fprintf(stderr, "Short read on pack\n"); return 1; }

    RdNewEngine eng;
    if (!eng.loadPack(pack.data(), pack.size())) {
        fprintf(stderr, "loadPack failed\n");
        return 1;
    }

    Mcu mcu(0);
    mcu.reset();
    mcu.commands_queue.push(0x30);
    mcu.commands_queue.push(0xE0);
    mcu.commands_queue.push(0x00);
    mcu.commands_queue.push(0x00);
    for (int i = 0; i < 1024; i++) mcu.get_next_sample();
    mcu.loadPatch(patch);
    uint32_t rate = (uint32_t)mcu.getCurrentSampleRate();

    // Fresh-boot quirk: the firmware kills high FIRST notes after ~70 samples.
    // A quiet primer note settles the allocator; its tail is negligible at vel 1.
    mcu.sendMidiCmd(0x90, 60, 1);
    for (size_t i = 0; i < (size_t)(0.05 * rate); ++i) mcu.get_next_sample();
    mcu.sendMidiCmd(0x80, 60, 0);
    for (size_t i = 0; i < (size_t)(1.0 * rate); ++i) mcu.get_next_sample();

    size_t onSamples  = (size_t)(2.0 * rate);
    size_t offSamples = (size_t)(1.5 * rate);

    std::vector<int32_t> bufRef, bufNew;
    bufRef.reserve(onSamples + offSamples);
    bufNew.reserve(onSamples + offSamples);

    mcu.sendMidiCmd(0x90, note, vel);
    for (size_t i = 0; i < onSamples; i++) bufRef.push_back(mcu.get_next_sample());
    mcu.sendMidiCmd(0x80, note, 0);
    for (size_t i = 0; i < offSamples; i++) bufRef.push_back(mcu.get_next_sample());

    eng.noteOn((uint8_t)note, (uint8_t)vel);
    for (size_t i = 0; i < onSamples; i++) bufNew.push_back(eng.renderSample());
    eng.noteOff((uint8_t)note);
    for (size_t i = 0; i < offSamples; i++) bufNew.push_back(eng.renderSample());

    // Lag-scan cross-correlation (onset alignment is hypersensitive: a few
    // samples of shift flip r at high frequencies).
    size_t start = (size_t)(0.1 * rate);
    size_t L = (size_t)(1.5 * rate);
    if (start + L > std::min(bufRef.size(), bufNew.size())) {
        L = std::min(bufRef.size(), bufNew.size()) - start;
    }

    double bestR = -2.0;
    long bestLag = 0;
    for (long lag = -300; lag <= 300; ++lag) {
        double sAB = 0.0, sAA = 0.0, sBB = 0.0;
        for (size_t i = start; i < start + L; i += 2) {
            long j = (long)i + lag;
            if (j < 0 || (size_t)j >= bufNew.size()) continue;
            double a = bufRef[i];
            double b = bufNew[(size_t)j];
            sAB += a * b;
            sAA += a * a;
            sBB += b * b;
        }
        if (sAA > 0.0 && sBB > 0.0) {
            double r = sAB / std::sqrt(sAA * sBB);
            if (r > bestR) {
                bestR = r;
                bestLag = lag;
            }
        }
    }

    double sumSqRef = 0.0, sumSqNew = 0.0;
    for (size_t i = start; i < start + L; ++i) {
        sumSqRef += (double)bufRef[i] * bufRef[i];
        sumSqNew += (double)bufNew[i] * bufNew[i];
    }
    double rmsRef = std::sqrt(sumSqRef / L);
    double rmsNew = std::sqrt(sumSqNew / L);
    double rmsRatio = (rmsRef > 0.0) ? (rmsNew / rmsRef) : 0.0;

    printf("bestLag=%ld r=%.6f rmsRef=%.1f rmsNew=%.1f rmsRatio=%.4f activeVoices=%d\n",
           bestLag, bestR, rmsRef, rmsNew, rmsRatio, eng.activeVoices());


    char path[512];
    snprintf(path, sizeof(path), "%s_ref.wav", prefix);
    writeWav(path, bufRef, rate);
    snprintf(path, sizeof(path), "%s_new.wav", prefix);
    writeWav(path, bufNew, rate);

    return 0;
}
