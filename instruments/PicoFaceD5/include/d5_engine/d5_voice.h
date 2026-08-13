// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// One voice of a D-50 tone: the two partials, the structure that decides how
// they meet, and the tone's LFOs and pitch envelope as this note hears them.
//
// The equalizer, the chorus and the reverb are not here. They sit behind the
// sum of all voices (see d5_patch.h) -- a per-voice chorus would be both
// wrong and, at six kilobytes of delay line each, expensive.
//
// The seven structures are the table on page 22 of the Advanced Course, and
// the block diagrams there carry a detail worth spelling out: in a ring
// structure the second partial is never heard on its own. What reaches the
// output is partial 1 plus the ring modulation of the two -- which is why
// those structures stay recognisable as partial 1 with something metallic
// growing out of it, rather than turning into two separate voices.
//
//   Str  P1  P2  output
//    1   S   S   P1 + P2
//    2   S   S   P1 + ring(P1, P2)
//    3   P   S   P1 + P2
//    4   P   S   P1 + ring(P1, P2)
//    5   S   P   P1 + ring(P1, P2)
//    6   P   P   P1 + P2
//    7   P   P   P1 + ring(P1, P2)
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_fastmath.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_lfo.h"
#include "d5_engine/d5_pcm_voice.h"
#include "d5_engine/d5_synth_voice.h"

namespace d5 {

enum class PartialType : uint8_t { kSynth = 0, kPcm = 1 };

struct Structure {
    PartialType p1;
    PartialType p2;
    bool ring;
};

inline constexpr Structure kStructures[7] = {
    {PartialType::kSynth, PartialType::kSynth, false},   // 1
    {PartialType::kSynth, PartialType::kSynth, true },   // 2
    {PartialType::kPcm,   PartialType::kSynth, false},   // 3
    {PartialType::kPcm,   PartialType::kSynth, true },   // 4
    {PartialType::kSynth, PartialType::kPcm,   true },   // 5
    {PartialType::kPcm,   PartialType::kPcm,   false},   // 6
    {PartialType::kPcm,   PartialType::kPcm,   true },   // 7
};

// How far a partial follows one of the three LFOs. The panel writes this as
// +1..+3 / -1..-3 (which LFO, and in which direction) plus a depth.
struct LfoRoute {
    int lfo = 0;            // 0..2
    float depth = 0.0f;     // -1..+1, sign is the panel's polarity
};

// What the pitch envelope does to a partial: the panel's WG Mod P-ENV Mode,
// off / rising with the envelope / inverted.
enum class PEnvMode : uint8_t { kOff = 0, kPositive = 1, kNegative = 2 };

struct VoiceSpec {
    int structure = 1;              // 1..7, panel "Structure No."
    float balance = 0.5f;           // 0..1, panel "Partial Balance", .5 = even
    // Bit 0 lets partial 1 sound, bit 1 partial 2. The panel calls this
    // "Partial Mute" and shows it as two bits; which bit is which is not
    // verified against hardware.
    uint8_t partials_on = 0x3;
    // Per-partial pitch in semitones, the panel's "WG Pitch Coarse". Detuning
    // the second partial is what makes a ring structure inharmonic rather
    // than just brighter.
    int coarse[2] = {0, 0};
    // The patch's Key Shift, in semitones. Not folded into coarse: the ROM
    // adds it to the key BEFORE the keyfollow multiply (IC25 0x0561-0x059D
    // feeding the MULUW at 0x0F09), so a partial with keyfollow 1/2 moves
    // by half the shift -- through coarse it moved by all of it. Past +/-48
    // the ROM folds by octaves rather than clamping (0x195C-0x196C).
    int key_shift = 0;
    // WG Pitch Keyfollow as a ratio: how far the oscillator follows the
    // keyboard, pivoted on C4. 1 is normal, 0 pins the partial to its coarse
    // pitch (drum layers), 1/2 plays quarter-tone steps -- sound design the
    // D-50 uses freely and the bank uses in 80 of 256 partials.
    float keyfollow[2] = {1.0f, 1.0f};
    // TVA Velocity Range as -1..+1: how far the strike reaches the level.
    // +1 is the raw-velocity behaviour this engine always had; 0 means the
    // partial ignores velocity, which 7 of the bank's partials ask for and
    // previously could not get; negative inverts.
    float velo_sens[2] = {1.0f, 1.0f};
    // P-Mod Lever (common offset 23): how much vibrato the player's left
    // hand may add on top of the standing depth. Gated per partial by the
    // WG LFO mode -- A&L partials respond to the lever alone, which is why
    // Living Calliope reads depth 0 and lever 23: the "living" is the hand.
    float lever_amount = 0.0f;
    float lever_gate[2] = {0.0f, 0.0f};
    // Panel "WG Pitch Fine" per partial, plus the instrument's master tune;
    // both in cents, both continuous, so they can detune a pair against each
    // other without landing on a semitone.
    float fine_cents[2] = {0.0f, 0.0f};
    float master_cents = 0.0f;

