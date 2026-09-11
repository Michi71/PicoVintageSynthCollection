// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// src/midi_reface.cpp
//
// PicoFaceDX — the reface DX on top of the shared reface MIDI layer.

#include "midi_reface.h"
#include "ipc.h"
#include "DX_Synth_Bridge.h"
#include "dx_patch_stage.h"
#include "presets.h"
#include <string.h>

extern "C" int ui_get_octave(void);

namespace {

// Byte 10 is the firmware version, read by the host as 1.0 + n/10. The ESP32
// reference reports 0x03 (1.3), the last reface DX firmware; editors may gate
// on a minimum version, so report the same rather than 1.0.
const uint8_t kIdentityReply[15] = {
    0xF0, 0x7E, 0x7F, 0x06, 0x02,
    0x43, 0x00, 0x41, 0x53, 0x06,
    0x03, 0x00, 0x00, 0x7F, 0xF7
};

const picoface::RefaceMidiBase::Model kModel = { 0x05, true, kIdentityReply, sizeof kIdentityReply };

} // namespace

RefaceMidi::RefaceMidi() : picoface::RefaceMidiBase(kModel) {}

void RefaceMidi::init(DX_Synth_Bridge* dx) {
    _dx = dx;
    resetSystem();
    _sys[SYS_TEMPO_0]      = 0;
    _sys[SYS_TEMPO_1]      = 0x78;
    _sys[SYS_LCD_CONTRAST] = 20;
    _sys[SYS_PEDAL_MODEL]  = 1;
}

int RefaceMidi::uiOctave() const { return ui_get_octave(); }

void RefaceMidi::engineNoteOn(uint8_t note, uint8_t vel) { ipc_send_dx_note_on(note, vel); }
void RefaceMidi::engineNoteOff(uint8_t note)             { ipc_send_dx_note_off(note); }
void RefaceMidi::enginePitchBend(uint16_t bend14)        { ipc_send_dx_pitch_bend(bend14); }
void RefaceMidi::engineAllSoundOff()                     { ipc_send_dx_cc(120, 0); }
void RefaceMidi::engineAllNotesOff()                     { ipc_send_dx_cc(123, 0); }

void RefaceMidi::resetControllers() {
    ipc_send_dx_pitch_bend(8192);
    ipc_send_dx_cc(1, 0);
    ipc_send_dx_cc(11, 127);
    ipc_send_dx_cc(64, 0);
}

// The 16-bit tune goes to the producer as it came off the wire; ipc_apply()
// in DX_Instrument.cpp turns it into cents and semitones for the engine.
void RefaceMidi::engineMasterTune(uint16_t raw) { ipc_send_dx_master_tune(raw); }

void RefaceMidi::sanitizeSystemBlock() {
    // Records written before the shared layer carry a master tune of four
    // zero nibbles: that build seeded the tune with 0 instead of the data
    // list's centre 1024 and never applied it at boot - until the record was
    // restored, which applied it as -102.4 cents. Read such a record as
    // "centred", which is what the device sounded like before its first
    // settings write.
    if (masterTuneRaw() == 0) {
        _sys[SYS_TUNE_0] = (kMasterTuneCentre >> 12) & 0x0F;
        _sys[SYS_TUNE_1] = (kMasterTuneCentre >> 8) & 0x0F;
        _sys[SYS_TUNE_2] = (kMasterTuneCentre >> 4) & 0x0F;
        _sys[SYS_TUNE_3] = kMasterTuneCentre & 0x0F;
    }
}

void RefaceMidi::engineControlChange(uint8_t cc, uint8_t val) {
    if (cc == 121) { resetControllers(); return; }

    // CC80 (algorithm) and CC85-90/102-119 (operator quick-edit) are gated by
    // the SYSTEM "MIDI Control" setting on real reface DX hardware; everything
    // else (mod wheel, volume, expression, sustain, all-sound-off/all-notes-off)
    // is always active. Confirmed against the official Yamaha Data List.
    const bool isVoiceEditCC = (cc == 80) || (cc >= 85 && cc <= 90) || (cc >= 102 && cc <= 119);
    if (isVoiceEditCC && !midiControlEnabled()) return;

    ipc_send_dx_cc(cc, val);
}

void RefaceMidi::engineProgramChange(uint8_t program) {
    if (program >= DX_NPRESETS) return;
    preset_set_current(program);
    preset_stage(program);
}

// The real reface DX implements Program Change 0-31 (4 banks of 8); this
// build now matches that fully with 32 real factory presets (DX_NPRESETS=32,
// see doc/PRESETS.md). Not gated by the MIDI Control setting -- like notes.
void RefaceMidi::txProgram(int preset) {
    if (preset < 0) preset = 0;
    if (preset > DX_NPRESETS - 1) preset = DX_NPRESETS - 1;
    txProgramChange((uint8_t) preset);
}

// ===========================================================================
// Voice blocks (reface DX MIDI spec, ported from
// tools/refacedx/RDX-Reface-DX-emu/RDX/RDX_Midi.h)
// ===========================================================================

// Writes go through the ring: the control side must not mutate the live engine
// patch, or a SysEx bulk dump would land in the middle of a rendered block.
void RefaceMidi::applyCommonParam(uint8_t addr, uint8_t value) {
    if (addr >= COMMON_BLOCK_SIZE) return;
    ipc_send_dx_raw_write(1, addr, value);   // blockSel 1 = common
}

