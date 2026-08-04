// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// tools/host_tests/yc/yc_engine_host_test.cpp
// Host test program for the PicoFaceYC engine (C++17, no Pico SDK needed).
// Build and run it with ./build_yc.sh; it renders a WAV next to the binary.

#include "yc_engine/yc_engine.h"

#include <vector>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

static void write_wav_mono_16bit(const char* filename,
                                 const std::vector<float>& samples,
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
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t chunk_size = 36 + data_bytes;

    // RIFF Header
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk_size, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);

    // fmt chunk
    std::fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    std::fwrite(&fmt_size, 4, 1, f);
    uint16_t audio_format = 1; // PCM
    std::fwrite(&audio_format, 2, 1, f);
    std::fwrite(&num_channels, 2, 1, f);
    std::fwrite(&sample_rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);
    std::fwrite(&bits_per_sample, 2, 1, f);

    // data chunk
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);

    for (float s : samples) {
        // float [-1,1] -> int16_t mit Clamping
        float clamped = std::max(-1.0f, std::min(1.0f, s));
        int16_t val = static_cast<int16_t>(clamped * 32767.0f);
        std::fwrite(&val, sizeof(int16_t), 1, f);
    }

    std::fclose(f);
    std::printf("WAV geschrieben: %s (%u Samples, %u Hz, 16-bit mono)\n",
                filename, (uint32_t)samples.size(), sample_rate);
}

int main()
{
    // 1. Engine initialisieren
    yc_engine_state_t state{};
    yc_rotary_state_t rstate{};
    yc_percussion_state_t pstate{};
    yc_vibrato_state_t vstate{};
    yc_reverb_state_t reverb_state{};
    yc_engine_init(state);
    yc_engine_set_param(state, 11, 1); // PERC_ON
    yc_engine_set_param(state, 15, 3); // VIBCHO_DEPTH
    state.rotary_speed = 3; // FAST
    yc_engine_set_param(state, 16, 60); // DISTORTION
    yc_engine_set_param(state, 17, 80); // REVERB

    std::printf("=== PicoFaceYC Engine Host-Test ===\n");

    // 2. C-Dur-Akkord: C4(60), E4(64), G4(67), velocity 100
    yc_engine_note_on(state, pstate, 60, 100);
    yc_engine_note_on(state, pstate, 64, 100);
    yc_engine_note_on(state, pstate, 67, 100);
    std::printf("Note On: C4(60), E4(64), G4(67) vel=100\n");

    // 3. 1 Sekunde rendern (44100 Samples), Bloecke zu 64
    const int total_samples = 44100;
    const int block_size = 64;
    const int half_samples = total_samples / 2; // 0.5s

    std::vector<float> audio;
    audio.reserve(total_samples);

    float outL[block_size];
    float outR[block_size];

    int rendered = 0;
    while (rendered < total_samples) {
        int n = std::min(block_size, total_samples - rendered);

        // 4. Note-Off fuer E4(64) nach 0.5s
        if (rendered < half_samples && rendered + n >= half_samples) {
            yc_engine_note_off(state, 64);
            std::printf("Note Off: E4(64) bei Sample %d (0.5s)\n", half_samples);
        }

        yc_engine_render_block(state, rstate, vstate, pstate, reverb_state, outL, outR, n);

        for (int i = 0; i < n; ++i) {
            audio.push_back(outL[i]); // nur linker Kanal (Mono)
        }
        rendered += n;
    }

    std::printf("Rendert: %d Samples\n", rendered);

    // 5. Sanity-Checks
    float peak = 0.0f;
    double sum_sq = 0.0;
    int nan_inf_count = 0;
    int nonzero_after_100 = 0;

    for (int i = 0; i < (int)audio.size(); ++i) {
        float s = audio[i];

        if (std::isnan(s) || std::isinf(s)) {
            nan_inf_count++;
            continue;
        }

        float abs_s = std::fabs(s);
        if (abs_s > peak) peak = abs_s;
        sum_sq += (double)s * (double)s;

        if (i >= 100 && s != 0.0f) {
            nonzero_after_100++;
        }
    }

    double rms = std::sqrt(sum_sq / (double)audio.size());

    std::printf("\n--- Sanity-Check Ergebnisse ---\n");
    std::printf("Peak-Amplitude (max abs):     %.6f\n", peak);
    std::printf("RMS:                         %.6f\n", rms);
    std::printf("Anzahl NaN/Inf-Samples:     %d  (muss 0 sein)\n", nan_inf_count);
    std::printf("Samples !=0 ab Sample 100:  %d / %d\n",
                nonzero_after_100, (int)audio.size() - 100);

    if (nan_inf_count == 0)
        std::printf("[OK] Keine NaN/Inf-Samples\n");
    else
        std::printf("[FEHLER] NaN/Inf gefunden!\n");

    if (nonzero_after_100 > 0)
        std::printf("[OK] Ton wird erzeugt (nicht nur Stille)\n");
    else
        std::printf("[WARNUNG] Nach Sample 100 nur Stille?\n");

    if (peak > 0.001f)
        std::printf("[OK] Signal hat hörbare Amplitude (peak > 0.001)\n");
    else
        std::printf("[WARNUNG] Sehr niedrige Amplitude\n");

    // 6. WAV-Datei schreiben
    write_wav_mono_16bit("test/yc_engine_host_test_output.wav",
                         audio, 44100);

    std::printf("\nTest abgeschlossen.\n");
    return 0;
}
