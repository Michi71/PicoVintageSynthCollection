// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_dump.cpp -- see vs_dump.h.

#include "vs_dump.h"

#include <cstring>

using provs::Engine;

namespace {

constexpr int kSdsPacketWords = 60;          // 120 bytes of two per 12-bit word
constexpr int kSdsPackets = 3;               // the VS always sends three (@0x5934)
constexpr size_t kSdsPacketBytes = 5 + 2 * kSdsPacketWords + 2;
constexpr uint32_t kSdsHeaderWaitMs = 2000;  // owner's manual: two seconds after the header
constexpr uint32_t kSdsPacketWaitMs = 20;    // and 20 ms after each packet

// Handshake messages of the sample dump standard
constexpr uint8_t kSdsAck = 0x7F, kSdsNak = 0x7E, kSdsCancel = 0x7D, kSdsWait = 0x7C;

}  // namespace

// ---------------------------------------------------------------------------
// Programs and all waves: one long message, produced byte by byte
// ---------------------------------------------------------------------------
void VS_Dump::start(Job job, uint8_t number, size_t unitNibbles, int units) {
    job_ = job;
    number_ = number;
    unitNibbles_ = unitNibbles;
    total_ = 4 + unitNibbles * (size_t)units + 1;
    pos_ = 0;
    unit_ = -1;
}

bool VS_Dump::sendProgram(const uint8_t* fields, int slot) {
    if (busy()) return false;
    memcpy(program_, fields, sizeof program_);
    start(Job::Program, (uint8_t)(slot < 0 ? 0 : (slot > 99 ? 99 : slot)), 2 * Engine::kVsProgramBytes, 1);
    return true;
}

bool VS_Dump::sendBank() {
    if (busy()) return false;
    start(Job::Bank, 0x64, 2 * Engine::kVsProgramBytes, 100);
    return true;
}

bool VS_Dump::sendWaves() {
    if (busy()) return false;
    start(Job::Waves, 0x7F, 2 * Engine::kWaveRecordBytes, Engine::kUserWaves);
    return true;
}

uint8_t VS_Dump::byteAt(size_t i) {
    static const uint8_t kHead[3] = {0xF0, 0x01, 0x0A};
    if (i < 3) return kHead[i];
    if (i == 3) return number_;
    if (i == total_ - 1) return 0xF7;
    const size_t nib = i - 4;
    const int unit = (int)(nib / unitNibbles_);
    if (unit != unit_) {                 // the nibbles of the next program or wave
        unit_ = unit;
        if (job_ == Job::Waves) {
            int16_t samples[Engine::kWaveSamples];
            uint8_t record[Engine::kWaveRecordBytes];
            Engine::userWave(unit, samples);             // a slot never written goes out as silence
            Engine::packWave(samples, record);
            for (int k = 0; k < Engine::kWaveRecordBytes; ++k) {
                cache_[2 * k] = record[k] >> 4;
                cache_[2 * k + 1] = record[k] & 15;
            }
        } else {
            Engine::dumpVsNibbles(job_ == Job::Bank ? Engine::vsProgram(unit) : program_, cache_);
        }
    }
    return cache_[nib % unitNibbles_];
}

// ---------------------------------------------------------------------------
// Sample dump standard, going out (@0x5878)
// ---------------------------------------------------------------------------
bool VS_Dump::sendWave(int wave, uint8_t channel, uint32_t nowMs) {
    if (busy() || wave < 0 || wave > 126) return false;
    if (wave < Engine::kUserWaves) {
        Engine::userWave(wave, sdsData_);                // a slot never written goes out as silence
    } else {
        // A wave of the ROM (@0x5894). A reconstructed one may overshoot
        // twelve bits a little; it is brought down to fit.
        Engine::waveSamples(wave, sdsData_);
        int peak = 2047;
        for (int16_t v : sdsData_) peak = v > peak ? v : (-v - 1 > peak ? -v - 1 : peak);
        if (peak > 2047)
            for (int16_t& v : sdsData_) v = (int16_t)((int)v * 2047 / peak);
    }
    sdsWave_ = wave;
    sdsChannel_ = channel & 15;
    sdsNext_ = 0;
    sdsGo_ = true;
    sdsDeadline_ = nowMs;
    return true;
}

// The header exactly as the VS sends it (template @0x59A4): twelve bits, a
// period of zero, and the length and loop end it states for every wave.
void VS_Dump::sdsHeader() {
    const uint8_t h[21] = {0xF0, 0x7E, sdsChannel_, 0x01, (uint8_t)sdsWave_, 0x00, 0x0C,
                           0x00, 0x00, 0x00, 0x7F, 0x01, 0x00, 0x00, 0x00, 0x00,
                           0x7F, 0x01, 0x00, 0x00, 0xF7};
    vs_sysex_write(h, sizeof h);
}

// 60 words, each as 0xxxxxxx 0xxxxx00, the twelve bits as they are in the
// wave memory (@0x58E8). The VS reads on past the wave for the words after
// the 128th; here they are zero, as the manual says they should be. The
// checksum is the standard's - the VS sends 00 and checks none.
void VS_Dump::sdsPacket(int packet) {
    uint8_t m[kSdsPacketBytes];
    m[0] = 0xF0; m[1] = 0x7E; m[2] = sdsChannel_; m[3] = 0x02; m[4] = (uint8_t)packet;
    uint8_t sum = (uint8_t)(0x7E ^ sdsChannel_ ^ 0x02 ^ packet);
    for (int k = 0; k < kSdsPacketWords; ++k) {
        const int word = packet * kSdsPacketWords + k;
        const int bits = word < Engine::kWaveSamples ? (sdsData_[word] & 0xFFF) : 0;
        const uint8_t b1 = (uint8_t)(bits >> 5), b2 = (uint8_t)((bits & 0x1F) << 2);
        m[5 + 2 * k] = b1;
        m[6 + 2 * k] = b2;
        sum ^= b1 ^ b2;
    }
    m[kSdsPacketBytes - 2] = sum & 0x7F;
    m[kSdsPacketBytes - 1] = 0xF7;
    vs_sysex_write(m, sizeof m);
}

