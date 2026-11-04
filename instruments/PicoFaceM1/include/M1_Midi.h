// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// M1_Midi.h -- MIDI front end.
//
// The M1 plays its combination timbres by MIDI channel. All 100 factory
// combinations put every timbre on the global channel, so the PicoFace treats
// the channel it listens on (or, in Omni, any channel) as the M1's global
// channel and hands the engine channel 0.
#ifndef M1_MIDI_H
#define M1_MIDI_H

#include <cstdint>

#include "M1_Bridge.h"

class M1_Controller;

class M1_Midi {
public:
    M1_Midi(M1_Bridge& bridge, M1_Controller& controller)
        : bridge_(bridge), controller_(controller) {}

    void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t ch, uint8_t note);
    void onControlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void onProgramChange(uint8_t ch, uint8_t pc);
    void onChannelPressure(uint8_t ch, uint8_t value);
    void onPitchBend(uint8_t ch, int16_t bend);

private:
    bool accepts(uint8_t ch) const;
    M1_Bridge& bridge_;
    M1_Controller& controller_;
};

#endif // M1_MIDI_H
