// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// src/midi_reface.cpp
//
// PicoFaceYC — the reface YC on top of the shared reface MIDI layer.

#include "midi_reface.h"
#include "YC_Synth_Bridge.h"
#include "ipc.h"

namespace {

// TODO: aus echtem Geraete-Dump verifizieren, NICHT ungeprueft verwenden
constexpr uint8_t YC_MODEL_ID = 0x00;

// Identity Request: F0 7E 7F 06 01 F7 (vereinfachte Erkennung fuer M6)
const uint8_t kIdentityReply[14] = {
    0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x43, 0x00, 0x06, YC_MODEL_ID, 0x00, 0x00, 0x00, 0x00, 0xF7
};

// checkModelId false: the model byte is unverified, so a message carrying any
// model byte is taken - as this port always did.
const picoface::RefaceMidiBase::Model kModel = { YC_MODEL_ID, false, kIdentityReply, sizeof kIdentityReply };

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

void RefaceMidi::enginePitchBend(uint16_t bend14) {
    (void) bend14;
    // TODO: Engine nutzt Pitch Bend noch nicht
}

// One panic action for both: the engine has all-notes-off and nothing finer.
void RefaceMidi::engineAllSoundOff() {}
void RefaceMidi::engineAllNotesOff() { ipc_send_yc_all_notes_off(); }

// Omni on/off (124/125) never get here, the base handles them.
void RefaceMidi::engineControlChange(uint8_t cc, uint8_t val) {
    const bool ctl = midiControlEnabled();
    switch(cc) {
        case 64: // SUSTAIN - IMMER verarbeiten, unabhaengig vom Flag
            ipc_send_yc_sustain(val >= 64 ? 1 : 0);
            break;
        case 18: // EFFECT DIST
            if (ctl) ipc_send_yc_panel_update(16, val);
            break;
        case 19: // ROTARY SPEED
            if (ctl) ipc_send_yc_rotary_target(quantizeRotary(val));
            break;
        case 77: // VIBRATO/CHORUS DEPTH
            if (ctl) ipc_send_yc_panel_update(15, quantize5(val));
            break;
        case 79: // VIBRATO/CHORUS SWITCH
            if (ctl) ipc_send_yc_panel_update(14, quantize2(val));
            break;
        case 80: // WAVE
            if (ctl) ipc_send_yc_panel_update(0, quantizeWave(val));
            break;
        case 91: // EFFECT REVERB
            if (ctl) ipc_send_yc_panel_update(17, val);
            break;
        case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110: // FOOTAGE
            if (ctl) ipc_send_yc_panel_update(cc - 100, quantize7(val));
            break;
        case 111: // PERCUSSION ON/OFF
            if (ctl) ipc_send_yc_panel_update(11, quantize2(val));
            break;
        case 112: // PERCUSSION TYPE
            if (ctl) ipc_send_yc_panel_update(12, quantize2(val));
            break;
        case 113: // PERCUSSION LENGTH
            if (ctl) ipc_send_yc_panel_update(13, quantize5(val));
            break;
        case 123: // ALL NOTES OFF
            engineAllNotesOff();
            break;
        // TODO: CC1(Mod)/7(Volume)/11(Expression) werden empfangen, aber nicht auf ein Panel-Feld abgebildet
        default:
            // TODO: Unbehandelte CCs
            break;
    }
}

// TG block (base 30 00 00); a Parameter Change carries one value, a Request
// is answered by the base through readParam().
void RefaceMidi::applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
    if (ah == 0x30 && am == 0x00 && len >= 1) applyTgParam(al, data[0]);
}

uint8_t RefaceMidi::readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) {
    if (ah != 0x30 || am != 0x00) return 0;
    out[0] = readTgParam(al);
    return 1;
}
// TODO: Dump Request (F0 43 2n 7F 1C ... F7) und vollstaendiger Bulk Dump sind
// fuer die TG-Bloecke bewusst noch nicht implementiert (der Parameter Change
// macht bereits jede TG-Adresse einzeln lesbar/schreibbar); der SYSTEM-Block
// kommt seit der gemeinsamen Schicht mit.

