// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "JV_Controller.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const char* kBankName[3] = {"User", "A", "B"};

// The order the panel walks, which is not the order the banks are numbered in:
// the two preset banks first, the user bank last. Numbered order would put User
// below A, so stepping down from A 01 enters the user bank backwards at 64.
// Behind B it is entered at 01, the way you would expect to arrive at it.
//
// The 4 MB build carries no user samples, so its list stops after B -- which is
// the other reason the user bank sits at the end rather than in the middle.
#ifdef JV_BANKS_AB_ONLY
const uint8_t kBankOrder[2] = {1, 2};
const uint8_t kBankSlot[3]  = {0, 0, 1};   // bank id -> list position (User -> A)
const int     kBankCount    = 2;
#else
const uint8_t kBankOrder[3] = {1, 2, 0};   // list position -> bank id
const uint8_t kBankSlot[3]  = {2, 0, 1};   // bank id -> list position
const int     kBankCount    = 3;
#endif

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
                const int here = kBankSlot[bank_ <= 2 ? bank_ : 0] * 64 + patch_;
                const int idx = clampInt(here + delta, 0, kBankCount * 64 - 1);
                bank_ = kBankOrder[idx / 64];
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
        case JvPage::VELO:
            veloScale_ = (uint8_t)clampInt(veloScale_ + delta * 5, 0, 100);
            bridge_.setVelocityScale(veloScale_);
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
    // Same order as the flat walk, so the two encoders agree about which bank
    // comes next.
    if (page_ == JvPage::PATCH) {
        const int slot = clampInt(kBankSlot[bank_ <= 2 ? bank_ : 0] + delta, 0, kBankCount - 1);
        bank_ = kBankOrder[slot];
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
        case JvPage::VELO:   return "VELO";
        case JvPage::SYS:    return "SYS";
        default:             return "";
    }
}

// The two encoders, each as one value the kit can lay out. Normalisation is
// per page because each range is its own; the kit only needs where the value
// sits between its own ends.
JV_Controller::Value JV_Controller::valueA() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case JvPage::PATCH: {
            // The names arrive space-padded to twelve characters.
            char nm[13] = {0};
            memcpy(nm, bridge_.patchName(), 12);
            for (int i = 11; i >= 0 && nm[i] == ' '; --i) nm[i] = 0;
            snprintf(v.text, sizeof v.text, "%s", nm);
            break;
        }
        case JvPage::VOLUME:
            v.name = "Volume";
            snprintf(v.text, sizeof v.text, "%u%%", volume_);
            v.norm = volume_ / 100.0f;
            break;
        case JvPage::VOICES:
            v.name = "Max";
            snprintf(v.text, sizeof v.text, "%u", voices_);
            v.norm = (float)voices_ / (float)jv::kMaxVoices;
            break;
        case JvPage::TUNE:
            v.name = "Tune";
            snprintf(v.text, sizeof v.text, "%+dct", tune_);
            v.norm = (tune_ + 50) / 100.0f;
            break;
        case JvPage::VELO:
            v.name = "Velo";
            if (veloScale_ >= 100) snprintf(v.text, sizeof v.text, "Orig");
            else                   snprintf(v.text, sizeof v.text, "%u%%", veloScale_);
            v.norm = veloScale_ / 100.0f;
            break;
        case JvPage::SYS:
            v.name = "MIDI";
            if (midiCh_ >= 16) snprintf(v.text, sizeof v.text, "Omni");
            else               snprintf(v.text, sizeof v.text, "Ch %u", (unsigned)(midiCh_ + 1));
            break;
        default:
            break;
    }
    return v;
}

JV_Controller::Value JV_Controller::valueB() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case JvPage::PATCH:
            v.name = "Bank";
            snprintf(v.text, sizeof v.text, "%s %02u", kBankName[bank_ <= 2 ? bank_ : 0],
                     (unsigned)(patch_ + 1));
            break;
        case JvPage::VOICES:
            // Not editable: what the engine is actually playing right now.
            v.name = "Act";
            snprintf(v.text, sizeof v.text, "%d", bridge_.activeVoices());
            break;
        case JvPage::TUNE:
            v.name = "A4";
            snprintf(v.text, sizeof v.text, "%.1fHz", 440.0 * pow(2.0, tune_ / 1200.0));
            break;
        case JvPage::VELO:
            // Show where a middle-strength note lands, which is the number that
            // actually tells you what the setting is doing.
            v.name = "64 ->";
            snprintf(v.text, sizeof v.text, "%u", (unsigned)bridge_.mapVelocity(64));
            break;
        default:
            break;
    }
    return v;
}

void JV_Controller::exportSettings(JvSettingsV1& s) const {
    s.bank = bank_;
    s.patch = patch_;
    s.volume = volume_;
    s.voices = voices_;
    s.midiCh = midiCh_;
    s.masterTune = tune_;
    s.veloScale = veloScale_;
}

void JV_Controller::importSettings(const JvSettingsV1& s) {
    // Round-trips unchanged on the full build; on the 4 MB one a stored User
    // bank lands on A rather than on 64 silent patches.
    bank_   = kBankOrder[kBankSlot[clampInt(s.bank, 0, 2)]];
    patch_  = (uint8_t)clampInt(s.patch, 0, 63);
    volume_ = (uint8_t)clampInt(s.volume, 0, 100);
    voices_ = (uint8_t)clampInt(s.voices, 1, jv::kMaxVoices);
    midiCh_ = (uint8_t)clampInt(s.midiCh, 0, 16);
    tune_   = (int8_t)clampInt(s.masterTune, -50, 50);
    veloScale_ = (uint8_t)clampInt(s.veloScale, 0, 100);

    bridge_.setVelocityScale(veloScale_);
    bridge_.setVolume(volume_);
    bridge_.setVoiceLimit(voices_);
    bridge_.setMasterTune(tune_);
    applyPatch();
}
