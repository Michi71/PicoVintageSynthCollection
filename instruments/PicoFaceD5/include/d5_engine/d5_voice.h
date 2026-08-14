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

// The portamento slew table, verbatim from the mask ROM (IC25 0x18E3, 101
// bytes indexed by the time byte 0..100). Once per control tick -- the same
// 112-Hz interrupt that walks the LFOs and envelopes, called from 0x27F3 --
// the slew routine at 0x1866 steps every voice's sounding pitch word by
// 4 * T[time] units of 1/256 semitone toward the note's target (ADDW/SUBW
// RP3,RP0 after SHLW RP0,2) and snaps on overshoot. In seconds that reads:
// T[time] / 64 semitones per tick, 1.75 * T[time] semitones per second --
// time 0 is never looked up: the note-on path at 0x05B4 reads the rate
// byte first and snaps whenever it is zero.
inline constexpr uint8_t kPortaRate[101] = {
    255, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243,
    242, 241, 240, 238, 236, 234, 232, 230, 228, 226, 224, 222, 220, 218,
    216, 214, 212, 210, 206, 202, 198, 194, 190, 186, 182, 178, 174, 170,
    166, 162, 154, 146, 138, 132, 124, 116, 108, 100,  92,  86,  80,  74,
     68,  62,  57,  52,  48,  44,  42,  39,  36,  33,  31,  29,  28,  27,
     26,  25,  24,  24,  23,  22,  21,  20,  19,  18,  18,  17,  16,  15,
     14,  13,  12,  12,  11,  10,   9,   8,   7,   6,   6,   5,   4,   3,
      2,   1,   1};

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
    // Aftertouch (channel pressure) feeds the same lanes the lever feeds.
    // P-Mod: common c[24] is the AT twin of the lever depth c[23] -- the ROM
    // adds kPModCurve[c24] * AT to the vibrato value additively, clamped at
    // 0x0FFF, and bypasses the LFO fade exactly like the lever (IC25
    // 0x1552-0x158E). at_gate mirrors lever_gate.
    float at_amount = 0.0f;
    float at_gate[2] = {0.0f, 0.0f};
    // Per-partial AT ranges, the panel's "...AT Range" bytes folded to
    // (raw-7)/7: PW p[12], TVF p[34], TVA p[53]. PW and TVF offset their
    // lanes by up to +/-full-scale at the keybed's hardest press (chip
    // transforms at 0x10CE/0x113B: s = raw-7, PW |s|*AT*48, TVF *16 -- the
    // engine's 0.65 lane scale is the PLAUSIBLE reading, hearing test
    // pending); TVA attenuates (0x11A9): a positive range rests quiet and
    // rises under pressure, a negative one ducks.
    float pw_at[2] = {0.0f, 0.0f};
    float tvf_at[2] = {0.0f, 0.0f};
    float tva_at[2] = {0.0f, 0.0f};
    // Patch-common AT bend (pb[27]-12): up to +/-12 semitones of press-bend
    // on every sounding voice, exactly (pb27-12)*AT/128 semitones (EPROM
    // 0x5C34-0x5D1C: 2*AT*|range| units of 1/256 st, clamp +/-0xC00).
    float at_bend_semis = 0.0f;
    // WG Mod Bend Mode, partial byte p[5] (IC25 0x0E4D gates on mode & 3,
    // zero skips the partial's bend entirely): OFF mutes the wheel here,
    // NORM takes it at full range (x0x5555 at 0x0E57), KF scales it by the
    // partial's SIGNED pitch keyfollow (0x0E92 against the 0x01F1 table;
    // the CMP A,#03H split inverts the bend direction for the negative
    // keyfollows below index 3 -- a partial whose pitch walks DOWN the
    // keyboard must bend down when the wheel goes up). So kf 1/2 bends
    // half way, kf 2 bends twice as far, OFF is 0. The wheel's own range
    // is patch-common (pb[26]) and arrives through set_bend_semis.
    float bend_scale[2] = {1.0f, 1.0f};
    // Panel "WG Pitch Fine" per partial, plus the instrument's master tune;
    // both in cents, both continuous, so they can detune a pair against each
    // other without landing on a semitone.
    float fine_cents[2] = {0.0f, 0.0f};
    float master_cents = 0.0f;

    // Portamento is patch-common on the D-50 (MIDI chart: mode pb[20]
    // U/L/UL picks the tones that glide, switch pb[41], time pb[28]); the
    // mapper collapses the mode into porta_mode_on so the voice only asks
    // switch && mode_on && time != 0. The firmware keeps the same split:
    // FE33.0 is the switch, C5C6/C5C8 bit 0 the per-tone mode, FE01/FE09
    // the time -- and the slew's global gate snaps the sounding pitch to
    // target the tick the switch goes off.
    bool porta_mode_on = false;
    bool porta_switch = false;
    int porta_time = 0;             // panel 0..100

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
        sr_ = sample_rate;
        const Structure& st = structure();
        const PartialType types[2] = {st.p1, st.p2};
        // The LFOs belong to the TONE: the 112-Hz tick walks one phase word
        // per LFO per tone (IC25 0x1508-0x160D) and every voice reads the
        // shared words from the CD40 merge area -- notes in a chord vibrate
        // together. bind_lfos() hands a real voice the tone's three
        // instances; a voice that was never bound (unit tests on Voice
        // alone) runs its own copies instead, advanced in next().
        if (lfo_[0] == nullptr) {
            lfo_local_ = true;
            for (int i = 0; i < 3; ++i) {
                local_lfo_[i].start(spec_.lfo[i], sample_rate,
                                    0x9E3779B9u * (i + 1));
                lfo_[i] = &local_lfo_[i];
            }
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

            // Portamento: the firmware's slew keeps the slot's pitch word
            // where the last note left it (CB20 survives note-off and keeps
            // converging) and walks it to the new target from there -- so a
            // glide always starts at wherever this voice's previous note
            // ended, legato or not. A fresh voice has no such history, and
            // switch off, tone excluded, or time 0 all snap instead; the
            // ROM's note-on path makes the same three decisions at
            // 0x05A7-0x05D1 (cold CB20 is zero, and the slew at 0x1866 does
            // not touch a word whose rate reads zero either).
            glide_tgt_[i] = n;
            if (!spec_.porta_switch || !spec_.porta_mode_on ||
                spec_.porta_time == 0 || !glide_warm_[i]) {
                glide_pos_[i] = n;
            }
            glide_warm_[i] = true;
            glide_step_[i] = porta_step(spec_.porta_time, sample_rate);
            const float cents = spec_.fine_cents[i] + spec_.master_cents;
            const float detune = cents != 0.0f
                                     ? std::pow(2.0f, cents / 1200.0f) : 1.0f;

            // The envelopes, resolved through the firmware's own segment
            // arithmetic (d5_env.h): the effective TVF depth D scales its
            // distances, the TVA thinks in raw panel units, and both need
            // the key and the velocity -- which is why this happens here
            // and not at patch load.
            SynthSpec& syn = spec_.synth[i];
            const bool isPcm = types[i] == PartialType::kPcm;
            if (syn.env_from_bytes) {
                const int v127 = static_cast<int>(vel127);
                const int sens = static_cast<int>(syn.tvf_velo * 100.0f);
                int bias = 109 - sens + ((sens * v127) >> 6);
                if (syn.tvf_depth_kf) bias -= key >> (4 - syn.tvf_depth_kf);
                bias = bias < 0 ? 0 : (bias > 255 ? 255 : bias);
                const int depth = static_cast<int>(syn.tvf_env_depth * 100.0f);
                int D = (depth * bias) >> 6;
                if (D > 255) D = 255;
                build_tvf_env(syn.tvf_bytes, D, key, syn.tvf_env);
                // The full level basis: p35, velocity range, resonance
                // compensation, keyboard bias -- all additive in the
                // chip's log unit, normalized so the 155-step design
                // ceiling is 1.0.
                const int chip = tva_chip_level(
                    syn.tva_level_byte, syn.tva_velo_byte, syn.reso_byte,
                    isPcm, syn.tva_bias_point, syn.tva_bias_level, key, v127);
                const float lvl = chip <= 0
                    ? 0.0f : fast_exp2((chip - 155) * 0.0625f);
                build_tva_env(syn.tva_bytes, key, v127, lvl, syn.tva_env);
                spec_.pcm_env[i] = syn.tva_env;
            }

            if (isPcm) {
                pcm_[i].note_on(spec_.pcm[i], n,
                                syn.env_from_bytes ? 1.0f : vel,
                                spec_.pcm_env[i], sample_rate, detune);
            } else {
                synth_[i].note_on(syn, n,
                                  syn.env_from_bytes ? velocity : vel,
                                  sample_rate, detune, key);
            }
        }
    }

    // Hands the voice the tone's three shared LFO instances (see note_on).
    void bind_lfos(const Lfo shared[3]) {
        lfo_[0] = &shared[0];
        lfo_[1] = &shared[1];
        lfo_[2] = &shared[2];
        lfo_local_ = false;
    }

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
        // The LFOs advance per sample (in the tone for shared instances, or
        // at the top of next() for the standalone fallback); the block only
        // reads the current gated values.
        const float l[3] = {lfo_[0]->value(), lfo_[1]->value(),
                            lfo_[2]->value()};
        const float pitch_env = penv_.next_n(kModPeriod);

        // Wheel bend plus AT bend, clamped once per voice the way the chip
        // does it (EPROM 0x5D1C, +/-0xC00 = +/-12 st) BEFORE the per-partial
        // bend-mode scale reads it.
        float ctl = bend_semis_;
        if (spec_.at_bend_semis != 0.0f && at_ != 0.0f) {
            ctl += spec_.at_bend_semis * at_ * (127.0f / 128.0f);
        }
        ctl_bend_st_ = ctl > 12.0f ? 12.0f : (ctl < -12.0f ? -12.0f : ctl);

        for (int i = 0; i < 2; ++i) {
            // Advance the glide by one block, then fold its offset into the
            // pitch factor: T/64 semitones per 112-Hz tick, scaled to this
            // block. The walk is linear in pitch space, so its octave rate
            // is constant -- the D-50's portamento is tempo-based, not the
            // per-distance kind the envelopes are.
            float glide = 1.0f;
            if (glide_pos_[i] != glide_tgt_[i]) {
                float off = glide_pos_[i] - glide_tgt_[i];
                const float s = glide_step_[i];
                off = off > 0.0f ? (off > s ? off - s : 0.0f)
                                 : (off < -s ? off + s : 0.0f);
                glide_pos_[i] = glide_tgt_[i] + off;
                if (off != 0.0f) glide = fast_exp2(off * (1.0f / 12.0f));
            }
            const LfoRoute& pr = spec_.pitch_lfo[i];
            // P-Mod rides LFO-1, and its two depth sources part ways at the
            // delay: the standing depth waits out the silence and swells
            // with the fade, the lever speaks at once -- the ROM multiplies
            // only the c[22] term by the fade ramp (IC25 0x177B-0x17A3),
            // the controller terms bypass it.
            const float depth = pr.depth * lfo_[0]->gate()
                + spec_.lever_gate[i] * spec_.lever_amount * wheel_
                + spec_.at_gate[i] * spec_.at_amount * at_;
            const float cents = 600.0f * depth * lfo_[0]->raw();
            float factor = cents != 0.0f
                               ? fast_exp2(cents * (1.0f / 1200.0f)) : 1.0f;
            if (spec_.penv_mode[i] == PEnvMode::kPositive) {
                factor *= pitch_env;
            } else if (spec_.penv_mode[i] == PEnvMode::kNegative) {
                factor /= pitch_env;
            }
            // The chip merges wheel bend and AT bend into ONE pitch word per
            // voice and clamps the sum to +/-12 semitones (EPROM 0x5D1C,
            // +/-0xC00 of 1/256-st units); only then does the per-partial
            // bend mode scale it (see VoiceSpec::bend_scale). The clamp
            // order matters: a KF partial reads the same clamped source its
            // OFF neighbour reads.
            const float partial_st = ctl_bend_st_ * spec_.bend_scale[i];
            const float ctl_bend =
                partial_st != 0.0f ? fast_exp2(partial_st * (1.0f / 12.0f))
                                   : 1.0f;
            const float tgt_pitch = factor * ctl_bend * glide;
            mod_[i].pw = 0.5f * spec_.pw_lfo[i].depth * lfo_value(l, spec_.pw_lfo[i])
                       + 0.65f * spec_.pw_at[i] * at_;
            mod_[i].cutoff = 0.5f * spec_.tvf_lfo[i].depth * lfo_value(l, spec_.tvf_lfo[i])
                           + 0.65f * spec_.tvf_at[i] * at_;
            // amplitude modulation only ever ducks, never boosts past unity
            const float am = spec_.tva_lfo[i].depth * lfo_value(l, spec_.tva_lfo[i]);
            float tgt_amp = 1.0f + 0.5f * (am - std::fabs(spec_.tva_lfo[i].depth));
            // TVA aftertouch (ROM 0x11A9): a positive range rests ~3 dB down
            // and rises to unity at full press; a negative one ducks ~3 dB
            // (the ROM's scale is 2|s| chip units of 0.376 dB, i.e. ~5.3 dB
            // at s=7 -- the 3-dB reading is PLAUSIBLE, hearing test pending).
            const float g = spec_.tva_at[i];
            if (g != 0.0f) {
                const float x = (g > 0.0f ? g * (at_ - 1.0f) : g * at_) * 0.5f;
                tgt_amp *= fast_exp2(x);
            }
            if (tgt_amp < 0.0f) tgt_amp = 0.0f;
            dpitch_[i] = (tgt_pitch - mod_[i].pitch) * (1.0f / kModPeriod);
            damp_[i] = (tgt_amp - mod_[i].amp) * (1.0f / kModPeriod);
            const PartialType t = (i == 0) ? st.p1 : st.p2;
            if (t == PartialType::kSynth) synth_[i].block_mod(mod_[i]);
        }
    }

    float D5_HOT_TAG(d5_voice_next, next)() {
        const Structure& st = structure();
        // Standalone voices advance their private LFOs themselves; a bound
        // voice's LFOs are stepped by the tone above it.
        if (lfo_local_) {
            local_lfo_[0].next();
            local_lfo_[1].next();
            local_lfo_[2].next();
        }
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
        // The asymmetric pulses carry a DC as large as the duty imbalance.
        // Inside the chip that DC is real -- the pair's ring mod multiplies
        // the raw partials -- but the D-50's line out is AC-coupled and
        // never passes it. So the direct partials are stripped here at the
        // mix point, and from the ring product only the pure dc*dc term
        // comes out: the leakage terms (dc_a * b) are a level-modulated
        // copy of the other partial, which the coupling keeps.
        const float a_raw = a;                    // ring reads pre-mute values:
        const float b_raw = b;                    // the mute buttons gate the
        float dca = (st.p1 == PartialType::kPcm) ? 0.0f : synth_[0].dc();
        float dcb = (st.p2 == PartialType::kPcm) ? 0.0f : synth_[1].dc();
        const float dca_raw = dca, dcb_raw = dcb; // direct terms only
        if (!(spec_.partials_on & 0x1)) { a = 0.0f; dca = 0.0f; }
        if (!(spec_.partials_on & 0x2)) { b = 0.0f; dcb = 0.0f; }

        // The chip multiplies in the log domain, which is an ordinary product
        // once decoded: sum and difference frequencies, and silence whenever
        // either side is silent. The product uses the raw partials -- the
        // bank forces this reading: Glockenspiel (bank 4) mutes partial 1 of
        // a ring structure, a dead preset if the mute reached the product,
        // whereas gating only the direct path is exactly the classic trick
        // of hiding the carrier and keeping the metallic product.
        const float second = st.ring ? a_raw * b_raw - dca_raw * dcb_raw
                                     : b - dcb;

        // The firmware's balance curve (EPROM bank code 0xB450): the
        // quieter side falls linearly to zero, the louder side RISES from
        // the 80/80 center to 100 at full tilt -- a +2 dB emphasis the
        // linear crossfade lacked. Normalized to 1.0 at center.
        // Direction PROVEN: balance 0 puts the full factor on the slot
        // that c46's bit 0 mutes, and that bit is partial 1 in the MIDI
        // chart -- low values favor partial 1.
        const float bal = spec_.balance < 0.0f ? 0.0f
                        : (spec_.balance > 1.0f ? 1.0f : spec_.balance);
        const float mn = bal < 0.5f ? bal : 1.0f - bal;
        const float fmin = mn * 2.0f;
        const float fmax = 1.0f + (0.5f - mn) * 0.5f;
        const float w1 = bal < 0.5f ? fmax : fmin;
        const float w2 = bal < 0.5f ? fmin : fmax;
        return (a - dca) * w1 + second * w2;
    }

    // Pitch bend reaches notes that are already sounding, so it cannot go
    // through the spec the way coarse and fine tune do. Portamento is the
    // same kind of live control (CC65/CC5), and the firmware's slew gate
    // answers it within a tick: the switch going off snaps the sounding
    // pitch to its target mid-glide.
    void set_bend_semis(float st) { bend_semis_ = st; }
    void set_wheel(float w) { wheel_ = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w); }
    // Channel pressure, 0..1. Like the wheel it is a live control on the
    // sounding voices, and like the wheel it survives a patch change
    // (the firmware's FE03/FE0B mirrors do).
    void set_aftertouch(float a) { at_ = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }
    void set_porta(bool sw, int time) {
        spec_.porta_switch = sw;
        spec_.porta_time = time < 0 ? 0 : (time > 100 ? 100 : time);
        for (int i = 0; i < 2; ++i) {
            glide_step_[i] = porta_step(spec_.porta_time, sr_);
            if (!sw || spec_.porta_time == 0) glide_pos_[i] = glide_tgt_[i];
        }
    }

    // Diagnostic handle for the host-side glide test: how far the sounding
    // pitch still sits from the target, in semitones.
    float glide_offset_semitones(int i) const {
        return glide_pos_[(i == 0) ? 0 : 1] - glide_tgt_[(i == 0) ? 0 : 1];
    }

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

    // Semitones per control block at the panel time: the ROM's 4 * T[time]
    // units of 1/256 semitone per 112-Hz tick, pre-multiplied for the
    // block. T[0] is never reached (time 0 snaps at note_on), so indexing
    // is safe for any byte.
    static float porta_step(int time, float sr) {
        const int t = time < 0 ? 0 : (time > 100 ? 100 : time);
        return kPortaRate[t] * (4.0f / 256.0f) * (kTickHz * kModPeriod / sr);
    }

    VoiceSpec spec_{};
    PcmVoice pcm_[2]{};
    SynthPartial synth_[2]{};
    // The LFOs the voice listens to: the tone's shared three when bound,
    // its own copies otherwise (unit tests on Voice alone; see note_on).
    const Lfo* lfo_[3] = {nullptr, nullptr, nullptr};
    Lfo local_lfo_[3]{};
    bool lfo_local_ = false;
    PitchEnv penv_{};
    float bend_semis_ = 0.0f;   // wheel position x patch range, in semitones
    float ctl_bend_st_ = 0.0f;  // wheel + AT bend after the +/-12 st clamp
    float wheel_ = 0.0f;
    float at_ = 0.0f;
    Modulation mod_[2]{};
    // The glide state survives note_on: the firmware's pitch word CB20 is
    // per slot and keeps walking toward its target after release, so the
    // next note always starts the glide where the last one stopped moving.
    float glide_pos_[2] = {0.0f, 0.0f};
    float glide_tgt_[2] = {0.0f, 0.0f};
    float glide_step_[2] = {0.0f, 0.0f};
    bool glide_warm_[2] = {false, false};
    float sr_ = 32000.0f;
    float dpitch_[2] = {0.0f, 0.0f};
    float damp_[2] = {0.0f, 0.0f};
    int mod_count_ = 0;
};

}  // namespace d5
