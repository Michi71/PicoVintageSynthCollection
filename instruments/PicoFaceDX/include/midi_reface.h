// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef __MIDI_REFACE_H__
#define __MIDI_REFACE_H__

//
// midi_reface.h
//
// Yamaha reface DX compatible MIDI layer. The reface dialect itself - SYSTEM
// block, active sensing, channel filter, exclusive framing, transmit - is
// picoface::RefaceMidiBase in the core; this is the DX on top of it:
//
//   - Common block (base 30 00 00, mirrors RDX_Common, 38 bytes).
//   - Operator blocks (base 31 <opNum> 00, mirror RDX_OpParams, 28 bytes each).
//   - Bulk Dump / Dump Request / Parameter Request serving whole voices.
//   - The CC map, voice-edit CCs gated by the SYSTEM "MIDI Control" setting.
//
// Engine mutations are forwarded through the same-core ring (ipc.h) so they
// land at the next block boundary. Implementation lives in src/midi_reface.cpp.
//

#include <stdint.h>

#include "picoface/reface_midi.h"

class DX_Synth_Bridge; // engine, Core 0 image; mutations via ipc.h

class RefaceMidi final : public picoface::RefaceMidiBase {
public:
    // The DX's own SYSTEM addresses; the shared ones are in the base. Field
    // order/offsets mirror dx_engine/RDX_Types.h's RDX_System byte-for-byte,
    // so getSystemBlock()/loadSystemBlock() may reinterpret_cast to/from RDX_System.
    enum DxSysAddr : uint8_t {
        SYS_TEMPO_0        = 0x08,
        SYS_TEMPO_1        = 0x09,
        SYS_LCD_CONTRAST   = 0x0A,
        SYS_PEDAL_MODEL    = 0x0B
    };

    // Common/Operator block address scheme (reface DX MIDI spec).
    // Ported from ESP32 reference tools/refacedx/RDX-Reface-DX-emu/RDX/RDX_Midi.h.
    static constexpr uint8_t COMMON_ADDR_H       = 0x30;
    static constexpr uint8_t OPERATOR_ADDR_H     = 0x31;
    static constexpr uint8_t COMMON_BLOCK_SIZE   = 38; // sizeof(RDX_Common)
    static constexpr uint8_t OPERATOR_BLOCK_SIZE = 28; // sizeof(RDX_OpParams)

    RefaceMidi();

    void init(DX_Synth_Bridge* dx);   // bind engine bridge, seed SYSTEM defaults
    void txProgram(int preset);       // -> Program Change 0..DX_NPRESETS-1

private:
    // RefaceMidiBase hooks
    void engineNoteOn(uint8_t note, uint8_t vel) override;
    void engineNoteOff(uint8_t note) override;
    void enginePitchBend(uint16_t bend14) override;
    void engineControlChange(uint8_t cc, uint8_t val) override;
    void engineProgramChange(uint8_t program) override;
    void engineAllSoundOff() override;
    void engineAllNotesOff() override;
    void engineMasterTune(uint16_t raw) override;
    int  uiOctave() const override;
    void sanitizeSystemBlock() override;
    void applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;
    uint8_t readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) override;
    void onParamRequest(uint8_t ah, uint8_t am, uint8_t al) override;
    void onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) override;
    void onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;

    void     resetControllers();                                    // PB center, sustain off, soft pedal off
    void     applyCommonParam(uint8_t addr, uint8_t value);
    void     applyOperatorParam(uint8_t opNum, uint8_t addr, uint8_t value);
    uint8_t  readCommonParam(uint8_t addr) const;
    uint8_t  readOperatorParam(uint8_t opNum, uint8_t addr) const;
    void     txCommonBlock();                                       // 30 00 00 as one bulk block
    void     txOperatorBlock(uint8_t opNum);                        // 31 <op> 00 as one bulk block
    void     txVoiceBulk();                                         // header + common + 4 operators + footer

    DX_Synth_Bridge* _dx = nullptr;
};

#endif // __MIDI_REFACE_H__
