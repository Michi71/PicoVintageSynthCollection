#include "mcu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <climits>

// Helper: write little-endian 16/32 bit to FILE
static void w16(FILE* f, uint16_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}
static void w32(FILE* f, uint32_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

int main(int argc, char** argv) {
    int patch = argc > 1 ? atoi(argv[1]) : 0;

    // 1. Init MCU
    Mcu* mcu = new Mcu(0);
    mcu->reset();

    // 2. Handshake
    mcu->commands_queue.push(0x30);
    mcu->commands_queue.push(0xE0);
    mcu->commands_queue.push(0x00);
    mcu->commands_queue.push(0x00);
    for (int i = 0; i < 1024; ++i) {
        mcu->get_next_sample();
    }

    // 3. Load patch and print info
    mcu->loadPatch((u8)patch);
    char nm[64];
    mcu->getPatchName(nm);
    u32 rate = mcu->getCurrentSampleRate();
    printf("Patch=%d Name=%s SampleRate=%u\n", patch, nm, rate);

    std::vector<int16_t> buffer;
    int samplesPerSec = (int)rate;

    // Render helper for phases
    auto renderPhase = [&](double seconds, int phase) {
        int samples = (int)(samplesPerSec * seconds);
        s32 rawMin = INT32_MAX;
        s32 rawMax = INT32_MIN;
        double sumAbs = 0.0;
        for (int i = 0; i < samples; ++i) {
            s32 raw = mcu->get_next_sample();
            if (raw < rawMin) rawMin = raw;
            if (raw > rawMax) rawMax = raw;
            sumAbs += std::abs(raw);
            int16_t s = (int16_t)std::max(-32768, std::min(32767, (int)raw));
            buffer.push_back(s);
        }
        double avgAbs = samples > 0 ? sumAbs / samples : 0.0;
        printf("phase %d: samples=%d rawMin=%d rawMax=%d avgAbs=%.2f\n", phase, samples, rawMin, rawMax, avgAbs);
    };

    // 4. Render 4 phases
    renderPhase(0.5, 1);
    mcu->sendMidiCmd(0x90, 60, 100);
    renderPhase(2.0, 2);
    mcu->sendMidiCmd(0x80, 60, 0);
    renderPhase(1.0, 3);
    mcu->sendMidiCmd(0x90, 72, 110);
    renderPhase(1.0, 4);

    // 6. Write WAV file (mono, 16 bit)
    const char* outName = argc > 2 ? argv[2] : "rd_test.wav";
    FILE* f = fopen(outName, "wb");
    if (f) {
        uint32_t dataSize = (uint32_t)buffer.size() * 2;
        uint16_t numChannels = 1;
        uint16_t bitsPerSample = 16;
        uint32_t byteRate = rate * numChannels * bitsPerSample / 8;
        uint16_t blockAlign = numChannels * bitsPerSample / 8;
        uint32_t chunkSize = 36 + dataSize;

        fwrite("RIFF", 1, 4, f);
        w32(f, chunkSize);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        w32(f, 16);
        w16(f, 1);
        w16(f, numChannels);
        w32(f, rate);
        w32(f, byteRate);
        w16(f, blockAlign);
        w16(f, bitsPerSample);
        fwrite("data", 1, 4, f);
        w32(f, dataSize);
        for (size_t i = 0; i < buffer.size(); ++i) {
            w16(f, (uint16_t)buffer[i]);
        }
        fclose(f);
    }

    delete mcu;
    return 0;
}
