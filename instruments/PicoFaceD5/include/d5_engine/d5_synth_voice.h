// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// Synth partial of the LA engine: the half that is generated rather than
// sampled, and the half responsible for the D-50's sustains.
//
// The LA32 has no filter. It builds the *already filtered* waveform directly:
// a square is assembled from a rising cosine segment, a high plateau, a
// falling cosine segment and a low plateau, and the cutoff sets how wide the
// cosine segments are -- a low cutoff means long, soft slopes and few
// harmonics. Resonance is a decaying sine at the cutoff frequency, restarted
// every cycle and windowed so it does not click. A sawtooth is that square
// multiplied by a cosine at the fundamental -- which doubles the comb. The
// MT-32 compensates in its control ROM (munt TVP: saw basePitch 4096 units
// below square), but the D-50 does not: its pitch constant is built from
// coarse and fine only (ic25 0x0357, neutral constant 0x2480 = 36*256+128),
// no waveform byte is read anywhere on the pitch path. So the doubling
// survives to the outputs and the D-50's saw genuinely plays one octave
// above its square at equal settings -- the WaveSQU/WaveSAW captures show
// exactly that (262 Hz vs 524 Hz, same key), and the factory bank is
// written around it (square partials sit one coarse octave above the saw
// they double). This follows the model munt documents for the chip
// (mt32emu, LA32WaveGenerator); no code is taken from it -- LGPL-2.1
// upstream, and the description is what matters here.
//
// Doing it this way is also what makes the partial cheap enough for the
// RP2350: no filter state, no oversampling, one cosine table.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_env.h"
#include "d5_engine/d5_fastmath.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_mod.h"
#include "d5_engine/d5_mod.h"

