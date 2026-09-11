// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// src/midi_reface.cpp
//
// PicoFaceYC — the reface YC on top of the shared reface MIDI layer.

#include "midi_reface.h"
#include "YC_Synth_Bridge.h"
#include "ipc.h"

namespace {

// Model ID 06H and the Identity Reply, byte for byte from the Yamaha reface
// CS/DX/CP/YC Data List (PO-B0, 04/2016, "reface YC MIDI Data Format",
// 3-4-1-2). Yamaha's own Soundmondo editor keys the same values: series
// modelId 0x06, identity pair [0x54, 0x06] at bytes 8 and 9 of a 15-byte
// reply (reface-panel.js, REFACE_YC). Byte 10 is the firmware version read as
// 1.0 + n/10: the data list prints 0 for the 1.0 it was written against, the
// current reface YC firmware is 1.30 and what that update added (master tune,
// transmit and receive channel, local control) is all served here, so report
// 1.3 as the DX port does.
constexpr uint8_t YC_MODEL_ID = 0x06;

const uint8_t kIdentityReply[15] = {
    0xF0, 0x7E, 0x7F, 0x06, 0x02,
    0x43, 0x00, 0x41, 0x54, 0x06,
    0x03, 0x00, 0x00, 0x7F, 0xF7
};

const picoface::RefaceMidiBase::Model kModel = { YC_MODEL_ID, true, kIdentityReply, sizeof kIdentityReply };

// Upper limit per TG address, the data list's range column. An editor sends
// what it likes; the engine indexes tables with these values (a footage of
// 127 once read past a 7-entry LUT, changelog section 39).
const uint8_t kTgMax[RefaceMidi::TG_BLOCK_SIZE] = {
    127, 0,                       // volume, reserved
    4,                            // wave
    6, 6, 6, 6, 6, 6, 6, 6, 6,    // nine footages
    1, 4,                         // vibrato/chorus select, depth
    1, 1, 4,                      // percussion on, type, length
    3,                            // rotary speed
    127, 127,                     // distortion, reverb
    0, 0                          // reserved
};

} // namespace

RefaceMidi::RefaceMidi() : picoface::RefaceMidiBase(kModel) {}

void RefaceMidi::init(YC_Synth_Bridge* bridge) {
    _bridge = bridge;
    resetSystem();
}

// The YC's octave switch lives in the engine state, not in a UI global.
int RefaceMidi::uiOctave() const { return _bridge ? _bridge->state().octave : 0; }

void RefaceMidi::engineNoteOn(uint8_t note, uint8_t vel) { ipc_send_yc_note_on(note, vel); }
void RefaceMidi::engineNoteOff(uint8_t note)             { ipc_send_yc_note_off(note); }
void RefaceMidi::enginePitchBend(uint16_t bend14)        { ipc_send_yc_pitch_bend(bend14); }

// One panic action for both: the engine has all-notes-off and nothing finer.
void RefaceMidi::engineAllSoundOff() {}
void RefaceMidi::engineAllNotesOff() { ipc_send_yc_all_notes_off(); }

// Reset All Controllers (CC121) as the data list lists it: pitch bend to
// centre, expression to maximum, sustain off. "Modulation to minimum" is on
// that list too, but on the YC the wheel is the rotary speed switch (below),
// and a reset that silently parks the rotary at SLOW is not what a player
// wants from CC121 - the rotary stays where it is.
void RefaceMidi::resetControllers() {
    ipc_send_yc_pitch_bend(8192);
    ipc_send_yc_expression(127);
    ipc_send_yc_sustain(0);
}

// Omni on/off (124/125) never get here, the base handles them. Everything in
// the first group is recognised whatever the MIDI Control setting says; the
// panel CCs below it only with MIDI Control on (implementation chart, *1).
void RefaceMidi::engineControlChange(uint8_t cc, uint8_t val) {
    const bool ctl = midiControlEnabled();
    switch(cc) {
        case 1:   // MODULATION: an external mod wheel switches the reface YC's
                  // rotary between SLOW and FAST (Yamaha, "Rotary Speed Control
                  // - reface YC"). The threshold is ours; the article gives none.
            ipc_send_yc_rotary_target(val >= 64 ? 3 : 2);
            break;
        case 7:   // VOLUME
            ipc_send_yc_midi_volume(val);
            break;
        case 11:  // EXPRESSION
            ipc_send_yc_expression(val);
            break;
        case 64:  // SUSTAIN
            ipc_send_yc_sustain(val >= 64 ? 1 : 0);
            break;
        case 120: // ALL SOUND OFF
        case 123: // ALL NOTES OFF
        case 126: // MONO - "same function as all sound off" on the YC
        case 127: // POLY - likewise
            engineAllNotesOff();
            break;
        case 121: // RESET ALL CONTROLLERS
            resetControllers();
            break;

        case 18: // EFFECT DIST
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_DISTORTION, val);
            break;
        case 19: // ROTARY SPEED
            if (ctl) ipc_send_yc_rotary_target(quantizeRotary(val));
            break;
        case 77: // VIBRATO/CHORUS DEPTH
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_VIBCHO_DEPTH, quantize5(val));
            break;
        case 79: // VIBRATO/CHORUS SWITCH
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_VIBCHO_SELECT, quantize2(val));
            break;
        case 80: // WAVE
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_WAVE, quantizeWave(val));
            break;
        case 91: // EFFECT REVERB
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_REVERB, val);
            break;
        case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110: // FOOTAGE 16' .. 1'
            if (ctl) ipc_send_yc_panel_update((uint8_t) (YC_PARAM_FOOTAGE_16 + (cc - 102)), quantize7(val));
            break;
        case 111: // PERCUSSION ON/OFF
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_PERC_ON, quantize2(val));
            break;
        case 112: // PERCUSSION TYPE
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_PERC_TYPE, quantize2(val));
            break;
        case 113: // PERCUSSION LENGTH
            if (ctl) ipc_send_yc_panel_update(YC_PARAM_PERC_LENGTH, quantize5(val));
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// TG block (base 30 00 00). A Parameter Change carries one value, a Parameter
// Request is answered by the base through readParam(), a Dump Request on the
// bulk header address gets header + block + footer.
// ---------------------------------------------------------------------------

