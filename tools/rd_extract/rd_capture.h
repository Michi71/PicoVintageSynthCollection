// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Host-only register-write capture. The original firmware is its own
// params_rom parser, so rather than reading the parameter ROM we record what
// the firmware programs into the sound chip and replay that.
//
// make_packs.sh injects the two call sites into a scratch copy of the
// reference emulator; nothing of that emulator is in this repository.

#ifndef RD_CAPTURE_H
#define RD_CAPTURE_H

#include <stdint.h>
#include <vector>

struct RdCaptureEvent {
    uint64_t sample;
    uint8_t  voice;
    uint8_t  part;
    uint8_t  field;
    uint8_t  value;
};

extern std::vector<RdCaptureEvent>* g_rd_capture;       // nullptr = capture off
extern uint64_t                     g_rd_capture_clock; // advanced once per generated sample by the capture driver

#endif // RD_CAPTURE_H
