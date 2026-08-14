// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The two levels above a voice, as the D-50 arranges them:
//
//   Patch ─┬─ Tone (Upper) ─ voices ─ equalizer ─ chorus ─┐
//          └─ Tone (Lower) ─ voices ─ equalizer ─ chorus ─┴─ reverb ─ out
//
// A tone is what the panel edits: two partials, a structure, LFOs, and its
// own equalizer and chorus. A patch pairs two of them and adds the reverb.
// Voice allocation lives here too, because the D-50's polyphony is counted in
// partial pairs: sixteen for a single tone, eight each when both play.
#pragma once

#include <cstdint>

#include "d5_engine/d5_effects.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_voice.h"

namespace d5 {

struct ToneSpec {
    VoiceSpec voice{};
    EqSpec eq{};
    ChorusSpec chorus{};
    float level = 1.0f;
};

// One tone: its voices, then its two insert effects.
template <int kVoices = 8>
class Tone {
public:
    // How many of this tone's slots the key mode grants it. Voices past
    // the limit keep sounding until their release ends -- the render walks
    // every slot regardless, so a mode change never cuts a held note.
    void set_voice_limit(int n) {
        limit_ = n < 1 ? 1 : (n > kVoices ? kVoices : n);
        refill_free_list();
    }

    void configure(const ToneSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        eq_.configure(spec.eq, sample_rate);
        chorus_.configure(spec.chorus, sample_rate);
        // The tone's three LFOs are single shared instances -- the D-50's
        // 112-Hz tick walks one phase word per LFO per tone (IC25
        // 0x1508-0x160D), so a chord vibrates coherently and a legato note
        // joins the running wobble. They free-run from here on.
        for (int i = 0; i < 3; ++i) {
            lfo_[i].start(spec.voice.lfo[i], sample_rate, 0x9E3779B9u * (i + 1));
        }
    }

    // Returns false when the pool had nothing to give and the note was
    // dropped -- the caller uses that to leave everything else alone too.
    bool note_on(int note, float velocity) {
        // The slot comes off a free list, first in first out -- the D-50
        // keeps one per tone at 0xC330/0xC338, entries holding slot+1 with
        // zero for empty, and a note-on pops its head (0x2DA1 called from
        // 0x2B6F). A key-up appends its slot back immediately (0x2B8C ->
        // 0x63B9 -> 0x2DC5), before the release has finished, so the next
        // note lands on the slot that was released longest ago and cuts
        // whatever tail it still had. And when the head is zero the
        // firmware simply raises its no-voice flag and starts nothing:
        // the machine drops the new note rather than stealing a held one.
        //
        // This runs BEFORE the LFO work: the note-transition handler
        // builds its masks from the slots that actually took a note, so a
        // dropped note is invisible to it -- no phase reset, no delay
        // restart, nothing.
        const bool from_silence = !sounding();
        if (q_count_ == 0) {
            // Cannot happen while note-offs arrive in pairs -- the pool
            // and the governor's limit are the same number. If a note-off
            // was ever lost and no finger is down, take the pool back.
            bool any_key = false;
            for (int i = 0; i < kVoices; ++i) any_key = any_key || key_[i];
            if (any_key) return false;
            refill_free_list();
        }
        const int slot = free_q_[q_head_];
        q_head_ = (q_head_ + 1) % kVoices;
        --q_count_;
        freed_[slot] = false;
        shedding_[slot] = false;
        // Sync roles per the note-transition handler (EPROM 0x28FC-0x2991):
        // a tone going from silence to sounding restarts the phase and
        // delay of every LFO whose sync byte is nonzero (the 0x1655 loop
        // gates on [UP+3] != 0); mid-phrase, only LFO-1 may restart, and
        // only when its own byte is exactly 2 -- the KEY mode the panel
        // offers on LFO-1 alone (the gates at 0x2929/0x294E/0x2981 read
        // only C49C/C55C; a stray 2 on LFO-2/3 simply behaves as ON).
        if (from_silence) {
            for (int i = 0; i < 3; ++i) {
                if (spec_.voice.lfo[i].sync != 0) lfo_[i].retrigger();
            }
        } else if (spec_.voice.lfo[0].sync == 2) {
            lfo_[0].retrigger();
        }
        voices_[slot].bind_lfos(lfo_);
        voices_[slot].note_on(spec_.voice, note, velocity, sr_);
        note_[slot] = note;
        active_[slot] = true;
        key_[slot] = true;
        age_[slot] = 0;
        for (int i = 0; i < kVoices; ++i) {
            if (i != slot && active_[i]) ++age_[i];
        }
        return true;
    }