void RefaceMidi::applyOperatorParam(uint8_t opNum, uint8_t addr, uint8_t value) {
    if (opNum >= 4 || addr >= OPERATOR_BLOCK_SIZE) return;
    ipc_send_dx_raw_write((uint8_t) (2 + opNum), addr, value);   // blockSel 2..5 = operator 0..3
}

// Reads are allowed directly (read-only access to the live patch from the
// control side is an accepted convention here, matching DX_Controller's usage).
uint8_t RefaceMidi::readCommonParam(uint8_t addr) const {
    if (!_dx || addr >= COMMON_BLOCK_SIZE) return 0;
    return reinterpret_cast<const uint8_t*>(&_dx->patch().common)[addr];
}

uint8_t RefaceMidi::readOperatorParam(uint8_t opNum, uint8_t addr) const {
    if (!_dx || opNum >= 4 || addr >= OPERATOR_BLOCK_SIZE) return 0;
    return reinterpret_cast<const uint8_t*>(&_dx->patch().ops[opNum])[addr];
}

void RefaceMidi::applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
    if (len < 1) return;
    if (ah == COMMON_ADDR_H) applyCommonParam(al, data[0]);
    else if (ah == OPERATOR_ADDR_H && am < 4) applyOperatorParam(am, al, data[0]);
}

uint8_t RefaceMidi::readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) {
    if (ah == COMMON_ADDR_H && am == 0x00 && al < COMMON_BLOCK_SIZE) {
        out[0] = readCommonParam(al);
        return 1;
    }
    if (ah == OPERATOR_ADDR_H && am < 4 && al < OPERATOR_BLOCK_SIZE) {
        out[0] = readOperatorParam(am, al);
        return 1;
    }
    return 0;
}

void RefaceMidi::onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
    RDX_Patch& staged = dx_patch_stage();
    if (ah == COMMON_ADDR_H && am == 0x00 && al == 0x00 && len == COMMON_BLOCK_SIZE) {
        memcpy(&staged.common, data, COMMON_BLOCK_SIZE);
    } else if (ah == OPERATOR_ADDR_H && am < 4 && al == 0x00 && len == OPERATOR_BLOCK_SIZE) {
        memcpy(&staged.ops[am], data, OPERATOR_BLOCK_SIZE);
    } else if (ah == 0x0E && am == 0x0F) {
        // bulk header: seed the staging area with the current live patch so a
        // partial dump (e.g. common block only) doesn't clobber unrelated fields
        // with stale staging-buffer leftovers from an earlier preset load.
        if (_dx) staged = _dx->patch();
    } else if (ah == 0x0F && am == 0x0F) {
        // bulk footer: patch fully staged (common + all 4 operator blocks), apply now
        ipc_send_dx_patch_apply();
    }
}

void RefaceMidi::txCommonBlock() {
    uint8_t common[COMMON_BLOCK_SIZE];
    for (uint8_t i = 0; i < COMMON_BLOCK_SIZE; i++) common[i] = readCommonParam(i);
    txBulkBlock(COMMON_ADDR_H, 0x00, 0x00, common, COMMON_BLOCK_SIZE);
}

void RefaceMidi::txOperatorBlock(uint8_t opNum) {
    if (opNum >= 4) return;
    uint8_t opBuf[OPERATOR_BLOCK_SIZE];
    for (uint8_t i = 0; i < OPERATOR_BLOCK_SIZE; i++) opBuf[i] = readOperatorParam(opNum, i);
    txBulkBlock(OPERATOR_ADDR_H, opNum, 0x00, opBuf, OPERATOR_BLOCK_SIZE);
}

void RefaceMidi::txVoiceBulk() {
    txBulkBlock(0x0E, 0x0F, 0x00, nullptr, 0);   // bulk header
    txCommonBlock();
    for (uint8_t op = 0; op < 4; op++) txOperatorBlock(op);
    txBulkBlock(0x0F, 0x0F, 0x00, nullptr, 0);   // bulk footer
}

void RefaceMidi::onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) {
    if (ah == 0x0E && am == 0x0F && al == 0x00) {
        txVoiceBulk();
    } else if (ah == COMMON_ADDR_H && am == 0x00 && al == 0x00) {
        txCommonBlock();
    } else if (ah == OPERATOR_ADDR_H && am < 4 && al == 0x00) {
        txOperatorBlock(am);
    }
}

// Parameter Request. The Yamaha convention answers a parameter address with a
// single Parameter Change, but editors also use this command with a block base
// address to read back a whole voice, and the ESP32 reference (RDX_Midi.h,
// case 0x3) replies to those with full bulk blocks. Serve both: offset 0 of a
// block address returns the block, every other address the single value.
void RefaceMidi::onParamRequest(uint8_t ah, uint8_t am, uint8_t al) {
    if (ah == 0x0E && am == 0x0F && al == 0x00) {
        txVoiceBulk();
    } else if (ah == COMMON_ADDR_H && am == 0x00 && al == 0x00) {
        txCommonBlock();
    } else if (ah == OPERATOR_ADDR_H && am < 4 && al == 0x00) {
        txOperatorBlock(am);
    } else {
        txParamChange(ah, am, al);
    }
}
