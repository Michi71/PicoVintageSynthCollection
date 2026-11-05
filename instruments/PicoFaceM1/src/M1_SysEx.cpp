// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "M1_SysEx.h"

#include <cstring>

namespace {

// Staging image for incoming dumps: a bank is unpacked here and only copied
// into memory once the whole message has arrived with the right length.
m1::MemoryImage g_stage;

enum : uint8_t {
    F_MODE_REQ = 0x12, F_PROG_REQ = 0x10, F_ALLPROG_REQ = 0x1C, F_COMBI_REQ = 0x19,
    F_ALLCOMBI_REQ = 0x1D, F_GLOBAL_REQ = 0x0E, F_ALLDATA_REQ = 0x0F, F_SEQ_REQ = 0x18,
    F_DRUMNAME_REQ = 0x1F, F_MSNAME_REQ = 0x16, F_PROG_WRITE = 0x11, F_COMBI_WRITE = 0x1A,
    F_PROG = 0x40, F_ALLPROG = 0x4C, F_COMBI = 0x49, F_ALLCOMBI = 0x4D, F_SEQ = 0x48,
    F_GLOBAL = 0x51, F_ALLDATA = 0x50, F_MODE_CHANGE = 0x4E, F_PARAM = 0x41, F_MODE_DATA = 0x42,
    R_FORMAT_ERROR = 0x26, R_LOAD_OK = 0x23, R_LOAD_ERROR = 0x24, R_WRITE_OK = 0x21, R_WRITE_ERROR = 0x22,
};

constexpr size_t kProg = 143, kCombi = 124, kGlobal = 21 + 840;

uint8_t preBytes(uint8_t f) {
    switch (f) {
        case F_ALLPROG_REQ: case F_ALLCOMBI_REQ: case F_GLOBAL_REQ: case F_ALLDATA_REQ:
        case F_SEQ_REQ: case F_ALLPROG: case F_ALLCOMBI: case F_GLOBAL: return 1;
        case F_PROG_WRITE: case F_COMBI_WRITE: case F_MODE_CHANGE: return 2;
        case F_ALLDATA: case F_SEQ: return 3;
        case F_PARAM: return 4;
        default: return 0;
    }
}

}  // namespace

// Output, provided by the instrument (USB and DIN on the device, a capture
// buffer on the host).
extern size_t m1_sysex_room();
extern void   m1_sysex_write(const uint8_t* data, size_t len);

void M1_SysEx::feed(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) byte(d[i]);
}

void M1_SysEx::byte(uint8_t b) {
    if (b == 0xF0) {
        rx_ = Rx::Header;
        hdrLen_ = 0;
        hdr_[hdrLen_++] = b;
        return;
    }
    if (b == 0xF7) {
        if (rx_ == Rx::Payload) finish();
        rx_ = Rx::Idle;
        return;
    }
    if (b & 0x80) { rx_ = Rx::Idle; return; }       // stray status byte: message lost

    switch (rx_) {
    case Rx::Idle:
    case Rx::Skip:
        return;
    case Rx::Header:
        hdr_[hdrLen_++] = b;
        if (hdrLen_ == 2) {
            universal_ = b == 0x7E;
            if (b != 0x42 && b != 0x7E) { rx_ = Rx::Skip; return; }
        }
        if (universal_ && hdrLen_ == 5) {
            // F0 7E ch 06 01: device inquiry
            if (hdr_[3] == 0x06 && hdr_[4] == 0x01) {
                const uint8_t r[] = {0xF0, 0x7E, (uint8_t)(host_.globalChannel() & 0x0F), 0x06, 0x02,
                                     0x42, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0xF7};
                if (m1_sysex_room() >= sizeof r) m1_sysex_write(r, sizeof r);
            }
            rx_ = Rx::Skip;
            return;
        }
        if (!universal_ && hdrLen_ == 5) {
            // F0 42 3n 19 func
            const int ch = host_.globalChannel();
            if ((hdr_[2] & 0xF0) != 0x30 || hdr_[3] != 0x19 ||
                (ch < 16 && (hdr_[2] & 0x0F) != ch)) { rx_ = Rx::Skip; return; }
            func_ = b;
            preNeed_ = preBytes(func_);
            preLen_ = 0;
            groupLen_ = 0;
            out_ = 0;
            dst_ = reinterpret_cast<uint8_t*>(&g_stage);
            switch (func_) {
                case F_PROG:   cap_ = kProg; break;
                case F_COMBI:  cap_ = kCombi; break;
                case F_ALLPROG: cap_ = 100 * kProg; dst_ = g_stage.programs; break;
                case F_ALLCOMBI: cap_ = 100 * kCombi; dst_ = g_stage.combis; break;
                case F_GLOBAL: cap_ = kGlobal; break;
                case F_ALLDATA: cap_ = sizeof(m1::MemoryImage); break;
                default: cap_ = 0; break;
            }
            rx_ = Rx::Payload;
        }
        return;
    case Rx::Payload:
        if (preLen_ < preNeed_) { pre_[preLen_++] = b; return; }
        if (cap_ == 0) return;
        // 7-bit groups: the first byte carries bit 7 of the next seven.
        group_[groupLen_++] = b;
        if (groupLen_ == 8) {
            for (int j = 1; j < 8; ++j) {
                const uint8_t v = (uint8_t)(group_[j] | (((group_[0] >> (j - 1)) & 1) << 7));
                if (out_ < cap_) dst_[out_] = v;
                ++out_;
            }
            groupLen_ = 0;
        }
        return;
    }
}

