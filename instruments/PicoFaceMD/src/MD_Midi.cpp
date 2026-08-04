// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// =====================================================================
// MD_Midi.cpp -- MIDI front end for PicoFaceMD
//
// Recognised:
//   Note On / Note Off    the full MIDI range. A Model D has 44 keys, but the
//                         pitch of the instrument comes from a control
//                         voltage, and nothing in the circuit stops that
//                         voltage going past the ends of its own keyboard.
//                         Folding stray notes back into F1..C5 -- as the
//                         Solina does, because a divider organ genuinely has
//                         no note outside its manual -- would take the bottom
//                         octave of a 61-key controller away from a synth
//                         whose main job is bass. Note On with velocity 0
//                         counts as Note Off.
//   Control Change        every panel control, through the table in
//                         moog_params.cpp. See the README for the list.
//   CC 64                 sustain pedal, >= 64 = on
//   CC 120 / 123          all sound off / all notes off
//   CC 121                reset all controllers
//   Program Change        preset, modulo MOOG_NPROGRAMS above the last one
//   Pitch Bend            travel set by the Bend parameter, default two
//                         semitones
//
// Not implemented:
//   Velocity              the keyboard of the original produces a gate, not a
//                         velocity: every key sounds at the same level however
//                         it is struck
// =====================================================================

#include "MD_Midi.h"
#include "MD_Controller.h"
#include "md_ipc.h"
#include "moog/moog_params.h"

void MD_Midi::init(uint8_t rxChannel)
{
    setRxChannel(rxChannel);
}

void MD_Midi::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    if (vel == 0) {
        ipc_send_note_off(note);
        return;
    }
    ipc_send_note_on(note, vel);
}

void MD_Midi::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch)
{
    (void) vel;   // no release velocity
    if (!channelMatches(ch)) return;

    ipc_send_note_off(note);
}

void MD_Midi::onControlChange(uint8_t cc, uint8_t val, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    switch (cc) {
        case 64:    // Sustain pedal
        case 120:   // All Sound Off
        case 123:   // All Notes Off
            ipc_send_cc(cc, val);
            return;

        case 121:   // Reset All Controllers
            ipc_send_cc(64, 0);
            ipc_send_pitch_bend(8192);
            ipc_send_param(MOOG_MOD_WHEEL, 0);
            if (ui_) ui_->onMidiParam(MOOG_MOD_WHEEL, 0);
            return;

        default:
            break;
    }

    // Everything else is looked up in the panel table, so the mapping lives
    // in one place and cannot drift from what the display and the README say.
    const int id = moogParamForCc(cc);
    if (id < 0) return;

    const uint16_t perMille = (uint16_t) (((uint32_t) val * 1000u) / 127u);
    ipc_send_param((uint8_t) id, perMille);

    // Keep the shadow the display reads from in step. Without this, moving a
    // fader would change the sound while the screen went on showing the old
    // number -- and the next encoder click would then jump back to it.
    if (ui_) ui_->onMidiParam(id, perMille);
}

void MD_Midi::onProgramChange(uint8_t program, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    const int32_t p = (int32_t) (program % MOOG_NPROGRAMS);
    ipc_send_param(MD_PARAM_PROGRAM, (uint16_t) p);
    if (ui_) ui_->onMidiProgram(p);
}

void MD_Midi::onPitchBend(uint16_t value, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    if (value > 16383) value = 16383;
    ipc_send_pitch_bend(value);
}

uint8_t MD_Midi::getRxChannel() const
{
    return rxChannel_;
}

void MD_Midi::setRxChannel(uint8_t ch)
{
    if (ch > MD_MIDI_OMNI)
        ch = MD_MIDI_OMNI;
    rxChannel_ = ch;
}
