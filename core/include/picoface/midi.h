// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef PICOFACE_MIDI_H
#define PICOFACE_MIDI_H

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace picoface {
namespace midi {

// MIDI status bytes (high nibble for channel messages, full byte for system).
enum class Status : uint8_t {
    NoteOff           = 0x80,
    NoteOn            = 0x90,
    PolyAftertouch    = 0xA0,
    ControlChange     = 0xB0,
    ProgramChange     = 0xC0,
    ChannelAftertouch = 0xD0,
    PitchBend         = 0xE0,
    SystemExclusive   = 0xF0,
};

constexpr uint8_t kMaxChannels = 16;
constexpr uint8_t kOmniChannel = 0xFF;  // 0xFF = receive on all channels

// 14-bit pitch bend is transmitted as offset binary: 0..16383, center 8192.
constexpr int16_t  kPitchBendMin    = -8192;
constexpr int16_t  kPitchBendMax    =  8191;
constexpr uint16_t kPitchBendCenter =  8192;

// Compact 4-byte MIDI event.
// POD, trivially copyable: safe to pass through a lock-free SPSC queue
// between core0 (USB/UART RX) and core1 (audio/engine) on the RP2350.
struct Event {
    uint8_t status;    // Status byte without channel nibble (see enum class Status)
    uint8_t channel;   // Channel 0..15 (or kOmniChannel for wildcard matching)
    uint8_t data1;     // Note number / CC number / program / bend LSB
    uint8_t data2;     // Velocity / CC value / bend MSB (0 for 1-data-byte messages)

    // --- Factory methods ---------------------------------------------------

    static constexpr Event noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
        return Event{static_cast<uint8_t>(Status::NoteOn),
                     static_cast<uint8_t>(ch & 0x0F), note, vel};
    }

    static constexpr Event noteOff(uint8_t ch, uint8_t note, uint8_t vel) {
        return Event{static_cast<uint8_t>(Status::NoteOff),
                     static_cast<uint8_t>(ch & 0x0F), note, vel};
    }

    static constexpr Event controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
        return Event{static_cast<uint8_t>(Status::ControlChange),
                     static_cast<uint8_t>(ch & 0x0F), cc, value};
    }

    static constexpr Event programChange(uint8_t ch, uint8_t program) {
        return Event{static_cast<uint8_t>(Status::ProgramChange),
                     static_cast<uint8_t>(ch & 0x0F), program, 0};
    }

    // bend: -8192..8191. Encoded as 14-bit offset binary, split into
    // 7-bit LSB (data1) and 7-bit MSB (data2).
    static constexpr Event pitchBend(uint8_t ch, int16_t bend) {
        const uint16_t value14 =
            static_cast<uint16_t>(static_cast<int32_t>(bend) + kPitchBendCenter);
        return Event{static_cast<uint8_t>(Status::PitchBend),
                     static_cast<uint8_t>(ch & 0x0F),
                     static_cast<uint8_t>(value14 & 0x7F),          // 7-bit LSB
                     static_cast<uint8_t>((value14 >> 7) & 0x7F)};  // 7-bit MSB
    }

    // --- Accessors ----------------------------------------------------------

    // Reconstruct bend value (-8192..8191) from the 7-bit LSB/MSB pair.
    constexpr int16_t pitchBendValue() const {
        const uint16_t value14 = static_cast<uint16_t>(
            (static_cast<uint16_t>(data2 & 0x7F) << 7) |
             static_cast<uint16_t>(data1 & 0x7F));
        return static_cast<int16_t>(static_cast<int32_t>(value14) - kPitchBendCenter);
    }

    constexpr Status type() const {
        return static_cast<Status>(status);
    }
};

static_assert(sizeof(Event) == 4,
              "Event must be exactly 4 bytes (one word) for atomic lock-free transfer");
static_assert(std::is_trivially_copyable<Event>::value,
              "Event must be trivially copyable for the lock-free core0/core1 queue");

}  // namespace midi
}  // namespace picoface

#endif  // PICOFACE_MIDI_H
