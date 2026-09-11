// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/midi_reface.h
//
// PicoFaceYC — Yamaha reface YC compatible MIDI layer. The reface dialect
// itself - SYSTEM block, active sensing, channel filter, exclusive framing,
// transmit - is picoface::RefaceMidiBase in the core; this is the YC on top
// of it: the TG block (base 30 00 00, 22 bytes) with the panel's drawbars,
// percussion, vibrato/chorus, rotary, effects, the CC map, and the TG bulk
// dump an editor asks for. Engine mutations go through the same-core ring
// (ipc.h).
//
// Model byte, identity reply and block layout follow the Yamaha reface
// CS/DX/CP/YC Data List (PO-B0, 04/2016); Yamaha's own Soundmondo editor
// carries the same bytes (see midi_reface.cpp).

#ifndef MIDI_REFACE_H
#define MIDI_REFACE_H
#include <stdint.h>

#include "picoface/reface_midi.h"

class YC_Synth_Bridge;

class RefaceMidi final : public picoface::RefaceMidiBase {
public:
    // Tone generator block, base address 30 00 00. Byte offsets as in the
    // data list's "MIDI Parameter Change Table (Tone Generator)"; 0x01, 0x14
    // and 0x15 are reserved and read as 0.
    enum TgAddr : uint8_t {
        TG_VOLUME        = 0x00,   // "can be set only via MIDI" - no CC, no panel knob on the original
        TG_WAVE          = 0x02,   // 0..4  H V F A Y
        TG_FOOTAGE_16    = 0x03,   // 0..6, nine in a row down to
        TG_FOOTAGE_1     = 0x0B,
        TG_VIBCHO_SELECT = 0x0C,   // 0..1
        TG_VIBCHO_DEPTH  = 0x0D,   // 0..4
        TG_PERC_ON       = 0x0E,   // 0..1
        TG_PERC_TYPE     = 0x0F,   // 0..1
        TG_PERC_LENGTH   = 0x10,   // 0..4
        TG_ROTARY_SPEED  = 0x11,   // 0..3  OFF STOP SLOW FAST
        TG_DISTORTION    = 0x12,   // 0..127
        TG_REVERB        = 0x13,   // 0..127
        TG_BLOCK_SIZE    = 0x16    // 22 bytes, bulk byte count 26
    };

    RefaceMidi();

    void init(YC_Synth_Bridge* bridge);

    // Front panel -> MIDI OUT (CC), gated by the MIDI Control setting.
    void txPanelMirror(uint8_t param_id, uint8_t internalValue);
    void txRotaryMirror(uint8_t speed);

    // The TG block as a bulk dump: header, block, footer - what an editor
    // gets for a Dump Request on the bulk header address 0E 0F 00.
    void txTgBulk();
    void txTgBlock();

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
    void onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) override;
    void onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;

    void resetControllers();

    static uint8_t quantize2(uint8_t val);
    static uint8_t quantize5(uint8_t val);
    static uint8_t quantize7(uint8_t val);
    static uint8_t quantizeRotary(uint8_t val);
    static uint8_t quantizeWave(uint8_t val);
    void applyTgParam(uint8_t addrLow, uint8_t value);
    uint8_t readTgParam(uint8_t addrLow) const;
    void readTgBlock(uint8_t* out) const;

    YC_Synth_Bridge* _bridge = nullptr;
};

#endif // MIDI_REFACE_H
