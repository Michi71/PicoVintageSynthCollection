// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "midi_serial.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

// DIN MIDI is 31250 baud, 8N1, no flow control.
static constexpr uint32_t kMidiBaud = 31250;
#define MIDI_UART uart1

// Lock-free SPSC ring: written by the RX interrupt, read by process().
// 256 bytes is a good 80 ms of MIDI at full rate - far more than any blocking
// menu screen holds the main loop.
static constexpr uint16_t kRxRingSize = 256;
static volatile uint8_t  s_rx[kRxRingSize];
static volatile uint16_t s_rxHead = 0;      // written by the IRQ
static volatile uint16_t s_rxTail = 0;      // written by process()
static volatile uint32_t s_rxDropped = 0;   // diagnostics: ring was full

// Kept RAM resident and tiny - it only moves bytes, all parsing happens in process().
static void __not_in_flash_func(midi_uart_irq)()
{
    while (uart_is_readable(MIDI_UART)) {
        const uint8_t b = uart_getc(MIDI_UART);
        const uint16_t next = (uint16_t)((s_rxHead + 1) % kRxRingSize);
        if (next != s_rxTail) {
            s_rx[s_rxHead] = b;
            s_rxHead = next;
        } else {
            s_rxDropped++;   // dropping is better than blocking in an IRQ
        }
    }
}

MIDISerial& midiSerial()
{
    static MIDISerial instance;
    return instance;
}

void MIDISerial::init()
{
    uart_init(MIDI_UART, kMidiBaud);
    gpio_set_function(PIN_MIDI_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_MIDI_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(MIDI_UART, false, false);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);

    const int irq = (MIDI_UART == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, midi_uart_irq);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(MIDI_UART, true, false);   // RX only; TX is polled

    // Below the audio DMA but above USB - a dropped MIDI byte is a stuck note.
    irq_set_priority(irq, 0x40);
}

void MIDISerial::write(const uint8_t* data, uint16_t len)
{
    // uart_putc_raw blocks only while the 32-byte TX FIFO is full;
    // a 3-byte message never gets that far.
    for (uint16_t i = 0; i < len; ++i) {
        uart_putc_raw(MIDI_UART, data[i]);
    }
}

void MIDISerial::sendNoteOn(uint8_t ch, uint8_t note, uint8_t vel)
{
    const uint8_t msg[3] = {
        (uint8_t)(0x90 | (ch & 0x0F)),
        (uint8_t)(note & 0x7F),
        (uint8_t)(vel & 0x7F)
    };
    write(msg, 3);
}

void MIDISerial::sendNoteOff(uint8_t ch, uint8_t note, uint8_t vel)
{
    const uint8_t msg[3] = {
        (uint8_t)(0x80 | (ch & 0x0F)),
        (uint8_t)(note & 0x7F),
        (uint8_t)(vel & 0x7F)
    };
    write(msg, 3);
}

void MIDISerial::sendControlChange(uint8_t ch, uint8_t cc, uint8_t value)
{
    const uint8_t msg[3] = {
        (uint8_t)(0xB0 | (ch & 0x0F)),
        (uint8_t)(cc & 0x7F),
        (uint8_t)(value & 0x7F)
    };
    write(msg, 3);
}

void MIDISerial::sendProgramChange(uint8_t ch, uint8_t program)
{
    const uint8_t msg[2] = {
        (uint8_t)(0xC0 | (ch & 0x0F)),
        (uint8_t)(program & 0x7F)
    };
    write(msg, 2);
}

void MIDISerial::sendPitchBend(uint8_t ch, uint16_t value14)
{
    const uint8_t msg[3] = {
        (uint8_t)(0xE0 | (ch & 0x0F)),
        (uint8_t)(value14 & 0x7F),
        (uint8_t)((value14 >> 7) & 0x7F)
    };
    write(msg, 3);
}

void MIDISerial::process()
{
    while (s_rxTail != s_rxHead) {
        const uint8_t b = s_rx[s_rxTail];
        s_rxTail = (uint16_t)((s_rxTail + 1) % kRxRingSize);

        // real-time bytes may appear anywhere, even between the data bytes
        // of another message, and must not disturb the running status
        if (b >= 0xF8) {
            if (realtimeCb_) {
                realtimeCb_(b);
            }
            continue;
        }

        if (b == 0xF7) {
            if (inSysEx_) {
                if (sysexCb_) {
                    sysexCb_(sysex_, sysexLen_);
                }
                inSysEx_ = false;
            }
            continue;
        }

        if (b >= 0x80) {
            if (b == 0xF0) {
                inSysEx_  = true;
                sysexLen_ = 0;
                // a System Common message cancels running status
                status_ = 0;
                continue;
            }
            if (b >= 0xF1) { // 0xF1..0xF6
                status_  = 0;
                inSysEx_ = false;
                continue;
            }
            // channel voice message: this becomes the new running status
            inSysEx_  = false;
            status_   = b;
            dataLen_  = 0;
            // Program Change and Channel Aftertouch carry a single data byte
            dataNeeded_ = ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 1 : 2;
            if (activityCb_) {
                activityCb_();
            }
            continue;
        }

        if (inSysEx_) {
            // an oversized dump is truncated rather than smashing the stack
            if (sysexLen_ < kSysExMax) {
                sysex_[sysexLen_++] = b;
            }
            continue;
        }

        // data without a preceding status byte - we joined the stream mid-message
        if (status_ == 0) {
            continue;
        }

        data_[dataLen_++] = b;
        if (dataLen_ == dataNeeded_) {
            dispatch();
            // dataLen_ back to 0, not status_ - that is exactly what running status means
            dataLen_ = 0;
        }
    }
}

void MIDISerial::dispatch()
{
    const uint8_t type = status_ & 0xF0;
    const uint8_t ch   = status_ & 0x0F;

    switch (type) {
        case 0x80:
            if (noteOffCb_) {
                noteOffCb_(data_[0], data_[1], ch);
            }
            break;

        case 0x90:
            // note-on with velocity 0 is the classic note-off, required for
            // running status to be useful
            if (data_[1] == 0) {
                if (noteOffCb_) {
                    noteOffCb_(data_[0], 0, ch);
                }
            } else {
                if (noteOnCb_) {
                    noteOnCb_(data_[0], data_[1], ch);
                }
            }
            break;

        case 0xB0:
            if (ccCb_) {
                ccCb_(data_[0], data_[1], ch);
            }
            break;

        case 0xC0:
            if (programChangeCb_) {
                programChangeCb_(data_[0], ch);
            }
            break;

        case 0xE0:
            if (pitchBendCb_) {
                pitchBendCb_((uint16_t)(data_[0] | (data_[1] << 7)), ch);
            }
            break;

        default:
            // poly and channel aftertouch are not used by any instrument here
            break;
    }
}
