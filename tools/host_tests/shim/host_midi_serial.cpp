// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// Host stub for the DIN MIDI transport.
//
// Instrument controllers send their panel changes to both wires. On the host
// there is no uart1, so the two symbols a controller actually references are
// satisfied here and the bytes go nowhere. Link this into any host test that
// pulls in a controller.
#include "midi_serial.h"

void MIDISerial::write(const uint8_t*, uint16_t) {}
void MIDISerial::sendNoteOn(uint8_t, uint8_t, uint8_t) {}
void MIDISerial::sendNoteOff(uint8_t, uint8_t, uint8_t) {}
void MIDISerial::sendControlChange(uint8_t, uint8_t, uint8_t) {}
void MIDISerial::sendProgramChange(uint8_t, uint8_t) {}
void MIDISerial::sendPitchBend(uint8_t, uint16_t) {}

MIDISerial& midiSerial()
{
    static MIDISerial instance;
    return instance;
}