    void note_off(int note) {
        for (int i = 0; i < kVoices; ++i) {
            // Keyed by the KEY, not by whether the voice still sounds:
            // the firmware's key array outlives the envelope, so a
            // percussive voice that fell silent under a held finger still
            // owns its slot until the finger lifts -- and still gives it
            // back when it does.
            if (!key_[i] || note_[i] != note) continue;
            key_[i] = false;
            if (active_[i]) voices_[i].note_off();
            // The slot rejoins the pool at key-up, not when its release
            // ends -- that is what lets a fast passage keep taking voices
            // whose tails are still audible.
            if (!freed_[i]) {
                freed_[i] = true;
                free_q_[(q_head_ + q_count_) % kVoices] = static_cast<uint8_t>(i);
                ++q_count_;
            }
        }
    }

    void set_wheel(float w) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_wheel(w);
    }

    // Mono fold takes the left side, the L/MONO jack: the chorus wet is
    // anti-phase on the right, so a plain average would silence it.
    float D5_HOT(next)() {
        float l, r;
        next_stereo(l, r);
        return l;
    }

    // The tone's stereo image comes entirely from its chorus: the voice
    // sum and the EQ are a mono chain, and the chorus's two counter-swept
    // wet reads open the field (the chip's effect stage does the same job).
    void D5_HOT(next_stereo)(float& l, float& r) {
        // The shared LFOs walk every sample, silent or not -- the tick
        // engine's loop at 0x1508 runs unconditionally, which is why a
        // sync-off LFO never waits for a key.
        lfo_[0].next();
        lfo_[1].next();
        lfo_[2].next();
        float sum = 0.0f;
        for (int i = 0; i < kVoices; ++i) {
            if (!active_[i]) continue;
            sum += voices_[i].next();
            if (!voices_[i].active()) active_[i] = false;
        }
        chorus_.process(eq_.process(sum), l, r);
        l *= spec_.level;
        r *= spec_.level;
    }

    bool sounding() const {
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i]) return true;
        }
        return false;
    }

    // Diagnostic: how many slots are actually rendering right now.
    int voices_sounding() const {
        int n = 0;
        for (int i = 0; i < kVoices; ++i) n += active_[i] ? 1 : 0;
        return n;
    }

    // CPU governor: retire the tail that has been ringing longest, i.e.
    // the oldest voice whose key is already up. Returns false when every
    // sounding voice is still under a finger -- those are never touched,
    // because cutting a note the player is holding is the one thing worse
    // than an underrun.
    bool shed_oldest_tail() {
        int victim = -1;
        for (int i = 0; i < kVoices; ++i) {
            // Skipping the ones already fading is the whole point: a voice
            // stays the oldest tail for the length of its fade, so without
            // this flag the governor re-picks it every block, restarts the
            // fade from its lower level each time, and nothing ever
            // actually stops. That was 500 sheds and no relief.
            if (!active_[i] || key_[i] || shedding_[i]) continue;
            if (victim < 0 || age_[i] > age_[victim]) victim = i;
        }
        if (victim < 0) return false;
        voices_[victim].quick_release();
        shedding_[victim] = true;
        return true;
    }

    // Diagnostic handles for the host-side LFO sync test: the shared LFO's
    // phase and its delay/fade gate, like Voice::glide_offset_semitones().
    float lfo_phase(int i) const { return lfo_[(i < 0 || i > 2) ? 0 : i].phase(); }
    float lfo_gate(int i) const { return lfo_[(i < 0 || i > 2) ? 0 : i].gate(); }

    // Live edits that must not interrupt sounding notes.
    void set_level(float v) { spec_.level = v; }
    void set_chorus_balance(float b) { chorus_.set_balance(b); }
    void set_master_cents(float c) { spec_.voice.master_cents = c; }
    void set_bend_semis(float st) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_bend_semis(st);
    }
    void set_aftertouch(float a) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_aftertouch(a);
    }

    // CC65/CC5 override the patch's portamento while it plays; the mode
    // stays the patch's, so this can only quiet a tone the patch excluded,
    // never add one.
    void set_porta(bool sw, int time) {
        spec_.voice.porta_switch = sw;
        spec_.voice.porta_time = time;
        for (int i = 0; i < kVoices; ++i) voices_[i].set_porta(sw, time);
    }

