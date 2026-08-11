// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Midi.h -- MIDI front end. Both transports (USB and DIN) arrive here
// through the same core callbacks, so this never learns which wire an event
// came in on.

#ifndef D5_MIDI_H
#define D5_MIDI_H

#include <cstdint>

#include "D5_Bridge.h"
#include "D5_Controller.h"

class D5_Midi {
public:
    D5_Midi(D5_Bridge& bridge, D5_Controller& controller)
        : bridge_(bridge), controller_(controller) {}

    void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t ch, uint8_t note);
    void onControlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void onPitchBend(uint8_t ch, int16_t bend);

    bool accepts(uint8_t ch) const;

private:
    D5_Bridge& bridge_;
    D5_Controller& controller_;
};

#endif // D5_MIDI_H
