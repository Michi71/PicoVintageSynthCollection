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
// The exponent is bent: gamma 1.5 was fitted against the reference
// recording of the factory Pizzagogo, whose note bodies decay at a constant
// -34 dB/s and reaches -40 dB in 1.2 s; gamma 1.15 with the -60 dB floor
// lands the engine there. One parameter, one
// measurement, said openly -- the firmware's own time tables have not been
// identified, and this is the best calibration a real recording affords.
// P-ENV passes gamma 1, its range having shown no such disagreement.
inline float env_time(uint8_t v, float lo = 0.004f, float hi = 80.0f,
                      float span = 100.0f, float gamma = 1.15f) {
    float t = v / span;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return lo * std::pow(hi / lo, std::pow(t, gamma));
}

inline float level01(uint8_t v) { return v * 0.01f; }

// Falling-segment decay rate in dB/s from the time byte. Two anchors from
// reference recordings: Horn Section's release byte 37 measures -37.8 dB/s,
// and Pizzagogo's body bytes around 50..59 measure a constant -34; the
// exponential through them puts Soundtrack's byte 67 at -27, safely under
// its Chapel reverb's -16.5, which is exactly what that recording shows.
// Below byte 8 the duration path stays in charge -- those are the instant
// releases, where a rate would drag.
inline float env_rate_db_s(uint8_t v) {
    if (v < 8) return 0.0f;
    return 37.0f * fast_exp2(-(static_cast<float>(v) - 40.0f) * (1.0f / 60.0f));
}

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

// The pitch-modulation depth curve, verbatim from the D-50's mask ROM
// (IC25 0x17F4, 101 bytes to 0..127; readers at 0x155A/0x156E/0x17A3 bind
// it to EXACTLY three parameters: P-Mod LFO Depth c[22], the lever c[23],
// and aftertouch c[24]). Full scale is +-600 cents -- the x4 shift a
// cluster agent thought it saw at the consumer was refuted in the listing.
// This displaces both earlier readings: kDepthCurve (assigned by shape,
// which made a mid vibrato of 43 into 11 cents where the ROM says 128)
// and the linear lever fit against the Living Calliope recording (whose
// 138 measured cents the ROM's 61 contradicts -- the firmware is the
// master template, and the recording's vibrato has a player in the loop).
inline constexpr float kPModCurve[101] = {
    0.000000f, 0.000000f, 0.007874f, 0.007874f, 0.015748f, 0.015748f, 0.023622f, 0.023622f,
    0.031496f, 0.039370f, 0.039370f, 0.047244f, 0.047244f, 0.055118f, 0.062992f, 0.062992f,
    0.070866f, 0.070866f, 0.078740f, 0.086614f, 0.086614f, 0.094488f, 0.094488f, 0.102362f,
    0.110236f, 0.110236f, 0.118110f, 0.125984f, 0.125984f, 0.133858f, 0.141732f, 0.141732f,
    0.149606f, 0.157480f, 0.157480f, 0.165354f, 0.173228f, 0.181102f, 0.181102f, 0.188976f,
    0.196850f, 0.196850f, 0.204724f, 0.212598f, 0.212598f, 0.220472f, 0.228346f, 0.228346f,
    0.236220f, 0.244094f, 0.251969f, 0.259843f, 0.275591f, 0.291339f, 0.299213f, 0.307087f,
    0.322835f, 0.330709f, 0.346457f, 0.354331f, 0.370079f, 0.377953f, 0.393701f, 0.401575f,
    0.409449f, 0.417323f, 0.433071f, 0.440945f, 0.448819f, 0.456693f, 0.472441f, 0.480315f,
    0.488189f, 0.496063f, 0.511811f, 0.519685f, 0.527559f, 0.543307f, 0.551181f, 0.566929f,
    0.582677f, 0.590551f, 0.598425f, 0.614173f, 0.622047f, 0.629921f, 0.645669f, 0.669291f,
    0.700787f, 0.724409f, 0.748031f, 0.771654f, 0.795276f, 0.826772f, 0.858268f, 0.881890f,
    0.905512f, 0.929134f, 0.952756f, 0.984252f, 1.000000f};

