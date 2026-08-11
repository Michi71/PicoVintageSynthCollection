// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "D5_Controller.h"

#include <cstdio>

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void D5_Controller::onEncoderSel(int8_t delta) {
    if (!delta) return;
    page_ = (page_ + delta) % kPageCount;
    if (page_ < 0) page_ += kPageCount;
}

void D5_Controller::onEncoderA(int8_t delta) {
    if (!delta) return;
    switch (page_) {
        case kPagePatch:
            bridge_.selectPatch(bridge_.patch() + delta);
            break;
        case kPageMix:
            volume_ = clampi(volume_ + delta, 0, 100);
            bridge_.setVolume(volume_);
            break;
        case kPageTune:
            tune_ = clampi(tune_ + delta, -50, 50);
            bridge_.setMasterTune(tune_);
            break;
        default: break;
    }
}

void D5_Controller::onEncoderB(int8_t delta) {
    if (!delta) return;
    switch (page_) {
        case kPagePatch:
            voices_ = clampi(voices_ + delta, 1, d5::kMaxVoicesPerTone);
            bridge_.setVoiceLimit(voices_);
            break;
        case kPageMix:
            reverb_ = clampi(reverb_ + delta, 0, 100);
            bridge_.setReverb(reverb_);
            break;
        case kPageTune:
            // 0..15 are the channels, 16 means omni; the MIDI front end reads
            // this back through midiChannel().
            midiCh_ = clampi(midiCh_ + delta, 0, 16);
            break;
        default: break;
    }
}

const char* D5_Controller::title() const { return "PicoFaceD5"; }

const char* D5_Controller::pageName() const {
    switch (page_) {
        case kPageMix:  return "Mix";
        case kPageTune: return "Tune";
        case kPagePatch:
        default:        return "Patch";
    }
}

void D5_Controller::lineA(char* out, size_t n) const {
    switch (page_) {
        case kPagePatch:
            snprintf(out, n, "%d %s", bridge_.patch() + 1, bridge_.patchName());
            break;
        case kPageMix:
            snprintf(out, n, "Vol %d  Cho %d", volume_, chorus_);
            break;
        case kPageTune:
            snprintf(out, n, "Tune %+d ct", tune_);
            break;
        default:
            snprintf(out, n, " ");
            break;
    }
}

void D5_Controller::lineB(char* out, size_t n) const {
    switch (page_) {
        case kPagePatch:
            snprintf(out, n, "%s  %d vc", bridge_.structureName(), voices_);
            break;
        case kPageMix:
            snprintf(out, n, "Reverb %d", reverb_);
            break;
        case kPageTune:
            if (midiCh_ >= 16) snprintf(out, n, "MIDI Omni");
            else snprintf(out, n, "MIDI ch %d", midiCh_ + 1);
            break;
        default:
            snprintf(out, n, " ");
            break;
    }
}

void D5_Controller::exportSettings(D5SettingsV1& s) const {
    s.patch = (uint8_t)bridge_.patch();
    s.volume = (uint8_t)volume_;
    s.voices = (uint8_t)voices_;
    s.midiCh = (uint8_t)midiCh_;
    s.masterTune = (int8_t)tune_;
    s.reverb = (uint8_t)reverb_;
    s.chorus = (uint8_t)chorus_;
}

void D5_Controller::importSettings(const D5SettingsV1& s) {
    volume_ = clampi(s.volume, 0, 100);
    voices_ = clampi(s.voices, 1, d5::kMaxVoicesPerTone);
    midiCh_ = clampi(s.midiCh, 0, 16);
    tune_ = clampi(s.masterTune, -50, 50);
    reverb_ = clampi(s.reverb, 0, 100);
    chorus_ = clampi(s.chorus, 0, 100);

    // Order matters: the patch load resets the engine's levels, so push the
    // mixer values afterwards.
    bridge_.selectPatch(clampi(s.patch, 0, bridge_.patchCount() - 1));
    bridge_.setVoiceLimit(voices_);
    bridge_.setVolume(volume_);
    bridge_.setReverb(reverb_);
    bridge_.setChorus(chorus_);
    bridge_.setMasterTune(tune_);
}
