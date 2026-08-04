// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/ipc.h
//
// Same-core SPSC ring between the control side (MIDI dispatch, front panel,
// SysEx) and the audio producer. Both run on core0: the producer drains the
// ring at the top of render(), so an edit lands on the very next block.
//
// Was a cross-core channel over the SIO FIFO until PicoFaceDX moved to the
// standard runtime model. multicore_fifo_push_blocking() cannot survive that
// move - with no consumer on the other core it would block forever. The
// IPC_CMD_FLASH_LOCK packet is gone with it: it asked core0 to park in RAM
// while core1 wrote flash, and the veeprom write now runs on core0 itself,
// between two audio blocks.
// Modelled on instruments/PicoFaceYC/include/ipc.h.
#ifndef __IPC_H__
#define __IPC_H__

#include <stdint.h>

enum IpcCommand : uint8_t {
    IPC_CMD_DX_NOTE_ON   = 0x0B,
    IPC_CMD_DX_NOTE_OFF  = 0x0C,
    IPC_CMD_DX_PARAM     = 0x0D,
    IPC_CMD_DX_PITCH_BEND = 0x0E,
    IPC_CMD_DX_CC        = 0x0F,
    IPC_CMD_DX_RAW_WRITE = 0x10,  // blockSel: 0=system(unused, the system block stays UI-side), 1=common, 2..5=operator 0..3
    IPC_CMD_DX_PATCH_APPLY = 0x11,
    IPC_CMD_DX_MASTER_TUNE = 0x12,
    IPC_CMD_DX_MASTER_VOLUME = 0x13  // global output level, 0..100; NOT part of a patch
};

enum DxParamId : uint8_t {
    DX_PARAM_OP1_FREQ      = 0,
    DX_PARAM_OP1_LEVEL     = 1,
    DX_PARAM_OP2_FREQ      = 2,
    DX_PARAM_OP2_LEVEL     = 3,
    DX_PARAM_OP3_FREQ      = 4,
    DX_PARAM_OP3_LEVEL     = 5,
    DX_PARAM_OP4_FREQ      = 6,
    DX_PARAM_OP4_LEVEL     = 7,
    DX_PARAM_LFO_SPEED     = 8,
    DX_PARAM_LFO_PMD       = 9,
    DX_PARAM_ALGO          = 10,
    DX_PARAM_OP1_FEEDBACK  = 11
};

static inline uint32_t ipc_pack(uint8_t type, uint8_t d1, uint16_t d2) {
    return ((uint32_t)type << 24) | ((uint32_t)d1 << 16) | (uint32_t)d2;
}

static inline uint8_t ipc_type(uint32_t p) {
    return (uint8_t)(p >> 24);
}

static inline uint8_t ipc_d1(uint32_t p) {
    return (uint8_t)(p >> 16);
}

static inline uint16_t ipc_d2(uint32_t p) {
    return (uint16_t)(p & 0xFFFF);
}

// C++17 inline variables: exactly ONE ring across all translation units. A
// header-level `static` would give every .cpp its own copy and silently
// disconnect producer and consumer.
// 256 entries: a full ring dropping a note-off is the classic stuck-note bug,
// and a SysEx bulk dump pushes one packet per patch byte - 38 for the common
// block, 28 per operator - in a single burst.
inline volatile uint32_t dx_ipc_buf[256];
inline volatile uint16_t dx_ipc_head = 0;
inline volatile uint16_t dx_ipc_tail = 0;
inline volatile uint32_t dx_ipc_dropped = 0;   // diagnostic, shown on the CPU Load screen

static inline void dx_ipc_push(uint32_t pkt) {
    uint16_t next_head = (dx_ipc_head + 1) & 255;
    if (next_head != dx_ipc_tail) {
        dx_ipc_buf[dx_ipc_head] = pkt;
        dx_ipc_head = next_head;
    } else {
        dx_ipc_dropped++;
    }
}

static inline bool dx_ipc_pop(uint32_t* pkt) {
    if (dx_ipc_head != dx_ipc_tail) {
        *pkt = dx_ipc_buf[dx_ipc_tail];
        dx_ipc_tail = (dx_ipc_tail + 1) & 255;
        return true;
    }
    return false;
}

static inline void ipc_send_dx_note_on(uint8_t note, uint8_t vel) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_NOTE_ON, note, vel));
}

static inline void ipc_send_dx_note_off(uint8_t note) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_NOTE_OFF, note, 0));
}

static inline void ipc_send_dx_param(uint8_t paramId, uint8_t value) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_PARAM, paramId, value));
}

static inline void ipc_send_dx_pitch_bend(uint16_t bend14) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_PITCH_BEND, 0, bend14));
}

static inline void ipc_send_dx_cc(uint8_t cc, uint8_t value) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_CC, cc, value));
}

// blockSel: 0=system(unused, the system block stays UI-side), 1=common, 2..5=operator 0..3
static inline void ipc_send_dx_raw_write(uint8_t blockSel, uint8_t byteOffset, uint8_t value) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_RAW_WRITE, byteOffset, ((uint16_t)blockSel << 8) | value));
}

static inline uint8_t ipc_raw_write_block_sel(uint32_t p) {
    return (uint8_t)(ipc_d2(p) >> 8);
}

static inline uint8_t ipc_raw_write_value(uint32_t p) {
    return (uint8_t)(ipc_d2(p) & 0xFF);
}

static inline void ipc_send_dx_patch_apply(void) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_PATCH_APPLY, 0, 0));
}

// rawTune: 16-bit combined value from the 4 SYSTEM Master Tune nibbles
// (SYS_TUNE_0..3), i.e. (tune0<<12)|(tune1<<8)|(tune2<<4)|tune3. The adapter
// decodes this into cents/semitones (see applyIpc() in DX_Instrument.cpp).
static inline void ipc_send_dx_master_tune(uint16_t rawTune) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_MASTER_TUNE, 0, rawTune));
}

// Master volume, 0..100 %. A device-level setting edited in the SYSTEM menu
// and persisted with the settings record -- deliberately NOT a patch field, so
// it survives preset changes and is never written by a SysEx voice dump.
static inline void ipc_send_dx_master_volume(uint8_t vol) {
    dx_ipc_push(ipc_pack(IPC_CMD_DX_MASTER_VOLUME, 0, vol));
}

#endif // __IPC_H__
