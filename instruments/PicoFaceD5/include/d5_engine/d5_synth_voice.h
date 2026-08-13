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
// multiplied by a cosine at the fundamental. This follows the model munt
// documents for the chip (mt32emu, LA32WaveGenerator); no code is taken from
// it -- LGPL-2.1 upstream, and the description is what matters here.
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
        env_units_ = spec.tvf_env_depth * 100.0f * (109.0f / 64.0f)
                     * velTerm * (100.0f / 256.0f) * 0.01f;
        // env_units_ is "chip units per unit of envelope level" -- the
        // Env5 output 0..1 scales it below.
        base_cv_ = spec.cutoff * 100.0f + 78.0f
                   + (spec.cutoff_keyfollow - spec.pitch_keyfollow)
                     * (21.0f / 16.0f) * (note - 60.0f);
        // TVF bias: past the bias point, in its direction, the cutoff
        // tilts. The table maxima give 170/128 = 1.33 units per semitone,
        // which sits right beside the 21/16 the keyfollow uses -- the two
        // unit systems agree.
        if (spec.bias_slope != 0.0f) {
            const int rel = key_rel60 - spec.bias_note_rel;
            const int dist = spec.bias_above ? (rel > 0 ? rel : 0)
                                             : (rel < 0 ? -rel : 0);
            base_cv_ += spec.bias_slope * static_cast<float>(dist);
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
        atten_ = 1.0f;
        res_on_ = false;
        h_frac_ = 0.0f;

        inc_ = freq_ / sr_;
        tvf_.start(spec_.tvf_env, sr_);
        tva_.start(spec_.tva_env, sr_);
    }

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
        if (cv > 240.0f) cv = 240.0f;                       // the chip's clamp
        edge_frac_ = 0.5f;
        atten_ = 1.0f;
        res_on_ = false;
        if (cv > 128.0f) {
            edge_frac_ = 0.5f * fast_exp2(-(cv - 128.0f) * (1.0f / 16.0f));
            if (spec_.resonance > 0.0f) {
                res_on_ = true;
                // fade the resonance in over cutoff 128..144 (quarter sine)
                res_amp_eff_ = res_amp_;
                if (cv < 144.0f) {
                    res_amp_eff_ *= fast_sin((cv - 128.0f) * (1.0f / 64.0f));
                }
            }
        } else if (cv < 128.0f) {
            atten_ = fast_exp2(-0.125f * (128.0f - cv));
        }
        // The D-50's pulse width byte is duty percent, NOT the MT-32's
        // 0..255 scale with its symmetric lower half: the Pizzagogo
        // reference shows even harmonics at -4..+6 dB against the
        // fundamental for PW bytes 5 and 11, which only an asymmetric
        // narrow pulse produces -- munt's rule would have made those plain
        // squares with no even content at all. The velocity term keeps
        // munt's scale on the D-50's own semantics.
        float pwf = (pw255_ + mod.pw * 255.0f) * (1.0f / 255.0f);
        if (pwf < 0.05f) pwf = 0.05f;
        if (pwf > 0.95f) pwf = 0.95f;
        pulse_frac_ = pwf;
        h_frac_ = pulse_frac_ - edge_frac_;
        if (h_frac_ < 0.0f) h_frac_ = 0.0f;
    }

    float D5_HOT_TAG(d5_synth_next, next)(const Modulation& mod = Modulation{}) {
        if (!active_) return 0.0f;

        // The chip's square: a rising cosine edge, a high shelf, a falling
        // edge, a low shelf -- with playback starting in the centre of the
        // first edge (munt shifts relWavePos by half the cosine). All
        // lengths are fractions of the fundamental period here; the chip
        // divides by waveLen in samples, which cancels out.
        const float e = edge_frac_;
        float rel = phase_ + 0.5f * e;
        if (rel > 1.0f) rel -= 1.0f;

        float out;
        if (rel < e) {
            out = -fast_cos(0.5f * rel / e);
        } else if (rel < e + h_frac_) {
            out = 1.0f;
        } else if (rel < 2.0f * e + h_frac_) {
            out = fast_cos(0.5f * (rel - (e + h_frac_)) / e);
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
            if (rr >= half) { sgn = -1.0f; rr -= half; df += 0.25f; }
            const float over = rr / e;
            float res = sgn * fast_sin(0.5f * over)
                        * fast_exp2(-0.125f * df * over);
            // sync window around the nearest edge centre
            float r2 = phase_;
            if (r2 >= 1.0f - 0.5f * e) r2 -= 1.0f;
            else if (r2 >= half - 0.5f * e && r2 >= 0.5f * e) r2 -= half;
            if (r2 < 0.5f * e) {
                const float w = fast_sin(0.5f * r2 / e);
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
        return out * amp * gain_ * mod.amp * 0.5f;
    }

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
    float edge_frac_ = 0.5f;       // cosine edge as fraction of the period
    float pulse_frac_ = 0.5f;
    float h_frac_ = 0.0f;
    float atten_ = 1.0f;           // sub-middle broadband attenuation
    bool res_on_ = false;
    float res_ = 1.0f;
    float res_amp_ = 0.0f;
    float res_amp_eff_ = 0.0f;
    float res_decay_f_ = 8.0f;
    float pw255_ = 128.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
