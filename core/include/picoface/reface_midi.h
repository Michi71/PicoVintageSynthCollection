// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// reface_midi.h - what the three Yamaha reface ports (CP, DX, YC) share of
// their MIDI layer.
//
// Every reface speaks the same dialect: a 32-byte SYSTEM common block at
// address 00 00 00 (transmit and receive channel, master tune as four nibbles,
// local control, transpose, MIDI control ...), exclusive messages with the
// header F0 43 <cmd|dev> 7F 1C <model> - Parameter Change (1n), Parameter
// Request (3n), Dump Request (2n) and Bulk Dump (0n) with a byte count and a
// checksum - the Identity Reply, active sensing every 200 ms, and panel edits
// mirrored as CCs on the transmit channel. Until this class existed each port
// carried its own copy of all that, and the three copies had drifted (#161's
// fix would have been three fixes had it lived here instead of in the core).
//
// What differs between the models stays with the instrument: the CC map, the
// tone-generator / voice blocks behind the SYSTEM block, how a parameter
// reaches the engine, the identity bytes. The instrument derives from this
// class, keeps its public name RefaceMidi, and fills in the hooks below.
//
// Runs on core0 like everything else; the hooks forward engine mutations
// through the instrument's same-core ring (ipc.h) so they land at the next
// block boundary.

#ifndef PICOFACE_REFACE_MIDI_H
#define PICOFACE_REFACE_MIDI_H

#include <stdint.h>

namespace picoface {

class RefaceMidiBase {
public:
    // SYSTEM common block, base address 00 00 00. The layout is the same on
    // every reface for the addresses named here; 0x08..0x0B differ per model
    // (DX: tempo, LCD contrast, pedal model; CP: sustain pedal select at 0x0B)
    // and are named by the instrument, see applyModelSystemParam().
    enum SysAddr : uint8_t {
        SYS_TX_CH          = 0x00,
        SYS_RX_CH          = 0x01,
        SYS_TUNE_0         = 0x02,   // master tune, four nibbles, bit 15-12 first
        SYS_TUNE_1         = 0x03,
        SYS_TUNE_2         = 0x04,
        SYS_TUNE_3         = 0x05,
        SYS_LOCAL          = 0x06,
        SYS_TRANSPOSE      = 0x07,   // 0x34..0x4C, 0x40 = none
        SYS_AUTO_POWER_OFF = 0x0C,
        SYS_SPEAKER        = 0x0D,
        SYS_MIDI_CONTROL   = 0x0E,
        SYS_BLOCK_SIZE     = 0x20
    };

    static constexpr uint8_t  RX_CH_ALL       = 0x10;   // omni
    static constexpr uint16_t kMasterTuneCentre = 0x0400; // 1024: 0 cents, the data list's centre

    // What tells one reface from another on the wire.
    struct Model {
        uint8_t        modelId;          // byte 5 of every reface exclusive: 0x04 CP, 0x05 DX
        bool           checkModelId;     // false: accept any model byte on receive
        const uint8_t* identityReply;    // the complete Identity Reply, F0 .. F7
        uint8_t        identityReplyLen;
    };

    virtual ~RefaceMidiBase() = default;

    // Active sensing: one 0xFE every 200 ms out, and once the other side has
    // sent one, silence for 350 ms means the cable is gone - all sound off.
    // Call from uiTick().
    void tick();

    // Receive entry points; the core calls these for both wires. Channel
    // filtering, transpose and the SYSTEM-level CCs happen here, the rest
    // reaches the instrument through the hooks.
    void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);       // vel 0 is a note off
    void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);
    void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);
    void onProgramChange(uint8_t program, uint8_t ch);
    void onPitchBend(uint16_t bend14, uint8_t ch);              // 0..16383, centre 8192
    void onRealtime(uint8_t status);
    void onSysEx(const uint8_t* data, uint16_t len);            // complete message, F0 and F7 included
    void notifyActivity();                                       // any received byte resets the sense timeout

    uint8_t getRxChannel() const { return _sys[SYS_RX_CH]; }    // 0..15 = ch 1-16, RX_CH_ALL = omni
    void    setRxChannel(uint8_t ch);                            // clamps to 0..RX_CH_ALL
    bool    midiControlEnabled() const { return _sys[SYS_MIDI_CONTROL] != 0; }
    void    setMidiControlEnabled(bool on) { _sys[SYS_MIDI_CONTROL] = on ? 1 : 0; }

    // The SYSTEM image, 32 bytes, for the settings record.
    void getSystemBlock(uint8_t* dst) const;
    void loadSystemBlock(const uint8_t* src);                    // sanitises and re-applies the master tune

    // Transmit, on the SYSTEM transmit channel, to USB and DIN alike.
    void txCC(uint8_t cc, uint8_t val);
    void txProgramChange(uint8_t program);
    void txParamChange(uint8_t ah, uint8_t am, uint8_t al);      // Parameter Change carrying the current value(s)
    void txBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint8_t len);