void M1_SysEx::finish() {
    ++rxCount_;
    if (cap_ && groupLen_ > 1) {           // a short last group
        for (int j = 1; j < groupLen_; ++j) {
            const uint8_t v = (uint8_t)(group_[j] | (((group_[0] >> (j - 1)) & 1) << 7));
            if (out_ < cap_) dst_[out_] = v;
            ++out_;
        }
        groupLen_ = 0;
    }
    if (preLen_ < preNeed_) { ++rxErrors_; reply(R_FORMAT_ERROR); return; }
    m1::Memory& mem = host_.memory();
    // Bank byte 0000 00mc: only the internal memory in the 100/100 layout.
    const bool internal = preNeed_ == 0 || (pre_[0] & 0x03) == 0;
    auto loadBank = [&](size_t need, uint8_t* to, size_t at) {
        if (out_ < need) { ++rxErrors_; reply(R_FORMAT_ERROR); return; }
        if (!internal || mem.protect) { ++rxErrors_; reply(R_LOAD_ERROR); return; }
        memcpy(to, dst_, need);
        mem.touch(at, need);
        host_.memoryLoaded();
        reply(R_LOAD_OK);
    };
    m1::MemoryImage& img = mem.image();
    switch (func_) {
    case F_PROG:
        if (out_ < kProg) { ++rxErrors_; reply(R_FORMAT_ERROR); break; }
        memcpy(host_.programEdit(), dst_, kProg);
        host_.programEditLoaded();
        reply(R_LOAD_OK);
        break;
    case F_COMBI:
        if (out_ < kCombi) { ++rxErrors_; reply(R_FORMAT_ERROR); break; }
        memcpy(host_.combiEdit(), dst_, kCombi);
        host_.combiEditLoaded();
        reply(R_LOAD_OK);
        break;
    case F_ALLPROG:
        loadBank(100 * kProg, img.programs, offsetof(m1::MemoryImage, programs));
        break;
    case F_ALLCOMBI:
        loadBank(100 * kCombi, img.combis, offsetof(m1::MemoryImage, combis));
        break;
    case F_GLOBAL:
        loadBank(kGlobal, img.global, 0);
        break;
    case F_ALLDATA:
        // The sequencer part is read past and dropped: there is no sequencer.
        loadBank(sizeof(m1::MemoryImage), img.global, 0);
        break;
    case F_PROG_WRITE: {
        const bool ok = internal && pre_[1] < 100 && !mem.protect && host_.writeProgram(pre_[1]);
        reply(ok ? R_WRITE_OK : R_WRITE_ERROR);
        break;
    }
    case F_COMBI_WRITE: {
        const bool ok = internal && pre_[1] < 100 && !mem.protect && host_.writeCombi(pre_[1]);
        reply(ok ? R_WRITE_OK : R_WRITE_ERROR);
        break;
    }
    case F_MODE_REQ: {
        const uint8_t r[] = {0xF0, 0x42, (uint8_t)(0x30 | (host_.globalChannel() & 0x0F)), 0x19,
                             F_MODE_DATA, (uint8_t)host_.m1Mode(), 0x00, 0x00, 0x00, 0xF7};
        if (m1_sysex_room() >= sizeof r) m1_sysex_write(r, sizeof r);
        break;
    }
    case F_PROG_REQ:     sendProgramEdit(); break;
    case F_COMBI_REQ:    sendCombiEdit(); break;
    case F_ALLPROG_REQ:  if (internal) sendAllPrograms(); else reply(R_LOAD_ERROR); break;
    case F_ALLCOMBI_REQ: if (internal) sendAllCombis(); else reply(R_LOAD_ERROR); break;
    case F_GLOBAL_REQ:   if (internal) sendGlobal(); else reply(R_LOAD_ERROR); break;
    case F_ALLDATA_REQ:  if (internal) sendAllData(); else reply(R_LOAD_ERROR); break;
    case F_MODE_CHANGE:
        // 000b 0mmm: b = 1 leaves the mode alone (NOTE 2).
        if (!(pre_[0] & 0x10) && (pre_[0] & 7) <= 4) host_.setM1Mode(pre_[0] & 7);
        reply(R_LOAD_OK);
        break;
    case F_PARAM: {
        // Value: 14 bits, bits 13.. are the sign (NOTE 13).
        int v = pre_[2] | (pre_[3] << 7);
        if (v & 0x2000) v -= 0x4000;
        reply(host_.parameterChange(pre_[0], pre_[1], v) ? R_LOAD_OK : R_LOAD_ERROR);
        break;
    }
    default:
        // Sequencer data, card names: there is no sequencer and no card.
        reply(R_LOAD_ERROR);
        break;
    }
}

