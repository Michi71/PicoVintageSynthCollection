// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// tools/host_tests/yc/yc_engine_host_test.cpp
// Host test of the PicoFaceYC engine (C++17, no Pico SDK needed). Build and
// run it with ./build_yc.sh; it renders a stereo WAV next to the binary.
//
// Four things are pinned here, in this order:
//   1. a chord through the full chain (percussion, vibrato, distortion,
//      rotary FAST, reverb) renders finite, audible, and *stereo* - the
//      rotary is what tells the two channels apart;
//   2. with the rotary OFF the two channels are equal bit for bit, which is
//      the pre-stereo mono path;
//   3. pitch bend scales the cached phase increments by 2^(semitones/12) and
//      the centre restores them exactly;
//   4. expression 0 mutes voices and percussion alike, 127 is a gain of
//      exactly 1.

#include "yc_engine/yc_engine.h"

#include <vector>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) failures++;
}

static void write_wav_stereo_16bit(const char* filename,
                                   const std::vector<float>& l,
                                   const std::vector<float>& r,
                                   uint32_t sample_rate)
{
    FILE* f = std::fopen(filename, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open WAV for writing: %s\n", filename);
        return;
    }

    const uint16_t num_channels = 2;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    const uint16_t block_align = num_channels * (bits_per_sample / 8);
    const uint32_t frames = (uint32_t) std::min(l.size(), r.size());
    const uint32_t data_bytes = frames * block_align;
    const uint32_t chunk_size = 36 + data_bytes;

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk_size, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);

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

    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);

    for (uint32_t i = 0; i < frames; ++i) {
        const float c[2] = { l[i], r[i] };
        for (float s : c) {
            float clamped = std::max(-1.0f, std::min(1.0f, s));
            int16_t val = static_cast<int16_t>(clamped * 32767.0f);
            std::fwrite(&val, sizeof(int16_t), 1, f);
        }
    }

    std::fclose(f);
    std::printf("WAV written: %s (%u frames, %u Hz, 16-bit stereo)\n",
                filename, frames, sample_rate);
}

struct Engine {
    yc_engine_state_t state{};
    yc_rotary_state_t rstate{};
    yc_percussion_state_t pstate{};
    yc_vibrato_state_t vstate{};
    yc_reverb_state_t reverb_state{};
    Engine() { yc_engine_init(state); }
    void render(std::vector<float>& l, std::vector<float>& r, int samples,
                int note_off_at = -1, uint8_t note_off = 0)
    {
        const int block_size = 64;
        float outL[block_size], outR[block_size];
        int rendered = 0;
        while (rendered < samples) {
            int n = std::min(block_size, samples - rendered);
            if (note_off_at >= 0 && rendered < note_off_at && rendered + n >= note_off_at) {
                yc_engine_note_off(state, note_off);
            }
            yc_engine_render_block(state, rstate, vstate, pstate, reverb_state, outL, outR, (size_t) n);
            l.insert(l.end(), outL, outL + n);
            r.insert(r.end(), outR, outR + n);
            rendered += n;
        }
    }
};

