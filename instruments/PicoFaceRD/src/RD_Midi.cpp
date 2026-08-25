// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// =====================================================================
// RD_Midi.cpp
//
// Roland MK-80 MIDI implementation chart (recognized side).
//
// Recognized and mapped:
//   Note On / Note Off    true voice 15..113; out-of-range notes are
//                         octave-shifted into that range before dispatch.
//                         Note On velocity 0 is interpreted as Note Off.
//   CC 64  Damper         >= 64 = ON (forwarded to engine).
//   CC 92  Tremolo        switch: 0..63 OFF, 64..127 ON.
//   CC 93  Chorus         switch: 0..63 OFF, 64..127 ON.
//   CC 95  Phaser         switch: 0..63 OFF, 64..127 ON.
//   CC 121 Reset All Ctrl hold -> off, pitch bend -> center;
//                         modulation -> 0 (n/a: engine has no vibrato).
//   CC 123 All Notes Off  forwarded to engine.
//   Program Change 0..63  chart range; folded onto the shipped patches.
//   Pitch Bend            default bender depth +-2 semitones.
//
// Omitted (per chart / hardware constraints):
//   CC 1  Modulation      engine has no vibrato path.
//   Active Sensing        handled by the USB MIDI transport layer.
//   SysEx (model 2FH)     edits MK-80 patch RAM; our packs are ROM.
// =====================================================================

#include "RD_Midi.h"
#include "rd_cc_map.h"
#include "rd_ipc_local.h"
#include "rd_params.h"

// ---------------------------------------------------------------------
// Fold a MIDI note number into the range covered by the descriptor packs
// (21..108, the captured 88-key sweep) by octave-shifting. The MK-80
// chart folds into 15..113, but the engine has no entries for 15..20 /
// 109..113 -- folding to the pack range keeps those notes audible (an
// octave off) instead of silently dropped. Applied identically to Note
// On and Note Off so that a triggered pair always matches.
// ---------------------------------------------------------------------
static uint8_t foldToRange(uint8_t note) {
    while (note < 21)  note += 12;
    while (note > 108) note -= 12;
    return note;
}

void RD_Midi::init(uint8_t rxChannel) {
    setRxChannel(rxChannel);
}

void RD_Midi::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
    if (!channelMatches(ch)) return;

    // Velocity 0 is treated as Note Off per MIDI spec.
    if (vel == 0) {
        ipc_send_dx_note_off(foldToRange(note));
        return;
    }

    ipc_send_dx_note_on(foldToRange(note), vel);
}

void RD_Midi::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
    (void)vel; // release velocity is not modeled by the RD engine.
    if (!channelMatches(ch)) return;

    ipc_send_dx_note_off(foldToRange(note));
}

void RD_Midi::onControlChange(uint8_t cc, uint8_t val, uint8_t ch) {
    if (!channelMatches(ch)) return;

    switch (cc) {
        // Damper pedal (sustain): >= 64 = ON.
        case 64:
            ipc_send_dx_cc(cc, val);
            break;

        // Tremolo switch: 0..63 OFF, 64..127 ON.
        case 92:
            ipc_send_dx_param(RD_PARAM_TREM_ON, val >= 64 ? 255 : 0);
            break;

        // Chorus switch: 0..63 OFF, 64..127 ON.
        case 93:
            ipc_send_dx_param(RD_PARAM_CHORUS_ON, val >= 64 ? 255 : 0);
            break;

        // Phaser switch: 0..63 OFF, 64..127 ON.
        case 95:
            ipc_send_dx_param(RD_PARAM_PHASER_ON, val >= 64 ? 255 : 0);
            break;

        // Reset All Controllers: hold -> off, pitch bend -> center.
        // Modulation -> 0 is n/a (engine has no vibrato path).
        case 121:
            ipc_send_dx_cc(64, 0);
            ipc_send_dx_pitch_bend(8192);
            break;

        // All Notes Off.
        case 123:
            ipc_send_dx_cc(cc, val);
            break;

        default: {
            // Panel parameters: the same table the front panel sends through,
            // so the assignment can never drift apart between the two
            // directions. The explicit cases above win, so the three switches
            // keep their dedicated handling.
            const int pid = rdParamForCc(cc);
            if (pid >= 0) {
                // Engine wire format is 0..255; toggles are 0 or 255, with the
                // usual 0..63 OFF / 64..127 ON split.
                const bool isToggle = (pid == RD_PARAM_CHORUS_ON ||
                                       pid == RD_PARAM_TREM_ON ||
                                       pid == RD_PARAM_PHASER_ON ||
                                       pid == RD_PARAM_DAC_FILTER_ON);
                const uint8_t wire = isToggle
                                     ? (val >= 64 ? 255 : 0)
                                     : (uint8_t)(((int)val * 255 + 63) / 127);
                ipc_send_dx_param((uint8_t) pid, wire);
            }
            break;
        }
    }
}

void RD_Midi::onProgramChange(uint8_t program, uint8_t ch) {
    if (!channelMatches(ch)) return;

    // Chart says 0..63; we fold it onto whatever this build ships, which is
    // sixteen patches normally and eight for a single-machine build.
    ipc_send_dx_param(RD_PARAM_INSTRUMENT,
                      static_cast<uint16_t>(program % RD_PATCH_COUNT));
}

void RD_Midi::onPitchBend(uint16_t value, uint8_t ch) {
    if (!channelMatches(ch)) return;

    // Clamp to valid 14-bit range.
    if (value > 16383) value = 16383;

    ipc_send_dx_pitch_bend(value);
}

uint8_t RD_Midi::getRxChannel() const {
    return rxChannel_;
}

void RD_Midi::setRxChannel(uint8_t ch) {
    // Clamp to valid range: 0..15 or Omni (0x10).
    if (ch > RD_MIDI_OMNI) {
        ch = RD_MIDI_OMNI;
    }
    rxChannel_ = ch;
}
