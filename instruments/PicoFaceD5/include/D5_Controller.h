// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Controller.h -- what the three encoders do. The select encoder walks the
// pages, the other two edit the two values that page offers.

#ifndef D5_CONTROLLER_H
#define D5_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "D5_Bridge.h"
#include "d5_settings.h"

class D5_Controller {
public:
    explicit D5_Controller(D5_Bridge& bridge) : bridge_(bridge) {}

    void onEncoderSel(int8_t delta);
    void onEncoderA(int8_t delta);
    void onEncoderB(int8_t delta);

    const char* title() const;
    const char* pageName() const;
    void lineA(char* out, size_t n) const;
    void lineB(char* out, size_t n) const;

    void exportSettings(D5SettingsV2& s) const;
    void importSettings(const D5SettingsV2& s);

    int midiChannel() const { return midiCh_; }

private:
    enum Page { kPagePatch = 0, kPageMix, kPageTune, kPageCount };

    D5_Bridge& bridge_;
    int page_ = kPagePatch;
    int volume_ = 80;
    int reverb_ = 100;
    int chorus_ = 100;
    int voices_ = 8;
    int midiCh_ = 16;      // 16 = Omni
    int tune_ = 0;
};

#endif // D5_CONTROLLER_H