void RefaceMidi::txPanelMirror(uint8_t param_id, uint8_t internalValue) {
    if (!midiControlEnabled()) return;

    static const uint8_t depth5[5] = {0, 32, 64, 95, 127};
    static const uint8_t foot7[7] = {0, 21, 42, 64, 85, 106, 127};
    static const uint8_t bin2[2] = {0, 127};

    switch (param_id) {
        case 0: // WAVE
            txCC(80, depth5[internalValue]);
            break;
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: // FOOTAGE_16..FOOTAGE_1
            txCC(102 + (param_id - 2), foot7[internalValue]);
            break;
        case 11: // PERC_ON
            txCC(111, bin2[internalValue]);
            break;
        case 12: // PERC_TYPE
            txCC(112, bin2[internalValue]);
            break;
        case 13: // PERC_LENGTH
            txCC(113, depth5[internalValue]);
            break;
        case 14: // VIBCHO_SELECT
            txCC(79, bin2[internalValue]);
            break;
        case 15: // VIBCHO_DEPTH
            txCC(77, depth5[internalValue]);
            break;
        case 16: // DISTORTION
            txCC(18, internalValue);
            break;
        case 17: // REVERB
            txCC(91, internalValue);
            break;
        default:
            // TODO: OCTAVE (1) und VOLUME (18) werden laut Spec nicht gespiegelt
            break;
    }
}

void RefaceMidi::txRotaryMirror(uint8_t speed) {
    if (!midiControlEnabled()) return;
    static const uint8_t rot4[4] = {0, 42, 85, 127};
    txCC(19, rot4[speed]);
}

void RefaceMidi::applyTgParam(uint8_t addrLow, uint8_t value) {
    switch (addrLow) {
        case 0x00: ipc_send_yc_panel_update(18, value); break; // VOLUME
        case 0x02: ipc_send_yc_panel_update(0, value); break;  // WAVE
        case 0x03: ipc_send_yc_panel_update(2, value); break;  // FOOTAGE_16
        case 0x04: ipc_send_yc_panel_update(3, value); break;  // FOOTAGE_513
        case 0x05: ipc_send_yc_panel_update(4, value); break;  // FOOTAGE_8
        case 0x06: ipc_send_yc_panel_update(5, value); break;  // FOOTAGE_4
        case 0x07: ipc_send_yc_panel_update(6, value); break;  // FOOTAGE_223
        case 0x08: ipc_send_yc_panel_update(7, value); break;  // FOOTAGE_2
        case 0x09: ipc_send_yc_panel_update(8, value); break;  // FOOTAGE_135
        case 0x0A: ipc_send_yc_panel_update(9, value); break;  // FOOTAGE_113
        case 0x0B: ipc_send_yc_panel_update(10, value); break; // FOOTAGE_1
        case 0x0C: ipc_send_yc_panel_update(14, value); break; // VIBCHO_SELECT
        case 0x0D: ipc_send_yc_panel_update(15, value); break; // VIBCHO_DEPTH
        case 0x0E: ipc_send_yc_panel_update(11, value); break; // PERC_ON
        case 0x0F: ipc_send_yc_panel_update(12, value); break; // PERC_TYPE
        case 0x10: ipc_send_yc_panel_update(13, value); break; // PERC_LENGTH
        case 0x11: ipc_send_yc_rotary_target(value); break;    // ROTARY_SPEED
        case 0x12: ipc_send_yc_panel_update(16, value); break; // DISTORTION
        case 0x13: ipc_send_yc_panel_update(17, value); break; // REVERB
        default: break; // TODO: unbekannte/reservierte Adresse ignorieren
    }
}

uint8_t RefaceMidi::readTgParam(uint8_t addrLow) const {
    const yc_engine_state_t& st = _bridge->state();
    switch (addrLow) {
        case 0x00: return st.volume;
        case 0x02: return st.wave;
        case 0x03: return st.footage[0];
        case 0x04: return st.footage[1];
        case 0x05: return st.footage[2];
        case 0x06: return st.footage[3];
        case 0x07: return st.footage[4];
        case 0x08: return st.footage[5];
        case 0x09: return st.footage[6];
        case 0x0A: return st.footage[7];
        case 0x0B: return st.footage[8];
        case 0x0C: return st.vibcho_select;
        case 0x0D: return st.vibcho_depth;
        case 0x0E: return st.perc_on;
        case 0x0F: return st.perc_type;
        case 0x10: return st.perc_length;
        case 0x11: return st.rotary_speed;
        case 0x12: return st.distortion;
        case 0x13: return st.reverb;
        default: return 0;
    }
}

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
