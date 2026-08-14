// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Bridge.cpp -- the engine seen from the firmware side.
//
// The blob is linked in through d5_blob.S, which .incbin's the converted PCM
// data: 262144 samples of 16-bit audio, decoded from the ROM pair at build
// time so the firmware needs no log table at runtime.

#include "D5_Bridge.h"

#include <cmath>
#include <cstdio>

#include "d5_presets.h"
#include "pico/time.h"

// Host builds have no flash to stay out of.
#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

// A converted SysEx bank if the build found one, the hand-built patches
// otherwise. The instrument plays either without knowing the difference.
#if __has_include("d5_patch_data.h")
#include "d5_patch_data.h"
#include "d5_engine/d5_patch_map.h"
#define D5_HAVE_BANK 1
#endif

extern "C" {
extern const int16_t d5_pcm_blob[];
extern const int16_t d5_pcm_blob_end[];
}

namespace {
const char* kStructureNames[7] = {
    "S+S", "S*S ring", "P+S", "P*S ring", "S*P ring", "P+P", "P*P ring"};
const char* g_patch_name = "";
int g_structure = 1;
}  // namespace

void D5_Bridge::init() {
#ifdef D5_HAVE_BANK
    // The sustained cycles move to RAM before the first patch is built, so
    // every PcmSampleRef the mapping hands out already points there.
    static int16_t loopRam[d5::loop_ram_words()];
    d5::install_loop_ram(d5_pcm_blob, loopRam, d5::loop_ram_words());
#endif
    applyPatch();
}

// Worst block cost, measured at boot with four held notes and nothing else
// running -- no UI, no USB, no settings writes. This is the honest per-block
// price of the engine on this hardware; if the live P far exceeds it, the
// overload is coming from outside the render.
int D5_Bridge::bootBenchPercent() {
    int32_t buf[2 * 64];
    noteOn(48, 100); noteOn(60, 100); noteOn(67, 100); noteOn(72, 100);
    int64_t worst = 0;
    for (int b = 0; b < 96; ++b) {
        const absolute_time_t t0 = get_absolute_time();
        fillBufferI32(buf, 64);
        const int64_t us = absolute_time_diff_us(t0, get_absolute_time());
        if (b >= 2 && us > worst) worst = us;
    }
    // Leave nothing behind. The chord above went to a scratch buffer, but
    // the chorus and reverb lines kept every sample of it, and a hall runs
    // for seconds -- so the instrument used to sing a few notes to itself
    // as soon as the real output started, right after the splash screen.
    // Releasing the notes is not enough; the lines have to be emptied.
    allNotesOff();
    patch_.silence();
    cpuPeak_ = 0;
    outPeak_ = 0;
    shedTotal_ = 0;
    shedWindow_ = 0;
    shedRate_ = 0;
    noteOnTotal_ = 0;
    const int64_t budget = 64 * 1000000LL / (int64_t)sampleRate();
    return (int)(worst * 100 / (budget > 0 ? budget : 1));
}