    SynthSpec synth[2]{};           // used where the structure says S
    PcmSampleRef pcm[2]{};          // used where it says P
    Env5Spec pcm_env[2]{};

    // ---- common block: three LFOs and the pitch envelope, shared by both
    LfoSpec lfo[3]{};
    PitchEnvSpec penv{};

    // pitch modulation: the LFO route reaches +/- 600 cents at full depth,
    // the pitch envelope up to +/- 2400 (service notes, "PITCH MODULATION")
    LfoRoute pitch_lfo[2]{};
    PEnvMode penv_mode[2] = {PEnvMode::kOff, PEnvMode::kOff};

    LfoRoute pw_lfo[2]{};           // panel "WG PW LFO Select / Depth"
    LfoRoute tvf_lfo[2]{};          // panel "TVF Mod LFO Select / Depth"
    LfoRoute tva_lfo[2]{};          // panel "TVA Mod LFO Select / Depth"
};

class Voice {
public:
    void note_on(const VoiceSpec& spec, int note, float velocity,
                 float sample_rate) {
        spec_ = spec;
        const Structure& st = structure();
        const PartialType types[2] = {st.p1, st.p2};
        for (int i = 0; i < 3; ++i) {
            lfo_[i].start(spec_.lfo[i], sample_rate, 0x9E3779B9u * (i + 1));
        }

        // The effective key: shifted, C4-relative, folded into +/-48 by
        // octaves the way the ROM does it. This one number feeds the pitch
        // keyfollow, the P-ENV's time keyfollow, the TVF's depth and time
        // keyfollows, the TVA's time keyfollow and the bias distance --
        // the firmware keeps it at 0xFE79 for exactly that reason.
        int key = note - 60 + spec_.key_shift;
        while (key > 48) key -= 12;
        while (key < -48) key += 12;

        const float vel127 = velocity * 127.0f;
        penv_.start(spec_.penv, sample_rate, key, vel127);

        mod_count_ = 0;
        mod_[0] = Modulation{};
        mod_[1] = Modulation{};
        dpitch_[0] = dpitch_[1] = 0.0f;
        damp_[0] = damp_[1] = 0.0f;
        for (int i = 0; i < 2; ++i) {
            const float sv = spec_.velo_sens[i];
            const float vel = sv >= 0.0f ? 1.0f + sv * (velocity - 1.0f)
                                         : 1.0f + sv * velocity;
            const float n = 60.0f
                + spec_.keyfollow[i] * static_cast<float>(key)
                + static_cast<float>(spec_.coarse[i]);
            const float cents = spec_.fine_cents[i] + spec_.master_cents;
            const float detune = cents != 0.0f
                                     ? std::pow(2.0f, cents / 1200.0f) : 1.0f;

            // Envelope time keyfollows, applied as byte shifts under each
            // law's own exponent: the chip halves a phase per -8 on the
            // byte, the TVA rate law gains 2^(1/60) per byte. Velocity's
            // (vel-64) >> (6-p49) runs ungated -- the ROM has no zero
            // check there -- the key terms are gated like their sources.
            SynthSpec& syn = spec_.synth[i];
            const int voff = static_cast<int>(vel127 - 64.0f)
                             >> (6 - (syn.tva_time_vkf > 4 ? 4 : syn.tva_time_vkf));
            const int koff = syn.tva_time_kkf
                ? (key >> (4 - (syn.tva_time_kkf > 4 ? 4 : syn.tva_time_kkf))) : 0;
            const int tva_off = voff + koff;
            if (tva_off != 0) {
                const float tf = fast_exp2(-tva_off * 0.125f);
                const float rf = fast_exp2(tva_off * (1.0f / 60.0f));
                for (int k = 0; k < 5; ++k) {
                    syn.tva_env.t[k] *= tf;
                    syn.tva_env.r[k] *= rf;
                    spec_.pcm_env[i].t[k] *= tf;
                    spec_.pcm_env[i].r[k] *= rf;
                }
            }
            if (syn.tvf_time_kf != 0) {
                const int kf = syn.tvf_time_kf > 4 ? 4 : syn.tvf_time_kf;
                const float tf = fast_exp2(-(key >> (5 - kf)) * 0.125f);
                for (int k = 0; k < 5; ++k) syn.tvf_env.t[k] *= tf;
            }

            if (types[i] == PartialType::kPcm) {
                pcm_[i].note_on(spec_.pcm[i], n, vel,
                                spec_.pcm_env[i], sample_rate, detune);
            } else {
                synth_[i].note_on(syn, n, vel, sample_rate, detune, key);
            }
        }
    }

