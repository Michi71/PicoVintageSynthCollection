#include "midi_reface.h"
#include "midi_serial.h"
#include "YC_Synth_Bridge.h"
#include "ipc.h"
#include "tusb.h"
#include "pico/time.h"

// TODO: aus echtem Geraete-Dump verifizieren, NICHT ungeprueft verwenden
constexpr uint8_t YC_MODEL_ID = 0x00;

void RefaceMidi::init(YC_Synth_Bridge* bridge) {
    _bridge = bridge;
}

void RefaceMidi::tick() {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Active Sensing TX alle 200ms
    if (now - _lastTxSenseMs >= 200) {
        uint8_t fe = 0xFE;
        txBytes(&fe, 1);
        _lastTxSenseMs = now;
    }

    // RX-Timeout 350ms
    if (_senseActive && (now - _lastRxMs >= 350)) {
        _senseActive = false;
        // Panik-Aktion bei Timeout
        ipc_send_yc_all_notes_off();
    }
}

bool RefaceMidi::channelOk(uint8_t ch) const {
    return _rxChannel == RX_CH_ALL || _rxChannel == ch;
}

void RefaceMidi::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch) {
    if (!channelOk(ch)) return;
    int transposed = note + (_bridge->state().octave * 12);
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;
    ipc_send_yc_note_on((uint8_t)transposed, vel);
}

void RefaceMidi::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch) {
    if (!channelOk(ch)) return;
    int transposed = note + (_bridge->state().octave * 12);
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;
    ipc_send_yc_note_off((uint8_t)transposed);
}

void RefaceMidi::onControlChange(uint8_t cc, uint8_t val, uint8_t ch) {
    if (!channelOk(ch)) return;
    switch(cc) {
        case 64: // SUSTAIN - IMMER verarbeiten, unabhaengig vom Flag
            ipc_send_yc_sustain(val >= 64 ? 1 : 0);
            break;
        case 18: // EFFECT DIST
            if (_midiControlEnabled) ipc_send_yc_panel_update(16, val);
            break;
        case 19: // ROTARY SPEED
            if (_midiControlEnabled) ipc_send_yc_rotary_target(quantizeRotary(val));
            break;
        case 77: // VIBRATO/CHORUS DEPTH
            if (_midiControlEnabled) ipc_send_yc_panel_update(15, quantize5(val));
            break;
        case 79: // VIBRATO/CHORUS SWITCH
            if (_midiControlEnabled) ipc_send_yc_panel_update(14, quantize2(val));
            break;
        case 80: // WAVE
            if (_midiControlEnabled) ipc_send_yc_panel_update(0, quantizeWave(val));
            break;
        case 91: // EFFECT REVERB
            if (_midiControlEnabled) ipc_send_yc_panel_update(17, val);
            break;
        case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110: // FOOTAGE
            if (_midiControlEnabled) ipc_send_yc_panel_update(cc - 100, quantize7(val));
            break;
        case 111: // PERCUSSION ON/OFF
            if (_midiControlEnabled) ipc_send_yc_panel_update(11, quantize2(val));
            break;
        case 112: // PERCUSSION TYPE
            if (_midiControlEnabled) ipc_send_yc_panel_update(12, quantize2(val));
            break;
        case 113: // PERCUSSION LENGTH
            if (_midiControlEnabled) ipc_send_yc_panel_update(13, quantize5(val));
            break;
        // TODO: CC1(Mod)/7(Volume)/11(Expression) werden empfangen, aber nicht auf ein Panel-Feld abgebildet
        default:
            // TODO: Unbehandelte CCs
            break;
    }
}

void RefaceMidi::onPitchBend(uint16_t bend14, uint8_t ch) {
    if (!channelOk(ch)) return;
    // TODO: Engine nutzt Pitch Bend noch nicht
}