void D5_Bridge::applyPatch() {
#ifdef D5_HAVE_BANK
    const int i = patchIndex_ % d5::kPatchCount;
    // Into the temporary area first, and built from there: that is where
    // the D-50 plays from, and where SysEx edits land.
    const uint8_t* src = storedPatch(i);
    for (int k = 0; k < kPatchBytes; ++k) temp_[k] = src[k];
    d5::PatchSpec spec = d5::patch_from_bytes(temp_, d5_pcm_blob);
    if (overlayFind(i) >= 0) {
        // A received patch carries its own name, eighteen characters in the
        // patch block on the panel's own alphabet.
        static const char kChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                     "abcdefghijklmnopqrstuvwxyz1234567890-";
        int end = 0;
        for (int k = 0; k < 18; ++k) {
            const int v = temp_[6 * 64 + k] & 0x3F;
            nameBuf_[k] = (v < (int)sizeof(kChars) - 1) ? kChars[v] : ' ';
            if (nameBuf_[k] != ' ') end = k + 1;
        }
        nameBuf_[end] = 0;
        g_patch_name = nameBuf_;
    } else {
        g_patch_name = d5::kPatchNames[i];
    }
#else
    d5::Preset pr = d5::preset(patchIndex_ % d5::kPresetCount);
    d5::preset_bind(pr.spec, d5_pcm_blob, pr.pcm1, pr.pcm2);
    d5::PatchSpec spec = pr.spec;
    g_patch_name = pr.name;
#endif

    // The patch's own level; the panel's master volume scales it.
    baseVolume_ = spec.volume;
    // The patch's OWN reverb and chorus balance, in the D-50's own 0..100,
    // straight from the bytes. The panel knobs set these outright rather
    // than scaling them: scaling could only ever take away, so on a dry
    // patch the knob did nothing at all -- which is exactly what it looked
    // like from the outside. On the real machine these are patch data
    // (Reverb Balance in the patch block, Chorus Balance per tone), so the
    // knobs follow the patch whenever it changes.
#ifdef D5_HAVE_BANK
    const uint8_t* pb = d5::patch_block(temp_, d5::kBlkPatch);
    const uint8_t* uc = d5::patch_block(temp_, d5::kBlkUpperCommon);
    patchReverbBal_ = pb[31] > 100 ? 100 : pb[31];
    patchChorusBal_ = uc[45] > 100 ? 100 : uc[45];
#else
    patchReverbBal_ = 30;
    patchChorusBal_ = 50;
#endif
    reverb_ = patchReverbBal_;
    chorus_ = patchChorusBal_;
#ifdef D5_HAVE_BANK
    choType_ = uc[42] > 7 ? 7 : uc[42];
    choRate_ = uc[43] > 100 ? 100 : uc[43];
    choDepth_ = uc[44] > 100 ? 100 : uc[44];
    eqLoF_ = uc[37] > 15 ? 15 : uc[37];
    eqLoG_ = uc[38] > 24 ? 24 : uc[38];
    eqHiF_ = uc[39] > 21 ? 21 : uc[39];
    eqHiQ_ = uc[40] > 8 ? 8 : uc[40];
    eqHiG_ = uc[41] > 24 ? 24 : uc[41];
    toneBal_ = pb[33] > 100 ? 100 : pb[33];
#endif
    wholeMode_ = spec.key_mode == d5::KeyMode::kWhole;
    // A patch change ends the CC65/CC5 override: the controllers reassert
    // themselves with their next message, as the D-50's own switch does.
    portaSwitch_ = spec.upper.voice.porta_switch;
    portaTime_ = spec.upper.voice.porta_time;
    // A patch change also ends the RPN bender-range override: the patch
    // loader writes pb[26] over FE04/FE0C the same way (EPROM 0x5D60).
    bendRange_ = spec.bend_range;

    patch_.configure(spec, static_cast<float>(sampleRate()));
    g_structure = spec.upper.voice.structure;
    applyLevels();
}

int D5_Bridge::patchCount() const {
#ifdef D5_HAVE_BANK
    return d5::kPatchCount;
#else
    return d5::kPresetCount;
#endif
}

const char* D5_Bridge::patchName() const { return g_patch_name; }

const char* D5_Bridge::structureName() const {
    const int i = (g_structure < 1 || g_structure > 7) ? 0 : g_structure - 1;
    return kStructureNames[i];
}

void D5_Bridge::selectPatch(int index) {
    // Against patchCount(), not against kPresetCount: with a converted bank
    // there are 64 of them, and clamping to the 8 built-in presets made the
    // other 56 unreachable on hardware while the UI cheerfully reported 64.
    const int n = patchCount();
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    if (index == patchIndex_) return;
    allNotesOff();
    patchIndex_ = index;
    applyPatch();
}

// These run while notes are sounding, so none of them may reconfigure the
// patch: that would clear the chorus and reverb buffers and cut the sound off
// mid-chord, which on hardware reads as a fault in the knob.
void D5_Bridge::applyLevels() {
    patch_.set_volume(baseVolume_ * volume_ * 0.01f);
    // Through the same curves the patch mapping uses, so the panel's number
    // means what the D-50's own parameter means: the reverb balance rides
    // the amount family (x^1.8), the chorus balance is linear.
    patch_.set_reverb_balance(d5::kAmountCurve[reverb_ > 100 ? 100 : reverb_]);
    patch_.set_chorus_balance(chorus_ * 0.01f);
    patch_.set_master_cents(static_cast<float>(tune_));
}

void D5_Bridge::setVolume(int percent) {
    volume_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    applyLevels();
}

void D5_Bridge::setMasterTune(int cents) {
    tune_ = cents < -50 ? -50 : (cents > 50 ? 50 : cents);
    applyLevels();
}

void D5_Bridge::setReverb(int percent) {
    reverb_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    applyLevels();
}