void VS_Dump::handshake(uint8_t channel, uint8_t what, uint8_t packet) {
    const uint8_t m[6] = {0xF0, 0x7E, (uint8_t)(channel & 15), what, (uint8_t)(packet & 0x7F), 0xF7};
    if (vs_sysex_room() >= sizeof m) vs_sysex_write(m, sizeof m);
}

bool VS_Dump::poll(uint32_t nowMs) {
    bool done = false;

    // Up to four pieces a tick: more would only fill the rings faster than
    // the wires empty them.
    for (int piece = 0; piece < 4 && job_ != Job::None; ++piece) {
        size_t n = vs_sysex_room();
        if (n < 16 && n < total_ - pos_) break;
        uint8_t buf[64];
        if (n > sizeof buf) n = sizeof buf;
        if (n > total_ - pos_) n = total_ - pos_;
        for (size_t k = 0; k < n; ++k) buf[k] = byteAt(pos_ + k);
        vs_sysex_write(buf, n);
        pos_ += n;
        if (pos_ == total_) {
            job_ = Job::None;
            done = true;
        }
    }

    if (sdsWave_ >= 0 && (sdsGo_ || (int32_t)(nowMs - sdsDeadline_) >= 0) &&
        vs_sysex_room() >= kSdsPacketBytes) {
        sdsGo_ = false;
        if (sdsNext_ == 0) {
            sdsHeader();
            sdsDeadline_ = nowMs + kSdsHeaderWaitMs;
            sdsNext_ = 1;
        } else if (sdsNext_ <= kSdsPackets) {
            sdsPacket(sdsNext_ - 1);
            sdsDeadline_ = nowMs + kSdsPacketWaitMs;
            ++sdsNext_;
        } else {                                 // the last packet has had its time
            sdsWave_ = -1;
            done = true;
        }
    }
    return done;
}

// ---------------------------------------------------------------------------
// Sample dump standard, coming in (@0x4C80)
// ---------------------------------------------------------------------------
int VS_Dump::sampleDump(const uint8_t* data, size_t length, uint8_t channel, uint32_t nowMs) {
    if (length < 5 || data[0] != 0xF0 || data[1] != 0x7E) return -1;
    // The channel byte is not looked at, as on the VS.
    switch (data[3]) {
    case 0x01: {                                 // dump header
        if (length < 8) return -1;
        const int wave = data[4] | (data[5] << 7);
        if (wave >= Engine::kUserWaves) return -1;             // @0x4D50: only the user waves
        rxWave_ = wave;
        rxCount_ = 0;
        // Two bytes carry up to 14 bits, three up to 21 (@0x4D86); only the
        // upper twelve bits are kept
        rxBytes_ = data[6] < 15 ? 2 : (data[6] < 22 ? 3 : 4);
        memset(rx_, 0, sizeof rx_);
        // The VS says nothing here and lets the sender's two seconds run
        // out; an acknowledge is what the standard asks for and saves them.
        handshake(channel, kSdsAck, 0);
        return -1;
    }
    case 0x02: {                                 // data packet
        if (rxWave_ < 0 || length != kSdsPacketBytes) return -1;
        const uint8_t packet = data[4];
        uint8_t sum = 0;
        for (size_t k = 1; k < kSdsPacketBytes - 2; ++k) sum ^= data[k];
        const uint8_t sent = data[kSdsPacketBytes - 2];
        // The VS sends 00 for a checksum and checks none, so 00 passes too.
        if (sent != (sum & 0x7F) && sent != 0) {
            handshake(channel, kSdsNak, packet);
            return -1;
        }
        const uint8_t* p = data + 5;
        for (int k = 0; k + rxBytes_ <= 2 * kSdsPacketWords && rxCount_ < Engine::kWaveSamples; k += rxBytes_) {
            const int bits = ((p[k] & 0x7F) << 5) | ((p[k + 1] & 0x7F) >> 2);      // @0x4CBE
            rx_[rxCount_++] = (int16_t)(bits >= 0x800 ? bits - 0x1000 : bits);
        }
        if (rxCount_ < Engine::kWaveSamples) {
            handshake(channel, kSdsAck, packet);
            return -1;
        }
        // 128 words are a wave. Whatever else the sender has is not wanted:
        // the VS cancels the dump here (@0x4D40).
        handshake(channel, kSdsCancel, packet);
        const int wave = rxWave_;
        rxWave_ = -1;
        return wave;
    }
    case 0x03:                                   // dump request (@0x4DA4)
        if (length >= 6) sendWave(data[4] | (data[5] << 7), channel, nowMs);
        return -1;
    case kSdsAck:
        if (sdsWave_ >= 0) sdsGo_ = true;
        return -1;
    case kSdsNak:                                // once more, the packet before
        if (sdsWave_ >= 0 && sdsNext_ > 1) { --sdsNext_; sdsGo_ = true; }
        return -1;
    case kSdsCancel:
        sdsWave_ = -1;
        return -1;
    case kSdsWait:                               // until the receiver says go
        if (sdsWave_ >= 0) sdsDeadline_ = nowMs + 30000u;
        return -1;
    default:
        return -1;
    }
}
