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

// Roland's depth law, read verbatim from the D-05 firmware (BQ3:Appli at
// file offset 0xE258E): 101 entries doubling exactly every 10 steps, so the
// bottom of the range is fine and the top is full -- depth 8 is 0.17% of
// full scale, not 8%. Read linearly, a factory patch asking for a breath of
// vibrato got half a semitone of it; that was the rubber band in the tone.
// The assignment of this particular table to the pitch depths rests on its
// shape and its 0..100 span, not on a disassembled call site -- but the law
// itself is Roland's own, which beats any curve we could have invented.
inline constexpr float kDepthCurve[101] = {
    0.000000f, 0.001047f, 0.001112f, 0.001177f, 0.001308f, 0.001374f,
    0.001505f, 0.001570f, 0.001701f, 0.001832f, 0.001962f, 0.002093f,
    0.002224f, 0.002420f, 0.002551f, 0.002747f, 0.002944f, 0.003205f,
    0.003402f, 0.003663f, 0.003925f, 0.004187f, 0.004514f, 0.004841f,
    0.005168f, 0.005495f, 0.005953f, 0.006345f, 0.006803f, 0.007261f,
    0.007784f, 0.008373f, 0.008962f, 0.009616f, 0.010336f, 0.011055f,
    0.011840f, 0.012691f, 0.013606f, 0.014588f, 0.015634f, 0.016746f,
    0.017924f, 0.019232f, 0.020606f, 0.022110f, 0.023680f, 0.025381f,
    0.027213f, 0.029175f, 0.031268f, 0.033493f, 0.035913f, 0.038464f,
    0.041211f, 0.044221f, 0.047361f, 0.050762f, 0.054425f, 0.058285f,
    0.062471f, 0.066985f, 0.071760f, 0.076928f, 0.082488f, 0.088376f,
    0.094721f, 0.101524f, 0.108785f, 0.116635f, 0.125008f, 0.133970f,
    0.143586f, 0.153922f, 0.164911f, 0.176751f, 0.189442f, 0.203048f,
    0.217636f, 0.233270f, 0.250016f, 0.267940f, 0.287172f, 0.307778f,
    0.329888f, 0.353568f, 0.378949f, 0.406097f, 0.435272f, 0.466540f,
    0.499967f, 0.535880f, 0.574344f, 0.615556f, 0.659776f, 0.707071f,
    0.757833f, 0.812259f, 0.870544f, 0.933015f, 1.000000f};

inline LfoRoute lfo_route(uint8_t select, uint8_t depth) {
    LfoRoute r;
    const int s = select > 5 ? 0 : select;
    r.lfo = s / 2;
    r.depth = ((s & 1) ? -1.0f : 1.0f) * kDepthCurve[depth > 100 ? 100 : depth];
    return r;
}

// The second curve family, from the same battery: 101 entries spanning
// exactly 0..1.0 in the firmware's Q15 unit, sitting immediately after the
// keyfollow table at 0xE28D8. Roughly x^1.8 -- convex, but nothing like the
// ten-octave depth law -- and quantized in the steps of Roland's original
// resolution. Used for the TVF envelope depth: the bank's median setting of
// 70 comes out at 46% effect instead of a linear 70%, which tames the
// factory sweeps without flattening them the way the depth law would have.
inline constexpr float kAmountCurve[101] = {
    0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f,
    0.015991f, 0.015991f, 0.015991f, 0.031982f, 0.031982f, 0.031982f,
    0.031982f, 0.047974f, 0.047974f, 0.047974f, 0.062988f, 0.062988f,
    0.062988f, 0.079987f, 0.079987f, 0.079987f, 0.079987f, 0.095978f,
    0.095978f, 0.095978f, 0.110992f, 0.110992f, 0.110992f, 0.127991f,
    0.127991f, 0.127991f, 0.142975f, 0.142975f, 0.142975f, 0.158997f,
    0.158997f, 0.174988f, 0.174988f, 0.174988f, 0.189972f, 0.189972f,
    0.189972f, 0.206970f, 0.206970f, 0.206970f, 0.222992f, 0.222992f,
    0.222992f, 0.237976f, 0.237976f, 0.254974f, 0.269989f, 0.284973f,
    0.284973f, 0.301971f, 0.316986f, 0.316986f, 0.333984f, 0.350983f,
    0.364990f, 0.364990f, 0.381989f, 0.396973f, 0.396973f, 0.411987f,
    0.428986f, 0.428986f, 0.443970f, 0.443970f, 0.460999f, 0.475983f,
    0.475983f, 0.491974f, 0.507996f, 0.507996f, 0.522980f, 0.539978f,
    0.539978f, 0.554993f, 0.571991f, 0.588989f, 0.588989f, 0.603973f,
    0.619995f, 0.619995f, 0.634979f, 0.665985f, 0.697998f, 0.714996f,
    0.745972f, 0.761993f, 0.793976f, 0.824982f, 0.855988f, 0.872986f,
    0.904999f, 0.919983f, 0.953979f, 0.984985f, 1.000000f};