void RefaceMidi::applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
    if (ah == 0x30 && am == 0x00 && len >= 1) applyTgParam(al, data[0]);
}

uint8_t RefaceMidi::readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) {
    if (ah != 0x30 || am != 0x00 || al >= TG_BLOCK_SIZE) return 0;
    out[0] = readTgParam(al);
    return 1;
}

void RefaceMidi::onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) {
    if (ah == 0x0E && am == 0x0F && al == 0x00) txTgBulk();          // "designate the Bulk Header address" - data list
    else if (ah == 0x30 && am == 0x00 && al == 0x00) txTgBlock();
}

// Header and footer carry nothing to stage: every byte of the block is a
// parameter of its own and goes to the engine through the ring like a
// Parameter Change would, so a dump that arrives mid-block lands at the next
// block boundary in one piece.
void RefaceMidi::onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
    if (ah != 0x30 || am != 0x00 || al != 0x00) return;
    if (len > TG_BLOCK_SIZE) len = TG_BLOCK_SIZE;
    for (uint8_t a = 0; a < len; ++a) applyTgParam(a, data[a]);
}

void RefaceMidi::txTgBlock() {
    uint8_t blk[TG_BLOCK_SIZE];
    readTgBlock(blk);
    txBulkBlock(0x30, 0x00, 0x00, blk, TG_BLOCK_SIZE);
}

void RefaceMidi::txTgBulk() {
    txBulkBlock(0x0E, 0x0F, 0x00, nullptr, 0);   // bulk header
    txTgBlock();
    txBulkBlock(0x0F, 0x0F, 0x00, nullptr, 0);   // bulk footer
}

// ---------------------------------------------------------------------------
// Panel -> MIDI OUT. Octave and volume have no CC in the data list and are
// not mirrored; the TG volume is "set only via MIDI" on the original.
// ---------------------------------------------------------------------------

void RefaceMidi::txPanelMirror(uint8_t param_id, uint8_t internalValue) {
    if (!midiControlEnabled()) return;

    static const uint8_t depth5[5] = {0, 32, 64, 95, 127};
    static const uint8_t foot7[7] = {0, 21, 42, 64, 85, 106, 127};
    static const uint8_t bin2[2] = {0, 127};

    switch (param_id) {
        case YC_PARAM_WAVE:
            if (internalValue < 5) txCC(80, depth5[internalValue]);
            break;
        case YC_PARAM_FOOTAGE_16: case YC_PARAM_FOOTAGE_513: case YC_PARAM_FOOTAGE_8:
        case YC_PARAM_FOOTAGE_4:  case YC_PARAM_FOOTAGE_223: case YC_PARAM_FOOTAGE_2:
        case YC_PARAM_FOOTAGE_135: case YC_PARAM_FOOTAGE_113: case YC_PARAM_FOOTAGE_1:
            if (internalValue < 7) txCC((uint8_t) (102 + (param_id - YC_PARAM_FOOTAGE_16)), foot7[internalValue]);
            break;
        case YC_PARAM_PERC_ON:
            txCC(111, bin2[internalValue ? 1 : 0]);
            break;
        case YC_PARAM_PERC_TYPE:
            txCC(112, bin2[internalValue ? 1 : 0]);
            break;
        case YC_PARAM_PERC_LENGTH:
            if (internalValue < 5) txCC(113, depth5[internalValue]);
            break;
        case YC_PARAM_VIBCHO_SELECT:
            txCC(79, bin2[internalValue ? 1 : 0]);
            break;
        case YC_PARAM_VIBCHO_DEPTH:
            if (internalValue < 5) txCC(77, depth5[internalValue]);
            break;
        case YC_PARAM_DISTORTION:
            txCC(18, internalValue);
            break;
        case YC_PARAM_REVERB:
            txCC(91, internalValue);
            break;
        default:   // YC_PARAM_OCTAVE, YC_PARAM_VOLUME
            break;
    }
}

