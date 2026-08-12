// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The D-50's own parameter bytes, turned into what the engine takes.
//
// This is the only place that knows what byte 22 of a partial means, and it is
// used both for the embedded bank and for patches arriving over MIDI -- the
// format is the same, so a SysEx patch dump can be played without a second
// implementation.
//
// The value ranges are the ones printed in the machine's MIDI implementation;
// where a panel value has to become a physical quantity, the range comes from
// the service notes rather than from taste:
//
//   TVF / TVA envelope times   4 ms .. 80 s      (panel 0..100)
//   P-ENV times                9 ms .. 9 s       (panel 0..50)
//   LFO rate                   0.0004 .. 27 Hz   (panel 0..100)
//   LFO delay                  0 .. 10 s
//   pitch modulation           +/- 600 cents by LFO, +/- 2400 by envelope
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_patch.h"
#include "d5_pcm_table.h"

namespace d5 {

// Seven 64-byte blocks per patch, in the order the dump has them.
enum PatchBlock {
    kBlkUpperP1 = 0, kBlkUpperP2, kBlkUpperCommon,
    kBlkLowerP1, kBlkLowerP2, kBlkLowerCommon, kBlkPatch
};

inline const uint8_t* patch_block(const uint8_t* patch, int block) {
    return patch + block * 64;
}

// Panel 0..100 to seconds, exponentially: the envelope range spans four
// decades, so the first half of the knob would be unusable otherwise.
inline float env_time(uint8_t v, float lo = 0.004f, float hi = 80.0f,
                      float span = 100.0f) {
    const float t = v / span;
    return lo * std::pow(hi / lo, t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
}

inline float level01(uint8_t v) { return v * 0.01f; }

// The bipolar panel values: 0..100 shown as -50..+50.
inline float bipolar(uint8_t v) { return (v - 50) * 0.02f; }

// LFO select 0..5 is +1,+2,+3,-1,-2,-3: which LFO, and in which direction.
inline LfoRoute lfo_route(uint8_t select, uint8_t depth) {
    LfoRoute r;
    const int s = select > 5 ? 0 : select;
    r.lfo = s % 3;
    r.depth = (s < 3 ? 1.0f : -1.0f) * level01(depth);
    return r;
}

// RAM residence for the sustained cycles. The 29 loops total 38528 words --
// 77 KiB -- and are read at rates up to 30 words per output sample, which
// through XIP means a fresh flash line on nearly every read. The attacks
// stay in flash: they play near-sequentially, which the cache handles.
// Filled once at boot by install_loop_ram(); until then every reference
// falls back to the blob, so the host tools work unchanged without it.
struct LoopRamMap {
    const int16_t* base = nullptr;
    uint32_t start[kPcmCount] = {};
};
inline LoopRamMap g_loop_ram{};

constexpr uint32_t loop_ram_words() {
    uint32_t n = 0;
    for (int i = 0; i < kPcmCount; ++i)
        if (kPcmSamples[i].looped && kPcmSamples[i].length) n += kPcmSamples[i].length;
    return n;
}

inline bool install_loop_ram(const int16_t* blob, int16_t* ram, uint32_t cap) {
    uint32_t used = 0;
    for (int i = 0; i < kPcmCount; ++i) {
        const PcmSample& smp = kPcmSamples[i];
        if (!smp.looped || smp.length == 0) continue;
        if (used + smp.length > cap) return false;   // all or nothing
        for (uint32_t k = 0; k < smp.length; ++k) ram[used + k] = blob[smp.start + k];
        g_loop_ram.start[i] = used;
        used += smp.length;
    }
    g_loop_ram.base = ram;
    return true;
}

inline void map_partial(const uint8_t* p, int index, VoiceSpec& v,
                        const int16_t* blob) {
    // ---- wave generator
    // 0..72 = C1..C7 per the parameter list. Which of those is "no
    // transposition" is not stated anywhere we have, and it is not free to
    // guess: the factory bank puts 207 of its 256 coarse bytes on exact
    // multiples of 12, so the wrong neutral detunes every patch by whole
    // octaves plus whatever the remainder is.
#ifndef D5_COARSE_NEUTRAL
#define D5_COARSE_NEUTRAL 36
#endif
    v.coarse[index] = static_cast<int>(p[0]) - D5_COARSE_NEUTRAL;
    v.fine_cents[index] = (static_cast<int>(p[1]) - 50);

    SynthSpec& s = v.synth[index];
    s.waveform = (p[6] == 0) ? Waveform::kSquare : Waveform::kSawtooth;
    s.pulse_width = level01(p[8]);

    // ---- PCM sound source
    const int wave = p[7];
    if (wave >= 0 && wave < kPcmCount) {
        const PcmSample& smp = kPcmSamples[wave];
        PcmSampleRef& r = v.pcm[index];
        r.data = blob;
        r.start = smp.start;
        if (smp.looped && g_loop_ram.base) {
            r.data = g_loop_ram.base;
            r.start = g_loop_ram.start[wave];
        }
        r.length = smp.length;
        r.looped = smp.looped;
        r.root_hz = smp.root_hz;
    }

    // ---- TVF: cutoff, resonance and its envelope
    s.cutoff = level01(p[13]);
    s.resonance = p[14] / 30.0f;
    s.tvf_env_depth = level01(p[18]);
    s.tvf_env.t[0] = env_time(p[22]);
    s.tvf_env.t[1] = env_time(p[23]);
    s.tvf_env.t[2] = env_time(p[24]);
    s.tvf_env.t[3] = env_time(p[25]);
    s.tvf_env.t[4] = env_time(p[26]);
    s.tvf_env.l[0] = level01(p[27]);
    s.tvf_env.l[1] = level01(p[28]);
    s.tvf_env.l[2] = level01(p[29]);
    s.tvf_env.sustain = level01(p[30]);
    s.tvf_env.end = p[31] ? 1.0f : 0.0f;

    // ---- TVA: level and its envelope
    const float level = level01(p[35]);
    s.tva_env.t[0] = env_time(p[39]);
    s.tva_env.t[1] = env_time(p[40]);
    s.tva_env.t[2] = env_time(p[41]);
    s.tva_env.t[3] = env_time(p[42]);
    s.tva_env.t[4] = env_time(p[43]);
    s.tva_env.l[0] = level01(p[44]) * level;
    s.tva_env.l[1] = level01(p[45]) * level;
    s.tva_env.l[2] = level01(p[46]) * level;
    s.tva_env.sustain = level01(p[47]) * level;
    s.tva_env.end = (p[48] ? 1.0f : 0.0f) * level;
    v.pcm_env[index] = s.tva_env;      // the sampled partial shares it

    // ---- modulation routes
    // WG Mod LFO Mode is off / positive / both ways / negative; the engine's
    // route carries the sign, and "both ways" is simply the full swing.
    const uint8_t mode = p[3];
    if (mode == 0) {
        v.pitch_lfo[index] = LfoRoute{};
    } else {
        v.pitch_lfo[index].lfo = 0;                    // LFO-1 is the pitch LFO
        // The mode byte says which way, not how far, and the byte that says
        // how far is not identified in this dump. It used to assume a quarter
        // of full swing, which is +-150 cents of vibrato applied to 201 of the
        // bank's 256 partials -- that is not a reading of the patch, it is an
        // invention, and it cost 15 of the 64 patches their tuning. Zero until
        // the depth byte is known: a route that does nothing is wrong in a way
        // that can be heard as missing, which is the honest kind.
        v.pitch_lfo[index].depth = 0.0f;   // sign would be mode == 3 ? -1 : +1
    }
    v.penv_mode[index] = (p[4] == 0) ? PEnvMode::kOff
                                     : (p[4] == 2 ? PEnvMode::kNegative
                                                  : PEnvMode::kPositive);
    v.pw_lfo[index] = lfo_route(p[10], p[11]);
    v.tvf_lfo[index] = lfo_route(p[32], p[33]);
    v.tva_lfo[index] = lfo_route(p[51], p[52]);
}

inline void map_common(const uint8_t* c, ToneSpec& tone) {
    VoiceSpec& v = tone.voice;
    v.structure = c[10] + 1;                     // panel 1..7, stored 0..6

    // ---- pitch envelope: four times, five bipolar levels
    for (int i = 0; i < 4; ++i) {
        v.penv.t[i] = env_time(c[13 + i], 0.009f, 9.0f, 50.0f);
    }
    v.penv.l0 = bipolar(c[17]);
    v.penv.l1 = bipolar(c[18]);
    v.penv.l2 = bipolar(c[19]);
    v.penv.sustain = bipolar(c[20]);
    v.penv.end = bipolar(c[21]);
    // Was pinned at the parameter's maximum, so any partial with the
    // envelope switched on bent two octaves. Its depth byte is not identified
    // either; zero for the same reason as the pitch LFO above.
    v.penv.depth_cents = 0.0f;

    // ---- the three LFOs
    for (int i = 0; i < 3; ++i) {
        const uint8_t* l = c + 25 + i * 4;
        LfoSpec& spec = v.lfo[i];
        spec.wave = static_cast<LfoWave>(l[0] > 3 ? 0 : l[0]);
        spec.rate = level01(l[1]);
        spec.delay = level01(l[2]);
        spec.key_sync = l[3] != 0;
    }

    // ---- equalizer and chorus
    tone.eq.low_freq = c[37];
    tone.eq.low_gain_db = static_cast<float>(c[38]) - 12.0f;
    tone.eq.high_freq = c[39];
    tone.eq.high_q = c[40];
    tone.eq.high_gain_db = static_cast<float>(c[41]) - 12.0f;
    tone.chorus.type = c[42];
    tone.chorus.rate = level01(c[43]);
    tone.chorus.depth = level01(c[44]);
    tone.chorus.balance = level01(c[45]);

    v.partials_on = c[46] & 0x3;
    v.balance = level01(c[47]);
}

// Turns 448 raw bytes into a playable patch. `blob` is the decoded PCM space.
inline PatchSpec patch_from_bytes(const uint8_t* patch, const int16_t* blob) {
    PatchSpec p;

    map_partial(patch_block(patch, kBlkUpperP1), 0, p.upper.voice, blob);
    map_partial(patch_block(patch, kBlkUpperP2), 1, p.upper.voice, blob);
    map_common(patch_block(patch, kBlkUpperCommon), p.upper);

    map_partial(patch_block(patch, kBlkLowerP1), 0, p.lower.voice, blob);
    map_partial(patch_block(patch, kBlkLowerP2), 1, p.lower.voice, blob);
    map_common(patch_block(patch, kBlkLowerCommon), p.lower);

    const uint8_t* pb = patch_block(patch, kBlkPatch);
    // Key modes 3..8 are the separate and solo variants; they differ in voice
    // assignment rather than in sound, so they fold onto the three the engine
    // knows until the allocator grows monophonic modes.
    switch (pb[18]) {
        case 1:  p.key_mode = KeyMode::kDual;  break;
        case 2:
        case 6:
        case 7:  p.key_mode = KeyMode::kSplit; break;
        default: p.key_mode = KeyMode::kWhole; break;
    }
    p.split_point = 36 + pb[19];                 // panel C2..C7
    p.upper.voice.master_cents = static_cast<float>(pb[24]) - 50.0f;
    p.lower.voice.master_cents = static_cast<float>(pb[25]) - 50.0f;
    p.upper.voice.coarse[0] += static_cast<int>(pb[22]) - 24;   // key shift
    p.upper.voice.coarse[1] += static_cast<int>(pb[22]) - 24;
    p.lower.voice.coarse[0] += static_cast<int>(pb[23]) - 24;
    p.lower.voice.coarse[1] += static_cast<int>(pb[23]) - 24;
    p.reverb.type = pb[30];
    p.reverb.balance = level01(pb[31]);
    p.volume = level01(pb[32]);
    p.balance = level01(pb[33]);

    // Headroom, not taste: eight voices per tone at full TVA level would ask
    // for eight times unity, and the saturator would then be working on every
    // chord instead of only on the loudest. A quarter of a voice's level per
    // tone leaves a four-note chord peaking around -3 dB.
    p.upper.level = 0.13f;
    p.lower.level = 0.13f;
    return p;
}

}  // namespace d5