private:
    ToneSpec spec_{};
    float sr_ = 32000.0f;
    Lfo lfo_[3]{};                  // the tone's shared three (see configure)
    // The free list: slot numbers waiting to be handed out, oldest
    // release first. Slots outside the current limit simply never enter.
    void refill_free_list() {
        q_head_ = 0;
        q_count_ = 0;
        for (int i = 0; i < kVoices; ++i) { freed_[i] = false; shedding_[i] = false; }
        for (int i = 0; i < limit_ && i < kVoices; ++i) {
            if (key_[i]) continue;             // a finger still owns it
            free_q_[q_count_++] = static_cast<uint8_t>(i);
            freed_[i] = true;
        }
    }

    Voice voices_[kVoices]{};
    int limit_ = kVoices;           // slots the key mode grants this tone
    uint8_t free_q_[kVoices] = {};
    int q_head_ = 0;
    int q_count_ = 0;
    bool freed_[kVoices] = {};      // slot is sitting in the free list
    bool key_[kVoices] = {};        // its key is still down
    bool shedding_[kVoices] = {};   // governor is already fading it out
    Equalizer eq_{};
    Chorus<> chorus_{};
    int note_[kVoices] = {};
    int age_[kVoices] = {};
    bool active_[kVoices] = {};
};

enum class KeyMode : uint8_t { kWhole = 0, kDual = 1, kSplit = 2 };

struct PatchSpec {
    ToneSpec upper{};
    ToneSpec lower{};
    KeyMode key_mode = KeyMode::kWhole;
    // The "-S" key modes (WHOL-S, DUAL-S, SEP-S) play monophonically: one
    // note at a time, the new note ends the old one's hold at once.
    bool solo = false;
    int split_point = 60;         // panel "Split Point", C4 by default
    float balance = 0.5f;         // panel "Tone Balance", upper to lower
    // Panel "Bender Range", pb[26], 0..12 semitones. Patch-common: the
    // firmware copies it to both tone slots at load time (EPROM 0x5D60,
    // C59A -> FE04/FE0C) and lets an RPN-0 data entry overwrite it until
    // the next load (0x4E72, clamped to 12).
    int bend_range = 2;
    ReverbSpec reverb{};
    float volume = 1.0f;
};

class Patch {
public:
    void configure(const PatchSpec& spec, float sample_rate) {
        spec_ = spec;
        upper_.configure(spec.upper, sample_rate);
        lower_.configure(spec.lower, sample_rate);
        reverb_.configure(spec.reverb, sample_rate);
        // The sixteen-slot pool, cut the way the key mode cuts it: whole
        // gives all of them to the upper tone (the D-50's sixteen-note
        // polyphony), every other mode gives eight to each (its eight).
        upper_.set_voice_limit(spec.key_mode == KeyMode::kWhole ? 16 : 8);
        lower_.set_voice_limit(8);
    }

    void note_on(int note, float velocity) {
        // Solo modes: the D-50's -S family shares one voice; a new note
        // supersedes the held one. We release the previous note into its
        // release segment rather than cutting it -- close enough to the
        // steal that no factory patch tells them apart, and it cannot
        // click.
        if (spec_.solo && solo_note_ >= 0 && solo_note_ != note) {
            upper_.note_off(solo_note_);
            lower_.note_off(solo_note_);
        }
        bool sounded = false;
        switch (spec_.key_mode) {
            case KeyMode::kDual: {
                const bool u = upper_.note_on(note, velocity);
                const bool l = lower_.note_on(note, velocity);
                sounded = u || l;
                break;
            }
            case KeyMode::kSplit:
                sounded = (note >= spec_.split_point)
                              ? upper_.note_on(note, velocity)
                              : lower_.note_on(note, velocity);
                break;
            case KeyMode::kWhole:
            default:
                sounded = upper_.note_on(note, velocity);
                break;
        }
        // The gate and reverse reverbs time their wet envelope against the
        // note; a note the pool refused never reached the chip, so it must
        // not re-arm them either.
        if (sounded) reverb_.note_activity();
        if (spec_.solo) solo_note_ = note;
    }

