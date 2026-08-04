// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// Same-core SPSC replacement for the cross-core SIO FIFO IPC; used by the PicoFaceMD build
// where UI/MIDI and the audio IRQ share Core 0 and Core 1 is a dedicated sound-chip worker.

#ifndef MD_IPC_H
#define MD_IPC_H

#include <stdint.h>

enum {
    IPC_CMD_MD_NOTE_ON    = 0x0B,
    IPC_CMD_MD_NOTE_OFF   = 0x0C,
    IPC_CMD_MD_PARAM      = 0x0D,
    IPC_CMD_MD_PITCH_BEND = 0x0E,
    IPC_CMD_MD_CC         = 0x0F
};

static inline uint32_t ipc_pack(uint8_t type, uint8_t d1, uint16_t d2) {
    return ((uint32_t)type << 24) | ((uint32_t)d1 << 16) | d2;
}

static inline uint8_t ipc_type(uint32_t pkt) { return pkt >> 24; }
static inline uint8_t ipc_d1(uint32_t pkt) { return (pkt >> 16) & 0xFF; }
static inline uint16_t ipc_d2(uint32_t pkt) { return pkt & 0xFFFF; }

// C++17 inline variables: exactly ONE ring shared across all translation units
// (a header-level `static` would give every .cpp its own private copy and
// silently disconnect producer and consumer).
// 256 entries: a full ring silently DROPPING a note-off is the classic
// stuck-note bug -- extreme-play bursts overflowed the old 64-entry ring.
inline volatile uint32_t md_ipc_buf[256];
inline volatile uint16_t md_ipc_head = 0;
inline volatile uint16_t md_ipc_tail = 0;
inline volatile uint32_t md_ipc_dropped = 0;  // diagnostic (shown as D on the OLED)

static inline void md_ipc_push(uint32_t pkt) {
    uint16_t next_head = (md_ipc_head + 1) & 255;
    if (next_head != md_ipc_tail) {
        md_ipc_buf[md_ipc_head] = pkt;
        md_ipc_head = next_head;
    } else {
        md_ipc_dropped++;
    }
}

static inline bool md_ipc_pop(uint32_t* pkt) {
    if (md_ipc_head != md_ipc_tail) {
        *pkt = md_ipc_buf[md_ipc_tail];
        md_ipc_tail = (md_ipc_tail + 1) & 255;
        return true;
    }
    return false;
}

static inline void ipc_send_note_on(uint8_t note, uint8_t vel) {
    md_ipc_push(ipc_pack(IPC_CMD_MD_NOTE_ON, note, vel));
}

static inline void ipc_send_note_off(uint8_t note) {
    md_ipc_push(ipc_pack(IPC_CMD_MD_NOTE_OFF, note, 0));
}

static inline void ipc_send_cc(uint8_t cc, uint8_t value) {
    md_ipc_push(ipc_pack(IPC_CMD_MD_CC, cc, value));
}

static inline void ipc_send_param(uint8_t paramId, uint16_t value) {
    md_ipc_push(ipc_pack(IPC_CMD_MD_PARAM, paramId, value));
}

static inline void ipc_send_pitch_bend(uint16_t bend14) {
    md_ipc_push(ipc_pack(IPC_CMD_MD_PITCH_BEND, 0, bend14));
}

#endif // MD_IPC_H