void D5_Bridge::setReverbType(int t) {
    patch_.set_reverb_type(t);
}

int D5_Bridge::reverbType() const { return patch_.reverb_type(); }

static int clampTo(int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); }

void D5_Bridge::setChorusType(int v) {
    choType_ = clampTo(v, 7);
    patch_.set_chorus_type(choType_);
}

void D5_Bridge::setChorusRate(int v) {
    choRate_ = clampTo(v, 100);
    patch_.set_chorus_rate(choRate_ * 0.01f);
}

void D5_Bridge::setChorusDepth(int v) {
    choDepth_ = clampTo(v, 100);
    // The depth family the whole machine uses: read linearly, a patch
    // asking for a breath of chorus got half a semitone of it.
    patch_.set_chorus_depth(d5::kDepthCurve[choDepth_]);
}

void D5_Bridge::applyEq() {
    d5::EqSpec e;
    e.low_freq = eqLoF_;
    e.low_gain_db = static_cast<float>(eqLoG_) - 12.0f;
    e.high_freq = eqHiF_;
    e.high_q = eqHiQ_;
    e.high_gain_db = static_cast<float>(eqHiG_) - 12.0f;
    patch_.set_eq(e);
}

void D5_Bridge::setEqLowFreq(int v)  { eqLoF_ = clampTo(v, 15); applyEq(); }
void D5_Bridge::setEqLowGain(int v)  { eqLoG_ = clampTo(v, 24); applyEq(); }
void D5_Bridge::setEqHighFreq(int v) { eqHiF_ = clampTo(v, 21); applyEq(); }
void D5_Bridge::setEqHighQ(int v)    { eqHiQ_ = clampTo(v, 8);  applyEq(); }
void D5_Bridge::setEqHighGain(int v) { eqHiG_ = clampTo(v, 24); applyEq(); }

float D5_Bridge::eqLowHz() const  { return d5::kLowEqFreq[clampTo(eqLoF_, 15)]; }
float D5_Bridge::eqHighHz() const { return d5::kHighEqFreq[clampTo(eqHiF_, 21)]; }

void D5_Bridge::setToneBalance(int v) {
    toneBal_ = clampTo(v, 100);
    patch_.set_tone_balance(toneBal_ * 0.01f);
}

void D5_Bridge::setChorus(int percent) {
    chorus_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    applyLevels();
}

void D5_Bridge::setVoiceLimit(int voices) {
    voiceLimit_ = voices < 1 ? 1 : (voices > d5::kMaxVoicesPerTone
                                        ? d5::kMaxVoicesPerTone : voices);
}

void D5_Bridge::setModWheel(float w) {
    patch_.set_mod_wheel(w);
}

void D5_Bridge::setAftertouch(float a) {
    patch_.set_aftertouch(a);
}

void D5_Bridge::setPitchBendSemis(float semis) {
    patch_.set_bend_semis(semis);
}

void D5_Bridge::setBendRange(int semis) {
    // The D-50's own clamp (EPROM 0x4E9C): the wheel never reaches past
    // 12 semitones, whatever the data entry asks.
    bendRange_ = semis < 0 ? 0 : (semis > 12 ? 12 : semis);
}

void D5_Bridge::setPortamentoSwitch(bool on) {
    portaSwitch_ = on;
    patch_.set_porta(on, portaTime_);
}

void D5_Bridge::setPortamentoTime(int percent) {
    portaTime_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    patch_.set_porta(portaSwitch_, portaTime_);
}

void D5_Bridge::noteOn(uint8_t note, uint8_t velocity) {
    if (note > 127) return;
    ++noteOnTotal_;
    // Re-striking a held note replaces its own voice inside the tone; only a
    // genuinely new note may steal, or full-polyphony retriggers eat a
    // neighbour for nothing.
    if (activeVoices_ >= noteLimit() && !held_[note]) {
        // The tone steals internally, but the governor's limit is ours: past
        // it we drop the oldest held note first so the count stays honest.
        for (int n = 0; n < 128; ++n) {
            if (held_[n]) { patch_.note_off(n); held_[n] = 0; --activeVoices_; break; }
        }
    }
    patch_.note_on(note, velocity * (1.0f / 127.0f));
    if (!held_[note]) ++activeVoices_;
    held_[note] = 1;
}

int D5_Bridge::overlayFind(int index) const {
    const uint16_t want = (uint16_t)(index + 1);
    for (int k = 0; k < kOverlaySlots; ++k) {
        if (overlayFor_[k] == want) return k;
    }
    return -1;
}

