// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// tools/host_tests/ob/ob_engine_host_test.cpp
// Host test program for the PicoFaceOB engine (C++17, no Pico SDK needed).
// Build and run it with ./build_ob.sh.
//
// Regression checks for the 2026-08 review fixes:
//   1. silence at boot, sound after note on, silence again after release
//   2. pitch bend obeys the bend assembly ranges (Narrow 2, Broad 12 semis)
//   3. LFO->cutoff works with LFO->pitch at zero (independent depth)
//   4. the modulation lever ADDS vibrato on top of a programmed depth of 0
//   5. every factory preset renders finite, bounded samples
//   6. applyPreset() leaves the bend range switch alone
// Afterwards it renders a short '32 Rez Bass phrase to a WAV for audition.

#include "OB_Engine.h"

#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

static constexpr float kSampleRate = 44100.f;
static int g_failures = 0;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            std::printf("[pass] " __VA_ARGS__);                                                    \
            std::printf("\n");                                                                     \
        } else {                                                                                   \
            std::printf("[FAIL] " __VA_ARGS__);                                                    \
            std::printf("\n");                                                                     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

// Renders in firmware-sized chunks of 64 so the engine sees the same call
// pattern as on the device.
static void render(OB_Engine& eng, std::vector<float>& out, float seconds)
{
    const int total = (int)(seconds * kSampleRate);
    float buf[64];
    int done = 0;
    while (done < total) {
        const int n = std::min(64, total - done);
        eng.renderBlock(buf, n);
        out.insert(out.end(), buf, buf + n);
        done += n;
    }
}

static void discard(OB_Engine& eng, float seconds)
{
    std::vector<float> tmp;
    render(eng, tmp, seconds);
}

static float rms(const std::vector<float>& v, size_t from = 0)
{
    if (from >= v.size()) return 0.f;
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i) acc += (double)v[i] * v[i];
    return (float)std::sqrt(acc / (double)(v.size() - from));
}

// Fundamental estimate: rising zero crossings with +-0.05 hysteresis. The
// saw's discontinuity sits at the amplitude extremes, so the slow ramp gives
// exactly one rising crossing per period.
static float measureFreq(OB_Engine& eng, float settleSec = 0.25f, float measureSec = 0.75f)
{
    discard(eng, settleSec);
    std::vector<float> v;
    render(eng, v, measureSec);

    int crossings = 0;
    bool below = false;
    for (float s : v) {
        if (s < -0.05f) below = true;
        else if (below && s > 0.05f) { crossings++; below = false; }
    }
    return (float)crossings / measureSec;
}

// Frequency per window, for vibrato spread checks.
static void windowFreqs(const std::vector<float>& v, int windowSamples, std::vector<float>& out)
{
    for (size_t start = 0; start + windowSamples <= v.size(); start += windowSamples) {
        int crossings = 0;
        bool below = false;
        for (int i = 0; i < windowSamples; ++i) {
            const float s = v[start + i];
            if (s < -0.05f) below = true;
            else if (below && s > 0.05f) { crossings++; below = false; }
        }
        out.push_back((float)crossings * kSampleRate / (float)windowSamples);
    }
}

static void write_wav_mono_16bit(const char* filename, const std::vector<float>& samples,
                                 uint32_t sample_rate)
{
    FILE* f = std::fopen(filename, "wb");
    if (!f) {
        std::fprintf(stderr, "Konnte WAV-Datei nicht öffnen: %s\n", filename);
        return;
    }

    const uint16_t num_channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    const uint16_t block_align = num_channels * (bits_per_sample / 8);
    const uint32_t data_bytes = (uint32_t)(samples.size() * sizeof(int16_t));
    const uint32_t chunk_size = 36 + data_bytes;

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk_size, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    std::fwrite(&fmt_size, 4, 1, f);
    uint16_t audio_format = 1;
    std::fwrite(&audio_format, 2, 1, f);
    std::fwrite(&num_channels, 2, 1, f);
    std::fwrite(&sample_rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);
    std::fwrite(&bits_per_sample, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);

    for (float s : samples) {
        const float clamped = std::max(-1.0f, std::min(1.0f, s));
        const int16_t val = (int16_t)(clamped * 32767.0f);
        std::fwrite(&val, sizeof(int16_t), 1, f);
    }

    std::fclose(f);
    std::printf("WAV geschrieben: %s (%u Samples, %u Hz, 16-bit mono)\n", filename,
                (uint32_t)samples.size(), sample_rate);
}

// A fresh engine with the pitch scatter disabled, so frequency measurements
// are exact.
static void initClean(OB_Engine& eng)
{
    eng.init(kSampleRate);
    eng.setParam(OB_VOICE_SLOP, 0.f);
}

