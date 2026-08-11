// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// Host harness for the PicoFaceD5 engine: loads the generated PCM blob and
// renders notes to a WAV, so the engine can be judged by ear long before it
// runs on hardware.
//
//   c++ -O2 -std=c++17 -I<blobdir> -I instruments/PicoFaceD5/include \
//       -o d5_render tools/host_tests/d5_engine_test/render.cpp
//   ./d5_render <blobdir>/d5_pcm.bin out.wav [pcm_number ...]
//
// With no PCM numbers it plays a survey: every resolved sample once, at its
// own root pitch, so the whole ROM can be checked in one listen.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "d5_engine/d5_pcm_voice.h"
#include "d5_engine/d5_synth_voice.h"
#include "d5_pcm_table.h"

namespace {

constexpr float kSampleRate = 32000.0f;

std::vector<int16_t> load_blob(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<int16_t> out(static_cast<size_t>(bytes) / 2);
    if (std::fread(out.data(), 1, static_cast<size_t>(bytes), f) !=
        static_cast<size_t>(bytes)) {
        std::fprintf(stderr, "short read on %s\n", path);
        std::exit(1);
    }
    std::fclose(f);
    return out;
}

void write_wav(const char* path, const std::vector<float>& x) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); std::exit(1); }
    const uint32_t n = static_cast<uint32_t>(x.size());
    const uint32_t data_bytes = n * 2;
    const uint32_t rate = static_cast<uint32_t>(kSampleRate);
    const uint32_t byte_rate = rate * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes);
    std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(1);
    u32(rate); u32(byte_rate); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(data_bytes);
    for (float v : x) {
        int s = static_cast<int>(v * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32767) s = -32767;
        u16(static_cast<uint16_t>(static_cast<int16_t>(s)));
    }
    std::fclose(f);
}

}  // namespace

// The synth half: a sweep through what the LA32's waveform generator does,
// so cutoff, resonance, pulse width and the two waveforms can be heard
// separately from the samples.
void render_synth(std::vector<float>& out) {
    struct Step { const char* what; d5::SynthSpec spec; int note; };
    std::vector<Step> steps;

    auto base = []() {
        d5::SynthSpec s;
        s.tva_env.t[0] = 0.01f;
        s.tva_env.sustain = 0.8f;
        s.tva_env.t[4] = 0.3f;
        s.tvf_env.t[0] = 0.15f;
        s.tvf_env.l[0] = 1.0f;
        s.tvf_env.sustain = 0.35f;
        s.tvf_env.t[4] = 0.3f;
        return s;
    };

    for (float c : {0.25f, 0.5f, 0.75f, 1.0f}) {
        d5::SynthSpec s = base();
        s.waveform = d5::Waveform::kSawtooth;
        s.cutoff = c;
        s.resonance = 0.0f;
        s.tvf_env_depth = 0.0f;
        steps.push_back({"saw, cutoff", s, 48});
    }
    for (float r : {0.0f, 0.35f, 0.7f, 0.95f}) {
        d5::SynthSpec s = base();
        s.waveform = d5::Waveform::kSawtooth;
        s.cutoff = 0.45f;
        s.resonance = r;
        s.tvf_env_depth = 0.0f;
        steps.push_back({"saw, resonance", s, 48});
    }
    for (float pw : {0.5f, 0.3f, 0.15f}) {
        d5::SynthSpec s = base();
        s.waveform = d5::Waveform::kSquare;
        s.pulse_width = pw;
        s.cutoff = 0.7f;
        s.resonance = 0.15f;
        s.tvf_env_depth = 0.0f;
        steps.push_back({"square, pulse width", s, 48});
    }
    {   // the classic sweep: envelope opening the cutoff over the note
        d5::SynthSpec s = base();
        s.waveform = d5::Waveform::kSawtooth;
        s.cutoff = 0.2f;
        s.resonance = 0.55f;
        s.tvf_env_depth = 0.6f;
        s.tvf_env.t[0] = 0.6f;
        steps.push_back({"saw, TVF envelope sweep", s, 40});
    }

    for (const Step& st : steps) {
        d5::SynthPartial v;
        v.note_on(st.spec, st.note, 0.9f, kSampleRate);
        const int hold = static_cast<int>(kSampleRate * 1.4f);
        for (int i = 0; i < hold; ++i) out.push_back(v.next());
        v.note_off();
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.5f); ++i)
            out.push_back(v.next());
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.2f); ++i)
            out.push_back(0.0f);
        std::printf("synth: %-24s cutoff %.2f res %.2f pw %.2f\n", st.what,
                    static_cast<double>(st.spec.cutoff),
                    static_cast<double>(st.spec.resonance),
                    static_cast<double>(st.spec.pulse_width));
    }
}

