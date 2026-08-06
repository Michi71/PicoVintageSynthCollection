// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Midi.h -- MIDI front end. Filters by receive channel, maps the controllers
// the engine actually consumes, and keeps the modulation matrix's three sources
// fed. Everything else is dropped rather than half-handled.

#ifndef JV_MIDI_H
#define JV_MIDI_H

#include <cstdint>

#include "JV_Bridge.h"

class JV_Controller;

class JV_Midi {
public:
    JV_Midi(JV_Bridge& bridge, const JV_Controller& controller)
        : bridge_(bridge), controller_(controller) {}

    void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t ch, uint8_t note);
    void onControlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void onChannelPressure(uint8_t ch, uint8_t value);
    void onPitchBend(uint8_t ch, int16_t bend);

private:
    bool accepts(uint8_t ch) const;

    JV_Bridge& bridge_;
    const JV_Controller& controller_;
    bool sustain_ = false;
    // Notes held only by the pedal, so they can be released together.
    uint32_t pedalHeld_[4] = {0, 0, 0, 0};
};

#endif // JV_MIDI_H
