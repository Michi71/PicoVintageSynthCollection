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
struct Ev { uint64_t at; uint8_t note; bool on; };
// fwd decl of Mcu-using helpers comes after includes



static uint64_t buildEvents(std::vector<Ev>& events, const char* mode, uint32_t rate, Mcu* mcu) {
    events.clear();
    uint64_t lastAt = 0;

    if (std::strcmp(mode, "fast") == 0) {
        for (int i = 0; i < 40; ++i) {
            uint8_t note = 96 - i * 2;
            uint64_t onAt = static_cast<uint64_t>(i * 0.015 * rate);
            uint64_t offAt = onAt + static_cast<uint64_t>(0.120 * rate);
            events.push_back({onAt, note, true});
            events.push_back({offAt, note, false});
            lastAt = std::max(lastAt, offAt);
        }
    } else if (std::strcmp(mode, "chord") == 0) {
        for (int i = 0; i < 8; ++i) {
            uint8_t b = 48 + i * 3;
            uint64_t onAt = static_cast<uint64_t>(i * 0.200 * rate);
            uint64_t offAt = onAt + static_cast<uint64_t>(0.150 * rate);
            int notes[6] = {b, b+4, b+7, b+12, b+16, b+19};
            for (int n = 0; n < 6; ++n) {
                events.push_back({onAt, static_cast<uint8_t>(notes[n]), true});
                events.push_back({offAt, static_cast<uint8_t>(notes[n]), false});
                lastAt = std::max(lastAt, offAt);
            }
        }
    } else if (std::strcmp(mode, "pedal") == 0) {
        mcu->sendMidiCmd(0xB0, 64, 127);
        for (int i = 0; i < 40; ++i) {
            uint8_t note = 96 - i * 2;
            uint64_t onAt = static_cast<uint64_t>(i * 0.015 * rate);
            events.push_back({onAt, note, true});
            lastAt = std::max(lastAt, onAt);
        }
        lastAt = lastAt + static_cast<uint64_t>(0.5 * rate); // pedal released here later
    }

    std::sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) {
        return a.at < b.at;
    });

    return lastAt;
}

static void renderBlocks(Mcu* mcu, std::vector<Ev>& events, uint64_t totalSamples, uint32_t rate, const char* mode, uint64_t pedalOffAt, std::vector<int16_t>& samples) {
    size_t ev_idx = 0;
    for (uint64_t blockStart = 0; blockStart < totalSamples; blockStart += 64) {
        if (std::strcmp(mode, "pedal") == 0) {
            if (pedalOffAt >= blockStart && pedalOffAt < blockStart + 64) {
                mcu->sendMidiCmd(0xB0, 64, 0);
            }
        }

        // Device timing: the audio IRQ drains the event ring only at block start.
        while (ev_idx < events.size() && events[ev_idx].at < blockStart + 64) {
            if (events[ev_idx].on) {
                mcu->sendMidiCmd(0x90, events[ev_idx].note, 110);
            } else {
                mcu->sendMidiCmd(0x80, events[ev_idx].note, 0);
            }
            ev_idx++;
        }

        for (int i = 0; i < 64; ++i) {
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
    }
}

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

    const char* mode = (argc > 3) ? argv[3] : "fast";
    std::vector<Ev> events;
    uint64_t lastAt = buildEvents(events, mode, rate, mcu);
    printf("mode=%s events=%zu\n", mode, events.size());

    uint64_t totalSamples = ((lastAt + (uint64_t)(3.0 * rate)) / 64 + 1) * 64;
    std::vector<int16_t> samples;
    samples.reserve(totalSamples);

    uint64_t pedalOffAt = lastAt;
    renderBlocks(mcu, events, totalSamples, rate, mode, pedalOffAt, samples);

    // Tail check: stuck notes keep ringing after all offs + decay.
    {
        uint64_t tailStart = samples.size() > (size_t)rate ? samples.size() - rate : 0;
        double acc = 0.0;
        for (size_t n = tailStart; n < samples.size(); ++n) {
            double v = (double)samples[n] / 32768.0;
            acc += v*v;
        }
        double tailRms = sqrt(acc / (double)(samples.size() - tailStart));
        printf("Tail RMS (last 1s): %.6f %s\n", tailRms, tailRms > 0.01 ? "<-- STUCK NOTES?" : "(ok)");
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