// What LA synthesis is actually about: a sampled attack dovetailed with a
// synthesized sustain, the two partials sounding as one note.
void render_la(const std::vector<int16_t>& blob, std::vector<float>& out) {
    struct Combo { const char* what; int pcm; float cutoff; float res;
                   d5::Waveform wave; int note; };
    const Combo combos[] = {
        {"Lpiano attack + saw sustain",   16, 0.45f, 0.20f, d5::Waveform::kSawtooth, 48},
        {"Marimba attack + square pad",    1, 0.55f, 0.10f, d5::Waveform::kSquare,   60},
        {"Steel attack + soft saw",       22, 0.35f, 0.35f, d5::Waveform::kSawtooth, 52},
        {"Breath attack + hollow square", 32, 0.40f, 0.45f, d5::Waveform::kSquare,   55},
    };

    for (const Combo& c : combos) {
        const d5::PcmSample& s = d5::kPcmSamples[c.pcm - 1];
        if (s.length == 0) continue;

        d5::PcmSampleRef ref;
        ref.data = blob.data();
        ref.start = s.start;
        ref.length = s.length;
        ref.looped = s.looped;
        ref.root_hz = s.root_hz;

        d5::Env5Spec pcm_env;          // attack only: short, decaying away
        pcm_env.t[0] = 0.002f;
        pcm_env.l[0] = 1.0f; pcm_env.l[1] = 0.5f; pcm_env.l[2] = 0.1f;
        pcm_env.t[1] = 0.08f; pcm_env.t[2] = 0.20f;
        pcm_env.sustain = 0.0f;
        pcm_env.t[4] = 0.05f;

        d5::SynthSpec sy;              // sustain: slow in, holds, fades out
        sy.waveform = c.wave;
        sy.cutoff = c.cutoff;
        sy.resonance = c.res;
        sy.tvf_env_depth = 0.25f;
        sy.tvf_env.t[0] = 0.35f;
        sy.tvf_env.sustain = 0.4f;
        sy.tva_env.t[0] = 0.12f;
        sy.tva_env.l[0] = 0.9f;
        sy.tva_env.sustain = 0.75f;
        sy.tva_env.t[4] = 0.45f;

        d5::PcmVoice pv;
        d5::SynthPartial sv;
        pv.note_on(ref, c.note, 0.95f, pcm_env, kSampleRate);
        sv.note_on(sy, c.note, 0.85f, kSampleRate);

        const int hold = static_cast<int>(kSampleRate * 1.6f);
        for (int i = 0; i < hold; ++i) out.push_back(pv.next() + sv.next());
        pv.note_off();
        sv.note_off();
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.7f); ++i)
            out.push_back(pv.next() + sv.next());
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.3f); ++i)
            out.push_back(0.0f);
        std::printf("LA: %-32s PCM %d + %s\n", c.what, c.pcm,
                    c.wave == d5::Waveform::kSquare ? "square" : "saw");
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <d5_pcm.bin> <out.wav> [pcm_number ...]\n"
                     "       %s --synth <out.wav>\n"
                     "       %s --la <d5_pcm.bin> <out.wav>\n",
                     argv[0], argv[0], argv[0]);
        return 1;
    }
    if (std::strcmp(argv[1], "--la") == 0 && argc >= 4) {
        std::vector<float> out;
        render_la(load_blob(argv[2]), out);
        write_wav(argv[3], out);
        std::printf("wrote %s (%.1f s)\n", argv[3], out.size() / kSampleRate);
        return 0;
    }
    if (std::strcmp(argv[1], "--synth") == 0) {
        std::vector<float> out;
        render_synth(out);
        write_wav(argv[2], out);
        std::printf("wrote %s (%.1f s)\n", argv[2], out.size() / kSampleRate);
        return 0;
    }
    const std::vector<int16_t> blob = load_blob(argv[1]);
    if (blob.size() < d5::kPcmWords) {
        std::fprintf(stderr, "blob has %zu samples, table expects %u\n",
                     blob.size(), d5::kPcmWords);
        return 1;
    }

    std::vector<int> want;
    for (int i = 3; i < argc; ++i) want.push_back(std::atoi(argv[i]));
    if (want.empty()) {
        for (int i = 1; i <= d5::kPcmCount; ++i) {
            if (d5::kPcmSamples[i - 1].length > 0) want.push_back(i);
        }
    }

    // One note per sample: attacks get their natural decay, loops are held.
    std::vector<float> out;
    for (int pcm : want) {
        if (pcm < 1 || pcm > d5::kPcmCount) continue;
        const d5::PcmSample& s = d5::kPcmSamples[pcm - 1];
        if (s.length == 0) continue;

        d5::PcmSampleRef ref;
        ref.data = blob.data();
        ref.start = s.start;
        ref.length = s.length;
        ref.looped = s.looped;
        ref.root_hz = s.root_hz;

        d5::TvaEnvSpec env;
        if (s.looped) {
            env.t[0] = 0.03f;                  // pad-ish, so the loop sustains
            env.sustain = 0.8f;
            env.t[4] = 0.35f;
        } else {
            env.t[0] = 0.002f;                 // let the transient through
            env.l[0] = 1.0f; env.l[1] = 0.9f; env.l[2] = 0.75f;
            env.sustain = 0.65f;
            env.t[4] = 0.25f;
        }

        d5::PcmVoice v;
        v.note_on(ref, 60, 0.9f, env, kSampleRate);
        const int hold = static_cast<int>(kSampleRate * (s.looped ? 1.2f : 0.9f));
        const int tail = static_cast<int>(kSampleRate * 0.6f);
        for (int i = 0; i < hold; ++i) out.push_back(v.next());
        v.note_off();
        for (int i = 0; i < tail; ++i) out.push_back(v.next());
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.25f); ++i)
            out.push_back(0.0f);
        std::printf("PCM %3d %-6s  root %7.1f Hz  %s\n", pcm, s.name,
                    static_cast<double>(s.root_hz),
                    s.looped ? "loop" : "one-shot");
    }

    write_wav(argv[2], out);
    std::printf("wrote %s (%.1f s)\n", argv[2], out.size() / kSampleRate);
    return 0;
}
