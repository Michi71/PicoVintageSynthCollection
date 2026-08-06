// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "JV_Controller.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const char* kBankName[3] = {"User", "A", "B"};

} // namespace

JV_Controller::JV_Controller(JV_Bridge& bridge) : bridge_(bridge) {}

void JV_Controller::onEncoderSel(int delta) {
    if (!delta) return;
    const int n = (int)JvPage::COUNT;
    int p = ((int)page_ + delta) % n;
    if (p < 0) p += n;
    page_ = (JvPage)p;
}

void JV_Controller::applyPatch() {
    bridge_.selectPatch(bank_, patch_);
}

void JV_Controller::onEncoderA(int delta) {
    if (!delta) return;
    switch (page_) {
        case JvPage::PATCH:
            // A walks the 192 patches as one list; the bank follows along.
            {
                int idx = clampInt(bank_ * 64 + patch_ + delta, 0, 191);
                bank_ = (uint8_t)(idx / 64);
                patch_ = (uint8_t)(idx % 64);
                applyPatch();
            }
            break;
        case JvPage::VOLUME:
            volume_ = (uint8_t)clampInt(volume_ + delta, 0, 100);
            bridge_.setVolume(volume_);
            break;
        case JvPage::VOICES:
            voices_ = (uint8_t)clampInt(voices_ + delta, 1, jv::kMaxVoices);
            bridge_.setVoiceLimit(voices_);
            break;
        case JvPage::TUNE:
            tune_ = (int8_t)clampInt(tune_ + delta, -50, 50);
            bridge_.setMasterTune(tune_);
            break;
        case JvPage::SYS:
            midiCh_ = (uint8_t)clampInt(midiCh_ + delta, 0, 16);
            break;
        default:
            break;
    }
}

void JV_Controller::onEncoderB(int delta) {
    if (!delta) return;
    // Only PATCH has a second parameter: B steps the bank, keeping the number.
    if (page_ == JvPage::PATCH) {
        bank_ = (uint8_t)clampInt(bank_ + delta, 0, 2);
        applyPatch();
    }
}

const char* JV_Controller::title() const { return "PicoFaceJV"; }

const char* JV_Controller::pageName() const {
    switch (page_) {
        case JvPage::PATCH:  return "PATCH";
        case JvPage::VOLUME: return "VOL";
        case JvPage::VOICES: return "VOICES";
        case JvPage::TUNE:   return "TUNE";
        case JvPage::SYS:    return "SYS";
        default:             return "";
    }
}

const char* JV_Controller::lineA(char* buf, size_t n) const {
    switch (page_) {
        case JvPage::PATCH: {
            char nm[13] = {0};
            memcpy(nm, bridge_.patchName(), 12);
            for (int i = 11; i >= 0 && nm[i] == ' '; --i) nm[i] = 0;
            snprintf(buf, n, "%s", nm);
            break;
        }
        case JvPage::VOLUME: snprintf(buf, n, "Volume %u%%", volume_); break;
        case JvPage::VOICES: snprintf(buf, n, "Max %u", voices_); break;
        case JvPage::TUNE:   snprintf(buf, n, "Tune %+d ct", tune_); break;
        case JvPage::SYS:
            if (midiCh_ >= 16) snprintf(buf, n, "MIDI Omni");
            else               snprintf(buf, n, "MIDI Ch %u", (unsigned)(midiCh_ + 1));
            break;
        default: buf[0] = 0; break;
    }
    return buf;
}

const char* JV_Controller::lineB(char* buf, size_t n) const {
    switch (page_) {
        case JvPage::PATCH:
            snprintf(buf, n, "%s %02u", kBankName[bank_ <= 2 ? bank_ : 0],
                     (unsigned)(patch_ + 1));
            break;
        case JvPage::VOICES:
            snprintf(buf, n, "Act %d", bridge_.activeVoices());
            break;
        case JvPage::TUNE:
            snprintf(buf, n, "A4 %.1f Hz", 440.0 * pow(2.0, tune_ / 1200.0));
            break;
        default: buf[0] = 0; break;
    }
    return buf;
}

void JV_Controller::exportSettings(JvSettingsV1& s) const {
    s.bank = bank_;
    s.patch = patch_;
    s.volume = volume_;
    s.voices = voices_;
    s.midiCh = midiCh_;
    s.masterTune = tune_;
}

void JV_Controller::importSettings(const JvSettingsV1& s) {
    bank_   = (uint8_t)clampInt(s.bank, 0, 2);
    patch_  = (uint8_t)clampInt(s.patch, 0, 63);
    volume_ = (uint8_t)clampInt(s.volume, 0, 100);
    voices_ = (uint8_t)clampInt(s.voices, 1, jv::kMaxVoices);
    midiCh_ = (uint8_t)clampInt(s.midiCh, 0, 16);
    tune_   = (int8_t)clampInt(s.masterTune, -50, 50);

    bridge_.setVolume(volume_);
    bridge_.setVoiceLimit(voices_);
    bridge_.setMasterTune(tune_);
    applyPatch();
}