namespace d5 {

enum class Waveform : uint8_t { kSquare = 0, kSawtooth = 1 };

struct SynthSpec {
    Waveform waveform = Waveform::kSawtooth;
    float pulse_width = 0.5f;      // 0..1, panel "WG Pulse Width"
    float cutoff = 0.7f;           // 0..1, panel "TVF Cutoff Frequency"
    float resonance = 0.3f;        // 0..1, panel "TVF Resonance"
    float tvf_env_depth = 0.4f;    // 0..1 of the panel byte; linear in chip units
    float cutoff_keyfollow = 0.0f; // ratio; the cutoff tracks the keyboard
    float tvf_velo = 0.0f;         // 0..1 of the sensitivity byte
    float pitch_keyfollow = 1.0f;  // the WG ratio; the TVF tracks the difference
    float pw_velo = 0.0f;          // -7..+7, PW Velocity Range (offset 9)
    // TVF ENV Depth Keyfollow (offset 20): the key subtracts from the
    // depth's velocity term (>> 4-value), hard zero at 0 (IC25
    // 0x1986-0x19A4; Roland patched this term's sign from ADD to SUB in
    // the mask ROM -- the dead original still sits at 0x08FE).
    uint8_t tvf_depth_kf = 0;      // 0..4
    // The raw envelope bytes; when env_from_bytes is set, Voice::note_on
    // resolves them through the firmware's own segment arithmetic
    // (build_tvf_env / build_tva_env) and the Env5Specs below are only
    // the hand-built preset path's fallback.
    EnvBytes tvf_bytes{};
    EnvBytes tva_bytes{};
    bool env_from_bytes = false;
    // Preset-path fallback level; the byte path below displaces it.
    float tva_level = 1.0f;
    // The raw bytes of the TVA level basis (tva_chip_level in d5_env.h):
    // level p35, velocity range p36, resonance p14 (its half compensates
    // the resonance recipe's loudness on synth partials), bias point and
    // level p37/p38.
    uint8_t tva_level_byte = 100;
    uint8_t tva_velo_byte = 50;
    uint8_t reso_byte = 0;
    uint8_t tva_bias_point = 0;
    uint8_t tva_bias_level = 12;
    // TVF Bias (offsets 16/17): beyond the bias point the cutoff tilts by
    // bias_slope chip units per semitone -- the ROM multiplies a 15-entry
    // magnitude table (0x08EC, symmetric around index 7) by the key
    // distance in the direction the point's bit 6 selects
    // (IC25 0x080C-0x0834). Slope carries the level's sign.
    int8_t bias_note_rel = -27;    // bias point, semitones from C4
    uint8_t bias_above = 0;        // 1: applies above the point, 0: below
    float bias_slope = 0.0f;       // chip cutoff units per semitone
    Env5Spec tvf_env{};
    Env5Spec tva_env{};
};

class SynthPartial {
public:
    void note_on(const SynthSpec& spec, float note, float velocity,
                 float sample_rate, float detune = 1.0f, int key_rel60 = 0) {
        spec_ = spec;
        sr_ = sample_rate;
        // On the byte path the strike's loudness lives inside the chip
        // level (velocity term of tva_chip_level); scaling the gain again
        // would count it twice.
        gain_ = spec.env_from_bytes ? 1.0f : velocity;
        phase_ = 0.0f;
        active_ = true;
        freq_ = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f) * detune;
        // No per-waveform pitch offset here: the D-50 firmware is
        // waveform-blind on the pitch path (proven: the only coarse/fine
        // ingestion is ic25 0x0357-0x03E3, and 0x2480 pins the neutral
        // coarse at 36), so the ring-mod doubling reaches the listener --
        // the saw plays an octave above the square. The factory bank
        // depends on it; halving here dropped every saw stack an octave
        // below the original (Michael: 37/44/45/46 too deep).
        // The whole cutoff path now runs in the LA32's own unit, ported
        // from munt (LA32FloatWaveGenerator.cpp, TVF.cpp): one unit per
        // panel cutoff step, neutral offset +78, chip middle at 128 (panel
        // byte 50), ceiling 240.
        //
        // Keyfollow is differential against the pitch keyfollow at
        // 21/16 units per semitone of ratio difference (TVF.cpp:65-68);
        // the envelope adds its own units through a velocity term the
        // chip computes as ((vel*sens)>>6)+109-sens, neutral near vel 64
        // (TVF.cpp:130ff).
        const float vel127 = velocity * 127.0f;
        const float sens = spec.tvf_velo * 100.0f;
        // The D-50's own ROM computes this exact term -- 109 + vel*sens/64
        // - sens stands verbatim at IC25 0x1974-0x1984, which is the
        // pleasant proof that munt read the chip right. The depth keyfollow
        // subtracts from it in the same units before the depth multiply
        // (0x1986-0x19A4): the key, arithmetic-shifted right by 4 minus the
        // panel value, zero-gated at panel 0.
        float velUnits = 109.0f + vel127 * sens * (1.0f / 64.0f) - sens;
        if (spec.tvf_depth_kf != 0) {
            const int kf = spec.tvf_depth_kf > 4 ? 4 : spec.tvf_depth_kf;
            velUnits -= static_cast<float>(key_rel60 >> (4 - kf));
        }
        float velTerm = velUnits * (1.0f / 109.0f);
        if (velTerm < 0.0f) velTerm = 0.0f;
        // The firmware's target byte is L * CC80 / 256 with CC80 = depth *
        // velTerm / 64 (IC25 0x0938-0x0951), which this product reproduces
        // -- and the chip applies that byte at two cutoff units per unit.
        // Roland's D-50 VST, dry, on Arco Strings: the sustain sits 44
        // units above the base at velocity 127 and 30 at 64; the byte says
        // 22 and 15. The base register itself is one unit per unit
        // (2 * panel + 54 lands where the VST's cutoff sweep lands), so the
        // factor belongs to the envelope path alone.
        env_units_ = 2.0f * spec.tvf_env_depth * 100.0f * (109.0f / 64.0f)
                     * velTerm * (100.0f / 256.0f) * 0.01f;
        // env_units_ is "chip units per unit of envelope level" -- the
        // Env5 output 0..1 scales it below.
        // The D-50's own base cutoff, byte-exact from IC25 0x0809-0x08EB:
        // a 16-bit accumulator takes (cutoff + 27) * 128, plus or minus
        // the bias (table 0x08EC magnitude times the distance past the
        // bias point) and the keyfollow ((kfTVF - kfWG) times the KEY
        // relative to C4, each keyfollow as the high byte of table 0x01F1,
        // i.e. 85 * ratio), then shifts left twice and keeps the high byte
        // -- everything lands divided by 64. So the chip register is
        // 2 * cutoff + 54 for the panel value, two chip units per panel
        // step, 154 at panel 50. The MT-32's CPU writes cutoff + 78 into
        // the same register (munt TVF.cpp), which is what stood here until
        // 02.09.2026 and left every D-50 partial 26 units -- more than an
        // octave and a half -- duller than the machine at panel 50, and
        // drifting further shut above it. Roland's D-50 VST, dry, puts
        // Arco Strings' tenth harmonic at -25 dB on three octaves; the old
        // base gave -57.
        base_cv_ = spec.cutoff * 200.0f + 54.0f
                   + (spec.cutoff_keyfollow - spec.pitch_keyfollow)
                     * (85.0f / 64.0f) * static_cast<float>(key_rel60);
        // TVF bias: past the bias point, in its direction, the cutoff
        // tilts by magnitude/64 chip units per semitone (0x081B-0x0830,
        // same /64 as everything else in that accumulator).
        if (spec.bias_slope != 0.0f) {
            const int rel = key_rel60 - spec.bias_note_rel;
            const int dist = spec.bias_above ? (rel > 0 ? rel : 0)
                                             : (rel < 0 ? -rel : 0);
            base_cv_ += spec.bias_slope * static_cast<float>(dist);
        }
        // The pitch cap (IC25 0x08CF-0x08DE): the note-on routine adds the
        // high byte of the composed pitch word -- 128 on C4, 16 per octave,
        // the word being relative to C4 -- to the base and, when the sum
        // carries past 256 by 0x58 or more, takes the excess off the base.
        // So the base cannot exceed 344 minus that byte: 216 on C4, 200 on
        // C5, 184 on C6. Panel 100 (254) is capped from the lowest keys
        // up, panel 70 (194) above C5, Arco Strings' 154 never below C8.
        // munt's TVF.cpp has the MT-32's form of the same guard.
        {
            const float cap = 216.0f - (16.0f / 12.0f) * (note - 60.0f);
            if (base_cv_ > cap) base_cv_ = cap;
        }
        // Pulse width: panel byte to the chip's 0..255, velocity-shifted
        // (Partial.cpp:222-227); at or below 128 the wave is symmetric.
        float pw255 = spec.pulse_width * 255.0f
                      + (vel127 - 64.0f) * spec.pw_velo;
        pw255_ = pw255 < 0.0f ? 0.0f : (pw255 > 255.0f ? 255.0f : pw255);
        res_ = spec.resonance * 30.0f + 1.0f;              // chip range 1..31
        res_amp_ = fast_exp2(1.0f - (32.0f - res_) * 0.25f);
        static constexpr float kDecayFactors[8] = {31, 16, 12, 8, 5, 3, 2, 1};
        res_decay_f_ = kDecayFactors[static_cast<int>(res_) >> 2 > 7
                                         ? 7 : static_cast<int>(res_) >> 2];
        edge_frac_ = 0.5f;              // neutral until the first block_mod
        inv_edge_ = 2.0f;
        atten_ = 1.0f;
        res_on_ = false;
        res_half_ = -1;
        res_sin_ = 0.0f;
        res_cos_ = 0.0f;
        h_frac_ = 0.0f;
        dc_shape_ = 0.0f;
        dc_out_ = 0.0f;

