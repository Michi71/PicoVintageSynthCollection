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
        // RPN 0 is pitch bend sensitivity. The D-50's own handler (EPROM
        // 0x4E72-0x4EA4) reads only the data-entry MSB and clamps it to 12
        // semitones; the override lasts until the next patch change.
        case 6:
            if (rpnMsb_ == 0 && rpnLsb_ == 0) bridge_.setBendRange(value);
            break;
        case 100: rpnLsb_ = value; break;           // RPN LSB
        case 101: rpnMsb_ = value; break;           // RPN MSB

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
    // The bender range is a patch parameter (pb[26], 0..12 semitones,
    // overrideable by RPN 0). 55 of the bank's 64 patches sit on the MIDI
    // default of two; the other nine now get theirs.
    const float semis = (bend / 8192.0f) * bridge_.bendRangeSemis();
    bridge_.setPitchBendSemis(semis);
}

void D5_Midi::onChannelPressure(uint8_t ch, uint8_t value) {
    if (!accepts(ch)) return;
    bridge_.setAftertouch(value * (1.0f / 127.0f));
}
