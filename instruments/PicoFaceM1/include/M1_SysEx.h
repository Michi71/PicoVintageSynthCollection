// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// M1_SysEx.h -- the M1's system exclusive messages (owner's manual, MIDI
// implementation, sections 2-5 and 3).
//
// Received as a byte stream (the bank dumps are 14-50 KB, far beyond the
// core's 512-byte buffers): the 7-bit groups are unpacked as they arrive into
// a staging image, checked for length at F7, and only then applied.
// Dumps going out are produced the same way, piece by piece, as the output
// rings take them.
#ifndef M1_SYSEX_H
#define M1_SYSEX_H

#include <cstddef>
#include <cstdint>

#include "m1_memory.h"

// What the SysEx layer needs from the rest of the instrument.
class M1_SysExHost {
public:
    virtual ~M1_SysExHost() {}
    // M1 mode numbers (manual NOTE 1): 0 Combination, 1 Edit Combi,
    // 2 Program, 3 Edit Program, 4 Global.
    virtual int  m1Mode() const = 0;
    virtual void setM1Mode(int mode) = 0;
    virtual uint8_t* programEdit() = 0;
    virtual uint8_t* combiEdit() = 0;
    virtual void programEditLoaded() = 0;      // a dump replaced the edit buffer
    virtual void combiEditLoaded() = 0;
    virtual bool writeProgram(int n) = 0;      // edit buffer -> memory n
    virtual bool writeCombi(int n) = 0;
    virtual void memoryLoaded() = 0;           // a bank dump replaced memory
    virtual bool parameterChange(int page, int pos, int value) = 0;
    virtual int  globalChannel() const = 0;    // 0..15, 16 = any
    virtual m1::Memory& memory() = 0;
};

class M1_SysEx {
public:
    explicit M1_SysEx(M1_SysExHost& host) : host_(host) {}

    void feed(const uint8_t* data, size_t len);   // raw bytes, F0..F7
    void poll();                                  // main loop: send what is queued

    // Transmit (also used by the panel's "send" functions).
    void sendProgramEdit();
    void sendCombiEdit();
    void sendAllPrograms();
    void sendAllCombis();
    void sendGlobal();
    void sendAllData();
    bool sending() const { return txState_ != Tx::Idle || replyLen_ > 0; }

    // Diagnostics for the panel.
    uint32_t received() const { return rxCount_; }
    uint32_t errors() const { return rxErrors_; }

private:
    void byte(uint8_t b);
    void finish();
    void reply(uint8_t func);
    void queueDump(uint8_t func, const uint8_t* data, size_t len, const uint8_t* pre, size_t preLen);

    M1_SysExHost& host_;

    // receive
    enum class Rx : uint8_t { Idle, Header, Payload, Skip };
    Rx       rx_ = Rx::Idle;
    uint8_t  hdr_[8] = {0};
    uint8_t  hdrLen_ = 0;
    uint8_t  func_ = 0;
    uint8_t  pre_[4] = {0};       // bytes between the function code and the data
    uint8_t  preNeed_ = 0, preLen_ = 0;
    bool     universal_ = false;
    uint8_t  group_[8] = {0};
    uint8_t  groupLen_ = 0;
    size_t   out_ = 0;            // unpacked bytes so far
    size_t   cap_ = 0;            // how many to keep
    uint8_t* dst_ = nullptr;
    uint32_t rxCount_ = 0, rxErrors_ = 0;

    // transmit: one dump at a time, packed on the fly
    enum class Tx : uint8_t { Idle, Head, Data, Tail };
    Tx       txState_ = Tx::Idle;
    uint8_t  txHead_[12] = {0};
    size_t   txHeadLen_ = 0, txHeadPos_ = 0;
    const uint8_t* txSrc_ = nullptr;
    size_t   txLen_ = 0, txPos_ = 0;      // source bytes
    uint8_t  txGroup_[8] = {0};
    uint8_t  txGroupLen_ = 0, txGroupPos_ = 0;
    uint8_t  replyBuf_[16] = {0};
    uint8_t  replyLen_ = 0;
};

#endif // M1_SYSEX_H
