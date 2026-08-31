// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once
#include <stdint.h>

/*
 * rd_settings.h -- persisted UI state record for RD_Controller.
 *
 * This struct is the payload handed to the veeprom append-log module.
 * All fields are in UI units (percent 0..100, toggles 0/1, etc.) --
 * except the three LFO rates, which hold a 0.05 Hz grid index. Version 2
 * is that change: a v1 record's rates would read as the wrong frequency,
 * so those records are discarded rather than reinterpreted.
 * Bump RD_SETTINGS_VERSION and adjust static_assert if the layout
 * changes -- veeprom discards records whose version field mismatches.
 */

#define RD_SETTINGS_VERSION 2u

struct __attribute__((packed)) RdSettingsV1 {
    uint8_t instrument;   // 0..15
    uint8_t volume;       // 0..100
    uint8_t chorusOn;     // 0/1
    uint8_t chorusRate;   // 0.05 Hz grid index, 7..114
    uint8_t chorusDepth;  // 0..100
    uint8_t tremOn;       // 0/1
    uint8_t tremRate;     // 0.05 Hz grid index, 10..154
    uint8_t tremDepth;    // 0..100
    uint8_t phaserOn;     // 0/1
    uint8_t phaserRate;   // 0.05 Hz grid index, 2..100
    uint8_t phaserDepth;  // 0..100
    uint8_t bass;         // 0..100
    uint8_t treble;       // 0..100
    uint8_t dacOn;        // 0/1
    uint8_t midiCh;       // 0..15, 16=Omni
    uint8_t voiceMode;    // 0..4
    int8_t  masterTune;   // -50..50 cents
};

static_assert(sizeof(RdSettingsV1) == 17, "RdSettingsV1 layout drifted");