const uint8_t* D5_Bridge::storedPatch(int index) const {
#ifdef D5_HAVE_BANK
    if (index < 0 || index >= d5::kPatchCount) return nullptr;
    const int k = overlayFind(index);
    return k >= 0 ? overlay_[k] : d5::kPatchData[index];
#else
    (void)index;
    return nullptr;
#endif
}

// One slot's worth of a received patch. A slot is seeded from flash the
// first time it is touched, so a message that carries only a few bytes
// leaves the rest of the patch intact instead of zeroing it.
void D5_Bridge::applyStored(int index, int off, const uint8_t* data, int len) {
#ifdef D5_HAVE_BANK
    if (index < 0 || index >= d5::kPatchCount) return;
    int k = overlayFind(index);
    if (k < 0) {
        k = -1;
        for (int i = 0; i < kOverlaySlots; ++i) {
            if (overlayFor_[i] == 0) { k = i; break; }
        }
        if (k < 0) {
            k = overlayNext_;
            overlayNext_ = (overlayNext_ + 1) % kOverlaySlots;
        } else if (overlayUsed_ < kOverlaySlots) {
            ++overlayUsed_;
        }
        for (int b = 0; b < kPatchBytes; ++b) overlay_[k][b] = d5::kPatchData[index][b];
        overlayFor_[k] = (uint16_t)(index + 1);
    }
    for (int b = 0; b < len; ++b) overlay_[k][off + b] = data[b] & 0x7F;
    // Playing this one? Then it takes effect now, without a rebuild that
    // would clear the effects.
    if (index == patchIndex_) sysexWriteTemp(off, data, len);
#else
    (void)index; (void)off; (void)data; (void)len;
#endif
}

void D5_Bridge::sysexWriteStored(uint32_t off, const uint8_t* data, int len) {
#ifdef D5_HAVE_BANK
    // Which sixty-four the panel is on -- the same window the requests use.
    const int bank = patchIndex_ / 64;
    while (len > 0) {
        const int slot = (int)(off / kPatchBytes);
        const int inSlot = (int)(off % kPatchBytes);
        if (slot >= 64) return;
        int n = kPatchBytes - inSlot;
        if (n > len) n = len;
        applyStored(bank * 64 + slot, inSlot, data, n);
        off += n; data += n; len -= n;
    }
#else
    (void)off; (void)data; (void)len;
#endif
}

void D5_Bridge::sysexWriteTemp(int offset, const uint8_t* data, int len) {
    if (offset < 0 || offset >= kPatchBytes || len <= 0) return;
    if (offset + len > kPatchBytes) len = kPatchBytes - offset;
    for (int k = 0; k < len; ++k) temp_[offset + k] = data[k] & 0x7F;
#ifdef D5_HAVE_BANK
    d5::PatchSpec spec = d5::patch_from_bytes(temp_, d5_pcm_blob);
    // The panel's own copies of the patch parameters follow the edit, so
    // the display keeps telling the truth about what is sounding.
    const uint8_t* pb = d5::patch_block(temp_, d5::kBlkPatch);
    const uint8_t* uc = d5::patch_block(temp_, d5::kBlkUpperCommon);
    reverb_ = pb[31] > 100 ? 100 : pb[31];
    chorus_ = uc[45] > 100 ? 100 : uc[45];
    choType_ = uc[42] > 7 ? 7 : uc[42];
    choRate_ = uc[43] > 100 ? 100 : uc[43];
    choDepth_ = uc[44] > 100 ? 100 : uc[44];
    eqLoF_ = uc[37] > 15 ? 15 : uc[37];
    eqLoG_ = uc[38] > 24 ? 24 : uc[38];
    eqHiF_ = uc[39] > 21 ? 21 : uc[39];
    eqHiQ_ = uc[40] > 8 ? 8 : uc[40];
    eqHiG_ = uc[41] > 24 ? 24 : uc[41];
    toneBal_ = pb[33] > 100 ? 100 : pb[33];
    baseVolume_ = spec.volume;
    wholeMode_ = spec.key_mode == d5::KeyMode::kWhole;
    bendRange_ = spec.bend_range;
    g_structure = spec.upper.voice.structure;
    patch_.reconfigure(spec);
#else
    (void)data;
#endif
}

