// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Midi.h -- MIDI front end. Filters by receive channel and maps the
// controllers the JV-880's own MIDI implementation lists as received; anything
// outside that list is dropped rather than half-handled.
//
// Covered: bank select (CC0), modulation (1), portamento time (5), data entry
// (6/38), volume (7), pan (10), expression (11), hold-1 (64), portamento
// switch (65), reverb and chorus send (91/93, the manual's Effect1 and Effect3
// depth), RPN (100/101) for bend range and fine/coarse tune, the channel mode
// messages, channel pressure and pitch bend.

#ifndef JV_MIDI_H
#define JV_MIDI_H

#include <cstdint>

#include "JV_Bridge.h"

class JV_Controller;

class JV_Midi {
public:
    JV_Midi(JV_Bridge& bridge, const JV_Controller& controller)
        : bridge_(bridge), controller_(controller) {}

    // Bank select is latched until a program change arrives, as the manual
    // specifies. Returns the patch bank and index to select, or false when the
    // program number is out of range.
    bool resolveProgram(uint8_t pc, int& bank, int& index) const;

    void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t ch, uint8_t note);
    void onControlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void onChannelPressure(uint8_t ch, uint8_t value);
    void onPitchBend(uint8_t ch, int16_t bend);

private:
    bool accepts(uint8_t ch) const;

    JV_Bridge& bridge_;
    const JV_Controller& controller_;
    void resetControllers();

    bool sustain_ = false;
    uint8_t bankMsb_ = 81;      // 80 user, 81 preset -- the manual's numbering
    // RPN state. The JV recognises RPN 0 (bend sensitivity), 1 (fine tune) and
    // 2 (coarse tune), and clears the LSB when it receives an MSB.
    uint8_t rpnMsb_ = 0x7F, rpnLsb_ = 0x7F;
    uint8_t dataMsb_ = 0;
    float   fineCents_ = 0.0f, coarseCents_ = 0.0f;
    float   revScale_ = 1.0f, choScale_ = 1.0f;
    // Notes held only by the pedal, so they can be released together.
    uint32_t pedalHeld_[4] = {0, 0, 0, 0};
};

#endif // JV_MIDI_H