void RefaceMidi::onRealtime(uint8_t status) {
    if (status == 0xFE) {
        // Nur echtes Active Sensing (0xFE) darf die Ueberwachung scharfschalten.
        _senseActive = true;
        _lastRxMs = to_ms_since_boot(get_absolute_time());
    }
}

void RefaceMidi::onSysEx(const uint8_t* data, uint16_t len) {
    // Identity Request: F0 7E 7F 06 01 F7 (vereinfachte Erkennung fuer M6)
    if (len >= 6 && data[0] == 0xF0 && data[1] == 0x7E && data[2] == 0x7F && data[3] == 0x06 && data[4] == 0x01 && data[len-1] == 0xF7) {
        txIdentityReply();
        return;
    }
    // Parameter Change (Set), TG-Block: F0 43 1n 7F 1C <ModelID> 30 00 <addrLow> <value> F7 (11 Bytes)
    if (len == 11 && data[0] == 0xF0 && data[1] == 0x43 && (data[2] & 0xF0) == 0x10 && data[3] == 0x7F && data[4] == 0x1C && data[6] == 0x30 && data[7] == 0x00 && data[10] == 0xF7) {
        applyTgParam(data[8], data[9]);
        return;
    }
    // Parameter Request, TG-Block: F0 43 3n 7F 1C <ModelID> 30 00 <addrLow> F7 (10 Bytes) -> Antwort per Parameter Change
    if (len == 10 && data[0] == 0xF0 && data[1] == 0x43 && (data[2] & 0xF0) == 0x30 && data[3] == 0x7F && data[4] == 0x1C && data[6] == 0x30 && data[7] == 0x00 && data[9] == 0xF7) {
        txParamRequestReply(data[8]);
        return;
    }
    // TODO: Dump Request (F0 43 2n 7F 1C ... F7) und vollstaendiger Bulk Dump (Checksum-Block)
    // sind bewusst noch nicht implementiert (hoeheres Risiko fuer Off-by-one-Fehler in der
    // Blockrahmung, geringerer Zusatznutzen ggue. Parameter Change, das bereits jede TG-Adresse
    // einzeln lesbar/schreibbar macht).
}

void RefaceMidi::notifyActivity() {
    // Normale MIDI-Aktivitaet aktualisiert nur den Zeitstempel, wenn bereits scharf.
    if (_senseActive) {
        _lastRxMs = to_ms_since_boot(get_absolute_time());
    }
}

void RefaceMidi::txBytes(const uint8_t* b, uint16_t n) {
    tud_midi_stream_write(0, b, n);
    // Everything the reface layer sends - panel CCs, SysEx replies - goes out
    // the DIN socket as well. Unconditional: unlike USB there is nothing to
    // enumerate, and a receiver that is not plugged in simply does not listen.
    midiSerial().write(b, n);
}

void RefaceMidi::txIdentityReply() {
    uint8_t reply[] = {0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x43, 0x00, 0x06, YC_MODEL_ID, 0x00, 0x00, 0x00, 0x00, 0xF7};
    txBytes(reply, sizeof(reply));
}

void RefaceMidi::txCC(uint8_t cc, uint8_t val) {
    // TODO: aus SYSTEM-Transmit-Channel lesen sobald verfuegbar
    uint8_t msg[3] = {(uint8_t)(0xB0 | 0), cc, val};
    txBytes(msg, 3);
}

void RefaceMidi::txPanelMirror(uint8_t param_id, uint8_t internalValue) {
    if (!_midiControlEnabled) return;

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
    if (!_midiControlEnabled) return;
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

void RefaceMidi::txParamChange(uint8_t addrLow, uint8_t value) {
    uint8_t msg[11] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, YC_MODEL_ID, 0x30, 0x00, addrLow, value, 0xF7};
    txBytes(msg, 11);
}

void RefaceMidi::txParamRequestReply(uint8_t addrLow) {
    txParamChange(addrLow, readTgParam(addrLow));
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
