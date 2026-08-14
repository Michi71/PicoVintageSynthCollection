// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once
#include <stdint.h>

/*
 * d5_settings.h -- persisted UI state for D5_Controller.
 *
 * Payload for the veeprom append log. All fields are in UI units. Bump
 * D5_SETTINGS_VERSION and adjust the static_assert on any layout change;
 * veeprom discards records whose version field does not match.
 *
 * V2: patch widened to uint16 -- with the D-05 bank table aboard the bank
 * holds 384 patches (six banks of 64), one byte no longer spans them.
 */

#define D5_SETTINGS_VERSION 2u

struct __attribute__((packed)) D5SettingsV2 {
    uint16_t patch;      // absolute index, 0..patchCount-1; 64 per bank
    uint8_t volume;      // 0..100
    uint8_t voices;      // polyphony cap per tone, 1..8
    uint8_t midiCh;      // 0..15, 16 = Omni
    int8_t  masterTune;  // -50..+50 cents
    uint8_t reverb;      // 0..100, scales the patch's own reverb balance
    uint8_t chorus;      // 0..100, scales the patch's own chorus balance
};

static_assert(sizeof(D5SettingsV2) == 8, "D5SettingsV2 layout drifted");