int main(int argc, char** argv)
{
    (void)argc;
    std::printf("=== PicoFaceYC engine host test ===\n");

    // ---------- 1. full chain, rotary FAST, stereo ----------
    std::printf("full chain, rotary FAST\n");
    std::vector<float> L, R;
    {
        Engine e;
        yc_engine_set_param(e.state, YC_PARAM_PERC_ON, 1);
        yc_engine_set_param(e.state, YC_PARAM_VIBCHO_DEPTH, 3);
        e.state.rotary_speed = 3; // FAST
        yc_engine_set_param(e.state, YC_PARAM_DISTORTION, 60);
        yc_engine_set_param(e.state, YC_PARAM_REVERB, 80);

        // C major: C4(60), E4(64), G4(67), velocity 100; E4 released at 0.5 s.
        yc_engine_note_on(e.state, e.pstate, 60, 100);
        yc_engine_note_on(e.state, e.pstate, 64, 100);
        yc_engine_note_on(e.state, e.pstate, 67, 100);
        e.render(L, R, 44100, 22050, 64);
    }

    float peak = 0.0f;
    double sum_sq = 0.0;
    int nan_inf_count = 0;
    int nonzero_after_100 = 0;
    int stereo_diff = 0;
    for (size_t i = 0; i < L.size(); ++i) {
        const float s = L[i], t = R[i];
        if (std::isnan(s) || std::isinf(s) || std::isnan(t) || std::isinf(t)) { nan_inf_count++; continue; }
        peak = std::max(peak, std::max(std::fabs(s), std::fabs(t)));
        sum_sq += (double)s * (double)s;
        if (i >= 100 && s != 0.0f) nonzero_after_100++;
        if (s != t) stereo_diff++;
    }
    const double rms = std::sqrt(sum_sq / (double)L.size());
    std::printf("  peak %.6f  rms(L) %.6f  nan/inf %d  non-zero after 100: %d  L!=R: %d of %zu\n",
                peak, rms, nan_inf_count, nonzero_after_100, stereo_diff, L.size());
    check(nan_inf_count == 0, "no NaN/Inf");
    check(nonzero_after_100 > 0, "audible (not silence)");
    check(peak > 0.001f, "peak above 0.001");
    // The rotors need a moment to spin up, then the mics disagree on most samples.
    check(stereo_diff > (int) L.size() / 2, "rotary FAST: left and right differ");

    // WAV next to the binary, like the OB test, whatever the call directory.
    std::string wavPath = "yc_engine_host_test.wav";
    if (const char* slash = std::strrchr(argv[0], '/')) {
        wavPath = std::string(argv[0], (size_t)(slash + 1 - argv[0])) + "yc_engine_host_test.wav";
    }
    write_wav_stereo_16bit(wavPath.c_str(), L, R, 44100);

    // ---------- 2. rotary OFF: mono, bit for bit ----------
    std::printf("rotary OFF\n");
    {
        Engine e;
        yc_engine_set_param(e.state, YC_PARAM_PERC_ON, 1);
        yc_engine_set_param(e.state, YC_PARAM_VIBCHO_DEPTH, 2);
        yc_engine_set_param(e.state, YC_PARAM_DISTORTION, 40);
        yc_engine_set_param(e.state, YC_PARAM_REVERB, 100);
        yc_engine_note_on(e.state, e.pstate, 60, 100);
        yc_engine_note_on(e.state, e.pstate, 67, 100);
        std::vector<float> l, r;
        e.render(l, r, 11025);
        int diff = 0, nz = 0;
        for (size_t i = 0; i < l.size(); ++i) { if (l[i] != r[i]) diff++; if (l[i] != 0.0f) nz++; }
        check(diff == 0, "rotary OFF: left equals right bit for bit");
        check(nz > 0, "rotary OFF: audible");
    }

    // ---------- 3. pitch bend ----------
    std::printf("pitch bend\n");
    {
        Engine e;
        yc_engine_note_on(e.state, e.pstate, 69, 100);   // A4, footage 8' = ratio 1.0
        yc_voice_t& v = e.state.voices[e.state.active_idx[0]];
        const float inc0 = v.phase_inc[2];
        const float expect_inc0 = (440.0f / YC_SAMPLE_RATE) * (float)YC_WAVETABLE_SIZE;
        check(std::fabs(inc0 - expect_inc0) < 1e-3f, "A4 8' increment = 440 Hz");

        yc_engine_set_pitch_bend(e.state, 16383);
        const float up = v.phase_inc[2] / inc0;
        check(std::fabs(up - std::pow(2.0f, YC_BEND_RANGE_SEMITONES / 12.0f)) < 1e-4f, "bend max = +2 semitones on an active voice");
        check(e.state.voices[e.state.active_idx[0]].phase_inc[0] / inc0 > 0.5f * up * 0.999f, "all footages follow the bend");

        yc_engine_set_pitch_bend(e.state, 0);
        const float down = v.phase_inc[2] / inc0;
        check(std::fabs(down - std::pow(2.0f, -YC_BEND_RANGE_SEMITONES / 12.0f)) < 1e-4f, "bend min = -2 semitones");

        yc_engine_set_pitch_bend(e.state, 8192);
        check(e.state.bend_ratio == 1.0f, "centre: factor exactly 1.0");
        check(v.phase_inc[2] == inc0, "centre restores the increment bit for bit");

        // A note started while bent comes in bent.
        yc_engine_set_pitch_bend(e.state, 16383);
        yc_engine_note_on(e.state, e.pstate, 57, 100);   // A3
        yc_voice_t& w = e.state.voices[e.state.active_idx[1]];
        check(std::fabs(w.phase_inc[2] / (0.5f * inc0) - up) < 1e-4f, "note-on under bend is bent");
    }

    // ---------- 4. expression / channel volume ----------
    std::printf("expression and channel volume\n");
    {
        Engine e;
        check(e.state.vol_gain == 1.0f, "defaults: gain exactly 1.0");
        yc_engine_set_param(e.state, YC_PARAM_PERC_ON, 1);
        yc_engine_set_expression(e.state, 0);
        yc_engine_note_on(e.state, e.pstate, 60, 127);
        std::vector<float> l, r;
        e.render(l, r, 2048);
        int nz = 0;
        for (float s : l) if (s != 0.0f) nz++;
        check(nz == 0, "expression 0 mutes voices and percussion");

        yc_engine_set_expression(e.state, 127);
        check(e.state.vol_gain == 1.0f, "expression 127: gain exactly 1.0 again");
        yc_engine_set_midi_volume(e.state, 64);
        check(std::fabs(e.state.vol_gain - 64.0f / 127.0f) < 1e-6f, "CC7 64 scales the gain");
        yc_engine_set_param(e.state, YC_PARAM_VOLUME, 0);
        check(e.state.vol_gain == 0.0f, "panel volume 0 wins");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
