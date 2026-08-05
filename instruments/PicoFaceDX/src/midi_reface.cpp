// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// src/midi_reface.cpp
//
// PicoFaceDX — Yamaha reface DX compatible MIDI layer, implementation.

#include "midi_reface.h"
#include "ipc.h"
#include "DX_Synth_Bridge.h"
#include "dx_patch_stage.h"
#include "presets.h"
#include "midi_serial.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <string.h>

extern "C" int ui_get_octave(void);

static inline uint32_t nowMs() {
    return to_ms_since_boot(get_absolute_time());
}

void RefaceMidi::init(DX_Synth_Bridge* dx) {
    _dx = dx;
    memset(_sys, 0, SYS_BLOCK_SIZE);
    _sys[SYS_TX_CH] = 0;
    _sys[SYS_RX_CH] = RX_CH_ALL;
    _sys[SYS_TUNE_0] = 0;
    _sys[SYS_TUNE_1] = 0;
    _sys[SYS_TUNE_2] = 0;
    _sys[SYS_TUNE_3] = 0;
    _sys[SYS_LOCAL] = 1;
    _sys[SYS_TRANSPOSE] = 0x40;
    _sys[SYS_TEMPO_0] = 0;
    _sys[SYS_TEMPO_1] = 0x78;
    _sys[SYS_LCD_CONTRAST] = 20;
    _sys[SYS_PEDAL_MODEL] = 1;
    _sys[SYS_AUTO_POWER_OFF] = 1;
    _sys[SYS_SPEAKER_ON] = 1;
    _sys[SYS_MIDI_CONTROL] = 1;
    _senseActive = false;
    _lastRxMs = nowMs();
    _lastTxSenseMs = nowMs();
}

void RefaceMidi::tick() {
    uint32_t now = nowMs();
    if (now - _lastTxSenseMs >= 200) { uint8_t fe = 0xFE; txBytes(&fe, 1); _lastTxSenseMs = now; }
    if (_senseActive && (now - _lastRxMs) > 350) { _senseActive = false; allSoundOff(); allNotesOff(); }
}

void RefaceMidi::notifyActivity() { _lastRxMs = nowMs(); }

void RefaceMidi::onRealtime(uint8_t s) { if (s == 0xFE) { _senseActive = true; } }

bool RefaceMidi::channelOk(uint8_t ch) const {
    uint8_t rx = _sys[SYS_RX_CH];
    return (rx == RX_CH_ALL) || ((ch & 0x0F) == rx);
}

int RefaceMidi::transposeNote(int note) const {
    note += (int)_sys[SYS_TRANSPOSE] - 0x40 + 12 * ui_get_octave();
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return note;
}

void RefaceMidi::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
    if (!channelOk(ch)) return;
    uint8_t tn = (uint8_t)transposeNote(note);
    if (vel == 0) { ipc_send_dx_note_off(tn); return; }
    ipc_send_dx_note_on(tn, vel);
}

void RefaceMidi::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
    (void)vel;
    if (!channelOk(ch)) return;
    uint8_t tn = (uint8_t)transposeNote(note);
    ipc_send_dx_note_off(tn);
}

void RefaceMidi::onPitchBend(uint16_t bend14, uint8_t ch) {
    if (!channelOk(ch)) return;
    ipc_send_dx_pitch_bend(bend14);
}

void RefaceMidi::allSoundOff() { ipc_send_dx_cc(120, 0); }
void RefaceMidi::allNotesOff() { ipc_send_dx_cc(123, 0); }

void RefaceMidi::resetControllers() {
    ipc_send_dx_pitch_bend(8192);
    ipc_send_dx_cc(1, 0);
    ipc_send_dx_cc(11, 127);
    ipc_send_dx_cc(64, 0);
}

void RefaceMidi::applyMasterTune() {
    // Reassemble the 16-bit tune value from 4 transmitted nibbles (official
    // Yamaha Data List: "1st bit 3-0: bit 15-12" etc.); Core 0 decodes this
    // into cents/semitones and applies it via DX_Synth_Bridge::setMasterTune
    // (see ipc_apply() in main.cpp).
    uint16_t raw = ((uint16_t)(_sys[SYS_TUNE_0] & 0x0F) << 12) |
                   ((uint16_t)(_sys[SYS_TUNE_1] & 0x0F) << 8)  |
                   ((uint16_t)(_sys[SYS_TUNE_2] & 0x0F) << 4)  |
                   (uint16_t)(_sys[SYS_TUNE_3] & 0x0F);
    ipc_send_dx_master_tune(raw);
}

