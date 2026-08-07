// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// USB MIDI sink. The test implements write() to append to a byte vector, which
// is the transmitted stream it then parses back into a patch.
#pragma once
#include <stdint.h>
struct MIDIOutStub { bool write(const uint8_t* d, uint16_t n); };
MIDIOutStub& usbMidiOut();
