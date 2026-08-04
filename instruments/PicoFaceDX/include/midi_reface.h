// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef __MIDI_REFACE_H__
#define __MIDI_REFACE_H__

//
// midi_reface.h
//
// Yamaha reface DX compatible MIDI layer for RP2350 / C++17.
//
// Implements:
//   - SYSTEM common block (base 00 00 00, mirrors dx_engine's RDX_System
//     byte layout exactly).
//   - Common block (base 30 00 00, mirrors RDX_Common, 38 bytes).
//   - Operator blocks (base 31 <opNum> 00, mirror RDX_OpParams, 28 bytes each).
//   - Bulk Dump / Dump Request SysEx.
//   - Identity Reply.
//   - Active Sensing (TX 200 ms / RX timeout 350 ms).
//   - Front-panel CC transmit.
//
// Runs on core0 like everything else; engine mutations are forwarded through
// the same-core ring (ipc.h) so they land at the next block boundary.
// Implementation lives in src/midi_reface.cpp.
//

#include <stdint.h>

class DX_Synth_Bridge; // engine, Core 0 image; mutations via ipc.h

class RefaceMidi {
public:
    // Field order/offsets mirror dx_engine/RDX_Types.h's RDX_System byte-for-byte,
    // so getSystemBlock()/loadSystemBlock() may reinterpret_cast to/from RDX_System.
    enum SysAddr : uint8_t {
        SYS_TX_CH          = 0x00,
        SYS_RX_CH          = 0x01,
        SYS_TUNE_0         = 0x02,
        SYS_TUNE_1         = 0x03,
        SYS_TUNE_2         = 0x04,
        SYS_TUNE_3         = 0x05,
        SYS_LOCAL          = 0x06,
        SYS_TRANSPOSE      = 0x07,
        SYS_TEMPO_0        = 0x08,
        SYS_TEMPO_1        = 0x09,
        SYS_LCD_CONTRAST   = 0x0A,
        SYS_PEDAL_MODEL    = 0x0B,
        SYS_AUTO_POWER_OFF = 0x0C,
        SYS_SPEAKER_ON     = 0x0D,
        SYS_MIDI_CONTROL   = 0x0E,
        SYS_BLOCK_SIZE     = 0x20
    };

    // Common/Operator block address scheme (reface DX MIDI spec).
    // Ported from ESP32 reference tools/refacedx/RDX-Reface-DX-emu/RDX/RDX_Midi.h.
    static constexpr uint8_t COMMON_ADDR_H      = 0x30;
    static constexpr uint8_t OPERATOR_ADDR_H   = 0x31;
    static constexpr uint8_t COMMON_BLOCK_SIZE  = 38; // sizeof(RDX_Common)
    static constexpr uint8_t OPERATOR_BLOCK_SIZE = 28; // sizeof(RDX_OpParams)

    static constexpr uint8_t RX_CH_ALL = 0x10;

    void init(DX_Synth_Bridge* dx);                                   // bind engine bridge, seed SYSTEM defaults
    void tick();                                                      // Active sensing TX (200ms) + RX timeout (350ms); call from uiTick().
    void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);             // incoming Note On
    void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);           // incoming Note Off
    void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);       // incoming CC
    void onProgramChange(uint8_t program, uint8_t ch);              // 0..DX_NPRESETS-1 -> factory DX preset
    void onPitchBend(uint16_t bend14, uint8_t ch);                   // 14-bit pitch bend
    void onRealtime(uint8_t status);                                 // Active Sensing / Reset / Clock etc.
    void onSysEx(const uint8_t* data, uint16_t len);                 // raw SysEx (without F0/F7)
    void notifyActivity();                                            // mark last RX time (resets sense timeout)

    void txProgram(int preset);                                      // -> Program Change 0..DX_NPRESETS-1

    uint8_t getRxChannel() const { return _sys[SYS_RX_CH]; }          // 0..15 = ch 1-16, RX_CH_ALL = omni
    void setRxChannel(uint8_t ch);                                   // clamps to 0..RX_CH_ALL

    void getSystemBlock(uint8_t* dst) const;                         // copy SYS_BLOCK_SIZE (32) bytes of the SYSTEM common image (for persistence)
    void loadSystemBlock(const uint8_t* src);                       // restore SYSTEM common image (32 bytes), sanitize, re-apply master tune

    bool midiControlEnabled() const { return _sys[SYS_MIDI_CONTROL] != 0; }

private:
    bool     channelOk(uint8_t ch) const;
    int      transposeNote(int note) const;                         // + master transpose + UI octave, clamp 0..127
    void     allSoundOff();
    void     allNotesOff();
    void     resetControllers();                                    // PB center, sustain off, soft pedal off
    void     applyMasterTune();                                     // 4-nibble tune -> engine tuning (semitones)
    void     applySystemParam(uint8_t addr, const uint8_t* data, uint16_t len);
    void     applyCommonParam(uint8_t addr, uint8_t value);
    void     applyOperatorParam(uint8_t opNum, uint8_t addr, uint8_t value);
    uint8_t  readCommonParam(uint8_t addr) const;
    uint8_t  readOperatorParam(uint8_t opNum, uint8_t addr) const;
    void     txBytes(const uint8_t* b, uint16_t n);                 // tud_midi_stream_write wrapper
    void     txCC(uint8_t cc, uint8_t val);
    void     txIdentityReply();
    void     txParamChange(uint8_t ah, uint8_t am, uint8_t al);     // current value(s)
    void     txBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint8_t len);
    void     handleYamahaSysEx(const uint8_t* d, uint16_t len);
    void     handleBulkDump(const uint8_t* d, uint16_t len);
    void     handleDumpRequest(uint8_t ah, uint8_t am, uint8_t al);

    DX_Synth_Bridge* _dx = nullptr;
    uint8_t  _sys[SYS_BLOCK_SIZE] = {};          // SYSTEM common image; defaults set in init()
    bool     _senseActive = false;               // FE received -> supervise 350ms timeout
    uint32_t _lastRxMs = 0;
    uint32_t _lastTxSenseMs = 0;
};

#endif // __MIDI_REFACE_H__
