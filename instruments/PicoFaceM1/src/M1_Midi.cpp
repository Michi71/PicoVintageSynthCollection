// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "M1_Midi.h"

#include "M1_Controller.h"

bool M1_Midi::accepts(uint8_t ch) const {
    const uint8_t rx = controller_.midiChannel();
    return rx >= 16 || rx == ch;
}

void M1_Midi::onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!accepts(ch) || note > 127) return;
    const int n = note + controller_.keyTranspose();   // the M1's global key transpose
    if (n < 0 || n > 127) return;
    if (vel == 0) { bridge_.noteOff(0, (uint8_t)n); return; }
    bridge_.noteOn(0, (uint8_t)n, vel);
}

void M1_Midi::onNoteOff(uint8_t ch, uint8_t note) {
    if (!accepts(ch) || note > 127) return;
    const int n = note + controller_.keyTranspose();
    if (n < 0 || n > 127) return;
    bridge_.noteOff(0, (uint8_t)n);
}

void M1_Midi::onProgramChange(uint8_t ch, uint8_t pc) {
    if (accepts(ch)) controller_.programChange(pc);
}

// The M1's controllers (owner's manual, MIDI implementation): joystick X =
// pitch bend, joystick +Y = CC 1, -Y = CC 2, damper = CC 64, the assignable
// pedal switch on CC 65 -- used here for the rotary speaker's slow/fast, which
// is what that switch does with Rotary SP selected.
void M1_Midi::onControlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    if (!accepts(ch)) return;
    switch (cc) {
        case 1:  bridge_.modWheel(0, value); break;
        case 2:  bridge_.modWheelNeg(0, value); break;
        case 7:  bridge_.setMidiVolume(value); break;
        case 64: bridge_.damper(0, value >= 64); break;
        case 65: bridge_.setRotaryFast(value >= 64); break;
        case 120:
            bridge_.allSoundOff();
            break;
        case 121:
            bridge_.modWheel(0, 0); bridge_.modWheelNeg(0, 0);
            bridge_.aftertouch(0, 0); bridge_.pitchBend(0, 0);
            bridge_.damper(0, false); bridge_.setMidiVolume(127);
            break;
        case 123: case 124: case 125: case 126: case 127:
            bridge_.allNotesOff();
            break;
        default: break;
    }
}

void M1_Midi::onChannelPressure(uint8_t ch, uint8_t value) {
    if (accepts(ch)) bridge_.aftertouch(0, value);
}

void M1_Midi::onPitchBend(uint8_t ch, int16_t bend) {
    if (accepts(ch)) bridge_.pitchBend(0, bend);
}
