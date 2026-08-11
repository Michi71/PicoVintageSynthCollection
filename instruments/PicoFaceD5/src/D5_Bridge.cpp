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
    applyPatch();
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
    if (index < 0) index = 0;
    if (index >= d5::kPresetCount) index = d5::kPresetCount - 1;
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

void D5_Bridge::setPitchBendCents(float cents) {
    patch_.set_bend(cents != 0.0f ? std::pow(2.0f, cents / 1200.0f) : 1.0f);
}

void D5_Bridge::noteOn(uint8_t note, uint8_t velocity) {
    if (note > 127) return;
    if (activeVoices_ >= voiceLimit_) {
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

void D5_Bridge::fillBufferI32(int32_t* out, int frames) {
    const absolute_time_t t0 = get_absolute_time();

    for (int i = 0; i < frames; ++i) {
        float v = patch_.next();
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        const int32_t s = static_cast<int32_t>(v * 32767.0f);
        out[2 * i] = s << 16;          // the pool wants the sample in the
        out[2 * i + 1] = s << 16;      // upper half, left then right
    }

    // Peak load as a percentage of the block's own budget, decayed slowly so
    // the footer shows the worst recent case rather than the last block.
    const int64_t us = absolute_time_diff_us(t0, get_absolute_time());
    const int64_t budget = (int64_t)frames * 1000000 / (int64_t)sampleRate();
    const int load = budget > 0 ? (int)(us * 100 / budget) : 0;
    if (load > cpuPeak_) cpuPeak_ = load;
    else if (cpuPeak_ > 0) --cpuPeak_;
}
