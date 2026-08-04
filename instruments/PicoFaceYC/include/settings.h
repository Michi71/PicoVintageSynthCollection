// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/settings.h
//
// PicoFaceYC — persisted settings snapshot (virtual EEPROM).
//
// Persisted state:
//   YC panel state (yc_panel_state_t): wave, octave, 9 footages, percussion,
//   vibrato/chorus, rotary speed, distortion, reverb, volume, MIDI control mode.
//
#pragma once
#include <stdint.h>

#define SETTINGS_VERSION         2

class YC_Synth_Bridge;
class RefaceMidi;

#pragma pack(push, 1)
struct yc_panel_state_t {
    uint8_t   wave;
    int8_t    octave;
    uint8_t   footage[9];
    uint8_t   perc_on;
    uint8_t   perc_type;
    uint8_t   perc_length;
    uint8_t   vibcho_select;
    uint8_t   vibcho_depth;
    uint8_t   rotary_speed;
    uint8_t   distortion;
    uint8_t   reverb;
    uint8_t   volume;
    uint8_t   midi_ctrl_mode;
};

struct SettingsV2 {
    yc_panel_state_t panel;
};
#pragma pack(pop)

static_assert(sizeof(SettingsV2) <= 240, "must fit VEEPROM_MAX_PAYLOAD");

// Restores the persisted record into engine and MIDI layer. One call since
// PicoFaceYC moved to the standard runtime model - it used to be split into a
// core0 and a core1 half, with the loaded record parked in a static in
// between, because engine and MIDI layer lived on different cores.
// The core has already run veeprom_init() by the time init() is called.
void settings_boot_restore(YC_Synth_Bridge* yc, RefaceMidi* rm);

// Debounced write-back, polled from uiTick().
void settings_task(YC_Synth_Bridge* yc, RefaceMidi* rm);
