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
        case kPageReverb:
            bridge_.setReverb(clampi(bridge_.reverbBalance() + delta, 0, 100));
            break;
        case kPageChorus:
            // Chorus Balance. It was on the display from the start but no
            // encoder ever wrote it -- the value simply could not move.
            bridge_.setChorus(clampi(bridge_.chorusBalance() + delta, 0, 100));
            break;
        case kPageChorusMod:
            bridge_.setChorusRate(clampi(bridge_.chorusRate() + delta, 0, 100));
            break;
        case kPageEqLow:
            bridge_.setEqLowFreq(clampi(bridge_.eqLowFreq() + delta, 0, 15));
            break;
        case kPageEqHigh:
            bridge_.setEqHighFreq(clampi(bridge_.eqHighFreq() + delta, 0, 21));
            break;
        case kPageEqQ:
            bridge_.setEqHighQ(clampi(bridge_.eqHighQ() + delta, 0, 8));
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
            bridge_.setToneBalance(clampi(bridge_.toneBalance() + delta, 0, 100));
            break;
        case kPageReverb:
            bridge_.setReverbType(clampi(bridge_.reverbType() + delta, 0, 31));
            break;
        case kPageChorus:
            bridge_.setChorusType(clampi(bridge_.chorusType() + delta, 0, 7));
            break;
        case kPageChorusMod:
            bridge_.setChorusDepth(clampi(bridge_.chorusDepth() + delta, 0, 100));
            break;
        case kPageEqLow:
            bridge_.setEqLowGain(clampi(bridge_.eqLowGain() + delta, 0, 24));
            break;
        case kPageEqHigh:
            bridge_.setEqHighGain(clampi(bridge_.eqHighGain() + delta, 0, 24));
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
        case kPageMix:       return "Mix";
        case kPageReverb:    return "Reverb";
        case kPageChorus:    return "Chorus";
        case kPageChorusMod: return "Cho Mod";
        case kPageEqLow:     return "EQ Low";
        case kPageEqHigh:    return "EQ High";
        case kPageEqQ:       return "EQ Q";
        case kPageTune: return "Tune";
        case kPagePatch:
        default:        return "Patch";
    }
}

const char* D5_Controller::patchStructure() const { return bridge_.structureName(); }

// The two encoders, each as one value the kit can lay out. Normalisation is
// per page because each range is its own: the panel numbers are 0..100 for the
// balances, 0..15 for the low EQ frequency, +-50 cents for the tune. The kit
// only needs where the value sits between its own ends.
D5_Controller::Value D5_Controller::valueA() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case kPagePatch:
            // The value names itself, and the patch page gives it the width.
            // With one bank aboard the plain number stays (a D-50 owns 64);
            // with the D-05's six banks the panel convention bank-patch tells
            // "2-37 Nightfall" from "5-37".
            if (bridge_.patchCount() > 64)
                snprintf(v.text, sizeof v.text, "%d-%d %s", bridge_.patch() / 64 + 1,
                         bridge_.patch() % 64 + 1, bridge_.patchName());
            else
                snprintf(v.text, sizeof v.text, "%d %s", bridge_.patch() + 1,
                         bridge_.patchName());
            break;
        case kPageMix:
            v.name = "Volume";
            snprintf(v.text, sizeof v.text, "%d", volume_);
            v.norm = volume_ / 100.0f;
            break;
        case kPageReverb:
            v.name = "Rev bal";
            snprintf(v.text, sizeof v.text, "%d", bridge_.reverbBalance());
            v.norm = bridge_.reverbBalance() / 100.0f;
            break;
        case kPageChorus:
            v.name = "Cho bal";
            snprintf(v.text, sizeof v.text, "%d", bridge_.chorusBalance());
            v.norm = bridge_.chorusBalance() / 100.0f;
            break;
        case kPageChorusMod:
            v.name = "Rate";
            snprintf(v.text, sizeof v.text, "%d", bridge_.chorusRate());
            v.norm = bridge_.chorusRate() / 100.0f;
            break;
        case kPageEqLow:
            v.name = "Lo freq";
            snprintf(v.text, sizeof v.text, "%dHz", (int)bridge_.eqLowHz());
            v.norm = bridge_.eqLowFreq() / 15.0f;
            break;
        case kPageEqHigh:
            v.name = "Hi freq";
            snprintf(v.text, sizeof v.text, "%dHz", (int)bridge_.eqHighHz());
            v.norm = bridge_.eqHighFreq() / 21.0f;
            break;
        case kPageEqQ:
            v.name = "Hi Q";
            snprintf(v.text, sizeof v.text, "%d", bridge_.eqHighQ() + 1);
            v.norm = bridge_.eqHighQ() / 8.0f;
            break;
        case kPageTune:
            v.name = "Tune";
            snprintf(v.text, sizeof v.text, "%+dct", tune_);
            v.norm = (tune_ + 50) / 100.0f;
            break;
        default:
            break;
    }
    return v;
}