// A bipolar parameter through the same law: fine around its center.
inline float bipolar_curved(uint8_t v) {
    const int d = (int)v - 50;
    const int m = d < 0 ? -d : d;
    const float c = kDepthCurve[m * 2 > 100 ? 100 : m * 2];
    return d < 0 ? -c : c;
}

// LFO select 0..5 is +1,-1,+2,-2,+3,-3 -- interleaved, per the MIDI
// implementation's parameter list. The first guess here was +1,+2,+3,-1,-2,-3,
// which sent every second modulation to the wrong LFO with the wrong sign.
// Their depth goes through kDepthCurve below, like every "LFO Depth" on this
// machine: the pitch one is proven from the D-05 firmware, and reading its
// siblings linearly gave wobbles the patch never asked for. The TVF ENV
// depth is deliberately NOT on this law -- at ten octaves of range a median
// factory sweep would come out near zero, so it keeps its linear reading
// until its own curve is identified.
inline LfoRoute lfo_route(uint8_t select, uint8_t depth);

// WG Pitch Keyfollow, parameter offset 2: seventeen ratios straight from the
// parameter list. Index 11 is the 1:1 the guessed mapping silently assumed
// for everything -- true for 176 of the bank's 256 partials, wrong for 80.
// s1 and s2 are Roland's stretched tunings, and the D-05 firmware states
// them outright: its keyfollow table (BQ3:Appli at 0xE2894, Q15) runs
// -32768, -16384, -8192, 0, 4096..32768, 40960, 49152, 65536, then 32786
// and 32862 -- s1 = 1.000549, s2 = 1.002869, about 1.3 and 6.9 cents of
// stretch at two octaves from center. Every other entry matches the
// fractions below exactly, which is also the proof they are read right.
inline constexpr float kKeyfollow[17] = {
    -1.0f, -0.5f, -0.25f, 0.0f, 0.125f, 0.25f, 0.375f, 0.5f,
    0.625f, 0.75f, 0.875f, 1.0f, 1.25f, 1.5f, 2.0f,
    1.0005493f, 1.0028687f};

inline float keyfollow_ratio(uint8_t v, int limit) {
    return kKeyfollow[v > limit ? 11 : v];
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
    s.tvf_env_depth = kAmountCurve[p[18] > 100 ? 100 : p[18]];
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

    v.keyfollow[index] = keyfollow_ratio(p[2], 16);
    s.cutoff_keyfollow = keyfollow_ratio(p[15], 14);

    // ---- modulation routes
    // WG Mod LFO Mode, offset 3: OFF, (+), (-), A&L. The magnitude is not
    // here -- it is the common block's P-Mod LFO Depth, applied in
    // map_common once it has been read -- so the route carries the sign
    // only. A&L means the depth comes from aftertouch and the bender lever
    // alone; neither performance control is implemented yet, so those
    // partials correctly get none, rather than a full-depth vibrato the
    // player never asked for.
    const uint8_t mode = p[3];
    if (mode == 0 || mode == 3) {
        v.pitch_lfo[index] = LfoRoute{};
    } else {
        v.pitch_lfo[index].lfo = 0;                    // P-Mod rides LFO-1
        v.pitch_lfo[index].depth = (mode == 2) ? -1.0f : 1.0f;
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
    v.penv.l0 = bipolar_curved(c[17]);
    v.penv.l1 = bipolar_curved(c[18]);
    v.penv.l2 = bipolar_curved(c[19]);
    v.penv.sustain = bipolar_curved(c[20]);
    v.penv.end = bipolar_curved(c[21]);
    // There is no separate P-ENV depth parameter -- the levels themselves
    // are the depth, bipolar around 50, and full scale is two octaves. The
    // 2400 here is the unit of those levels, not a guess; zeroing it (as an
    // earlier revision did, distrusting its own constant) silenced the
    // pitch envelope outright.
    v.penv.depth_cents = 2400.0f;

    // P-Mod LFO Depth, offset 22: the magnitude of the pitch vibrato whose
    // sign map_partial read from each partial's mode byte. Full scale is
    // +-600 cents, the service notes' LFO pitch range. This is the byte the
    // guessed mapping never found, and its absence was first patched with an
    // invented quarter-swing (audibly detuned) and then with zero (audibly
    // sterile).
    const float pmod = kDepthCurve[c[22] > 100 ? 100 : c[22]];
    v.pitch_lfo[0].depth *= pmod;
    v.pitch_lfo[1].depth *= pmod;

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
    // Chorus depth is another 0..100 "Depth" and takes the same law as the
    // rest of that family. Linear reading left String Ensemble with +-10
    // cents of coherent pitch wobble from the chorus alone -- the audible
    // "eiern"; through the curve its setting of 53..58 becomes a few cents
    // of shimmer.
    tone.chorus.depth = kDepthCurve[c[44] > 100 ? 100 : c[44]];
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
