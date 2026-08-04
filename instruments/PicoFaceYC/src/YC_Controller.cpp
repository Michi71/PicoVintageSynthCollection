// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71



#include "YC_Controller.h"
#include "midi_reface.h"
#include "ipc.h"

template<typename T>
static T clampAdd(T value, int delta, int lo, int hi) {
    int v = static_cast<int>(value) + delta;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return static_cast<T>(v);
}

YC_Controller::YC_Controller(YC_Synth_Bridge& bridge, RefaceMidi& midi)
    : bridge_(bridge), midi_(midi), page_(YcPage::VOLUME) {}

YcPage YC_Controller::advancePage(YcPage current, int delta) {
    int idx = static_cast<int>(current);
    int N = static_cast<int>(YcPage::COUNT);
    idx = ((idx + delta) % N + N) % N;
    return static_cast<YcPage>(idx);
}

void YC_Controller::onEncoder1(int delta) {
    page_ = advancePage(page_, delta);
}

void YC_Controller::onEncoder2(int delta) {
    switch (page_) {
        case YcPage::VOLUME: {
            uint8_t v = clampAdd(state().volume, delta, 0, 127);
            ipc_send_yc_panel_update(18, v);
            midi_.txPanelMirror(18, v);
            break;
        }
        case YcPage::WAVE_OCTAVE: {
            uint8_t v = clampAdd(state().wave, delta, 0, 4);
            ipc_send_yc_panel_update(0, v);
            midi_.txPanelMirror(0, v);
            break;
        }
        case YcPage::FOOT_16_513: {
            uint8_t v = clampAdd(state().footage[0], delta, 0, 6);
            ipc_send_yc_panel_update(2, v);
            midi_.txPanelMirror(2, v);
            break;
        }
        case YcPage::FOOT_8_4: {
            uint8_t v = clampAdd(state().footage[2], delta, 0, 6);
            ipc_send_yc_panel_update(4, v);
            midi_.txPanelMirror(4, v);
            break;
        }
        case YcPage::FOOT_223_2: {
            uint8_t v = clampAdd(state().footage[4], delta, 0, 6);
            ipc_send_yc_panel_update(6, v);
            midi_.txPanelMirror(6, v);
            break;
        }
        case YcPage::FOOT_135_113: {
            uint8_t v = clampAdd(state().footage[6], delta, 0, 6);
            ipc_send_yc_panel_update(8, v);
            midi_.txPanelMirror(8, v);
            break;
        }
        case YcPage::FOOT_1: {
            uint8_t v = clampAdd(state().footage[8], delta, 0, 6);
            ipc_send_yc_panel_update(10, v);
            midi_.txPanelMirror(10, v);
            break;
        }
        case YcPage::PERCUSSION: {
            uint8_t v = clampAdd(state().perc_type, delta, 0, 1);
            ipc_send_yc_panel_update(12, v);
            midi_.txPanelMirror(12, v);
            break;
        }
        case YcPage::VIBCHO: {
            uint8_t v = clampAdd(state().vibcho_select, delta, 0, 1);
            ipc_send_yc_panel_update(14, v);
            midi_.txPanelMirror(14, v);
            break;
        }
        case YcPage::ROTARY: {
            uint8_t v = clampAdd(state().rotary_speed, delta, 0, 3);
            ipc_send_yc_rotary_target(v);
            midi_.txRotaryMirror(v);
            break;
        }
        case YcPage::EFFECT: {
            uint8_t v = clampAdd(state().distortion, delta, 0, 127);
            ipc_send_yc_panel_update(16, v);
            midi_.txPanelMirror(16, v);
            break;
        }
        default: break;
    }
}

void YC_Controller::onEncoder3(int delta) {
    switch (page_) {
        case YcPage::VOLUME:
            break;   // reserviert
        case YcPage::WAVE_OCTAVE: {
            int8_t v = clampAdd(state().octave, delta, -2, 2);
            ipc_send_yc_panel_update(1, static_cast<uint16_t>(v));
            midi_.txPanelMirror(1, static_cast<uint16_t>(v));
            break;
        }
        case YcPage::FOOT_16_513: {
            uint8_t v = clampAdd(state().footage[1], delta, 0, 6);
            ipc_send_yc_panel_update(3, v);
            midi_.txPanelMirror(3, v);
            break;
        }
        case YcPage::FOOT_8_4: {
            uint8_t v = clampAdd(state().footage[3], delta, 0, 6);
            ipc_send_yc_panel_update(5, v);
            midi_.txPanelMirror(5, v);
            break;
        }
        case YcPage::FOOT_223_2: {
            uint8_t v = clampAdd(state().footage[5], delta, 0, 6);
            ipc_send_yc_panel_update(7, v);
            midi_.txPanelMirror(7, v);
            break;
        }
        case YcPage::FOOT_135_113: {
            uint8_t v = clampAdd(state().footage[7], delta, 0, 6);
            ipc_send_yc_panel_update(9, v);
            midi_.txPanelMirror(9, v);
            break;
        }
        case YcPage::PERCUSSION: {
            uint8_t v = clampAdd(state().perc_length, delta, 0, 4);
            ipc_send_yc_panel_update(13, v);
            midi_.txPanelMirror(13, v);
            break;
        }
        case YcPage::VIBCHO: {
            uint8_t v = clampAdd(state().vibcho_depth, delta, 0, 4);
            ipc_send_yc_panel_update(15, v);
            midi_.txPanelMirror(15, v);
            break;
        }
        case YcPage::ROTARY: {
            // reserviert
            break;
        }
        case YcPage::EFFECT: {
            uint8_t v = clampAdd(state().reverb, delta, 0, 127);
            ipc_send_yc_panel_update(17, v);
            midi_.txPanelMirror(17, v);
            break;
        }
        default: break;
    }
}

void YC_Controller::onButtonA() {
    if (page_ == YcPage::PERCUSSION) {
        uint8_t newVal = state().perc_on ? 0 : 1;
        ipc_send_yc_panel_update(11, newVal);
        midi_.txPanelMirror(11, newVal);
    }
}

YcPage YC_Controller::currentPage() const {
    return page_;
}

const char* YC_Controller::pageName() const {
    switch (page_) {
        case YcPage::VOLUME: return "VOLUME";
        case YcPage::WAVE_OCTAVE:    return "WAVE/OCT";
        case YcPage::FOOT_16_513:    return "FT 16/513";
        case YcPage::FOOT_8_4:       return "FT 8/4";
        case YcPage::FOOT_223_2:     return "FT 223/2";
        case YcPage::FOOT_135_113:   return "FT 135/113";
        case YcPage::FOOT_1:         return "FT 1";
        case YcPage::PERCUSSION:     return "PERCUSS";
        case YcPage::VIBCHO:         return "VIB/CHO";
        case YcPage::ROTARY:         return "ROTARY";
        case YcPage::EFFECT:         return "EFFECT";
        default: return "";
    }
}

yc_engine_state_t& YC_Controller::state() const {
    return bridge_.state();
}

