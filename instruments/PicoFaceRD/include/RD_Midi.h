// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// RD_Midi: Minimal MIDI front-end for the Roland MKS-20 / MK-80 emulation project.
// Runs exclusively on Core 1 of the RP2350. Parses incoming MIDI events and
// forwards the reduced event set (Note On, Note Off, Sustain CC64, Program
// Change) to Core 0 via the SIO-FIFO IPC helpers defined in "ipc.h", where
// RD_Synth_Bridge consumes them.
//
// Design intent: analogous to the RefaceMidi module of the DX project, but
// deliberately stripped down to only what the RD engine understands:
// notes, damper, FX switches (CC92/93/95), reset (CC121), pitch bend and
// continuous controllers, no dynamic allocation.

#ifndef RD_MIDI_H
#define RD_MIDI_H

#include <stdint.h>

// paramId used to carry a Program Change / instrument selection through IPC.
// Core 0 ipc_apply() maps this to bridge.setInstrument().
constexpr uint8_t RD_PARAM_INSTRUMENT = 0x7F;

// Special rxChannel value meaning "accept all channels" (Omni mode).
constexpr uint8_t RD_MIDI_OMNI = 0x10;

class RD_Midi {
public:
    // Initialize the receiver. rxChannel: 0..15 = channel 1..16,
    // RD_MIDI_OMNI (0x10) = Omni. Default is Omni.
    void init(uint8_t rxChannel = RD_MIDI_OMNI);

    // MIDI event entry points (channel is the raw MIDI channel 0..15).
    void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);
    void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);
    void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);
    // Returns the instrument index it sent to the engine, or -1 when the
    // channel did not match -- so the panel can follow without duplicating
    // the channel test.
    int  onProgramChange(uint8_t program, uint8_t ch);
    void onPitchBend(uint16_t value, uint8_t ch); // 0..16383, center 8192

    // Receiver channel accessors. setRxChannel clamps to 0..RD_MIDI_OMNI.
    uint8_t getRxChannel() const;
    void     setRxChannel(uint8_t ch);

private:
    // Returns true if the given MIDI channel should be accepted.
    inline bool channelMatches(uint8_t ch) const {
        return (rxChannel_ == RD_MIDI_OMNI) || (rxChannel_ == ch);
    }

    uint8_t rxChannel_ = RD_MIDI_OMNI;
};

#endif // RD_MIDI_H