void RefaceMidi::setRxChannel(uint8_t ch) {
    if (ch > RX_CH_ALL) ch = RX_CH_ALL;
    _sys[SYS_RX_CH] = ch;
}

void RefaceMidi::txBytes(const uint8_t* b, uint16_t n) {
    if (tud_midi_mounted()) { tud_midi_stream_write(0, b, n); }
    // Everything the reface layer sends - active sensing, panel CCs, SysEx
    // replies - goes out the DIN socket as well. Unconditional: unlike USB
    // there is nothing to enumerate, and a receiver that is not plugged in
    // simply does not listen.
    midiSerial().write(b, n);
}

void RefaceMidi::txCC(uint8_t cc, uint8_t val) {
    uint8_t m[3] = { (uint8_t)(0xB0 | (_sys[SYS_TX_CH] & 0x0F)), (uint8_t)(cc & 0x7F), (uint8_t)(val & 0x7F) };
    txBytes(m, 3);
}

void RefaceMidi::getSystemBlock(uint8_t* dst) const { memcpy(dst, _sys, SYS_BLOCK_SIZE); }

void RefaceMidi::loadSystemBlock(const uint8_t* src) {
    memcpy(_sys, src, SYS_BLOCK_SIZE);
    if (_sys[SYS_RX_CH] > RX_CH_ALL) _sys[SYS_RX_CH] = RX_CH_ALL;
    _sys[SYS_LOCAL] = _sys[SYS_LOCAL] ? 1 : 0;
    _sys[SYS_MIDI_CONTROL] = _sys[SYS_MIDI_CONTROL] ? 1 : 0;
    _sys[SYS_AUTO_POWER_OFF] = _sys[SYS_AUTO_POWER_OFF] ? 1 : 0;
    _sys[SYS_SPEAKER_ON] = _sys[SYS_SPEAKER_ON] ? 1 : 0;
    applyMasterTune();
}

void RefaceMidi::onControlChange(uint8_t cc, uint8_t val, uint8_t ch) {
    if (!channelOk(ch)) return;

    switch (cc) {
        case 121: resetControllers(); return;
        case 124: allNotesOff(); _sys[SYS_RX_CH] = 0; return;
        case 125: allNotesOff(); _sys[SYS_RX_CH] = RX_CH_ALL; return;
        default: break;
    }

    // CC80 (algorithm) and CC85-90/102-119 (operator quick-edit) are gated by
    // the SYSTEM "MIDI Control" setting on real reface DX hardware; everything
    // else (mod wheel, volume, expression, sustain, all-sound-off/all-notes-off)
    // is always active. Confirmed against the official Yamaha Data List.
    bool isVoiceEditCC = (cc == 80) || (cc >= 85 && cc <= 90) || (cc >= 102 && cc <= 119);
    if (isVoiceEditCC && !midiControlEnabled()) return;

    ipc_send_dx_cc(cc, val);
}

void RefaceMidi::onProgramChange(uint8_t program, uint8_t ch) {
    if (!channelOk(ch)) return;
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
    uint8_t msg[2];
    msg[0] = 0xC0 | (_sys[SYS_TX_CH] & 0x0F);
    msg[1] = (uint8_t)preset;
    txBytes(msg, 2);
}

// ===========================================================================
// SysEx dispatch (reface DX MIDI spec, ported from
// tools/refacedx/RDX-Reface-DX-emu/RDX/RDX_Midi.h)
// ===========================================================================
void RefaceMidi::onSysEx(const uint8_t* d, uint16_t len)
{
    if (len >= 6 && d[0] == 0xF0 && d[1] == 0x7E && d[3] == 0x06 && d[4] == 0x01) {
        txIdentityReply();
        return;
    }
    if (len >= 10 && d[0] == 0xF0 && d[1] == 0x43 && d[3] == 0x7F && d[4] == 0x1C) {
        handleYamahaSysEx(d, len);
    }
}

