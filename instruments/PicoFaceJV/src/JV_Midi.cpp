// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "JV_Midi.h"

#include "JV_Controller.h"

bool JV_Midi::accepts(uint8_t ch) const {
    const uint8_t rx = controller_.midiChannel();
    return rx >= 16 || rx == ch;
}

void JV_Midi::onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!accepts(ch) || note > 127) return;
    if (vel == 0) { onNoteOff(ch, note); return; }
    pedalHeld_[note >> 5] &= ~(1u << (note & 31));
    bridge_.noteOn(note, vel);
}

void JV_Midi::onNoteOff(uint8_t ch, uint8_t note) {
    if (!accepts(ch) || note > 127) return;
    if (sustain_) { pedalHeld_[note >> 5] |= 1u << (note & 31); return; }
    bridge_.noteOff(note);
}

void JV_Midi::onControlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    if (!accepts(ch)) return;
    switch (cc) {
        case 1:  bridge_.modWheel(value); break;
        case 11: bridge_.expression(value); break;
        case 64:
            sustain_ = value >= 64;
            if (!sustain_) {
                for (int n = 0; n < 128; ++n)
                    if (pedalHeld_[n >> 5] & (1u << (n & 31))) bridge_.noteOff((uint8_t)n);
                pedalHeld_[0] = pedalHeld_[1] = pedalHeld_[2] = pedalHeld_[3] = 0;
            }
            break;
        case 120:   // all sound off
        case 123:   // all notes off
            sustain_ = false;
            pedalHeld_[0] = pedalHeld_[1] = pedalHeld_[2] = pedalHeld_[3] = 0;
            bridge_.allNotesOff();
            break;
        case 121:   // reset all controllers
            bridge_.modWheel(0);
            bridge_.expression(0);
            bridge_.aftertouch(0);
            bridge_.setPitchBend(0);
            break;
        default: break;
    }
}

void JV_Midi::onChannelPressure(uint8_t ch, uint8_t value) {
    if (accepts(ch)) bridge_.aftertouch(value);
}

void JV_Midi::onPitchBend(uint8_t ch, int16_t bend) {
    if (accepts(ch)) bridge_.setPitchBend(bend);
}