    // Sync mode 2 on LFO-1: a new note anywhere in the tone restarts the
    // vibrato of the voices already sounding.
    void retrigger_lfo1() { lfo_[0].retrigger(); }

    void note_off() {
        penv_.release();
        const Structure& st = structure();
        const PartialType types[2] = {st.p1, st.p2};
        for (int i = 0; i < 2; ++i) {
            if (types[i] == PartialType::kPcm) pcm_[i].note_off();
            else synth_[i].note_off();
        }
    }

    bool active() const {
        const Structure& st = structure();
        const bool a = (st.p1 == PartialType::kPcm) ? pcm_[0].active()
                                                    : synth_[0].active();
        const bool b = (st.p2 == PartialType::kPcm) ? pcm_[1].active()
                                                    : synth_[1].active();
        // A ring structure has nothing left to say once partial 1 is gone:
        // its product is silent without it.
        return st.ring ? a : (a || b);
    }

    // Control rate: everything the LFOs and the pitch envelope feed changes
    // at tens of hertz at most, so it is computed once per kModPeriod
    // samples. Pitch and amplitude ramp linearly across the block so nothing
    // steps; cutoff and pulse width hold, and the synth partial recomputes
    // its own block-rate half from them at the same moment.
    void update_block() {
        mod_count_ = kModPeriod;
        const Structure& st = structure();
        const float l[3] = {lfo_[0].next_n(kModPeriod), lfo_[1].next_n(kModPeriod),
                            lfo_[2].next_n(kModPeriod)};
        const float pitch_env = penv_.next_n(kModPeriod);

        for (int i = 0; i < 2; ++i) {
            const LfoRoute& pr = spec_.pitch_lfo[i];
            // P-Mod rides LFO-1, and its two depth sources part ways at the
            // delay: the standing depth waits out the silence and swells
            // with the fade, the lever speaks at once -- the ROM multiplies
            // only the c[22] term by the fade ramp (IC25 0x177B-0x17A3),
            // the controller terms bypass it.
            const float depth = pr.depth * lfo_[0].gate()
                + spec_.lever_gate[i] * spec_.lever_amount * wheel_;
            const float cents = 600.0f * depth * lfo_[0].raw();
            float factor = cents != 0.0f
                               ? fast_exp2(cents * (1.0f / 1200.0f)) : 1.0f;
            if (spec_.penv_mode[i] == PEnvMode::kPositive) {
                factor *= pitch_env;
            } else if (spec_.penv_mode[i] == PEnvMode::kNegative) {
                factor /= pitch_env;
            }
            const float tgt_pitch = factor * bend_;
            mod_[i].pw = 0.5f * spec_.pw_lfo[i].depth * lfo_value(l, spec_.pw_lfo[i]);
            mod_[i].cutoff = 0.5f * spec_.tvf_lfo[i].depth * lfo_value(l, spec_.tvf_lfo[i]);
            // amplitude modulation only ever ducks, never boosts past unity
            const float am = spec_.tva_lfo[i].depth * lfo_value(l, spec_.tva_lfo[i]);
            float tgt_amp = 1.0f + 0.5f * (am - std::fabs(spec_.tva_lfo[i].depth));
            if (tgt_amp < 0.0f) tgt_amp = 0.0f;
            dpitch_[i] = (tgt_pitch - mod_[i].pitch) * (1.0f / kModPeriod);
            damp_[i] = (tgt_amp - mod_[i].amp) * (1.0f / kModPeriod);
            const PartialType t = (i == 0) ? st.p1 : st.p2;
            if (t == PartialType::kSynth) synth_[i].block_mod(mod_[i]);
        }
    }

