// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once
#include <stdint.h>

/*
 * m1_settings.h -- persisted UI state for M1_Controller.
 *
 * Payload for the veeprom append log. All fields are in UI units. Bump
 * M1_SETTINGS_VERSION and adjust the static_assert on any layout change;
 * veeprom discards records whose version field does not match.
 */

#define M1_SETTINGS_VERSION 1u

struct __attribute__((packed)) M1SettingsV1 {
    uint8_t mode;        // 0 = Program, 1 = Combination
    uint8_t program;     // 0..99
    uint8_t combi;       // 0..99
    uint8_t volume;      // 0..100
    uint8_t voices;      // polyphony cap, 1..16
    uint8_t midiCh;      // 0..15 = the M1's global channel, 16 = Omni (taken as global)
    int8_t  masterTune;  // -50..+50 cents
    uint8_t fxOn;        // 0 = effect section bypassed, 1 = on
};

static_assert(sizeof(M1SettingsV1) == 8, "M1SettingsV1 layout drifted");