void D5_Bridge::setSustain(bool on) {
    if (sustain_ == on) return;
    sustain_ = on;
    if (on) return;
    // Pedal up: every key that was let go while it was down releases now.
    for (int n = 0; n < 128; ++n) {
        if (!sustained_[n]) continue;
        sustained_[n] = 0;
        patch_.note_off(n);
        if (held_[n]) { held_[n] = 0; if (activeVoices_ > 0) --activeVoices_; }
    }
}

void D5_Bridge::noteOff(uint8_t note) {
    if (note > 127) return;
    // Under a held pedal the key-up is remembered, not performed: the slot
    // stays out of the free list exactly as it does on the original, whose
    // key array keeps the note until the pedal lifts.
    if (sustain_ && held_[note]) { sustained_[note] = 1; return; }
    patch_.note_off(note);
    if (held_[note]) { held_[note] = 0; if (activeVoices_ > 0) --activeVoices_; }
}

void D5_Bridge::allNotesOff() {
    sustain_ = false;
    for (int n = 0; n < 128; ++n) sustained_[n] = 0;
    for (int n = 0; n < 128; ++n) {
        if (held_[n]) { patch_.note_off(n); held_[n] = 0; }
    }
    activeVoices_ = 0;
}

// In RAM: the render runs from the audio path every block, and leaving it in
// flash puts it in the same XIP cache as the 512 KiB sample blob it reads.
void __not_in_flash_func(D5_Bridge::fillBufferI32)(int32_t* out, int frames) {
    const absolute_time_t t0 = get_absolute_time();

    float pk = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float l, r;
        patch_.next_stereo(l, r);
        if (l > 1.0f) l = 1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f;
        if (r < -1.0f) r = -1.0f;
        const float ml = l < 0.0f ? -l : l;
        const float mr = r < 0.0f ? -r : r;
        if (ml > pk) pk = ml;
        if (mr > pk) pk = mr;
        // the pool wants the sample in the upper half, left then right
        out[2 * i] = static_cast<int32_t>(l * 32767.0f) << 16;
        out[2 * i + 1] = static_cast<int32_t>(r * 32767.0f) << 16;
    }
    // Peak of what the engine actually produced, before the I2S ever sees
    // it. Silence with this at zero is the engine's fault; silence with it
    // alive means the samples are being lost on the way out. NaN state also
    // reads 0 here -- every comparison with NaN is false -- which is exactly
    // the verdict it should read.
    const int o = (int)(pk * 100.0f + 0.5f);
    if (o > outPeak_) outPeak_ = o;
    else if (outPeak_ > 0) --outPeak_;

    // Peak load as a percentage of the block's own budget, decayed slowly so
    // the footer shows the worst recent case rather than the last block.
    const int64_t us = absolute_time_diff_us(t0, get_absolute_time());
    const int64_t budget = (int64_t)frames * 1000000 / (int64_t)sampleRate();
    const int load = budget > 0 ? (int)(us * 100 / budget) : 0;

    // The CPU governor, ahead of the damage. Sixteen voices of a pad with
    // a three-second tail is more than this silicon renders in real time,
    // and the D-50's own polyphony is worth having wherever it fits -- so
    // instead of capping it, retire the longest-ringing TAIL whenever a
    // block runs hot. Keys under fingers are never touched. Rate-limited
    // to one per eight blocks (~16 ms) so the relief is heard before the
    // next decision, and armed only above 88% where there is still room
    // for the block to finish.
    if (load > 92) {
        // Already near the line: shed every block until it is not.
        shedHoldoff_ = 0;
        if (patch_.shed_voice()) { ++shedTotal_; ++shedWindow_; }
    } else if (load > 86) {
        // The precautionary rung. It sat at 82% while the resonance path
        // still cost a third of the render; with that path down to a
        // quarter there is measured room to let a voice or two more
        // stand, and the 92% rung is the actual safety net.
        if (++shedHoldoff_ >= 6) {
            shedHoldoff_ = 0;
            if (patch_.shed_voice()) { ++shedTotal_; ++shedWindow_; }
        }
    } else if (shedHoldoff_ > 0) {
        --shedHoldoff_;
    }

    // Sheds per second, latched once a second (500 blocks of 64 at 32 kHz).
    if (++blockCount_ >= (uint32_t)sampleRate() / (uint32_t)(frames ? frames : 1)) {
        blockCount_ = 0;
        shedRate_ = shedWindow_;
        shedWindow_ = 0;
    }
    if (load > cpuPeak_) cpuPeak_ = load;
    else if (cpuPeak_ > 0) --cpuPeak_;
}
