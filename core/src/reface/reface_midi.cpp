// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// reface_midi.cpp - the shared part of the reface ports' MIDI layer, see the header.

#include "picoface/reface_midi.h"

#include "midi_output_usb.h"
#include "midi_serial.h"
#include "pico/stdlib.h"
#include <string.h>

namespace picoface {

static inline uint32_t nowMs() {
    return to_ms_since_boot(get_absolute_time());
}

void RefaceMidiBase::resetSystem() {
    memset(_sys, 0, SYS_BLOCK_SIZE);
    _sys[SYS_TX_CH]          = 0;
    _sys[SYS_RX_CH]          = RX_CH_ALL;
    _sys[SYS_TUNE_0]         = (kMasterTuneCentre >> 12) & 0x0F;
    _sys[SYS_TUNE_1]         = (kMasterTuneCentre >> 8) & 0x0F;
    _sys[SYS_TUNE_2]         = (kMasterTuneCentre >> 4) & 0x0F;
    _sys[SYS_TUNE_3]         = kMasterTuneCentre & 0x0F;
    _sys[SYS_LOCAL]          = 1;
    _sys[SYS_TRANSPOSE]      = 0x40;
    _sys[SYS_AUTO_POWER_OFF] = 1;
    _sys[SYS_SPEAKER]        = 1;
    _sys[SYS_MIDI_CONTROL]   = 1;
    _senseActive   = false;
    _lastRxMs      = nowMs();
    _lastTxSenseMs = nowMs();
}

// --- active sensing -----------------------------------------------------------

void RefaceMidiBase::tick() {
    const uint32_t now = nowMs();
    if (now - _lastTxSenseMs >= 200) {
        const uint8_t fe = 0xFE;
        txBytes(&fe, 1);
        _lastTxSenseMs = now;
    }
    if (_senseActive && (now - _lastRxMs) > 350) {
        _senseActive = false;
        engineAllSoundOff();
        engineAllNotesOff();
    }
}

void RefaceMidiBase::notifyActivity() { _lastRxMs = nowMs(); }

void RefaceMidiBase::onRealtime(uint8_t status) {
    if (status == 0xFE) {
        _senseActive = true;
        _lastRxMs = nowMs();
    }
}

// --- channel voice messages -----------------------------------------------------

bool RefaceMidiBase::channelOk(uint8_t ch) const {
    const uint8_t rx = _sys[SYS_RX_CH];
    return (rx == RX_CH_ALL) || ((ch & 0x0F) == rx);
}

int RefaceMidiBase::transposeNote(int note) const {
    note += (int) _sys[SYS_TRANSPOSE] - 0x40 + 12 * uiOctave();
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return note;
}

void RefaceMidiBase::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
    if (!channelOk(ch)) return;
    const uint8_t tn = (uint8_t) transposeNote(note);
    if (vel == 0) { engineNoteOff(tn); return; }
    engineNoteOn(tn, vel);
}

void RefaceMidiBase::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
    (void) vel;
    if (!channelOk(ch)) return;
    engineNoteOff((uint8_t) transposeNote(note));
}

void RefaceMidiBase::onPitchBend(uint16_t bend14, uint8_t ch) {
    if (!channelOk(ch)) return;
    enginePitchBend(bend14);
}

void RefaceMidiBase::onControlChange(uint8_t cc, uint8_t val, uint8_t ch) {
    if (!channelOk(ch)) return;
    switch (cc) {
    case 124: engineAllNotesOff(); _sys[SYS_RX_CH] = 0;         return;   // omni off: channel 1
    case 125: engineAllNotesOff(); _sys[SYS_RX_CH] = RX_CH_ALL; return;   // omni on
    default:  engineControlChange(cc, val);                     return;
    }
}

void RefaceMidiBase::onProgramChange(uint8_t program, uint8_t ch) {
    if (!channelOk(ch)) return;
    engineProgramChange(program);
}

// --- SYSTEM block -------------------------------------------------------------------

void RefaceMidiBase::setRxChannel(uint8_t ch) {
    if (ch > RX_CH_ALL) ch = RX_CH_ALL;
    _sys[SYS_RX_CH] = ch;
}

uint16_t RefaceMidiBase::masterTuneRaw() const {
    // The data list sends the 16-bit tune as four nibbles, bit 15-12 first.
    return (uint16_t) (((uint16_t) (_sys[SYS_TUNE_0] & 0x0F) << 12) |
                       ((uint16_t) (_sys[SYS_TUNE_1] & 0x0F) << 8)  |
                       ((uint16_t) (_sys[SYS_TUNE_2] & 0x0F) << 4)  |
                       (uint16_t) (_sys[SYS_TUNE_3] & 0x0F));
}

