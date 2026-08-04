#include "mcu.h"
#include "sound_chip.h"
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

// Little-endian WAV helpers
static void w16(FILE* f, uint16_t v){ fwrite(&v,2,1,f); }
static void w32(FILE* f, uint32_t v){ fwrite(&v,4,1,f); }

struct ClickEvent { size_t pos; int32_t delta; };

int main(int argc, char** argv){
    int patch = (argc > 1) ? atoi(argv[1]) : 0;
    const char* outfile = (argc > 2) ? argv[2] : "gliss.wav";

    // --- Boot ---
    Mcu* mcu = new Mcu(0);
    mcu->reset();
    mcu->commands_queue.push(0x30);
    mcu->commands_queue.push(0xE0);
    mcu->commands_queue.push(0x00);
    mcu->commands_queue.push(0x00);
    for (int i = 0; i < 1024; ++i) mcu->get_next_sample();

    mcu->loadPatch((u8)patch);
    char name[64] = {0};
    mcu->getPatchName(name);
    uint32_t rate = mcu->getCurrentSampleRate();
    printf("Patch=%d Name='%s' Rate=%u\n", patch, name, rate);

    // --- Build event list (sample-accurate) ---
    struct Ev { uint64_t at; uint8_t note; bool on; };
    std::vector<Ev> events;
    for (int i = 0; i < 30; ++i){
        uint8_t note = (uint8_t)(96 - i*2);          // 96,94,...,38
        uint64_t onAt  = (uint64_t)(i * 0.060 * (double)rate);
        uint64_t offAt = (uint64_t)(onAt + 0.250 * (double)rate);
        events.push_back({onAt, note, true});
        events.push_back({offAt, note, false});
    }
    std::sort(events.begin(), events.end(),
              [](const Ev& a, const Ev& b){ return a.at < b.at; });

    // reproduces the device-only dual-core protocol on the host;
    // the mask-split/merge logic is identical, only the memory model differs.
    int dual = argc > 3 && strcmp(argv[3], "dual") == 0;
    std::atomic<bool> stopWorker{false};
    std::thread worker;
    if (dual) {
        SoundChip::worker_enable(mcu->getSoundChip(), true);
        worker = std::thread([&]{
            while (!stopWorker.load(std::memory_order_relaxed))
                SoundChip::worker_poll();
        });
        printf("dual-core worker path ENABLED\n");
    }

    uint64_t totalSamples = (uint64_t)((30.0*0.060 + 3.0) * (double)rate);
    std::vector<int16_t> samples;
    samples.reserve(totalSamples);

    // --- Render loop ---
    size_t evIdx = 0;
    for (uint64_t n = 0; n < totalSamples; ++n){
        while (evIdx < events.size() && events[evIdx].at == n){
            const Ev& e = events[evIdx];
            if (e.on) mcu->sendMidiCmd(0x90, e.note, 110);
            else      mcu->sendMidiCmd(0x80, e.note, 0);
            ++evIdx;
        }
        int32_t raw = mcu->get_next_sample();
        // Firmware-identical output pipeline: /131072 scale + softclip 0.9
        float x = (float)raw * (1.0f / 131072.0f);
        const float a = 0.9f, range = 0.1f;
        if (x > a)       x =  a + range * (1.0f - 1.0f / (1.0f + (x - a) / range));
        else if (x < -a) x = -a - range * (1.0f - 1.0f / (1.0f + (-x - a) / range));
        int32_t d16 = (int32_t)(x * 32767.0f);
        if (d16 >  32767) d16 =  32767;
        if (d16 < -32768) d16 = -32768;
        samples.push_back((int16_t)d16);
    }

    if (dual) {
        stopWorker.store(true);
        worker.join();
    }

    // --- Click detector: hard discontinuities |x[n]-x[n-1]| > 8000 ---
    size_t totalClicks = 0;
    std::vector<ClickEvent> clicks;
    for (size_t n = 1; n < samples.size(); ++n){
        int32_t d  = (int32_t)samples[n] - (int32_t)samples[n-1];
        int32_t ad = std::abs(d);
        if (ad > 8000){ ++totalClicks; clicks.push_back({n, d}); }
    }
    std::sort(clicks.begin(), clicks.end(),
              [](const ClickEvent& a, const ClickEvent& b){
                  return std::abs(a.delta) > std::abs(b.delta); });

    printf("Total clicks (|delta|>8000): %zu\n", totalClicks);
    size_t show = std::min<size_t>(10, clicks.size());
    for (size_t i = 0; i < show; ++i){
        double t = (double)clicks[i].pos / (double)rate;
        printf("  #%zu pos=%zu t=%.4fs delta=%d\n",
               i+1, clicks[i].pos, t, clicks[i].delta);
    }

    // --- RMS per second ---
    uint64_t fullSec = (totalSamples + rate - 1) / rate;
    for (uint64_t s = 0; s < fullSec; ++s){
        uint64_t start = s * rate;
        uint64_t end = std::min<uint64_t>(start + rate, (uint64_t)samples.size());
        if (end <= start) break;
        double acc = 0.0;
        for (uint64_t n = start; n < end; ++n){
            double v = (double)samples[n] / 32768.0;
            acc += v*v;
        }
        double rms = std::sqrt(acc / (double)(end - start));
        printf("  RMS sec %llu: %.6f (%.2f dBFS)\n",
               (unsigned long long)s, rms, 20.0*std::log10(rms+1e-12));
    }

    // --- Write WAV (mono 16bit) ---
    FILE* f = fopen(outfile, "wb");
    if (!f){ perror("fopen"); return 1; }
    uint32_t dataSize = (uint32_t)(samples.size() * 2);
    w32(f, 0x46464952);            // 'RIFF'
    w32(f, 36 + dataSize);
    w32(f, 0x45564157);           // 'WAVE'
    w32(f, 0x20746D66);           // 'fmt '
    w32(f, 16);
    w16(f, 1);                    // PCM
    w16(f, 1);                    // mono
    w32(f, rate);
    w32(f, rate * 2);             // byte rate
    w16(f, 2);                    // block align
    w16(f, 16);                   // bits per sample
    w32(f, 0x61746164);           // 'data'
    w32(f, dataSize);
    fwrite(samples.data(), 2, samples.size(), f);
    fclose(f);
    printf("Wrote %s (%llu samples, %u bytes)\n",
           outfile, (unsigned long long)samples.size(), dataSize);

    delete mcu;
    return 0;
}