// A P-ENV level byte -- panel -50..+50 around 50 -- through the ROM's own
// magnitude curve (kPEnvLevel, IC25 0x14D5), normalized to -1..+1. The
// per-note velocity scale turns it into cents in PitchEnv::start.
inline float penv_level(uint8_t v) {
    const int d = (int)v - 50;
    const int m = d < 0 ? (d < -50 ? 50 : -d) : (d > 50 ? 50 : d);
    const float c = kPEnvLevel[m] * (1.0f / 252.0f);
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
// s1 and s2 are Roland's stretched tunings. The D-50's OWN mask ROM
// (uPD78312 internal ROM, table at 0x01F1, scale 0x5555 = 1.0) states
// 0x5568 and 0x55B1: s1 = 1.000870, s2 = 1.004210 -- about 2.1 and 10.1
// cents at two octaves from center. The D-05 remake uses slightly gentler
// values (1.000549/1.002869); the original hardware outranks the remake.
// Every other entry of that table matches the fractions below exactly,
// which is also the proof they are read right.
inline constexpr float kKeyfollow[17] = {
    -1.0f, -0.5f, -0.25f, 0.0f, 0.125f, 0.25f, 0.375f, 0.5f,
    0.625f, 0.75f, 0.875f, 1.0f, 1.25f, 1.5f, 2.0f,
    1.0008699f, 1.0042115f};

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

// Only the single-cycle loops move to RAM. The combination waves 77..100
// are firmware-decoded REGIONS over the same ROM material -- 2 to 32
// pages, up to 65536 words each -- far past what RAM holds, and their
// long revolutions read near-sequentially, which the XIP cache handles
// the way it handles the attacks.
constexpr bool loop_in_ram(const PcmSample& s) {
    return s.looped && s.length != 0 && s.length <= 2048;
}

constexpr uint32_t loop_ram_words() {
    uint32_t n = 0;
    for (int i = 0; i < kPcmCount; ++i)
        if (loop_in_ram(kPcmSamples[i])) n += kPcmSamples[i].length;
    return n;
}

inline bool install_loop_ram(const int16_t* blob, int16_t* ram, uint32_t cap) {
    uint32_t used = 0;
    for (int i = 0; i < kPcmCount; ++i) {
        const PcmSample& smp = kPcmSamples[i];
        if (!loop_in_ram(smp)) continue;
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
        if (loop_in_ram(smp) && g_loop_ram.base) {
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
    // Linear per munt's TVF.cpp levelMult chain -- the earlier kAmountCurve
    // assignment (by table neighbourhood) is displaced by proven semantics.
    s.tvf_env_depth = level01(p[18]);
    // The TVF envelope times follow munt's proven ramp law: a phase lasts
    // 4 ms times 2^(byte/8), independent of the level distance, because the
    // chip's increment table adds 8 per doubling of distance and the ramp
    // doubles speed per 8 -- the two cancel (envLogarithmicTime and
    // LA32Ramp, closed form verified bit-exact). Byte 100 is 23.2 seconds,
    // which is how Horn Section holds its brightness through a note: its
    // decay bytes of 74 and 76 are two and three seconds a phase. The TVA
    // keeps its reference-calibrated rates -- recordings outrank analogy.
    for (int k = 0; k < 5; ++k) {
        s.tvf_env.t[k] = 0.004f * fast_exp2(p[22 + k] * 0.125f);
        s.tvf_env.r[k] = 0.0f;
    }
    s.tvf_env.t[4] = env_time(p[26]);
    s.tvf_env.l[0] = level01(p[27]);
    s.tvf_env.l[1] = level01(p[28]);
    s.tvf_env.l[2] = level01(p[29]);
    s.tvf_env.sustain = level01(p[30]);
    s.tvf_env.end = p[31] ? 1.0f : 0.0f;

    // ---- TVA: level and its envelope
    const float level = level01(p[35]);
    s.tva_level = level;
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
    s.tva_env.log_segments = true;     // TVA decays in dB, see Env5Spec
    for (int k = 0; k < 5; ++k) s.tva_env.r[k] = env_rate_db_s(p[39 + k]);
    // The release runs on a scale of its own, three times the decay rates:
    // Stereo Polysynth's release byte 52 measures about -100 dB/s where
    // Pizzagogo's decay bytes 50..59 measure -34, and with the factor in
    // place every one of the five reference tails comes out env- or
    // reverb-limited exactly as recorded.
    s.tva_env.r[4] *= 3.0f;
    v.pcm_env[index] = s.tva_env;      // the sampled partial shares it

    v.keyfollow[index] = keyfollow_ratio(p[2], 16);
    v.velo_sens[index] = (static_cast<int>(p[36]) - 50) * 0.02f;
    s.cutoff_keyfollow = keyfollow_ratio(p[15], 14);
    s.pitch_keyfollow = v.keyfollow[index];
    s.pw_velo = static_cast<float>(p[9]) - 7.0f;
    s.tvf_velo = level01(p[19]);

    // The envelope keyfollows and the TVF bias, bound by the IC25
    // disassembly (workflows wgt50aax0/wni28ji2j). The raw bytes go to the
    // spec; Voice::note_on resolves them per note through the firmware's
    // segment arithmetic in d5_env.h.
    s.tvf_depth_kf = p[20] > 4 ? 4 : p[20];
    s.env_from_bytes = true;
    for (int k = 0; k < 5; ++k) {
        s.tvf_bytes.t[k] = p[22 + k] > 100 ? 100 : p[22 + k];
        s.tva_bytes.t[k] = p[39 + k] > 100 ? 100 : p[39 + k];
    }
    for (int k = 0; k < 4; ++k) {
        s.tvf_bytes.l[k] = p[27 + k] > 100 ? 100 : p[27 + k];
        s.tva_bytes.l[k] = p[44 + k] > 100 ? 100 : p[44 + k];
    }
    s.tvf_bytes.end = p[31];
    s.tva_bytes.end = p[48];
    s.tvf_bytes.time_kf = p[21] > 4 ? 4 : p[21];
    s.tva_bytes.time_kf = p[50] > 4 ? 4 : p[50];
    s.tva_bytes.vel_kf = p[49] > 4 ? 4 : p[49];
    s.tva_level_byte = p[35] > 100 ? 100 : p[35];
    s.tva_velo_byte = p[36] > 100 ? 100 : p[36];
    s.reso_byte = p[14] > 30 ? 30 : p[14];
    s.tva_bias_point = p[37];
    s.tva_bias_level = p[38] > 12 ? 12 : p[38];
    // Bias point: values 0..63 are <A1..<C7 (the tilt reaches down the
    // keyboard), 64..127 the same notes reaching up; the ROM strips bit 6
    // for the direction and subtracts 27, putting A1 at -27 from C4
    // (IC25 0x080C-0x0834). Level 0..14 is -7..+7 through the magnitude
    // table at 0x08EC.
    static constexpr float kBiasMag[15] = {170, 120, 85, 54, 34, 21, 11, 0,
                                           11, 21, 34, 54, 85, 120, 170};
    s.bias_note_rel = static_cast<int8_t>((p[16] & 0x3F) - 27);
    s.bias_above = (p[16] >> 6) & 1;
    const int bias_lv = p[17] > 14 ? 14 : p[17];
    s.bias_slope = (bias_lv < 7 ? -1.0f : 1.0f)
                   * kBiasMag[bias_lv] * (1.0f / 128.0f);

    // ---- modulation routes
    // WG Mod LFO Mode, offset 3: OFF, (+), (-), A&L. The magnitude is not
    // here -- it is the common block's P-Mod LFO Depth, applied in
    // map_common once it has been read -- so the route carries the sign
    // only. A&L means the depth comes from aftertouch and the bender lever
    // alone; neither performance control is implemented yet, so those
    // partials correctly get none, rather than a full-depth vibrato the
    // player never asked for.
    const uint8_t mode = p[3];
    if (mode == 0) {
        v.pitch_lfo[index] = LfoRoute{};
        v.lever_gate[index] = 0.0f;
    } else {
        v.pitch_lfo[index].lfo = 0;                    // P-Mod rides LFO-1
        // A&L partials carry no standing depth; all non-off modes accept the
        // lever, signed like the route.
        const float sign = (mode == 2) ? -1.0f : 1.0f;
        v.pitch_lfo[index].depth = (mode == 3) ? 0.0f : sign;
        v.lever_gate[index] = sign;
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
    // All of it now the tick engine's own reading (IC25, workflow
    // wgt50aax0): the times are table indices resolved per note against
    // the time keyfollow c[12], the levels go through the 0x14D5 curve and
    // are scaled per note by velocity mode c[11]. The earlier
    // kDepthCurve/2400-cents pairing was two guesses that happened to
    // nearly cancel for small settings; the proven chain replaces both.
    for (int i = 0; i < 4; ++i) {
        v.penv.t_idx[i] = c[13 + i] > 50 ? 50 : c[13 + i];
    }
    v.penv.time_kf = c[12] > 4 ? 4 : c[12];
    v.penv.velo_mode = c[11] > 2 ? 2 : c[11];
    v.penv.l0 = penv_level(c[17]);
    v.penv.l1 = penv_level(c[18]);
    v.penv.l2 = penv_level(c[19]);
    v.penv.sustain = penv_level(c[20]);
    v.penv.end = penv_level(c[21]);

    // P-Mod LFO Depth, offset 22, and the lever, offset 23: both on the
    // ROM's own depth curve (kPModCurve above), full scale +-600 cents.
    const float pmod = kPModCurve[c[22] > 100 ? 100 : c[22]];
    v.pitch_lfo[0].depth *= pmod;
    v.pitch_lfo[1].depth *= pmod;
    v.lever_amount = kPModCurve[c[23] > 100 ? 100 : c[23]];

    // ---- the three LFOs
    for (int i = 0; i < 3; ++i) {
        const uint8_t* l = c + 25 + i * 4;
        LfoSpec& spec = v.lfo[i];
        spec.wave = static_cast<LfoWave>(l[0] > 3 ? 0 : l[0]);
        spec.rate = level01(l[1]);
        spec.delay_byte = l[2] > 100 ? 100 : l[2];
        spec.sync = l[3] > 2 ? 0 : l[3];
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
    // Key Shift moves the KEY, not the oscillator: the ROM adds it before
    // the keyfollow multiply (see VoiceSpec::key_shift). Adding it to
    // coarse -- the previous reading -- was only equivalent at keyfollow
    // 1.0, which 80 of the bank's partials do not use.
    p.upper.voice.key_shift = static_cast<int>(pb[22]) - 24;
    p.lower.voice.key_shift = static_cast<int>(pb[23]) - 24;
    p.reverb.type = pb[30];
    // Reverb Balance is another panel value on the amount family, and the
    // reference recordings vouch for it: through this curve Cathedral
    // Organ's byte 58 puts the tail 6 dB under the playing (recorded: 7.4)
    // and Pizzagogo's byte 33 puts it at 15 (recorded: 13.6). Read linearly
    // they were both practically as loud as the notes.
    p.reverb.balance = kAmountCurve[pb[31] > 100 ? 100 : pb[31]];
    p.volume = level01(pb[32]);
    p.balance = level01(pb[33]);

    // Headroom, not taste: eight voices per tone at full TVA level would ask
    // for eight times unity, and the saturator would then be working on every
    // chord instead of only on the loudest. A quarter of a voice's level per
    // tone leaves a four-note chord peaking around -3 dB.
    // Sixteen voices of two partials can stack during held or pedalled
    // playing, and at 0.13 a full legato pile sat pinned against the
    // saturator at -1.4 dBFS -- a wall of limiting that swallowed every new
    // attack, heard as "the notes cancelling each other". 0.09 keeps the
    // same pile at about -4 dBFS: dense, but with the transients alive.
    // 0.22 after the edge-law port: the correct waveforms carry about 8 dB
    // less crest than the invented narrow pulses, and the reverb taming took
    // another 6 -- the bank had sunk to -28.6 dBFS RMS with the volume knob
    // already at 98. At 0.22 the bank peaks just under full scale and the
    // brutal 24-note legato pile stays clear of the saturator wall.
    // 0.58 after the chip-level port: the firmware's TVA basis normalizes
    // to its 155-step design ceiling, which sits a median 8.4 dB under
    // the old linear reading across the bank -- this puts the median
    // patch back where the approved build had it, and the saturator
    // catches the few that the new law makes hotter.
    p.upper.level = 0.58f;
    p.lower.level = 0.58f;
    return p;
}

}  // namespace d5
