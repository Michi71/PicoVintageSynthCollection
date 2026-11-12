// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once
#include <stdint.h>

/*
 * cm_settings.h -- what PicoFaceCM keeps across a power cycle.
 *
 * A CM-32L keeps nothing: it wakes up with its factory settings, and whatever
 * plays it sets what it needs. So only the settings of the module as a whole
 * are stored here, the ones somebody sets once on the panel for the room and
 * the keyboard at hand. The eight parts start with their factory timbres, as
 * on the original.
 *
 * All fields are the module's own values, as they stand in its system area.
 * Bump CM_SETTINGS_VERSION and adjust the static_assert on any layout change;
 * veeprom discards records whose version field does not match.
 */

#define CM_SETTINGS_VERSION 1u

struct __attribute__((packed)) CMSettingsV1 {
    uint8_t masterVolume;   // 0..100
    uint8_t reverbMode;     // 0..3: Room, Hall, Plate, Tap delay
    uint8_t reverbTime;     // 0..7
    uint8_t reverbLevel;    // 0..7
    uint8_t masterTune;     // 0..127, 64 in the middle
    uint8_t channels;       // 0: parts on MIDI channels 2-9 (factory), 1: on 1-8; rhythm on 10
    uint8_t reserved[2];
};

static_assert(sizeof(CMSettingsV1) == 8, "CMSettingsV1 layout drifted");