    void note_off(int note) {
        upper_.note_off(note);
        lower_.note_off(note);
        if (note == solo_note_) solo_note_ = -1;
    }

    // Mono fold is the L/MONO jack again: the left side as it ships.
    float D5_HOT(next)() {
        float l, r;
        next_stereo(l, r);
        return l;
    }

    // Stereo: the tones keep their own left and right through the balance
    // weights into the reverb, whose two coprime networks take one side
    // each -- the chorus width of a tone survives into the room. The laws
    // themselves are unchanged from the mono path.
    void D5_HOT(next_stereo)(float& l, float& r) {
        // Tone balance per the firmware's mixer (bank code 0xB397): each
        // tone's factor is min(4*b, 255)/200 of its side, so the center
        // is 1.0 each and a full tilt reaches +2.1 dB on the loud side --
        // in whole mode both stay at 1.0 (the ROM forces factor 200).
        // Direction PROVEN: the copy routine at 0x686E feeds pb33's
        // factor into the gain words written to the upper tone's DSP
        // slots -- above 50 the upper tone wins.
        float uw = 1.0f, lw = 1.0f;
        if (spec_.key_mode != KeyMode::kWhole) {
            const float b = spec_.balance < 0 ? 0 : (spec_.balance > 1 ? 1 : spec_.balance);
            uw = 2.0f * b; if (uw > 1.275f) uw = 1.275f;
            lw = 2.0f * (1.0f - b); if (lw > 1.275f) lw = 1.275f;
        }
        float ul, ur, ll, lr;
        upper_.next_stereo(ul, ur);
        lower_.next_stereo(ll, lr);
        reverb_.process(ul * uw + ll * lw, ur * uw + lr * lw, l, r);
        l = saturate(l * spec_.volume);
        r = saturate(r * spec_.volume);
    }

    // Sixteen voices plus a reverb tail can ask for more than full scale, and
    // a converter answers that with hard clipping. This stays linear below
    // -3 dB and bends smoothly above, so loud chords lose their peaks instead
    // of tearing.
    static float saturate(float x) {
        constexpr float kKnee = 0.7f;
        const float a = x < 0.0f ? -x : x;
        if (a <= kKnee) return x;
        const float over = (a - kKnee) / (1.0f - kKnee);
        const float shaped = kKnee + (1.0f - kKnee) * (over / (1.0f + over));
        return x < 0.0f ? -shaped : shaped;
    }

    bool sounding() const { return upper_.sounding() || lower_.sounding(); }
    // Diagnostic: how many slots are actually rendering right now.
    int voices_sounding() const {
        return upper_.voices_sounding() + lower_.voices_sounding();
    }

    // CPU governor: retire one ringing tail, taking it from whichever tone
    // is carrying more of them.
    bool shed_voice() {
        if (upper_.voices_sounding() >= lower_.voices_sounding()) {
            return upper_.shed_oldest_tail() || lower_.shed_oldest_tail();
        }
        return lower_.shed_oldest_tail() || upper_.shed_oldest_tail();
    }

    // Panel controls that apply while the patch is playing. Anything that
    // would resize a delay line or restart a voice belongs in configure().
    void set_volume(float v) { spec_.volume = v; }
    void set_reverb_balance(float b) { reverb_.set_balance(b); }
    void set_chorus_balance(float b) {
        upper_.set_chorus_balance(b);
        lower_.set_chorus_balance(b);
    }
    void set_master_cents(float c) {
        upper_.set_master_cents(c);
        lower_.set_master_cents(c);
    }
    void set_bend_semis(float st) {
        upper_.set_bend_semis(st);
        lower_.set_bend_semis(st);
    }
    void set_mod_wheel(float w) {
        upper_.set_wheel(w);
        lower_.set_wheel(w);
    }
    void set_aftertouch(float a) {
        upper_.set_aftertouch(a);
        lower_.set_aftertouch(a);
    }
    void set_porta(bool sw, int time) {
        upper_.set_porta(sw, time);
        lower_.set_porta(sw, time);
    }

private:
    PatchSpec spec_{};
    // Sixteen slots on the upper tone, eight on the lower: the pool is the
    // D-50's own, and only the upper tone can ever be handed all of it.
    Tone<16> upper_{};
    Tone<8> lower_{};
    Reverb reverb_{};
    int solo_note_ = -1;          // the solo modes' single held note
};

}  // namespace d5