void M1_SysEx::reply(uint8_t func) {
    const uint8_t r[] = {0xF0, 0x42, (uint8_t)(0x30 | (host_.globalChannel() & 0x0F)), 0x19, func, 0xF7};
    if (replyLen_ == 0) { memcpy(replyBuf_, r, sizeof r); replyLen_ = sizeof r; }
    poll();
}

void M1_SysEx::queueDump(uint8_t func, const uint8_t* data, size_t len, const uint8_t* pre, size_t preLen) {
    if (txState_ != Tx::Idle) return;                 // one at a time; a second request is dropped
    txHead_[0] = 0xF0; txHead_[1] = 0x42;
    txHead_[2] = (uint8_t)(0x30 | (host_.globalChannel() & 0x0F));
    txHead_[3] = 0x19; txHead_[4] = func;
    txHeadLen_ = 5;
    for (size_t i = 0; i < preLen; ++i) txHead_[txHeadLen_++] = pre[i];
    txHeadPos_ = 0;
    txSrc_ = data; txLen_ = len; txPos_ = 0;
    txGroupLen_ = txGroupPos_ = 0;
    txState_ = Tx::Head;
    poll();
}

void M1_SysEx::sendProgramEdit() { queueDump(F_PROG, host_.programEdit(), kProg, nullptr, 0); }
void M1_SysEx::sendCombiEdit()   { queueDump(F_COMBI, host_.combiEdit(), kCombi, nullptr, 0); }
void M1_SysEx::sendAllPrograms() {
    const uint8_t pre[1] = {0};
    queueDump(F_ALLPROG, host_.memory().image().programs, 100 * kProg, pre, 1);
}
void M1_SysEx::sendAllCombis() {
    const uint8_t pre[1] = {0};
    queueDump(F_ALLCOMBI, host_.memory().image().combis, 100 * kCombi, pre, 1);
}
void M1_SysEx::sendGlobal() {
    const uint8_t pre[1] = {0};
    queueDump(F_GLOBAL, host_.memory().image().global, kGlobal, pre, 1);
}
void M1_SysEx::sendAllData() {
    // Global, kits, combinations and programs; the sequencer part a receiving
    // M1 expects is not stored here, so this sends with a sequence size of 0
    // and no sequencer data -- an M1 rejects that as too short. Kept for
    // other PicoFaces and editors, which read the four parts.
    const uint8_t pre[3] = {0, 0, 0};
    queueDump(F_ALLDATA, host_.memory().image().global, sizeof(m1::MemoryImage), pre, 3);
}

void M1_SysEx::poll() {
    if (replyLen_ && m1_sysex_room() >= replyLen_) {
        m1_sysex_write(replyBuf_, replyLen_);
        replyLen_ = 0;
    }
    if (txState_ == Tx::Idle) return;
    uint8_t buf[64];
    size_t room = m1_sysex_room();
    if (room > sizeof buf) room = sizeof buf;
    size_t n = 0;
    while (n < room && txState_ != Tx::Idle) {
        switch (txState_) {
        case Tx::Head:
            buf[n++] = txHead_[txHeadPos_++];
            if (txHeadPos_ == txHeadLen_) txState_ = txLen_ ? Tx::Data : Tx::Tail;
            break;
        case Tx::Data:
            if (txGroupPos_ == txGroupLen_) {
                // pack the next up-to-seven bytes
                uint8_t msb = 0;
                size_t k = 0;
                for (; k < 7 && txPos_ < txLen_; ++k, ++txPos_) {
                    const uint8_t v = txSrc_[txPos_];
                    msb |= (uint8_t)((v >> 7) << k);
                    txGroup_[1 + k] = v & 0x7F;
                }
                txGroup_[0] = msb;
                txGroupLen_ = (uint8_t)(1 + k);
                txGroupPos_ = 0;
            }
            buf[n++] = txGroup_[txGroupPos_++];
            if (txGroupPos_ == txGroupLen_ && txPos_ >= txLen_) txState_ = Tx::Tail;
            break;
        case Tx::Tail:
            buf[n++] = 0xF7;
            txState_ = Tx::Idle;
            break;
        default:
            break;
        }
    }
    if (n) m1_sysex_write(buf, n);
}
