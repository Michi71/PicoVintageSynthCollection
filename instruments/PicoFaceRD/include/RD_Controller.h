// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// RD_Controller.h
//
// Architecture: Runs on Core 1. Parameter mutations are sent to the engine on Core 0
// via IPC (ipc_send_dx_param). Shadow copies of parameters are maintained locally
// solely for display purposes; the engine on Core 0 remains the single source of truth.

#ifndef RD_CONTROLLER_H
#define RD_CONTROLLER_H

#include <cstdint>
#include "rd_params.h"
#include "rd_settings.h"
#include "RD_Midi.h"
#include "rd_ipc_local.h"

enum class RdPage : uint8_t {
    PATCH = 0,
    CHORUS,
    TREMOLO,
    PHASER,
    EQ,
    VOICES,
    TUNE,
    SYS,
    COUNT
};

class RD_Controller {
public:
    explicit RD_Controller(RD_Midi& midi);

    void onEncoder1(int delta);
    void onEncoder2(int delta);
    void onEncoder3(int delta);

    RdPage currentPage() const;
    const char* pageName() const;

    // Display Accessors
    const char* param2Name() const;
    const char* param3Name() const;
    uint8_t param2Value() const;
    uint8_t param3Value() const;

    // Persistence (veeprom payload, UI units)
    void exportSettings(RdSettingsV1& s) const;
    void importSettings(const RdSettingsV1& s); // clamps, applies, re-sends everything

private:
    RD_Midi& midi_;
    RdPage page_ = RdPage::PATCH;
    uint8_t shadow_[RD_PARAM_COUNT] = {}; // UI units: percent 0..100, toggles 0/1 (wire conversion at send time, see .cpp)
    uint8_t instrument_ = 0;
    uint8_t midiCh_ = 16;
    uint8_t voiceMode_ = 4; // 0..3 fixed 8/16/24/32, 4 = Auto (mirrors bridge default)
    int8_t masterTune_ = 0; // cents -50..+50

    static RdPage advancePage(RdPage current, int delta);
};

#endif // RD_CONTROLLER_H