D5_Controller::Value D5_Controller::valueB() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case kPagePatch:
            v.name = "Voices";
            snprintf(v.text, sizeof v.text, "%d", voices_);
            v.norm = (float)voices_ / (float)d5::kMaxVoicesPerTone;
            break;
        case kPageMix:
            // Tone Balance: 50 is even, above it the upper tone wins.
            v.name = "Tone bal";
            snprintf(v.text, sizeof v.text, "%d", bridge_.toneBalance());
            v.norm = bridge_.toneBalance() / 100.0f;
            break;
        case kPageReverb:
            // A reverb type is a name in a list, not a position - Chapel does
            // not sit "between" Large Hall and Box.
            v.name = "Type";
            snprintf(v.text, sizeof v.text, "%d", bridge_.reverbType() + 1);
            break;
        case kPageChorus:
            v.name = "Type";
            snprintf(v.text, sizeof v.text, "%d", bridge_.chorusType() + 1);
            break;
        case kPageChorusMod:
            v.name = "Depth";
            snprintf(v.text, sizeof v.text, "%d", bridge_.chorusDepth());
            v.norm = bridge_.chorusDepth() / 100.0f;
            break;
        case kPageEqLow:
            v.name = "Gain";
            snprintf(v.text, sizeof v.text, "%+ddB", bridge_.eqLowGain() - 12);
            v.norm = bridge_.eqLowGain() / 24.0f;
            break;
        case kPageEqHigh:
            v.name = "Gain";
            snprintf(v.text, sizeof v.text, "%+ddB", bridge_.eqHighGain() - 12);
            v.norm = bridge_.eqHighGain() / 24.0f;
            break;
        case kPageTune:
            v.name = "MIDI";
            if (midiCh_ >= 16) snprintf(v.text, sizeof v.text, "Omni");
            else               snprintf(v.text, sizeof v.text, "ch %d", midiCh_ + 1);
            break;
        case kPageEqQ:
        default:
            break;
    }
    return v;
}

void D5_Controller::exportSettings(D5SettingsV2& s) const {
    s.patch = (uint16_t)bridge_.patch();
    s.volume = (uint8_t)volume_;
    s.voices = (uint8_t)voices_;
    s.midiCh = (uint8_t)midiCh_;
    s.masterTune = (int8_t)tune_;
    // Reverb and chorus balance belong to the patch now, not to the
    // panel; they are stored with it and re-read on every change.
    s.reverb = (uint8_t)bridge_.reverbBalance();
    s.chorus = (uint8_t)bridge_.chorusBalance();
}

void D5_Controller::importSettings(const D5SettingsV2& s) {
    volume_ = clampi(s.volume, 0, 100);
    voices_ = clampi(s.voices, 1, d5::kMaxVoicesPerTone);
    midiCh_ = clampi(s.midiCh, 0, 16);
    tune_ = clampi(s.masterTune, -50, 50);


    // Order matters: the patch load resets the engine's levels, so push the
    // mixer values afterwards.
    bridge_.selectPatch(clampi(s.patch, 0, bridge_.patchCount() - 1));
    bridge_.setVoiceLimit(voices_);
    bridge_.setVolume(volume_);

    bridge_.setMasterTune(tune_);
}