void RefaceMidi::txRotaryMirror(uint8_t speed) {
    if (!midiControlEnabled()) return;
    static const uint8_t rot4[4] = {0, 42, 85, 127};
    if (speed < 4) txCC(19, rot4[speed]);
}

// ---------------------------------------------------------------------------
// TG addresses <-> engine parameters
// ---------------------------------------------------------------------------

void RefaceMidi::applyTgParam(uint8_t addrLow, uint8_t value) {
    if (addrLow >= TG_BLOCK_SIZE) return;
    if (value > kTgMax[addrLow]) value = kTgMax[addrLow];
    if (addrLow >= TG_FOOTAGE_16 && addrLow <= TG_FOOTAGE_1) {
        ipc_send_yc_panel_update((uint8_t) (YC_PARAM_FOOTAGE_16 + (addrLow - TG_FOOTAGE_16)), value);
        return;
    }
    switch (addrLow) {
        case TG_VOLUME:        ipc_send_yc_panel_update(YC_PARAM_VOLUME, value); break;
        case TG_WAVE:          ipc_send_yc_panel_update(YC_PARAM_WAVE, value); break;
        case TG_VIBCHO_SELECT: ipc_send_yc_panel_update(YC_PARAM_VIBCHO_SELECT, value); break;
        case TG_VIBCHO_DEPTH:  ipc_send_yc_panel_update(YC_PARAM_VIBCHO_DEPTH, value); break;
        case TG_PERC_ON:       ipc_send_yc_panel_update(YC_PARAM_PERC_ON, value); break;
        case TG_PERC_TYPE:     ipc_send_yc_panel_update(YC_PARAM_PERC_TYPE, value); break;
        case TG_PERC_LENGTH:   ipc_send_yc_panel_update(YC_PARAM_PERC_LENGTH, value); break;
        case TG_ROTARY_SPEED:  ipc_send_yc_rotary_target(value); break;
        case TG_DISTORTION:    ipc_send_yc_panel_update(YC_PARAM_DISTORTION, value); break;
        case TG_REVERB:        ipc_send_yc_panel_update(YC_PARAM_REVERB, value); break;
        default: break;   // reserved
    }
}

uint8_t RefaceMidi::readTgParam(uint8_t addrLow) const {
    if (!_bridge) return 0;
    const yc_engine_state_t& st = _bridge->state();
    if (addrLow >= TG_FOOTAGE_16 && addrLow <= TG_FOOTAGE_1) return st.footage[addrLow - TG_FOOTAGE_16];
    switch (addrLow) {
        case TG_VOLUME:        return st.volume;
        case TG_WAVE:          return st.wave;
        case TG_VIBCHO_SELECT: return st.vibcho_select;
        case TG_VIBCHO_DEPTH:  return st.vibcho_depth;
        case TG_PERC_ON:       return st.perc_on;
        case TG_PERC_TYPE:     return st.perc_type;
        case TG_PERC_LENGTH:   return st.perc_length;
        case TG_ROTARY_SPEED:  return st.rotary_speed;
        case TG_DISTORTION:    return st.distortion;
        case TG_REVERB:        return st.reverb;
        default:               return 0;   // reserved
    }
}

void RefaceMidi::readTgBlock(uint8_t* out) const {
    for (uint8_t a = 0; a < TG_BLOCK_SIZE; ++a) out[a] = readTgParam(a);
}

// ---------------------------------------------------------------------------
// CC value bins, the data list's "Recognized" column
// ---------------------------------------------------------------------------

uint8_t RefaceMidi::quantize2(uint8_t val) {
    return val < 64 ? 0 : 1;
}

uint8_t RefaceMidi::quantize5(uint8_t val) {
    if (val <= 25) return 0;
    if (val <= 51) return 1;
    if (val <= 76) return 2;
    if (val <= 102) return 3;
    return 4;
}

uint8_t RefaceMidi::quantize7(uint8_t val) {
    if (val <= 18) return 0;
    if (val <= 36) return 1;
    if (val <= 54) return 2;
    if (val <= 73) return 3;
    if (val <= 91) return 4;
    if (val <= 109) return 5;
    return 6;
}

uint8_t RefaceMidi::quantizeRotary(uint8_t val) {
    if (val <= 32) return 0;
    if (val <= 64) return 1;
    if (val <= 95) return 2;
    return 3;
}

uint8_t RefaceMidi::quantizeWave(uint8_t val) {
    if (val <= 25) return 0;
    if (val <= 51) return 1;
    if (val <= 76) return 2;
    if (val <= 102) return 3;
    return 4;
}
