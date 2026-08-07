// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "midi_output_usb.h"

MIDIOutputUSB& usbMidiOut()
{
    static MIDIOutputUSB out;
    return out;
}

bool MIDIOutputUSB::write(const uint8_t* data, uint16_t len)
{
    if (!data || len == 0) {
        return true;
    }

    // Nothing is listening, so drop rather than fill the queue with traffic
    // that would flood the host the moment it enumerates.
    if (!tud_midi_mounted()) {
        _head  = 0;
        _count = 0;
        return false;
    }

    if ((uint16_t)(kBufSize - _count) < len) {
        _dropped++;
        return false;
    }

    uint16_t w = (uint16_t)((_head + _count) % kBufSize);
    for (uint16_t i = 0; i < len; i++) {
        _buf[w] = data[i];
        if (++w == kBufSize) w = 0;
    }
    _count = (uint16_t)(_count + len);
    return true;
}

void MIDIOutputUSB::process()
{
    if (_count == 0) {
        return;
    }

    if (!tud_midi_mounted()) {
        _head  = 0;
        _count = 0;
        return;
    }

    // One pass per main loop iteration. tud_midi_stream_write() stops at the
    // FIFO boundary and reports how far it got; whatever is left stays queued
    // for the next round, once tud_task() has freed the endpoint again.
    while (_count > 0) {
        const uint16_t contiguous = (uint16_t)((_head + _count > kBufSize)
                                             ? (kBufSize - _head)
                                             : _count);
        const uint32_t written = tud_midi_stream_write(0, &_buf[_head], contiguous);
        if (written == 0) {
            break;                      // FIFO full - resume after tud_task()
        }
        _head  = (uint16_t)((_head + written) % kBufSize);
        _count = (uint16_t)(_count - written);
    }
}