void RefaceMidi::handleYamahaSysEx(const uint8_t* d, uint16_t len)
{
    uint8_t cmd = d[2] & 0x70;
    switch (cmd) {
    case 0x10: {
        if (len < 11 || d[5] != 0x05) return;
        uint8_t ah = d[6], am = d[7], al = d[8];
        uint16_t dlen = len - 10;
        const uint8_t* data = &d[9];
        if (ah == 0x00 && am == 0x00) {
            applySystemParam(al, data, dlen);
        } else if (ah == COMMON_ADDR_H && dlen >= 1) {
            applyCommonParam(al, data[0]);
        } else if (ah == OPERATOR_ADDR_H && am < 4 && dlen >= 1) {
            applyOperatorParam(am, al, data[0]);
        }
        break;
    }
    case 0x30: {
        if (len < 10 || d[5] != 0x05) return;
        handleParamRequest(d[6], d[7], d[8]);
        break;
    }
    case 0x20: {
        if (len < 10 || d[5] != 0x05) return;
        handleDumpRequest(d[6], d[7], d[8]);
        break;
    }
    case 0x00: {
        handleBulkDump(d, len);
        break;
    }
    default:
        break;
    }
}

void RefaceMidi::applySystemParam(uint8_t addr, const uint8_t* data, uint16_t len) {
    if (len < 1 || addr >= SYS_BLOCK_SIZE) return;
    uint8_t v = data[0] & 0x7F;
    switch (addr) {
        case SYS_TX_CH:
            if (v <= 0x0F) _sys[addr] = v;
            break;
        case SYS_RX_CH:
            if (v <= RX_CH_ALL) _sys[addr] = v;
            break;
        case SYS_TUNE_0:
        case SYS_TUNE_1:
        case SYS_TUNE_2:
        case SYS_TUNE_3:
            _sys[addr] = v;
            applyMasterTune();
            break;
        case SYS_LOCAL:
        case SYS_AUTO_POWER_OFF:
        case SYS_SPEAKER_ON:
        case SYS_MIDI_CONTROL:
            _sys[addr] = (v != 0) ? 1 : 0;
            break;
        case SYS_TRANSPOSE:
            if (v < 0x34) v = 0x34;
            if (v > 0x4C) v = 0x4C;
            _sys[addr] = v;
            break;
        default:
            _sys[addr] = v;
            break;
    }
}

// Writes go through the ring: the control side must not mutate the live engine
// patch, or a SysEx bulk dump would land in the middle of a rendered block.
void RefaceMidi::applyCommonParam(uint8_t addr, uint8_t value) {
    if (addr >= COMMON_BLOCK_SIZE) return;
    ipc_send_dx_raw_write(1, addr, value);   // blockSel 1 = common
}

