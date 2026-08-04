// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71



#ifndef YC_CONTROLLER_H
#define YC_CONTROLLER_H

#include "YC_Synth_Bridge.h"

class RefaceMidi;

enum class YcPage : uint8_t {
    VOLUME = 0,
    WAVE_OCTAVE,
    FOOT_16_513,
    FOOT_8_4,
    FOOT_223_2,
    FOOT_135_113,
    FOOT_1,
    PERCUSSION,
    VIBCHO,
    ROTARY,
    EFFECT,
    COUNT
};

class YC_Controller {
public:
    YC_Controller(YC_Synth_Bridge& bridge, RefaceMidi& midi);

    void onEncoder1(int delta);
    void onEncoder2(int delta);
    void onEncoder3(int delta);
    void onButtonA();

    YcPage currentPage() const;
    const char* pageName() const;
    yc_engine_state_t& state() const;

private:
    YC_Synth_Bridge& bridge_;
    RefaceMidi& midi_;
    YcPage page_;

    static YcPage advancePage(YcPage current, int delta);
};

#endif // YC_CONTROLLER_H

