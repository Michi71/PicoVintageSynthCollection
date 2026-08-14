// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "D5_Midi.h"

#include "midi_output_usb.h"
#include "midi_serial.h"

bool D5_Midi::accepts(uint8_t ch) const {
    const int want = controller_.midiChannel();
    return want >= 16 || ch == (uint8_t)want;      // 16 = Omni
}

void D5_Midi::onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!accepts(ch)) return;
    // A note-on with velocity zero is a note-off; instruments that miss this
    // hang notes with any sequencer that uses running status.
    if (vel == 0) { bridge_.noteOff(note); return; }
    bridge_.noteOn(note, vel);
}

void D5_Midi::onNoteOff(uint8_t ch, uint8_t note) {
    if (!accepts(ch)) return;
    bridge_.noteOff(note);
}

void D5_Midi::onControlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    if (!accepts(ch)) return;
    switch (cc) {
        case 1:                                     // mod wheel -> P-Mod lever
            bridge_.setModWheel(value * (1.0f / 127.0f));
            break;
        case 5:                                     // portamento time
            bridge_.setPortamentoTime(value * 100 / 127);
            break;
        case 64:                                    // hold pedal
            bridge_.setSustain(value >= 64);
            break;
        case 65:                                    // portamento switch
            bridge_.setPortamentoSwitch(value >= 64);
            break;
        // RPN 0 is pitch bend sensitivity. The D-50's own handler (EPROM
        // 0x4E72-0x4EA4) reads only the data-entry MSB and clamps it to 12
        // semitones; the override lasts until the next patch change.
        case 6:
            if (rpnMsb_ == 0 && rpnLsb_ == 0) bridge_.setBendRange(value);
            break;
        case 100: rpnLsb_ = value; break;           // RPN LSB
        case 101: rpnMsb_ = value; break;           // RPN MSB

        // The D-50's own CC list, read from its dispatch table (EPROM
        // 0x4E33): 1, 5, 6, 7, 38, 64, 65, 98, 99, 100, 101. Everything
        // below this line is ours, not the machine's -- 91 and 93 are
        // General MIDI sends from 1991, four years after the D-50, and CC0
        // is how the six D-05 banks are reached at all. Harmless, useful,
        // and not original: worth knowing when comparing against a real one.
        case 7:                                     // channel volume
            bridge_.setVolume(value * 100 / 127);
            break;
        case 91:                                    // reverb send
            bridge_.setReverb(value * 100 / 127);
            break;
        case 93:                                    // chorus send
            bridge_.setChorus(value * 100 / 127);
            break;
        case 120:                                   // all sound off
        case 123:                                   // all notes off
            bridge_.allNotesOff();
            break;
        default:
            break;
    }
}

void D5_Midi::onPitchBend(uint8_t ch, int16_t bend) {
    if (!accepts(ch)) return;
    // The bender range is a patch parameter (pb[26], 0..12 semitones,
    // overrideable by RPN 0). 55 of the bank's 64 patches sit on the MIDI
    // default of two; the other nine now get theirs.
    const float semis = (bend / 8192.0f) * bridge_.bendRangeSemis();
    bridge_.setPitchBendSemis(semis);
}

void D5_Midi::onChannelPressure(uint8_t ch, uint8_t value) {
    if (!accepts(ch)) return;
    bridge_.setAftertouch(value * (1.0f / 127.0f));
}

// ---------------------------------------------------------------- SysEx
//
// The D-50 speaks Roland's one-way exclusive: F0 41 <device> 14 <command>
// <address, three 7-bit bytes> <data ...> <checksum> F7, where the checksum
// is the two's complement of the address and data bytes in 7 bits. Two
// commands matter here -- DT1 (0x12) carries data, RQ1 (0x11) asks for it
// by address and size.
//
// The address space is a flat 7-bit counter: aa*16384 + bb*128 + cc. Two
// regions are answered here, both verified against a factory dump made by
// the machine itself:
//
//   0x000000  the temporary area -- the 448 bytes of the patch being
//             played, in the same seven 64-byte blocks a bulk dump uses.
//             Writing here is how an editor programs the instrument.
//   0x008000  internal memory, 448 bytes per patch. Readable, so a
//             librarian can pull the bank out; not writable, because our
//             patches live in flash rather than in battery-backed RAM.
//
// (02-00-00 is 0x8000 in that flat counter, which is where a real dump
// starts; the factory file steps its messages 256 bytes at a time from
// there, which is what the chunking below reproduces.)