        inc_ = freq_ / sr_;
        tvf_.start(spec_.tvf_env, sr_);
        tva_.start(spec_.tva_env, sr_);
    }

    // CPU governor: fade this partial out and let it finish.
    void quick_release() { tva_.quick_release(sr_); }

    void note_off() {
        tvf_.release();
        tva_.release();
    }

    bool active() const { return active_; }

    // The block-rate half, in the chip's own cutoff unit (munt port).
    // cutoffVal = base + envelope + LFO; above the middle (128) the cosine
    // edge halves every 16 units, below it the whole wave attenuates by
    // -0.75 dB per unit and the resonance is silent. All constants the
    // per-sample code needs are refreshed here.
    void block_mod(const Modulation& mod) {
        if (!active_) return;
        const float env = tvf_.next_n(kModPeriod);
        // mod.cutoff arrives on the old 0..1 scale from the LFO routes;
        // 100 units span that scale, same as the panel byte.
        float cv = base_cv_ + env_units_ * 100.0f * env + mod.cutoff * 100.0f;
        if (cv > kCutoffCeiling) cv = kCutoffCeiling;       // the chip's clamp
        edge_frac_ = 0.5f;
        inv_edge_ = 2.0f;
        atten_ = 1.0f;
        res_on_ = false;
        if (cv > 128.0f) {
            edge_frac_ = 0.5f * fast_exp2(-(cv - 128.0f) * (1.0f / 16.0f));
            inv_edge_ = 1.0f / edge_frac_;
            if (spec_.resonance > 0.0f) {
                res_on_ = true;
                // One sample's worth of decay, for each half of the pulse.
                // The step in `over` is the phase step scaled by the edge;
                // pitch modulation moves it by well under a percent inside
                // a block, which the envelope cannot show.
                const float dover = inc_ * mod.pitch * inv_edge_;
                const float kp = fast_exp2(-0.125f * res_decay_f_ * dover);
                const float kn = fast_exp2(-0.125f * (res_decay_f_ + 0.25f) * dover);
                const float dc = fast_cos(0.5f * dover);   // one sample of turn
                const float ds = fast_sin(0.5f * dover);
                res_a_pos_ = dc * kp;  res_b_pos_ = ds * kp;
                res_a_neg_ = dc * kn;  res_b_neg_ = ds * kn;
                res_half_ = -1;          // re-seed exactly on the next sample
                // fade the resonance in over cutoff 128..144 (quarter sine)
                res_amp_eff_ = res_amp_;
                if (cv < 144.0f) {
                    res_amp_eff_ *= fast_sin((cv - 128.0f) * (1.0f / 64.0f));
                }
            }
        } else if (cv < 128.0f) {
            atten_ = fast_exp2(-0.125f * (128.0f - cv));
        }
        // Pulse width follows the chip's own law (LA32FloatWaveGenerator
        // LA32Partial pair): the ROM maps the panel byte through the plain
        // 0..100 -> 0..255 scaler (IC25 0x02EC: T[k] = round(2.55k)), and
        // the chip answers with a positive-segment fraction of one half
        // (an honest square) at or below 128, contracting exponentially
        // above. The panel's lower dozen bytes therefore all render as the
        // same hollow square -- which is exactly what the Pipe Solo
        // reference shows (its held octave rank is odd-harmonic; a narrow
        // pulse would put even harmonics within a dB of the fundamental).
        float pw = pw255_ + mod.pw * 255.0f;
        if (pw < 0.0f) pw = 0.0f;
        if (pw > 255.0f) pw = 255.0f;
        // The chip's pulse law, calibrated on Roland's D-50 VST with one
        // square partial, dry: panel 0/25/50/75 give duties of 50/25/12.5/
        // 6.25 % -- the spectral nulls sit at h4, h8 and h16 -- and panel
        // 100 stays at 1/16. Halving every 25 panel steps is munt's
        // exponent (64 register units) but it runs from register 0, not
        // from 128: munt's "at or below 128 the wave is symmetric" gate
        // (LA32WaveGenerator.cpp:84) does not hold on this machine, and
        // the Owner's Manual (p. 35) draws panel 50 asymmetric. The
        // firmware writes T[panel] + velocity straight into the register
        // (IC25 0x07E6-0x0805, 0x072A), so the register IS the panel scale.
        // Until 03.09.2026 panel 1..50 were all squares here.
        pulse_frac_ = pw < 192.0f ? fast_exp2(-1.0f - pw * (1.0f / 64.0f))
                                  : 0.0625f;
        h_frac_ = pulse_frac_ - edge_frac_;
        if (h_frac_ < 0.0f) h_frac_ = 0.0f;
        // The shape's DC: the half-cosine edges average to zero, so the
        // mean is the duty imbalance of the shelves -- 2*pulse-1, or
        // 2*edge-1 when a pulse narrower than the edge pair clamps its
        // high shelf away. Only the contracted pulses (panel bytes above
        // 50) carry one; a square's mean is zero.
        dc_shape_ = h_frac_ > 0.0f ? 2.0f * pulse_frac_ - 1.0f
                                   : 2.0f * edge_frac_ - 1.0f;
    }

