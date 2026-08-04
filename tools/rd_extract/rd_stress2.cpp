// Bridge-level stress: identical-chord retriggers + sustain pedal, like the
// user's MIDI file. Renders through RD_Synth_Bridge (v2) on the host.
#include "RD_Synth_Bridge_v2.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <thread>
#include <atomic>

int main(int argc, char** argv) {
    int instr = argc > 1 ? atoi(argv[1]) : 4;
    int usePedal = argc > 2 ? atoi(argv[2]) : 1;
    int dual = argc > 3 ? atoi(argv[3]) : 0;
    RD_Synth_Bridge br;
    br.init();
    std::atomic<bool> stopW{false};
    std::thread wt;
    if (dual) {
        RdNewEngine::worker_enable(br.engineForWorker(), true);
        wt = std::thread([&]{ while (!stopW.load(std::memory_order_relaxed)) RdNewEngine::worker_poll(); });
        printf("dual-core block path ENABLED\n");
    }
    br.setInstrument((uint8_t)instr);
    uint32_t rate = br.currentSampleRate();

    int chord[6] = {48, 55, 60, 64, 67, 72};
    static int32_t out[128];

    auto renderMs = [&](double ms) {
        uint32_t n = (uint32_t)(ms * rate / 1000.0);
        for (uint32_t s = 0; s < n; s += 64) {
            int chunk = (int)((n - s) < 64 ? (n - s) : 64);
            br.fill_buffer_i32(out, chunk);
        }
    };

    // The user's pattern: same chord hammered repeatedly, pedal engaged.
    if (usePedal) br.sustain(127);
    for (int rep = 0; rep < 60; rep++) {
        for (int c = 0; c < 6; c++) br.noteOn((uint8_t)chord[c], 110);
        renderMs(60);
        for (int c = 0; c < 6; c++) br.noteOff((uint8_t)chord[c]);
        renderMs(40);
        if (usePedal && (rep % 7 == 6)) { br.sustain(0); br.sustain(127); } // pedal pumping
    }
    if (usePedal) br.sustain(0);

    // 6 s silence; measure the last 2 s.
    double acc = 0; uint32_t nAcc = 0;
    uint32_t total = 6 * rate;
    for (uint32_t s = 0; s < total; s += 64) {
        int chunk = (int)((total - s) < 64 ? (total - s) : 64);
        br.fill_buffer_i32(out, chunk);
        if (s > 4 * rate) {
            for (int k = 0; k < chunk; k++) {
                double v = (double)(out[2*k] >> 16) / 32768.0;
                acc += v * v; nAcc++;
            }
        }
    }
    double rms = sqrt(acc / (double)(nAcc ? nAcc : 1));
    if (dual) { stopW.store(true); wt.join(); }
    printf("instr=%d pedal=%d dual=%d tailRMS=%.6f %s\n", instr, usePedal, dual, rms,
           rms > 0.001 ? "<-- STUCK!" : "(clean)");
    return 0;
}