namespace {
constexpr uint8_t kRoland = 0x41;
constexpr uint8_t kD50    = 0x14;
constexpr uint8_t kRQ1    = 0x11;
constexpr uint8_t kDT1    = 0x12;

constexpr uint32_t kTempBase = 0x000000;
constexpr uint32_t kIntBase  = 0x008000;   // 02-00-00
constexpr int      kPatchLen = D5_Bridge::kPatchBytes;

uint8_t rolandChecksum(uint32_t addr, const uint8_t* data, uint16_t len) {
    uint32_t sum = ((addr >> 14) & 0x7F) + ((addr >> 7) & 0x7F) + (addr & 0x7F);
    for (uint16_t i = 0; i < len; ++i) sum += data[i] & 0x7F;
    return (uint8_t)((128 - (sum % 128)) % 128);
}
}  // namespace

void D5_Midi::sendDT1(uint8_t dev, uint32_t addr, const uint8_t* data, uint16_t len) {
    // 256 data bytes per message, the size the machine's own dumps use.
    while (len > 0) {
        const uint16_t n = len > 256 ? 256 : len;
        uint8_t msg[10 + 256];
        msg[0] = 0xF0; msg[1] = kRoland; msg[2] = dev & 0x7F;
        msg[3] = kD50; msg[4] = kDT1;
        msg[5] = (addr >> 14) & 0x7F;
        msg[6] = (addr >> 7) & 0x7F;
        msg[7] = addr & 0x7F;
        for (uint16_t i = 0; i < n; ++i) msg[8 + i] = data[i] & 0x7F;
        msg[8 + n] = rolandChecksum(addr, data, n);
        msg[9 + n] = 0xF7;
        usbMidiOut().write(msg, (uint16_t)(10 + n));
        midiSerial().write(msg, (uint16_t)(10 + n));
        data += n; addr += n; len = (uint16_t)(len - n);
    }
}

void D5_Midi::answerRequest(uint8_t dev, uint32_t addr, uint32_t size) {
    if (size == 0 || size > 0x10000) return;
    // The temporary area first: an editor asking "what are you playing?"
    if (addr >= kTempBase && addr < kTempBase + kPatchLen) {
        const uint32_t off = addr - kTempBase;
        uint32_t n = kPatchLen - off;
        if (n > size) n = size;
        sendDT1(dev, addr, bridge_.tempPatch() + off, (uint16_t)n);
        return;
    }
    // Internal memory: whichever bank the panel is on, so the sixty-four
    // slots a D-50 librarian expects are the sixty-four it can see.
    if (addr >= kIntBase) {
        const uint32_t off = addr - kIntBase;
        const int bank = bridge_.patch() / 64;
        uint32_t left = size;
        uint32_t a = addr;
        uint32_t o = off;
        while (left > 0) {
            const int slot = (int)(o / kPatchLen);
            const uint32_t inSlot = o % kPatchLen;
            if (slot >= 64) return;
            const uint8_t* p = bridge_.storedPatch(bank * 64 + slot);
            if (!p) return;
            uint32_t n = kPatchLen - inSlot;
            if (n > left) n = left;
            sendDT1(dev, a, p + inSlot, (uint16_t)n);
            a += n; o += n; left -= n;
        }
    }
}

void D5_Midi::onSysEx(const uint8_t* d, uint16_t n) {
    if (n < 10 || d[0] != 0xF0 || d[n - 1] != 0xF7) return;
    if (d[1] != kRoland || d[3] != kD50) return;
    const uint8_t dev = d[2];
    const uint8_t cmd = d[4];
    const uint32_t addr = ((uint32_t)(d[5] & 0x7F) << 14) |
                          ((uint32_t)(d[6] & 0x7F) << 7) | (d[7] & 0x7F);

    if (cmd == kDT1) {
        const uint16_t len = (uint16_t)(n - 10);      // F0 41 dev 14 12 aa bb cc | data | sum F7
        if (rolandChecksum(addr, d + 8, len) != d[n - 2]) return;
        if (addr >= kTempBase && addr < kTempBase + kPatchLen) {
            bridge_.sysexWriteTemp((int)(addr - kTempBase), d + 8, (int)len);
        }
        // Internal memory: kept in RAM, shadowing the flash bank, so a
        // librarian can send a whole sixty-four and play them without
        // reflashing. The D-50's own sixty-four sit in battery-backed
        // memory; ours do not survive a power cycle.
        else if (addr >= kIntBase) {
            bridge_.sysexWriteStored(addr - kIntBase, d + 8, (int)len);
        }
        return;
    }

    if (cmd == kRQ1 && n >= 13) {   // F0 41 dev 14 11 aa bb cc ss tt uu sum F7
        const uint32_t size = ((uint32_t)(d[8] & 0x7F) << 14) |
                              ((uint32_t)(d[9] & 0x7F) << 7) | (d[10] & 0x7F);
        uint8_t body[6] = {d[5], d[6], d[7], d[8], d[9], d[10]};
        uint32_t sum = 0;
        for (int i = 0; i < 6; ++i) sum += body[i] & 0x7F;
        if ((uint8_t)((128 - (sum % 128)) % 128) != d[11]) return;
        answerRequest(dev, addr, size);
    }
}