void RefaceMidiBase::applySystemParam(uint8_t addr, const uint8_t* data, uint16_t len) {
    if (len < 1 || addr >= SYS_BLOCK_SIZE) return;
    const uint8_t v = data[0] & 0x7F;
    switch (addr) {
    case SYS_TX_CH:
        if (v <= 0x0F) _sys[addr] = v;
        break;
    case SYS_RX_CH:
        if (v <= RX_CH_ALL) _sys[addr] = v;
        break;
    case SYS_TUNE_0:
        // The tune is one four-byte parameter; an editor writes all four
        // nibbles in one Parameter Change, a single-byte write moves one.
        if (len >= 4) {
            _sys[SYS_TUNE_0] = data[0] & 0x0F;
            _sys[SYS_TUNE_1] = data[1] & 0x0F;
            _sys[SYS_TUNE_2] = data[2] & 0x0F;
            _sys[SYS_TUNE_3] = data[3] & 0x0F;
        } else {
            _sys[addr] = v & 0x0F;
        }
        applyMasterTune();
        break;
    case SYS_TUNE_1:
    case SYS_TUNE_2:
    case SYS_TUNE_3:
        _sys[addr] = v & 0x0F;
        applyMasterTune();
        break;
    case SYS_LOCAL:
    case SYS_AUTO_POWER_OFF:
    case SYS_SPEAKER:
    case SYS_MIDI_CONTROL:
        _sys[addr] = (v != 0) ? 1 : 0;
        break;
    case SYS_TRANSPOSE:
        _sys[addr] = (v < 0x34) ? 0x34 : (v > 0x4C) ? 0x4C : v;
        break;
    default:
        if (addr >= 0x08 && addr <= 0x0B) applyModelSystemParam(addr, v);
        else _sys[addr] = v;
        break;
    }
}

void RefaceMidiBase::getSystemBlock(uint8_t* dst) const { memcpy(dst, _sys, SYS_BLOCK_SIZE); }

void RefaceMidiBase::loadSystemBlock(const uint8_t* src) {
    memcpy(_sys, src, SYS_BLOCK_SIZE);
    if (_sys[SYS_RX_CH] > RX_CH_ALL) _sys[SYS_RX_CH] = RX_CH_ALL;
    if (_sys[SYS_TX_CH] > 0x0F) _sys[SYS_TX_CH] = 0;
    for (uint8_t a = SYS_TUNE_0; a <= SYS_TUNE_3; ++a) _sys[a] &= 0x0F;
    if (_sys[SYS_TRANSPOSE] < 0x34 || _sys[SYS_TRANSPOSE] > 0x4C) _sys[SYS_TRANSPOSE] = 0x40;
    _sys[SYS_LOCAL]          = _sys[SYS_LOCAL] ? 1 : 0;
    _sys[SYS_AUTO_POWER_OFF] = _sys[SYS_AUTO_POWER_OFF] ? 1 : 0;
    _sys[SYS_SPEAKER]        = _sys[SYS_SPEAKER] ? 1 : 0;
    _sys[SYS_MIDI_CONTROL]   = _sys[SYS_MIDI_CONTROL] ? 1 : 0;
    sanitizeSystemBlock();
    applyMasterTune();
}

// --- transmit -----------------------------------------------------------------------

void RefaceMidiBase::txBytes(const uint8_t* b, uint16_t n) {
    // Queued rather than written straight to TinyUSB: a full voice bulk is 241
    // bytes across seven messages and one TX FIFO holds 48 SysEx bytes, so the
    // direct call dropped everything past the first block and a half without
    // saying so. See core/include/midi_output_usb.h.
    usbMidiOut().write(b, n);
    // Everything the reface layer sends - active sensing, panel CCs, SysEx
    // replies - goes out the DIN socket as well. Unconditional: unlike USB
    // there is nothing to enumerate, and a receiver that is not plugged in
    // simply does not listen.
    midiSerial().write(b, n);
}

void RefaceMidiBase::txCC(uint8_t cc, uint8_t val) {
    const uint8_t m[3] = { (uint8_t) (0xB0 | (_sys[SYS_TX_CH] & 0x0F)), (uint8_t) (cc & 0x7F), (uint8_t) (val & 0x7F) };
    txBytes(m, 3);
}

void RefaceMidiBase::txProgramChange(uint8_t program) {
    const uint8_t m[2] = { (uint8_t) (0xC0 | (_sys[SYS_TX_CH] & 0x0F)), (uint8_t) (program & 0x7F) };
    txBytes(m, 2);
}

void RefaceMidiBase::txIdentityReply() {
    txBytes(_model.identityReply, _model.identityReplyLen);
}

