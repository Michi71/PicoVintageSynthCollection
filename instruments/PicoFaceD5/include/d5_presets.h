// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// d5_presets.h -- the fallback patches, for a build without a bank dump.
//
// These are NOT the D-50's factory patches. Those are read by
// tools/d5_extract/d5_syx_to_patches.py out of SysEx bulk dumps and reach the
// firmware as the generated d5_patch_data.h; when that header is present the
// bridge uses it and nothing here is played. What remains here is the case
// where somebody builds with the ROM set but without a single bank dump:
// eight patches built by hand from the engine's own parameters, chosen to
// cover the ground -- every structure appears at least once, both waveforms,
// ring modulation, the pitch envelope and each effect.

#pragma once

#include "d5_engine/d5_patch.h"
#include "d5_pcm_table.h"

namespace d5 {

struct Preset {
    const char* name;
    int pcm1;                 // PCM number for partial 1, 0 = unused
    int pcm2;
    PatchSpec spec;
};

// Fills in the PCM pointers, which only exist once the blob is linked.
inline void preset_bind(PatchSpec& spec, const int16_t* blob, int pcm1, int pcm2) {
    const int pcms[2] = {pcm1, pcm2};
    for (int i = 0; i < 2; ++i) {
        if (pcms[i] < 1 || pcms[i] > kPcmCount) continue;
        const PcmSample& s = kPcmSamples[pcms[i] - 1];
        if (s.length == 0) continue;
        PcmSampleRef& r = spec.upper.voice.pcm[i];
        r.data = blob;
        r.start = s.start;
        r.length = s.length;
        r.looped = s.looped;
        r.root_hz = s.root_hz;
    }
}

inline PatchSpec preset_common() {
    PatchSpec p;
    p.key_mode = KeyMode::kWhole;
    p.balance = 0.0f;                     // upper tone only until patches use both
    p.volume = 0.9f;
    p.upper.level = 0.35f;      // same headroom the bank path took, see d5_patch_map.h
    p.upper.voice.balance = 0.5f;
    // a partial pair with an attack that decays and a sustain that holds
    p.upper.voice.pcm_env[0].t[0] = 0.002f;
    p.upper.voice.pcm_env[0].l[0] = 1.0f;
    p.upper.voice.pcm_env[0].l[1] = 0.45f;
    p.upper.voice.pcm_env[0].l[2] = 0.15f;
    p.upper.voice.pcm_env[0].t[1] = 0.10f;
    p.upper.voice.pcm_env[0].t[2] = 0.25f;
    p.upper.voice.pcm_env[0].sustain = 0.0f;
    p.upper.voice.pcm_env[0].t[4] = 0.15f;
    p.upper.voice.pcm_env[1] = p.upper.voice.pcm_env[0];
    p.upper.voice.pcm_env[1].sustain = 0.7f;
    p.upper.voice.pcm_env[1].l[1] = 0.8f;
    p.upper.voice.pcm_env[1].l[2] = 0.75f;
    p.upper.voice.pcm_env[1].t[4] = 0.4f;
    for (int i = 0; i < 2; ++i) {
        SynthSpec& s = p.upper.voice.synth[i];
        s.waveform = Waveform::kSawtooth;
        s.cutoff = 0.5f;
        s.resonance = 0.2f;
        s.tvf_env_depth = 0.25f;
        s.tvf_env.t[0] = 0.25f;
        s.tvf_env.sustain = 0.45f;
        s.tva_env.t[0] = 0.06f;
        s.tva_env.sustain = 0.8f;
        s.tva_env.t[4] = 0.45f;
    }
    p.upper.voice.lfo[0].rate = 0.55f;    // ~5 Hz, the vibrato LFO
    p.upper.voice.lfo[1].rate = 0.40f;
    p.upper.voice.lfo[2].wave = LfoWave::kRandom;
    p.upper.voice.lfo[2].rate = 0.60f;
    p.reverb.type = 12;
    p.reverb.balance = 0.30f;
    return p;
}

inline Preset preset(int index) {
    Preset pr{"", 0, 0, preset_common()};
    PatchSpec& p = pr.spec;
    VoiceSpec& v = p.upper.voice;

    switch (index & 7) {
        case 0:                                   // the machine's calling card
            pr.name = "Fantasia";
            pr.pcm1 = 16;                         // Lpiano attack
            v.structure = 3;                      // PCM + synth
            v.synth[1].cutoff = 0.42f;
            v.synth[1].resonance = 0.3f;
            v.pitch_lfo[1] = {0, 0.04f};
            p.upper.chorus.type = 4;
            p.upper.chorus.balance = 0.45f;
            p.reverb.type = 14;
            p.reverb.balance = 0.38f;
            break;
        case 1:
            pr.name = "Glass Voices";
            pr.pcm1 = 65;                         // Aah loop
            v.structure = 3;
            v.pcm_env[0].sustain = 0.7f;
            v.pcm_env[0].l[1] = 0.85f;
            v.synth[1].waveform = Waveform::kSquare;
            v.synth[1].cutoff = 0.55f;
            p.upper.eq.high_freq = 16;
            p.upper.eq.high_gain_db = 5.0f;
            p.upper.chorus.type = 5;
            p.upper.chorus.balance = 0.5f;
            break;
        case 2:
            pr.name = "Soft Pad";
            v.structure = 1;                      // two synth partials
            v.coarse[1] = 12;
            v.synth[0].cutoff = 0.38f;
            v.synth[1].cutoff = 0.44f;
            v.synth[1].waveform = Waveform::kSquare;
            v.pitch_lfo[0] = {0, 0.03f};
            p.upper.chorus.type = 6;
            p.upper.chorus.balance = 0.5f;
            p.reverb.type = 15;
            p.reverb.balance = 0.42f;
            break;
        case 3:
            pr.name = "Bell Ring";
            v.structure = 2;                      // synth + synth, ring
            v.coarse[1] = 7;
            v.synth[0].cutoff = 0.7f;
            v.synth[1].cutoff = 0.8f;
            v.synth[0].tva_env.t[0] = 0.003f;
            v.synth[0].tva_env.sustain = 0.25f;
            v.synth[1].tva_env.t[0] = 0.003f;
            v.synth[1].tva_env.sustain = 0.3f;
            p.reverb.type = 10;
            p.reverb.balance = 0.4f;
            break;
        case 4:
            pr.name = "Steel Pluck";
            pr.pcm1 = 22;                         // Steel strings attack
            v.structure = 4;                      // PCM + synth, ring
            v.coarse[1] = 12;
            v.synth[1].cutoff = 0.62f;
            v.synth[1].resonance = 0.35f;
            p.upper.chorus.type = 2;
            p.upper.chorus.balance = 0.35f;
            break;
        case 5:
            pr.name = "Breath Choir";
            pr.pcm1 = 32;                         // Breath attack
            pr.pcm2 = 67;                         // Male loop
            v.structure = 6;                      // two PCM partials
            v.pcm_env[1].sustain = 0.8f;
            p.upper.eq.low_freq = 4;
            p.upper.eq.low_gain_db = 4.0f;
            p.upper.chorus.type = 3;
            p.upper.chorus.balance = 0.45f;
            p.reverb.type = 13;
            p.reverb.balance = 0.4f;
            break;
        case 6:
            pr.name = "Digital Bass";
            pr.pcm1 = 27;                         // Pick bass attack
            v.structure = 3;
            v.coarse[1] = -12;
            v.synth[1].waveform = Waveform::kSquare;
            v.synth[1].pulse_width = 0.32f;
            v.synth[1].cutoff = 0.4f;
            v.synth[1].resonance = 0.4f;
            v.synth[1].tvf_env_depth = 0.35f;
            v.synth[1].tvf_env.t[0] = 0.06f;
            p.reverb.balance = 0.12f;
            break;
        default:
            pr.name = "Sweep Lead";
            v.structure = 1;
            v.coarse[1] = -12;
            v.synth[0].cutoff = 0.22f;
            v.synth[0].resonance = 0.6f;
            v.synth[0].tvf_env_depth = 0.55f;
            v.synth[0].tvf_env.t[0] = 0.5f;
            v.synth[1] = v.synth[0];
            // -280 cents scooping in over ~50 ms, in the ROM-derived spec:
            // mode 0 puts one unit of level at 1200 cents.
            v.penv.l0 = -0.233f;
            v.penv.t_idx[0] = 6;
            v.penv_mode[0] = PEnvMode::kPositive;
            v.penv_mode[1] = PEnvMode::kPositive;
            p.upper.chorus.type = 7;
            p.upper.chorus.balance = 0.3f;
            break;
    }
    return pr;
}

inline constexpr int kPresetCount = 8;

}  // namespace d5
