// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// PCM partial of the LA engine: plays one of the 100 ROM samples at a pitch,
// through the TVA envelope. This is the half of LA synthesis that comes out
// of the sample ROMs; the synth partials (sawtooth/square with TVF, and the
// ring modulation that pairs them) are separate.
//
// There is no root pitch per sample anywhere in the machine. The firmware
// sends a PCM partial the pitch word a square would get, four octaves lower
// (IC25 0x0F49), and the chip turns that into a playback step by one rule
// for every sample -- see kPcmPitchRefHz below. Roland tuned the material to
// that rule, not the other way round: the sustained loops are 2^k-word
// cycles so they land on C octaves, and the attacks sit wherever the patch
// programmer's coarse puts them. Noise follows the key like everything else.
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_env.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_mod.h"

namespace d5 {

// The TVA envelope is the shared five-segment shape; the synth partial uses
// the same one for its TVF.
using TvaEnvSpec = Env5Spec;
using TvaEnv = Env5;

struct PcmSampleRef {
    const int16_t* data = nullptr;   // whole PCM space
    uint32_t start = 0;
    uint32_t length = 0;
    bool looped = false;
};

// The D-50 transposes every PCM sample by one rule, whatever the material.
// Firmware IC25 0x0F44-0x0F4E (and again at 0x0F9A, the negative-keyfollow
// branch of the same composer) shifts the partial's type byte and, when the
// carry -- bit 7, the PCM flag -- is set, subtracts 0x4000 from the pitch
// word: exactly four octaves in the chip's 4096-per-octave scale. The chip
// itself (munt LA32WaveGenerator: synth step 2^(p/4096+4) over a 2^20
// period, PCM step 2^(p/4096+3) in 1/256 word) advances a PCM by
// f*2048/32000 words per output sample for the pitch word that makes a
// square wave sound at f. Together: words per sample = f / (32000/128) =
// f / 250 Hz. At coarse 36 on C4 that is 1.046, the stored rate -- which is
// also why every sustained loop is a 2^k-word cycle: 32000/2^k Hz times
// 261.6/250 lands them on the C octaves by construction. Nothing in the
// firmware carries a root pitch per sample; the PCM number only selects the
// start page and the length class (IC25 0x04B0-0x04CF). So the natural
// pitch of the material must not enter the playback rate. It did, until
// issue #142: FluteH and Breath (~1 kHz material) were transposed onto the
// square's pitch and came out two octaves low, the Spect loops four octaves
// high, the bass and piano loops one to two octaves high.
inline constexpr float kPcmPitchRefHz = 32000.0f / 128.0f;

class PcmVoice {
public:
    void note_on(const PcmSampleRef& s, float note, float velocity,
                 const TvaEnvSpec& env, float sample_rate,
                 float detune = 1.0f) {
        s_ = s;
        index_ = 0;
        frac_ = 0.0f;
        active_ = s.data != nullptr && s.length > 0;
        gain_ = velocity;
        const float f = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        rate_ = f / kPcmPitchRefHz;
        rate_ *= sample_rate > 0.0f ? (32000.0f / sample_rate) : 1.0f;
        rate_ *= detune;
        sr_ = sample_rate;
        env_.start(env, sample_rate);
    }

    void note_off() { env_.release(); }
    void quick_release() { env_.quick_release(sr_); }
    bool active() const { return active_; }

    float D5_HOT_TAG(d5_pcm_next, next)(const Modulation& mod = Modulation{}) {
        if (!active_) return 0.0f;

        const uint32_t i = index_;
        // The partner for interpolation is the next word, and at the end of a
        // loop that word is the first one again -- not the one past the
        // sample. Wrapping the position instead of the partner drops the last
        // word of every revolution and plays the first one twice, which on a
        // 128-word cycle is 250 corrupted words a second. It cost up to 2.4%
        // of the signal on VIOLlp, and exactly nothing on Noise, whose last
        // word happens to equal its first.
        const uint32_t j = (i + 1 < s_.length) ? i + 1 : (s_.looped ? 0u : i);
        const float a = s_.data[s_.start + i] * (1.0f / 32768.0f);
        const float b = s_.data[s_.start + j] * (1.0f / 32768.0f);
        const float sample = a + (b - a) * frac_;

        advance(rate_ * mod.pitch);
        const float amp = env_.next();
        if (env_.finished()) active_ = false;
        return sample * amp * gain_ * mod.amp;
    }

private:
    // Position is an exact word index plus a float fraction, not a double.
    // The Cortex-M33 has no double unit, so every step of the old version --
    // the add, the floor, the compare, the truncation -- was a soft-float
    // call, and there were sixteen voices asking for them 32000 times a
    // second. This is also *more* precise: the integer part cannot lose bits
    // to the mantissa however long the note is held.
    //
    // The whole-word step is f/250 words: past the length of a 128-word
    // cycle only for a square pitch above 32 kHz, which no key and coarse
    // combination in the bank produces. One conditional subtract wraps;
    // the modulo below is the guard for the absurd case.
    void advance(float step) {
        frac_ += step;
        const uint32_t whole = static_cast<uint32_t>(frac_);
        if (whole) {
            frac_ -= static_cast<float>(whole);
            index_ += whole;
            if (index_ >= s_.length) {
                if (!s_.looped) { active_ = false; return; }
                index_ -= s_.length;
                if (index_ >= s_.length) index_ %= s_.length;   // paranoia
            }
        }
    }

    PcmSampleRef s_{};
    TvaEnv env_{};
    uint32_t index_ = 0;
    float frac_ = 0.0f;
    float sr_ = 32000.0f;
    float rate_ = 1.0f;
    float gain_ = 1.0f;
    bool active_ = false;
};

}  // namespace d5
