// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/midi_reface.h
//
// PicoFaceYC — Yamaha reface YC compatible MIDI layer. The reface dialect
// itself - SYSTEM block, active sensing, channel filter, exclusive framing,
// transmit - is picoface::RefaceMidiBase in the core; this is the YC on top
// of it: the TG block (base 30 00 00) with the panel's drawbars, percussion,
// vibrato/chorus, rotary, effects, and the CC map. Engine mutations go
// through the same-core ring (ipc.h).
//
// The model byte and the identity bytes are not verified against a real
// reface YC (see midi_reface.cpp); until they are, exclusive messages are
// accepted whatever model byte they carry.

#ifndef MIDI_REFACE_H
#define MIDI_REFACE_H
#include <stdint.h>

#include "picoface/reface_midi.h"

class YC_Synth_Bridge;

class RefaceMidi final : public picoface::RefaceMidiBase {
public:
    RefaceMidi();

    void init(YC_Synth_Bridge* bridge);

    // Front panel -> MIDI OUT (CC), gated by the MIDI Control setting.
    void txPanelMirror(uint8_t param_id, uint8_t internalValue);
    void txRotaryMirror(uint8_t speed);

private:
    // RefaceMidiBase hooks
    void engineNoteOn(uint8_t note, uint8_t vel) override;
    void engineNoteOff(uint8_t note) override;
    void enginePitchBend(uint16_t bend14) override;
    void engineControlChange(uint8_t cc, uint8_t val) override;
    void engineAllSoundOff() override;
    void engineAllNotesOff() override;
    int  uiOctave() const override;
    void applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;
    uint8_t readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) override;

    static uint8_t quantize2(uint8_t val);
    static uint8_t quantize5(uint8_t val);
    static uint8_t quantize7(uint8_t val);
    static uint8_t quantizeRotary(uint8_t val);
    static uint8_t quantizeWave(uint8_t val);
    void applyTgParam(uint8_t addrLow, uint8_t value);
    uint8_t readTgParam(uint8_t addrLow) const;

    YC_Synth_Bridge* _bridge = nullptr;
};

#endif // MIDI_REFACE_H
