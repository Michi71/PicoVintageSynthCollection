// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "D5_Midi.h"

bool D5_Midi::accepts(uint8_t ch) const {
    const int want = controller_.midiChannel();
    return want >= 16 || ch == (uint8_t)want;      // 16 = Omni
}

void D5_Midi::onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!accepts(ch)) return;
    // A note-on with velocity zero is a note-off; instruments that miss this
    // hang notes with any sequencer that uses running status.
    if (vel == 0) { bridge_.noteOff(note); return; }
    bridge_.noteOn(note, vel);
}

void D5_Midi::onNoteOff(uint8_t ch, uint8_t note) {
    if (!accepts(ch)) return;
    bridge_.noteOff(note);
}

void D5_Midi::onControlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    if (!accepts(ch)) return;
    switch (cc) {
        case 1:                                     // mod wheel -> P-Mod lever
            bridge_.setModWheel(value * (1.0f / 127.0f));
            break;
        case 5:                                     // portamento time
            bridge_.setPortamentoTime(value * 100 / 127);
            break;
        case 65:                                    // portamento switch
            bridge_.setPortamentoSwitch(value >= 64);
            break;
        case 7:                                     // channel volume
            bridge_.setVolume(value * 100 / 127);
            break;
        case 91:                                    // reverb send
            bridge_.setReverb(value * 100 / 127);
            break;
        case 93:                                    // chorus send
            bridge_.setChorus(value * 100 / 127);
            break;
        case 120:                                   // all sound off
        case 123:                                   // all notes off
            bridge_.allNotesOff();
            break;
        default:
            break;
    }
}

void D5_Midi::onPitchBend(uint8_t ch, int16_t bend) {
    if (!accepts(ch)) return;
    // The D-50's bender reaches +/-12 semitones and the engine takes cents,
    // but the range is a patch parameter the preset table does not carry yet,
    // so this holds at the MIDI default of two semitones.
    const float semis = (bend / 8192.0f) * 2.0f;
    bridge_.setPitchBendCents(semis * 100.0f);
}

void D5_Midi::onChannelPressure(uint8_t ch, uint8_t value) {
    if (!accepts(ch)) return;
    bridge_.setAftertouch(value * (1.0f / 127.0f));
}
