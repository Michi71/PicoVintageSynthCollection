// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// Host shim for pico/stdlib.h.
//
// Only exists so that core headers which are otherwise host-safe can be
// included in a host test. core/include/midi_serial.h is the case that forced
// it: J6_Controller.cpp mirrors panel edits to DIN MIDI, so the header comes
// along even though the test never sends a byte. Everything the shim needs to
// provide is the standard integer types.
//
// The clock is declared but not defined here: midi_reface.cpp stamps its
// active sensing timers with it, and a test that pulls such a file in supplies
// whatever notion of time it wants to test against. A test that never
// references them links fine without.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t absolute_time_t;
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t);