int main(int argc, char** argv)
{
    (void)argc;
    // The WAV goes next to the binary, whatever the current directory is.
    std::string wavPath = "ob_engine_host_test.wav";
    if (const char* slash = std::strrchr(argv[0], '/')) {
        wavPath = std::string(argv[0], (size_t)(slash + 1 - argv[0])) + "ob_engine_host_test.wav";
    }

    // --- 1. silence / sound / silence ------------------------------------
    {
        OB_Engine eng;
        initClean(eng);

        std::vector<float> v;
        render(eng, v, 0.25f);
        CHECK(rms(v) < 1e-6f, "Stille nach dem Einschalten (RMS %.2e)", rms(v));

        eng.noteOn(69, 100);
        v.clear();
        render(eng, v, 0.5f);
        CHECK(rms(v, v.size() / 2) > 0.02f, "Note klingt (RMS %.3f)", rms(v, v.size() / 2));

        eng.noteOff(69);
        discard(eng, 3.f);
        v.clear();
        render(eng, v, 0.25f);
        CHECK(rms(v) < 1e-5f, "Release klingt aus (RMS %.2e)", rms(v));
        CHECK(eng.soundingVoices() == 0, "keine Stimme mehr aktiv (%d)", eng.soundingVoices());
    }

    // --- 2. pitch bend: Narrow 2 semitones, Broad 12 ----------------------
    {
        OB_Engine eng;
        initClean(eng);
        eng.noteOn(69, 100);

        const float f0 = measureFreq(eng);
        CHECK(std::fabs(f0 - 440.f) < 440.f * 0.02f, "A4 = %.1f Hz", f0);

        eng.setPitchBend(1.f);
        const float fNarrowUp = measureFreq(eng);
        const float expNarrow = 440.f * std::pow(2.f, 2.f / 12.f);
        CHECK(std::fabs(fNarrowUp - expNarrow) < expNarrow * 0.02f,
              "Bend +1 Narrow: %.1f Hz (soll %.1f, +2 Halbtoene)", fNarrowUp, expNarrow);

        eng.setParam(OB_BEND_RANGE, 1.f);
        const float fBroadUp = measureFreq(eng);
        CHECK(std::fabs(fBroadUp - 880.f) < 880.f * 0.02f,
              "Bend +1 Broad: %.1f Hz (soll 880, +1 Oktave)", fBroadUp);

        eng.setPitchBend(-1.f);
        const float fBroadDown = measureFreq(eng);
        CHECK(std::fabs(fBroadDown - 220.f) < 220.f * 0.02f,
              "Bend -1 Broad: %.1f Hz (soll 220, -1 Oktave)", fBroadDown);
    }

    // --- 3. LFO->cutoff with LFO->pitch at zero ---------------------------
    {
        OB_Engine eng;
        initClean(eng);
        eng.setParam(OB_CUTOFF, 0.30f);       // ~260 Hz, modulation clearly audible
        eng.setParam(OB_LFO_RATE, 0.42f);     // ~2 Hz on the upstream curve
        eng.setParam(OB_LFO_TO_PITCH, 0.f);   // the regression: pitch depth zero...
        eng.setParam(OB_LFO_TO_CUTOFF, 0.8f); // ...must not silence the cutoff depth
        eng.noteOn(45, 100);
        discard(eng, 0.5f);

        std::vector<float> v;
        render(eng, v, 2.f);
        std::vector<float> w;
        const int win = 2048;
        for (size_t s = 0; s + win <= v.size(); s += win) {
            std::vector<float> part(v.begin() + s, v.begin() + s + win);
            w.push_back(rms(part));
        }
        const float hi = *std::max_element(w.begin(), w.end());
        const float lo = *std::min_element(w.begin(), w.end());
        CHECK(lo > 0.f && hi / lo > 1.5f,
              "Filter wobbelt ohne LFO->Pitch (Fenster-RMS max/min = %.2f)", hi / lo);

        eng.setParam(OB_LFO_TO_CUTOFF, 0.f);
        discard(eng, 0.5f);
        v.clear();
        render(eng, v, 2.f);
        w.clear();
        for (size_t s = 0; s + win <= v.size(); s += win) {
            std::vector<float> part(v.begin() + s, v.begin() + s + win);
            w.push_back(rms(part));
        }
        const float hi2 = *std::max_element(w.begin(), w.end());
        const float lo2 = *std::min_element(w.begin(), w.end());
        CHECK(lo2 > 0.f && hi2 / lo2 < 1.2f,
              "ohne Depth wieder statisch (Fenster-RMS max/min = %.2f)", hi2 / lo2);
    }

    // --- 3b. LFO->volume tremolo (own depth, decoupled from amt2) ---------
    {
        OB_Engine eng;
        initClean(eng);
        eng.setParam(OB_LFO_RATE, 0.42f);  // ~2 Hz
        eng.setParam(OB_LFO_TO_VOL, 0.8f);
        eng.noteOn(57, 100);
        discard(eng, 0.5f);
        std::vector<float> v;
        render(eng, v, 2.f);
        std::vector<float> w;
        const int win = 2048;
        for (size_t s = 0; s + win <= v.size(); s += win) {
            std::vector<float> part(v.begin() + s, v.begin() + s + win);
            w.push_back(rms(part));
        }
        const float ratio = *std::max_element(w.begin(), w.end()) /
                            *std::min_element(w.begin(), w.end());
        CHECK(ratio > 1.3f, "Tremolo wobbelt (Fenster-RMS max/min = %.2f)", ratio);
    }

    // --- 4. modulation lever adds vibrato on a depth-0 patch --------------
    {
        OB_Engine eng;
        initClean(eng);
        eng.setParam(OB_LFO_WAVE, 0.75f);   // square: pitch alternates two values
        eng.setParam(OB_LFO_RATE, 0.42f);   // ~2 Hz
        eng.setParam(OB_LFO_TO_PITCH, 0.f); // programmed depth zero
        eng.noteOn(69, 100);

        eng.setModWheel(0.f);
        discard(eng, 0.5f);
        std::vector<float> v;
        render(eng, v, 2.f);
        std::vector<float> f;
        windowFreqs(v, (int)(0.2f * kSampleRate), f);
        const float flat = *std::max_element(f.begin(), f.end()) /
                           *std::min_element(f.begin(), f.end());

        eng.setModWheel(1.f);
        discard(eng, 0.5f);
        v.clear();
        render(eng, v, 2.f);
        f.clear();
        windowFreqs(v, (int)(0.2f * kSampleRate), f);
        const float wob = *std::max_element(f.begin(), f.end()) /
                          *std::min_element(f.begin(), f.end());

        CHECK(flat < 1.02f, "Hebel unten: Tonhoehe steht (Spread %.3f)", flat);
        CHECK(wob > 1.04f, "Hebel oben: Vibrato da (Spread %.3f, soll ~1.06)", wob);
    }

    // --- 5. every preset renders finite, bounded samples ------------------
    {
        OB_Engine eng;
        initClean(eng);
        // Failure = NaN/inf or a runaway level. Merely HOT presets (the
        // engine can exceed +-1 with ring mod and Xpander mixes; upstream
        // does the same and the DAC path clips) are only reported.
        bool allGood = true;
        int hot = 0;
        for (int p = 0; p < OB_NPRESETS; ++p) {
            eng.applyPreset(p);
            eng.noteOn(48, 100);
            eng.noteOn(55, 100);
            eng.noteOn(60, 100);
            std::vector<float> v;
            render(eng, v, 0.6f);
            eng.allNotesOff();
            discard(eng, 0.2f);
            float peak = 0.f;
            bool finite = true;
            for (float s : v) {
                if (!std::isfinite(s)) { finite = false; break; }
                peak = std::max(peak, std::fabs(s));
            }
            if (!finite || peak > 32.f) {
                std::printf("       Preset %d (%s): %s, Peak %.1f\n", p, obPresets[p].name,
                            finite ? "Runaway" : "NaN/inf", peak);
                allGood = false;
            } else if (peak > 4.f) {
                hot++;
            }
        }
        CHECK(allGood, "alle %d Presets endlich und stabil (%d davon heiss, clippen am DAC)",
              OB_NPRESETS, hot);
    }

    // --- 6. applyPreset leaves the bend range switch alone ----------------
    {
        OB_Engine eng;
        initClean(eng);
        eng.setParam(OB_BEND_RANGE, 1.f);
        eng.applyPreset(0);
        CHECK(eng.getParam(OB_BEND_RANGE) == 1.f, "Bend-Range ueberlebt den Preset-Wechsel");
    }

    // --- WAV demo: '32 Rez Bass phrase ------------------------------------
    {
        OB_Engine eng;
        eng.init(kSampleRate);
        int rez = 0;
        for (int p = 0; p < OB_NPRESETS; ++p) {
            if (std::strcmp(obPresets[p].name, "'32 Rez Bass") == 0) { rez = p; break; }
        }
        eng.applyPreset(rez);

        std::vector<float> v;
        const uint8_t line[] = {36, 36, 43, 36, 46, 43, 36, 36};
        for (uint8_t note : line) {
            eng.noteOn(note, 100);
            render(eng, v, 0.22f);
            eng.noteOff(note);
            render(eng, v, 0.08f);
        }
        render(eng, v, 1.5f);
        write_wav_mono_16bit(wavPath.c_str(), v, (uint32_t)kSampleRate);
    }

    std::printf(g_failures ? "\n%d Test(s) FEHLGESCHLAGEN\n" : "\nAlle Tests bestanden.\n",
                g_failures);
    return g_failures ? 1 : 0;
}
