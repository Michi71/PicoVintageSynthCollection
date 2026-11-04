// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "M1_Controller.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

void M1_Controller::onEncoderSel(int delta) {
    if (!delta) return;
    const int n = (int)M1Page::COUNT;
    int p = ((int)page_ + delta) % n;
    if (p < 0) p += n;
    page_ = (M1Page)p;
}

void M1_Controller::apply() {
    if (mode_) bridge_.selectCombi(combi_);
    else       bridge_.selectProgram(program_);
}

void M1_Controller::programChange(uint8_t pc) {
    if (pc > 99) return;
    if (mode_) combi_ = pc; else program_ = pc;
    apply();
}

void M1_Controller::onEncoderA(int delta) {
    if (!delta) return;
    switch (page_) {
        case M1Page::PATCH:
            if (mode_) combi_ = (uint8_t)clampInt(combi_ + delta, 0, 99);
            else       program_ = (uint8_t)clampInt(program_ + delta, 0, 99);
            apply();
            break;
        case M1Page::VOLUME:
            volume_ = (uint8_t)clampInt(volume_ + delta, 0, 100);
            bridge_.setVolume(volume_);
            break;
        case M1Page::VOICES:
            voices_ = (uint8_t)clampInt(voices_ + delta, 1, m1::kVoices);
            bridge_.setVoiceLimit(voices_);
            break;
        case M1Page::TUNE:
            tune_ = (int8_t)clampInt(tune_ + delta, -50, 50);
            bridge_.setMasterTune(tune_);
            break;
        case M1Page::FX:
            fxOn_ = delta > 0 ? 1 : 0;
            bridge_.setFxOn(fxOn_ != 0);
            break;
        case M1Page::SYS:
            midiCh_ = (uint8_t)clampInt(midiCh_ + delta, 0, 16);
            break;
        default:
            break;
    }
}

void M1_Controller::onEncoderB(int delta) {
    if (!delta) return;
    if (page_ == M1Page::PATCH) {
        mode_ = delta > 0 ? 1 : 0;
        apply();
    }
}

const char* M1_Controller::pageName() const {
    switch (page_) {
        case M1Page::PATCH:  return mode_ ? "COMBI" : "PROG";
        case M1Page::VOLUME: return "VOL";
        case M1Page::VOICES: return "VOICES";
        case M1Page::TUNE:   return "TUNE";
        case M1Page::FX:     return "FX";
        case M1Page::SYS:    return "SYS";
        case M1Page::DIAG:   return "DIAG";
        default:             return "";
    }
}

M1_Controller::Value M1_Controller::valueA() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case M1Page::PATCH: {
            char nm[11] = {0};
            memcpy(nm, bridge_.patchName(), 10);
            for (int i = 9; i >= 0 && (nm[i] == ' ' || nm[i] == 0); --i) nm[i] = 0;
            snprintf(v.text, sizeof v.text, "%s", nm);
            break;
        }
        case M1Page::VOLUME:
            v.name = "Volume";
            snprintf(v.text, sizeof v.text, "%u%%", volume_);
            v.norm = volume_ / 100.0f;
            break;
        case M1Page::VOICES:
            v.name = "Max";
            snprintf(v.text, sizeof v.text, "%u", voices_);
            v.norm = (float)voices_ / (float)m1::kVoices;
            break;
        case M1Page::TUNE:
            v.name = "Tune";
            snprintf(v.text, sizeof v.text, "%+dct", tune_);
            v.norm = (tune_ + 50) / 100.0f;
            break;
        case M1Page::FX:
            v.name = "Effects";
            snprintf(v.text, sizeof v.text, "%s", fxOn_ ? "On" : "Bypass");
            break;
        case M1Page::SYS:
            v.name = "MIDI";
            if (midiCh_ >= 16) snprintf(v.text, sizeof v.text, "Omni");
            else               snprintf(v.text, sizeof v.text, "Ch %u", (unsigned)(midiCh_ + 1));
            break;
        default:
            break;
    }
    return v;
}

M1_Controller::Value M1_Controller::valueB() const {
    Value v{"", {0}, -1.0f};
    switch (page_) {
        case M1Page::PATCH:
            v.name = mode_ ? "Combi" : "Prog";
            snprintf(v.text, sizeof v.text, "%s %02u", mode_ ? "C" : "P",
                     (unsigned)(mode_ ? combi_ : program_));
            break;
        case M1Page::VOICES:
            v.name = "Act";
            snprintf(v.text, sizeof v.text, "%d", bridge_.activeVoices());
            break;
        case M1Page::TUNE:
            v.name = "A4";
            snprintf(v.text, sizeof v.text, "%.1fHz", 440.0 * pow(2.0, tune_ / 1200.0));
            break;
        default:
            break;
    }
    return v;
}

void M1_Controller::exportSettings(M1SettingsV1& s) const {
    s.mode = mode_; s.program = program_; s.combi = combi_;
    s.volume = volume_; s.voices = voices_; s.midiCh = midiCh_;
    s.masterTune = tune_; s.fxOn = fxOn_;
}

void M1_Controller::importSettings(const M1SettingsV1& s) {
    mode_ = s.mode ? 1 : 0;
    program_ = (uint8_t)clampInt(s.program, 0, 99);
    combi_ = (uint8_t)clampInt(s.combi, 0, 99);
    volume_ = (uint8_t)clampInt(s.volume, 0, 100);
    voices_ = (uint8_t)clampInt(s.voices, 1, m1::kVoices);
    midiCh_ = (uint8_t)clampInt(s.midiCh, 0, 16);
    tune_ = (int8_t)clampInt(s.masterTune, -50, 50);
    fxOn_ = s.fxOn ? 1 : 0;
    bridge_.setVolume(volume_);
    bridge_.setVoiceLimit(voices_);
    bridge_.setMasterTune(tune_);
    bridge_.setFxOn(fxOn_ != 0);
    apply();
}