protected:
    explicit RefaceMidiBase(const Model& model) : _model(model) {}

    // Seeds the SYSTEM defaults (transmit 1, receive omni, tune centred, local
    // on, no transpose, MIDI control on) and the sense timers. The
    // instrument's init() calls it first and then sets its model bytes.
    void resetSystem();

    void     txBytes(const uint8_t* b, uint16_t n);
    int      transposeNote(int note) const;                      // SYSTEM transpose + panel octave, clamped
    uint16_t masterTuneRaw() const;                              // the four nibbles as one 16-bit value
    void     applyMasterTune() { engineMasterTune(masterTuneRaw()); }

    uint8_t _sys[SYS_BLOCK_SIZE] = {};

    // --- what the instrument supplies ---------------------------------------
    // Notes arrive transposed, everything arrives on an accepted channel.
    virtual void engineNoteOn(uint8_t note, uint8_t vel) = 0;
    virtual void engineNoteOff(uint8_t note) = 0;
    virtual void enginePitchBend(uint16_t bend14) = 0;
    // Every CC but omni on/off (124/125), which the base handles: the model's
    // CC map, including the channel-mode messages the engine wants to see.
    virtual void engineControlChange(uint8_t cc, uint8_t val) = 0;
    virtual void engineProgramChange(uint8_t program) { (void) program; }
    virtual void engineAllSoundOff() = 0;
    virtual void engineAllNotesOff() = 0;
    // The master tune as the data list transmits it: 1024 = centre, 0.1 cent per step.
    virtual void engineMasterTune(uint16_t raw) { (void) raw; }
    // The panel's octave switch, -2..+2, added to every received note.
    virtual int  uiOctave() const { return 0; }
    // SYSTEM addresses 0x08..0x0B, which mean different things per model.
    // Default: store the 7-bit value as it came.
    virtual void applyModelSystemParam(uint8_t addr, uint8_t v) { _sys[addr] = v; }
    // Model-specific checks after loadSystemBlock() has done the common ones.
    virtual void sanitizeSystemBlock() {}

    // Exclusive messages that address something other than the SYSTEM block.
    // A Parameter Change with its data bytes.
    virtual void applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) = 0;
    // The current value(s) at an address for a Parameter Change reply: write
    // them to out (room for 8) and return how many; 0 = unknown address.
    virtual uint8_t readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) = 0;
    // A Parameter Request. The convention answers with one Parameter Change;
    // a model that also serves whole blocks this way overrides.
    virtual void onParamRequest(uint8_t ah, uint8_t am, uint8_t al) { txParamChange(ah, am, al); }
    // A Dump Request for anything but the SYSTEM block.
    virtual void onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) { (void) ah; (void) am; (void) al; }
    // One block of a received Bulk Dump, framing and checksum already
    // verified, for anything but the SYSTEM block - the bulk header
    // (0E 0F 00) and footer (0F 0F 00) come through here too, with len 0.
    virtual void onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) {
        (void) ah; (void) am; (void) al; (void) data; (void) len;
    }

private:
    bool channelOk(uint8_t ch) const;
    bool modelOk(uint8_t id) const { return !_model.checkModelId || id == _model.modelId; }
    void applySystemParam(uint8_t addr, const uint8_t* data, uint16_t len);
    void handleYamahaSysEx(const uint8_t* d, uint16_t len);
    void handleBulkDump(const uint8_t* d, uint16_t len);
    void txIdentityReply();

    Model    _model;
    bool     _senseActive   = false;   // 0xFE seen: supervise the 350 ms timeout
    uint32_t _lastRxMs      = 0;
    uint32_t _lastTxSenseMs = 0;
};

} // namespace picoface

#endif // PICOFACE_REFACE_MIDI_H
