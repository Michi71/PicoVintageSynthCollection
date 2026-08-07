// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef __MIDI_SERIAL_H__
#define __MIDI_SERIAL_H__

// DIN MIDI on uart1. The callback signatures are deliberately identical to
// MIDIInputUSB, so the same dispatch functions serve both transports - an
// instrument does not care which wire an event arrived on. RX is interrupt
// driven into a lock-free ring; at 31250 baud a byte arrives every 320 us
// and the 32-byte hardware FIFO would only give about 10 ms of slack, which
// a blocking front panel menu can exceed.

#include <stdint.h>
#include "pico/stdlib.h"
#include "project_config.h"

class MIDISerial {
public:
    using NoteOnCallback = void (*)(uint8_t, uint8_t, uint8_t);      // note, velocity, channel
    using NoteOffCallback = void (*)(uint8_t, uint8_t, uint8_t);
    using CCCallback = void (*)(uint8_t, uint8_t, uint8_t);          // cc, value, channel
    using ProgramChangeCallback = void (*)(uint8_t, uint8_t);        // program, channel
    using PitchBendCallback = void (*)(uint16_t, uint8_t);           // 0..16383, channel
    using RealtimeCallback = void (*)(uint8_t);
    using SysExCallback = void (*)(const uint8_t*, uint16_t);
    using ActivityCallback = void (*)(void);

    void init();     // uart1 at 31250 baud 8N1 on PIN_MIDI_RX / PIN_MIDI_TX, RX interrupt enabled
    void process();  // drain the ring and dispatch; call from the same place as MIDIInputUSB::process()

    // --- transmit ---
    // Queued, never blocking. At 31250 baud a byte takes 320 us, so writing a
    // reface DX voice dump (241 bytes) straight to the UART busy-waited for
    // about 77 ms - against an audio pool of six 64-sample buffers, 8.7 ms at
    // 44.1 kHz. The pool ran dry and the I2S DMA repeated it, which is heard as
    // a low tone rather than a gap. A message that does not fit the queue is
    // dropped whole, never in part. process() moves it out.
    void write(const uint8_t* data, uint16_t len);   // raw bytes, e.g. a SysEx reply
    void sendNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void sendNoteOff(uint8_t ch, uint8_t note, uint8_t vel);
    void sendControlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void sendProgramChange(uint8_t ch, uint8_t program);
    void sendPitchBend(uint8_t ch, uint16_t value14);

    void setNoteOnCallback(NoteOnCallback cb)               { noteOnCb_ = cb; }
    void setNoteOffCallback(NoteOffCallback cb)             { noteOffCb_ = cb; }
    void setCCCallback(CCCallback cb)                       { ccCb_ = cb; }
    void setProgramChangeCallback(ProgramChangeCallback cb) { programChangeCb_ = cb; }
    void setPitchBendCallback(PitchBendCallback cb)         { pitchBendCb_ = cb; }
    void setRealtimeCallback(RealtimeCallback cb)           { realtimeCb_ = cb; }
    void setSysExCallback(SysExCallback cb)                 { sysexCb_ = cb; }
    void setActivityCallback(ActivityCallback cb)           { activityCb_ = cb; }

private:
    void dispatch();   // act on a complete channel message in status_/data_
    void txPump();     // move queued bytes into the UART FIFO, never waiting

    static constexpr uint16_t kSysExMax = 256;

    NoteOnCallback       noteOnCb_ = nullptr;
    NoteOffCallback      noteOffCb_ = nullptr;
    CCCallback           ccCb_ = nullptr;
    ProgramChangeCallback programChangeCb_ = nullptr;
    PitchBendCallback    pitchBendCb_ = nullptr;
    RealtimeCallback     realtimeCb_ = nullptr;
    SysExCallback        sysexCb_ = nullptr;
    ActivityCallback     activityCb_ = nullptr;

    uint8_t  status_ = 0;        // running status, 0 = none
    uint8_t  data_[2] = {0, 0};
    uint8_t  dataLen_ = 0;       // data bytes collected for the current message
    uint8_t  dataNeeded_ = 0;    // 1 or 2, depending on the status byte
    bool     inSysEx_ = false;
    uint16_t sysexLen_ = 0;
    uint8_t  sysex_[kSysExMax];
};

MIDISerial& midiSerial();  // the single instance; core and instruments share it

#endif // __MIDI_SERIAL_H__
