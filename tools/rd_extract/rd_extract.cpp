#include "mcu.h"
#include "rd_capture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <patch> <outpath> [notes] [velocity]\n", argv[0]);
        return 1;
    }
    int patch = atoi(argv[1]);
    const char* outpath = argv[2];
    std::string notesStr = (argc > 3) ? argv[3] : "36,48,60,72,84,96";
    int vel = (argc > 4) ? atoi(argv[4]) : 100;

    // Parse comma-separated note list
    std::vector<int> notes;
    {
        const char* p = notesStr.c_str();
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            int n = atoi(p);
            notes.push_back(n);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }

    // Boot MCU
    Mcu mcu(0);
    mcu.reset();
    mcu.commands_queue.push(0x30);
    mcu.commands_queue.push(0xE0);
    mcu.commands_queue.push(0x00);
    mcu.commands_queue.push(0x00);
    for (int i = 0; i < 1024; i++) mcu.get_next_sample();

    mcu.loadPatch((u8)patch);
    char name[64];
    mcu.getPatchName(name);
    int rate = (int)mcu.getCurrentSampleRate();
    printf("Patch %d: \"%s\" @ %d Hz\n", patch, name, rate);

    // Primer: the firmware kills the very first note after a fresh boot
    // (~70 samples). Play and discard a quiet note so every captured note
    // sees a settled allocator.
    mcu.sendMidiCmd(0x90, 60, 1);
    for (uint64_t i = 0; i < (uint64_t)(0.2 * rate); i++) mcu.get_next_sample();
    mcu.sendMidiCmd(0x80, 60, 0);
    for (uint64_t i = 0; i < (uint64_t)(1.0 * rate); i++) mcu.get_next_sample();

    // 4 s attack window: long decays (vibraphone!) exceed 2 s and got their
    // segment chains truncated in the first sweep.
    uint64_t attackSamples = (uint64_t)(4.0 * (double)rate);
    uint64_t releaseSamples = (uint64_t)(2.0 * (double)rate);

    FILE* out = fopen(outpath, "a");
    if (!out) { fprintf(stderr, "cannot open %s for append\n", outpath); return 1; }

    for (int note : notes) {
        std::vector<RdCaptureEvent> log;
        g_rd_capture = &log;
        g_rd_capture_clock = 0;

        mcu.sendMidiCmd(0x90, (u8)note, (u8)vel);
        for (uint64_t i = 0; i < attackSamples; i++) {
            mcu.get_next_sample();
            g_rd_capture_clock++;
        }

        uint64_t noteOffClock = g_rd_capture_clock;
        mcu.sendMidiCmd(0x80, (u8)note, 0);
        for (uint64_t i = 0; i < releaseSamples; i++) {
            mcu.get_next_sample();
            g_rd_capture_clock++;
        }

        g_rd_capture = nullptr;

        uint32_t voiceMask = 0;
        uint8_t highestPart = 0;
        for (const auto& e : log) {
            if (e.voice < 32) voiceMask |= (1u << e.voice);
            if (e.part > highestPart) highestPart = e.part;
        }
        int voiceCount = 0;
        for (int v = 0; v < 32; v++) if (voiceMask & (1u << v)) voiceCount++;

        for (const auto& e : log) {
            fprintf(out,
                "{\"patch\":%d,\"note\":%d,\"vel\":%d,\"t\":%llu,\"noteoff_t\":%llu,\"voice\":%u,\"part\":%u,\"field\":%u,\"val\":%u}\n",
                patch, note, vel,
                (unsigned long long)e.sample,
                (unsigned long long)noteOffClock,
                (unsigned)e.voice, (unsigned)e.part,
                (unsigned)e.field, (unsigned)e.value);
        }
        fflush(out);

        printf("Note %d vel %d: %zu events, %d voices, highest part %u (noteoff_t=%llu)\n",
               note, vel, log.size(), voiceCount,
               (unsigned)highestPart, (unsigned long long)noteOffClock);
    }

    fclose(out);
    return 0;
}