    float D5_HOT_TAG(d5_synth_next, next)(const Modulation& mod = Modulation{}) {
        if (!active_) {
            dc_out_ = 0.0f;
            return 0.0f;
        }

        // The chip's square: a rising cosine edge, a high shelf, a falling
        // edge, a low shelf -- with playback starting in the centre of the
        // first edge (munt shifts relWavePos by half the cosine). All
        // lengths are fractions of the fundamental period here; the chip
        // divides by waveLen in samples, which cancels out.
        // The reciprocal is a block constant; dividing by it per sample
        // per partial cost three VDIVs a sample on the M33, which at
        // sixteen voices is a fifth of the render.
        const float e = edge_frac_;
        const float inv_e = inv_edge_;
        float rel = phase_ + 0.5f * e;
        if (rel > 1.0f) rel -= 1.0f;

        float out;
        if (rel < e) {
            out = -fast_cos(0.5f * rel * inv_e);
        } else if (rel < e + h_frac_) {
            out = 1.0f;
        } else if (rel < 2.0f * e + h_frac_) {
            out = fast_cos(0.5f * (rel - (e + h_frac_)) * inv_e);
        } else {
            out = -1.0f;
        }
        out *= atten_;                     // sub-middle cutoff closes by fading

        if (res_on_) {
            // The resonance sine: period two edges, restarted each half of
            // the pulse, decaying exponentially per edge-length travelled,
            // the negative half slightly faster; a squared-sine window
            // before each edge centre keeps the restarts click-free
            // (munt LA32FloatWaveGenerator.cpp:202-253).
            float rr = phase_;
            float sgn = 1.0f;
            float df = res_decay_f_;
            const float half = e + h_frac_;
            int hi = 0;
            if (rr >= half) { sgn = -1.0f; rr -= half; df += 0.25f; hi = 1; }
            const float over = rr * inv_e;
            // A decaying sine is a damped rotation, and both of its
            // arguments advance by a fixed step per sample within one half
            // of the pulse. So instead of a sine table and an exponential
            // every sample, the pair (sin, cos) -- already carrying the
            // decay -- is rotated forward with four multiplies, and the
            // tables are touched only where the run starts: at each half
            // boundary and after a block changes the cutoff. The restart
            // is frequent enough that the rotation's own drift never
            // accumulates (a few hundred steps at most, well under a part
            // in ten thousand).
            if (hi != res_half_) {
                res_half_ = hi;
                const float d0 = fast_exp2(-0.125f * df * over);
                res_sin_ = fast_sin(0.5f * over) * d0;
                res_cos_ = fast_cos(0.5f * over) * d0;
            } else {
                const float a = hi ? res_a_neg_ : res_a_pos_;
                const float b = hi ? res_b_neg_ : res_b_pos_;
                const float s = res_sin_ * a + res_cos_ * b;
                res_cos_ = res_cos_ * a - res_sin_ * b;
                res_sin_ = s;
            }
            float res = sgn * res_sin_;
            // sync window around the nearest edge centre
            float r2 = phase_;
            if (r2 >= 1.0f - 0.5f * e) r2 -= 1.0f;
            else if (r2 >= half - 0.5f * e && r2 >= 0.5f * e) r2 -= half;
            if (r2 < 0.5f * e) {
                const float w = fast_sin(0.5f * r2 * inv_e);
                res *= (r2 < 0.0f) ? w * w : (w < 0.0f ? -w : w);
            }
            out += res * res_amp_eff_;
        }

        if (spec_.waveform == Waveform::kSawtooth) {
            // the chip's sawtooth: the finished square (resonance included)
            // ring-modulated by a phase-locked cosine of the fundamental,
            // on the UNSHIFTED phase (munt cpp:256-259)
            out *= fast_cos(phase_);
        }

        phase_ += inc_ * mod.pitch;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        const float amp = tva_.next();
        if (tva_.finished()) active_ = false;
        // The partial's output DC with every scaling applied, for the
        // voice's mix point: the chip's ring modulation reads the raw
        // partial -- DC included -- but the D-50's line out is AC-coupled
        // and never passes it. A saw keeps its shape mean on the cosine
        // carrier (mean zero), so only the square reports one.
        dc_out_ = spec_.waveform == Waveform::kSquare
                      ? dc_shape_ * atten_ * amp * gain_ * mod.amp * 0.5f
                      : 0.0f;
        return out * amp * gain_ * mod.amp * 0.5f;
    }

