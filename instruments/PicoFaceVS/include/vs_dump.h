// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_dump.h -- SysEx going out, and the sample dump standard in both
// directions, as the Prophet VS does them (owner's manual ch. 14, firmware
// @0x57E2 ff. and @0x4C80 ff.):
//
//   F0 01 0A nn <164 nibbles> F7           one program, nn = 0..99
//   F0 01 0A 64 <100 x 164 nibbles> F7     all programs
//   F0 01 0A 7F <32 x 384 nibbles> F7      all user waves, the 192-byte records
//   F0 7E cc 01 ..., F0 7E cc 02 ...       one user wave, sample dump standard
//
// One transmit job runs at a time. It is fed from poll() a few bytes at a
// time, as the output rings of the two wires take it.
#ifndef VS_DUMP_H
#define VS_DUMP_H

#include <cstddef>
#include <cstdint>

#include "provs/engine.h"

// SysEx output, provided by the adapter (both wires) or by the host test.
// vs_sysex_room() is how many bytes may be written right now.
size_t vs_sysex_room();
void   vs_sysex_write(const uint8_t* data, size_t len);

class VS_Dump {
public:
    bool busy() const { return job_ != Job::None || sdsWave_ >= 0; }

    // Each returns false, and does nothing, while another job is running.
    bool sendProgram(const uint8_t* fields, int slot);
    bool sendBank();
    bool sendWaves();
    // Sample dump standard, for a user wave 0-31 or a ROM wave 32-126 as the
    // VS answers both: the header, then three packets of 60 words. The
    // next step goes out on an acknowledge, or after the standard's timeouts
    // when the receiver says nothing.
    bool sendWave(int wave, uint8_t channel, uint32_t nowMs);

    // Feeds the output. Returns true once when a job has finished.
    bool poll(uint32_t nowMs);

    // Takes one universal non-realtime message (F0 7E cc ...): a dump header,
    // a data packet, a dump request, or the handshake of a dump going out.
    // Returns the user wave slot that has just been received completely, its
    // samples in received(); -1 otherwise.
    int sampleDump(const uint8_t* data, size_t length, uint8_t channel, uint32_t nowMs);
    const int16_t* received() const { return rx_; }

private:
    enum class Job : uint8_t { None, Program, Bank, Waves };

    void start(Job job, uint8_t number, size_t unitNibbles, int units);
    uint8_t byteAt(size_t i);
    void sdsHeader();
    void sdsPacket(int packet);
    static void handshake(uint8_t channel, uint8_t what, uint8_t packet);

    Job     job_ = Job::None;
    size_t  pos_ = 0, total_ = 0, unitNibbles_ = 0;
    uint8_t number_ = 0;                 // the nn of the header
    int     unit_ = -1;                  // the program or wave whose nibbles are in cache_
    uint8_t cache_[2 * provs::Engine::kWaveRecordBytes];
    uint8_t program_[provs::Engine::kVsFields];

    // sample dump going out
    int      sdsWave_ = -1;
    int      sdsNext_ = 0;               // 0 = header, 1..3 = packet 0..2
    bool     sdsGo_ = false;             // acknowledged: no need to wait
    uint32_t sdsDeadline_ = 0;
    uint8_t  sdsChannel_ = 0;
    int16_t  sdsData_[provs::Engine::kWaveSamples];

    // sample dump coming in
    int      rxWave_ = -1;
    int      rxCount_ = 0;               // words so far
    int      rxBytes_ = 2;               // MIDI bytes per word, by the sample format
    int16_t  rx_[provs::Engine::kWaveSamples];
};

#endif // VS_DUMP_H