    float D5_HOT_TAG(d5_voice_next, next)() {
        const Structure& st = structure();
        if (mod_count_ == 0) update_block();
        --mod_count_;
        mod_[0].pitch += dpitch_[0];
        mod_[0].amp += damp_[0];
        mod_[1].pitch += dpitch_[1];
        mod_[1].amp += damp_[1];

        float a = (st.p1 == PartialType::kPcm) ? pcm_[0].next(mod_[0])
                                               : synth_[0].next(mod_[0]);
        float b = (st.p2 == PartialType::kPcm) ? pcm_[1].next(mod_[1])
                                               : synth_[1].next(mod_[1]);
        if (!(spec_.partials_on & 0x1)) a = 0.0f;
        if (!(spec_.partials_on & 0x2)) b = 0.0f;

        // The chip multiplies in the log domain, which is an ordinary product
        // once decoded: sum and difference frequencies, and silence whenever
        // either side is silent.
        const float second = st.ring ? a * b : b;

        const float bal = spec_.balance < 0.0f ? 0.0f
                        : (spec_.balance > 1.0f ? 1.0f : spec_.balance);
        const float w1 = bal > 0.5f ? 2.0f * (1.0f - bal) : 1.0f;
        const float w2 = bal < 0.5f ? 2.0f * bal : 1.0f;
        return a * w1 + second * w2;
    }

    // Pitch bend reaches notes that are already sounding, so it cannot go
    // through the spec the way coarse and fine tune do.
    void set_bend(float factor) { bend_ = factor; }
    void set_wheel(float w) { wheel_ = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w); }

    const Structure& structure() const {
        const int i = (spec_.structure < 1 || spec_.structure > 7)
                          ? 0 : spec_.structure - 1;
        return kStructures[i];
    }

private:
    static float lfo_value(const float l[3], const LfoRoute& r) {
        const int i = (r.lfo < 0 || r.lfo > 2) ? 0 : r.lfo;
        return l[i];
    }

    VoiceSpec spec_{};
    PcmVoice pcm_[2]{};
    SynthPartial synth_[2]{};
    Lfo lfo_[3]{};
    PitchEnv penv_{};
    float bend_ = 1.0f;
    float wheel_ = 0.0f;
    Modulation mod_[2]{};
    float dpitch_[2] = {0.0f, 0.0f};
    float damp_[2] = {0.0f, 0.0f};
    int mod_count_ = 0;
};

}  // namespace d5
