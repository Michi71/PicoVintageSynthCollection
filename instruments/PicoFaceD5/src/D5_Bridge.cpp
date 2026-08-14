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
    allNotesOff();
    for (int b = 0; b < 64; ++b) fillBufferI32(buf, 64);
    cpuPeak_ = 0;
    outPeak_ = 0;
    const int64_t budget = 64 * 1000000LL / (int64_t)sampleRate();
    return (int)(worst * 100 / (budget > 0 ? budget : 1));
}

void D5_Bridge::applyPatch() {
#ifdef D5_HAVE_BANK
    const int i = patchIndex_ % d5::kPatchCount;
    d5::PatchSpec spec = d5::patch_from_bytes(d5::kPatchData[i], d5_pcm_blob);
    g_patch_name = d5::kPatchNames[i];
#else
    d5::Preset pr = d5::preset(patchIndex_ % d5::kPresetCount);
    d5::preset_bind(pr.spec, d5_pcm_blob, pr.pcm1, pr.pcm2);
    d5::PatchSpec spec = pr.spec;
    g_patch_name = pr.name;
#endif

    // Remember what the patch itself asks for: the UI's global controls scale
    // these, so a dry patch stays drier than a wet one at the same setting.
    baseReverb_ = spec.reverb.balance;
    baseChorus_ = spec.upper.chorus.balance;
    baseVolume_ = spec.volume;
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
    patch_.set_reverb_balance(baseReverb_ * reverb_ * 0.01f);
    patch_.set_chorus_balance(baseChorus_ * chorus_ * 0.01f);
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

void D5_Bridge::noteOff(uint8_t note) {
    if (note > 127) return;
    patch_.note_off(note);
    if (held_[note]) { held_[note] = 0; if (activeVoices_ > 0) --activeVoices_; }
}

void D5_Bridge::allNotesOff() {
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
    if (load > cpuPeak_) cpuPeak_ = load;
    else if (cpuPeak_ > 0) --cpuPeak_;
}
