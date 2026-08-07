// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once
#include <stdint.h>

/*
 * jv_settings.h -- persisted UI state for JV_Controller.
 *
 * Payload for the veeprom append log. All fields are in UI units. Bump
 * JV_SETTINGS_VERSION and adjust the static_assert on any layout change;
 * veeprom discards records whose version field does not match.
 */

#define JV_SETTINGS_VERSION 2u

struct __attribute__((packed)) JvSettingsV1 {
    uint8_t bank;        // 0 = User, 1 = A, 2 = B
    uint8_t patch;       // 0..63
    uint8_t volume;      // 0..100
    uint8_t voices;      // polyphony cap, 1..24
    uint8_t midiCh;      // 0..15, 16 = Omni
    int8_t  masterTune;  // -50..+50 cents
    uint8_t veloScale;   // 0..100 %, 100 = the machine's own response
};

static_assert(sizeof(JvSettingsV1) == 7, "JvSettingsV1 layout drifted");
