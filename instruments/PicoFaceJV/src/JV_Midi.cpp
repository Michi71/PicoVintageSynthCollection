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
    bridge_.noteOn(note, bridge_.mapVelocity(vel));
}

void JV_Midi::onNoteOff(uint8_t ch, uint8_t note) {
    if (!accepts(ch) || note > 127) return;
    if (sustain_) { pedalHeld_[note >> 5] |= 1u << (note & 31); return; }
    bridge_.noteOff(note);
}

// 80 is the user bank, 81 the presets; the presets hold A and B back to back,
// which is how all 192 patches are reachable over MIDI.
bool JV_Midi::resolveProgram(uint8_t pc, int& bank, int& index) const {
    if (pc > 127) return false;
    if (bankMsb_ == 80) {
        if (pc >= 64) return false;
        bank = 0; index = pc; return true;
    }
    bank = (pc < 64) ? 1 : 2;
    index = pc & 63;
    return true;
}

void JV_Midi::resetControllers() {
    bridge_.modWheel(0);
    bridge_.expression(0);
    bridge_.aftertouch(0);
    bridge_.setPitchBend(0);
    bridge_.setMidiVolume(127);
    bridge_.setMidiPan(64);
    bridge_.setSendScale(1.0f, 1.0f);
    bridge_.setPortaSwitchOverride(-1);
    bridge_.setPortaTimeOverride(-1);
    rpnMsb_ = rpnLsb_ = 0x7F;
}

void JV_Midi::onControlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    if (!accepts(ch)) return;
    switch (cc) {
        case 0:  bankMsb_ = value; break;   // MSB only; the LSB is ignored
        case 1:  bridge_.modWheel(value); break;
        case 5:  bridge_.setPortaTimeOverride(value); break;
        case 7:  bridge_.setMidiVolume(value); break;
        case 10: bridge_.setMidiPan(value); break;
        case 11: bridge_.expression(value); break;
        case 65: bridge_.setPortaSwitchOverride(value >= 64 ? 1 : 0); break;
        case 91: revScale_ = value / 127.0f;                 // Effect1 depth
                 bridge_.setSendScale(revScale_, choScale_); break;
        case 93: choScale_ = value / 127.0f;                 // Effect3 depth
                 bridge_.setSendScale(revScale_, choScale_); break;
        case 101: rpnMsb_ = value; rpnLsb_ = 0; break;
        case 100: rpnLsb_ = value; break;
        case 38:  break;   // data entry LSB, only fine tune uses it
        case 6: {          // data entry MSB
            dataMsb_ = value;
            if (rpnMsb_ != 0) break;
            if (rpnLsb_ == 0) {            // bend sensitivity, 0..12 semitones
                bridge_.setBendRangeOverride(value > 12 ? 12 : value);
            } else if (rpnLsb_ == 1) {     // fine tune, 0x40 centre, +-100 cents
                fineCents_ = ((float)value - 64.0f) * (100.0f / 64.0f);
                bridge_.setRpnTuneCents(fineCents_ + coarseCents_);
            } else if (rpnLsb_ == 2) {     // coarse tune, 0x40 centre, semitones
                coarseCents_ = ((float)value - 64.0f) * 100.0f;
                bridge_.setRpnTuneCents(fineCents_ + coarseCents_);
            }
            break;
        }
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
            resetControllers();
            break;
        case 124: case 125:   // omni off / on, both taken as all notes off
            bridge_.allNotesOff();
            break;
        case 126:   // mono -- the JV's SOLO, forced on regardless of the patch
            bridge_.allNotesOff();
            bridge_.setMonoOverride(1);
            break;
        case 127:   // poly
            bridge_.allNotesOff();
            bridge_.setMonoOverride(-1);
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
