// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef __MIDI_OUTPUT_USB__H__
#define __MIDI_OUTPUT_USB__H__

#include <pico/stdlib.h>
#include "project_config.h"
#include "tusb.h"
#include <stdint.h>

// Outgoing USB MIDI, buffered.
//
// tud_midi_stream_write() is not a "write these bytes" call - it takes what
// fits in the TinyUSB TX FIFO and returns how much that was. The FIFO holds
// CFG_TUD_MIDI_TX_BUFSIZE bytes of four-byte USB packets, and a SysEx packet
// carries only three payload bytes, so at 64 bytes the ceiling is 48 SysEx
// bytes between two tud_task() calls. Anything past that is dropped.
//
// That ceiling is well below a reface DX voice dump (241 bytes across seven
// messages), which is why writing straight to TinyUSB truncated it: the header
// went out, the common block was cut off mid-message, and the four operator
// blocks and the footer never left at all. Worse than the loss itself, a
// truncated SysEx leaves TinyUSB's stream state inside a message, so the next
// bytes written - active sensing - get packed as SysEx continuation and the
// outgoing stream stays corrupted until an F0 resynchronises it.
//
// So instruments queue here instead, and the main loop drains into TinyUSB
// after every tud_task(), a FIFO-full at a time. Nothing blocks: the audio
// producer shares that loop and a dump is several USB frames long.
class MIDIOutputUSB {
public:
    MIDIOutputUSB() = default;

    // Queue a complete message. All or nothing: a message that does not fit is
    // dropped whole and counted, never written in part, because a half SysEx on
    // the wire is worse for the receiver than a missing one. Never blocks.
    bool write(const uint8_t* data, uint16_t len);

    // Main loop, straight after tud_task(): hand over as much as TinyUSB takes.
    void process();

    bool     empty() const { return _count == 0; }
    uint16_t pending() const { return _count; }
    uint32_t dropped() const { return _dropped; }   // messages, not bytes

private:
    // Room for a full reface DX voice dump (241 bytes) several times over, so a
    // burst of dump requests queues rather than drops. 1 KB of RAM against a
    // silently lost patch transfer is a trade worth making.
    static constexpr uint16_t kBufSize = 1024;

    uint8_t  _buf[kBufSize] = {0};
    uint16_t _head    = 0;   // read position
    uint16_t _count   = 0;   // bytes queued
    uint32_t _dropped = 0;
};

MIDIOutputUSB& usbMidiOut();

#endif // __MIDI_OUTPUT_USB__H__