void RefaceMidi::applyOperatorParam(uint8_t opNum, uint8_t addr, uint8_t value) {
    if (opNum >= 4 || addr >= OPERATOR_BLOCK_SIZE) return;
    ipc_send_dx_raw_write((uint8_t)(2 + opNum), addr, value);   // blockSel 2..5 = operator 0..3
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

void RefaceMidi::txIdentityReply()
{
    // Byte 10 is the firmware version, read by the host as 1.0 + n/10. The
    // ESP32 reference reports 0x03 (1.3), the last reface DX firmware; editors
    // may gate on a minimum version, so report the same rather than 1.0.
    static const uint8_t r[15] = {
        0xF0, 0x7E, 0x7F, 0x06, 0x02,
        0x43, 0x00, 0x41, 0x53, 0x06,
        0x03, 0x00, 0x00, 0x7F, 0xF7
    };
    txBytes(r, sizeof(r));
}

void RefaceMidi::txParamChange(uint8_t ah, uint8_t am, uint8_t al)
{
    uint8_t buf[16];
    uint16_t n = 0;
    buf[n++] = 0xF0;
    buf[n++] = 0x43;
    buf[n++] = 0x10;
    buf[n++] = 0x7F;
    buf[n++] = 0x1C;
    buf[n++] = 0x05;
    buf[n++] = ah & 0x7F;
    buf[n++] = am & 0x7F;
    buf[n++] = al & 0x7F;

    if (ah == 0x00 && am == 0x00) {
        if (al < SYS_BLOCK_SIZE) {
            buf[n++] = _sys[al];
        } else {
            return;
        }
    } else if (ah == COMMON_ADDR_H && am == 0x00 && al < COMMON_BLOCK_SIZE) {
        buf[n++] = readCommonParam(al);
    } else if (ah == OPERATOR_ADDR_H && am < 4 && al < OPERATOR_BLOCK_SIZE) {
        buf[n++] = readOperatorParam(am, al);
    } else {
        return;
    }

    buf[n++] = 0xF7;
    txBytes(buf, n);
}

void RefaceMidi::handleBulkDump(const uint8_t* d, uint16_t len)
{
    if (len < 13) return;
    uint16_t bc = ((uint16_t)(d[5] & 0x7F) << 7) | (d[6] & 0x7F);
    if (bc < 4 || len != (uint16_t)(bc + 9)) return;
    if (d[7] != 0x05) return;
    uint32_t sum = 0;
    for (uint16_t i = 7; i < (uint16_t)(len - 1); i++) sum += d[i];
    if (sum & 0x7F) return;
    uint8_t ah = d[8], am = d[9], al = d[10];
    uint16_t dlen = bc - 4;
    const uint8_t* data = &d[11];

    RDX_Patch& staged = dx_patch_stage();
    if (ah == 0x00 && am == 0x00 && al == 0x00 && dlen == SYS_BLOCK_SIZE) {
        for (uint16_t i = 0; i < dlen; i++) applySystemParam((uint8_t)i, &data[i], 1);
    } else if (ah == COMMON_ADDR_H && am == 0x00 && al == 0x00 && dlen == COMMON_BLOCK_SIZE) {
        memcpy(&staged.common, data, COMMON_BLOCK_SIZE);
    } else if (ah == OPERATOR_ADDR_H && am < 4 && al == 0x00 && dlen == OPERATOR_BLOCK_SIZE) {
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

void RefaceMidi::txCommonBlock()
{
    uint8_t common[COMMON_BLOCK_SIZE];
    for (uint8_t i = 0; i < COMMON_BLOCK_SIZE; i++) common[i] = readCommonParam(i);
    txBulkBlock(COMMON_ADDR_H, 0x00, 0x00, common, COMMON_BLOCK_SIZE);
}

void RefaceMidi::txOperatorBlock(uint8_t opNum)
{
    if (opNum >= 4) return;
    uint8_t opBuf[OPERATOR_BLOCK_SIZE];
    for (uint8_t i = 0; i < OPERATOR_BLOCK_SIZE; i++) opBuf[i] = readOperatorParam(opNum, i);
    txBulkBlock(OPERATOR_ADDR_H, opNum, 0x00, opBuf, OPERATOR_BLOCK_SIZE);
}

void RefaceMidi::txVoiceBulk()
{
    txBulkBlock(0x0E, 0x0F, 0x00, nullptr, 0);   // bulk header
    txCommonBlock();
    for (uint8_t op = 0; op < 4; op++) txOperatorBlock(op);
    txBulkBlock(0x0F, 0x0F, 0x00, nullptr, 0);   // bulk footer
}

void RefaceMidi::handleDumpRequest(uint8_t ah, uint8_t am, uint8_t al)
{
    if (ah == 0x00 && am == 0x00 && al == 0x00) {
        txBulkBlock(0x00, 0x00, 0x00, _sys, SYS_BLOCK_SIZE);
    } else if (ah == 0x0E && am == 0x0F && al == 0x00) {
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
void RefaceMidi::handleParamRequest(uint8_t ah, uint8_t am, uint8_t al)
{
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

void RefaceMidi::txBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint8_t len)
{
    // 11 header bytes + up to COMMON_BLOCK_SIZE (38) data bytes + checksum + F7 = 51 max.
    uint8_t buf[64];
    uint16_t bc = (uint16_t)4 + len;
    uint16_t n = 0;

    buf[n++] = 0xF0;
    buf[n++] = 0x43;
    buf[n++] = 0x00;
    buf[n++] = 0x7F;
    buf[n++] = 0x1C;
    buf[n++] = (uint8_t)((bc >> 7) & 0x7F);
    buf[n++] = (uint8_t)(bc & 0x7F);
    buf[n++] = 0x05;
    buf[n++] = ah & 0x7F;
    buf[n++] = am & 0x7F;
    buf[n++] = al & 0x7F;

    uint32_t sum = 0x05 + (ah & 0x7F) + (am & 0x7F) + (al & 0x7F);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = data[i] & 0x7F;
        buf[n++] = b;
        sum += b;
    }

    buf[n++] = (uint8_t)((0x80 - (sum & 0x7F)) & 0x7F);
    buf[n++] = 0xF7;
    txBytes(buf, n);
}