    // DC of this partial's contribution next() just returned (see above).
    float dc() const { return dc_out_; }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kLog2Range = 8.6438561897747246f;   // log2(400)

    static float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }


    SynthSpec spec_{};
    Env5 tvf_{};
    Env5 tva_{};
    float sr_ = 32000.0f;
    float freq_ = 440.0f;
    float inc_ = 0.0f;
    float phase_ = 0.0f;
    float base_cv_ = 128.0f;       // cutoffVal at env 0, chip units
    float env_units_ = 0.0f;       // chip units per unit of envelope level
    // Ceiling of the composed cutoff (base + envelope + modulation). munt
    // reads 240 for the MT-32; Roland's D-50 VST says about 218: Intruder
    // FX releases its TVF to full depth (target byte 66 on base 156, twice
    // that in chip units) with resonance 30, and the peak settles at
    // 3255 Hz -- 216 puts it at 3016, 220 at 3541. One key (C4) measured;
    // whether the ceiling follows the key like the base cap is open.
#ifndef D5_CUTOFF_CEILING
#define D5_CUTOFF_CEILING 218.0f
#endif
    static constexpr float kCutoffCeiling = D5_CUTOFF_CEILING;
    float edge_frac_ = 0.5f;       // cosine edge as fraction of the period
    float inv_edge_ = 2.0f;        // 1/edge_frac_, a block constant
    float pulse_frac_ = 0.5f;
    float h_frac_ = 0.0f;
    float dc_shape_ = 0.0f;        // mean of the bare shape (duty imbalance)
    float dc_out_ = 0.0f;          // dc_shape_ with all output scaling on
    float atten_ = 1.0f;           // sub-middle broadband attenuation
    bool res_on_ = false;
    int res_half_ = -1;            // which half of the pulse the run is in
    float res_sin_ = 0.0f;         // damped sine and its quadrature partner,
    float res_cos_ = 0.0f;         // rotated forward one sample at a time
    float res_a_pos_ = 1.0f;       // cos(step) * decay, positive half
    float res_b_pos_ = 0.0f;       // sin(step) * decay, positive half
    float res_a_neg_ = 1.0f;       // ... and the negative half, which the
    float res_b_neg_ = 0.0f;       //     chip decays a quarter faster
    float res_ = 1.0f;
    float res_amp_ = 0.0f;
    float res_amp_eff_ = 0.0f;
    float res_decay_f_ = 8.0f;
    float pw255_ = 128.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