void RefaceMidiBase::txParamChange(uint8_t ah, uint8_t am, uint8_t al) {
    uint8_t buf[24];
    uint16_t n = 0;
    buf[n++] = 0xF0;
    buf[n++] = 0x43;
    buf[n++] = 0x10;
    buf[n++] = 0x7F;
    buf[n++] = 0x1C;
    buf[n++] = _model.modelId;
    buf[n++] = ah & 0x7F;
    buf[n++] = am & 0x7F;
    buf[n++] = al & 0x7F;

    if (ah == 0x00 && am == 0x00) {
        if (al == SYS_TUNE_0) {
            for (uint8_t a = SYS_TUNE_0; a <= SYS_TUNE_3; ++a) buf[n++] = _sys[a];
        } else if (al < SYS_BLOCK_SIZE) {
            buf[n++] = _sys[al];
        } else {
            return;
        }
    } else {
        uint8_t out[8];
        const uint8_t k = readParam(ah, am, al, out);
        if (k == 0 || k > sizeof out) return;
        for (uint8_t i = 0; i < k; ++i) buf[n++] = out[i] & 0x7F;
    }

    buf[n++] = 0xF7;
    txBytes(buf, n);
}

void RefaceMidiBase::txBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint8_t len) {
    // 11 header bytes + the block + checksum + F7; the largest block any reface
    // sends is the DX common block, 38 bytes.
    uint8_t buf[64];
    if (len > sizeof buf - 13) return;
    const uint16_t bc = (uint16_t) 4 + len;
    uint16_t n = 0;

    buf[n++] = 0xF0;
    buf[n++] = 0x43;
    buf[n++] = 0x00;
    buf[n++] = 0x7F;
    buf[n++] = 0x1C;
    buf[n++] = (uint8_t) ((bc >> 7) & 0x7F);
    buf[n++] = (uint8_t) (bc & 0x7F);
    buf[n++] = _model.modelId;
    buf[n++] = ah & 0x7F;
    buf[n++] = am & 0x7F;
    buf[n++] = al & 0x7F;

    uint32_t sum = _model.modelId + (ah & 0x7F) + (am & 0x7F) + (al & 0x7F);
    for (uint8_t i = 0; i < len; ++i) {
        const uint8_t b = data[i] & 0x7F;
        buf[n++] = b;
        sum += b;
    }

    buf[n++] = (uint8_t) ((0x80 - (sum & 0x7F)) & 0x7F);
    buf[n++] = 0xF7;
    txBytes(buf, n);
}

// --- exclusive receive --------------------------------------------------------------

void RefaceMidiBase::onSysEx(const uint8_t* d, uint16_t len) {
    // Identity Request, any device number.
    if (len >= 6 && d[0] == 0xF0 && d[1] == 0x7E && d[3] == 0x06 && d[4] == 0x01) {
        txIdentityReply();
        return;
    }
    if (len >= 10 && d[0] == 0xF0 && d[1] == 0x43 && d[3] == 0x7F && d[4] == 0x1C) {
        handleYamahaSysEx(d, len);
    }
}

void RefaceMidiBase::handleYamahaSysEx(const uint8_t* d, uint16_t len) {
    switch (d[2] & 0x70) {
    case 0x10: {                                   // Parameter Change
        if (len < 11 || !modelOk(d[5])) return;
        const uint8_t ah = d[6], am = d[7], al = d[8];
        const uint16_t dlen = len - 10;            // between the address and the F7
        const uint8_t* data = &d[9];
        if (ah == 0x00 && am == 0x00) applySystemParam(al, data, dlen);
        else applyParam(ah, am, al, data, dlen);
        break;
    }
    case 0x30:                                     // Parameter Request
        if (len < 10 || !modelOk(d[5])) return;
        onParamRequest(d[6], d[7], d[8]);
        break;
    case 0x20:                                     // Dump Request
        if (len < 10 || !modelOk(d[5])) return;
        if (d[6] == 0x00 && d[7] == 0x00 && d[8] == 0x00) txBulkBlock(0x00, 0x00, 0x00, _sys, SYS_BLOCK_SIZE);
        else onDumpRequest(d[6], d[7], d[8]);
        break;
    case 0x00:                                     // Bulk Dump
        handleBulkDump(d, len);
        break;
    default:
        break;
    }
}

void RefaceMidiBase::handleBulkDump(const uint8_t* d, uint16_t len) {
    // F0 43 0n 7F 1C bh bl <model> ah am al <data> <sum> F7, byte count =
    // model + address + data, checksum over the same bytes.
    if (len < 13) return;
    const uint16_t bc = (uint16_t) (((uint16_t) (d[5] & 0x7F) << 7) | (d[6] & 0x7F));
    if (bc < 4 || len != (uint16_t) (bc + 9)) return;
    if (!modelOk(d[7])) return;
    uint32_t sum = 0;
    for (uint16_t i = 7; i < (uint16_t) (len - 1); ++i) sum += d[i];
    if (sum & 0x7F) return;

    const uint8_t ah = d[8], am = d[9], al = d[10];
    const uint16_t dlen = bc - 4;
    const uint8_t* data = &d[11];
    if (ah == 0x00 && am == 0x00 && al == 0x00 && dlen == SYS_BLOCK_SIZE) {
        for (uint16_t i = 0; i < dlen; ++i) applySystemParam((uint8_t) i, &data[i], 1);
    } else {
        onBulkBlock(ah, am, al, data, dlen);
    }
}

} // namespace picoface
