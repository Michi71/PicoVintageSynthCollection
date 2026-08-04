// include/ipc.h
#ifndef __IPC_H__
#define __IPC_H__

#include <stdint.h>
#include "pico/multicore.h"
#include "yc_engine/yc_core.h" // enum YcParamId (single source of truth, host-safe)

enum IpcCommand : uint8_t {
    IPC_CMD_FLASH_LOCK = 0x0A,
    IPC_CMD_YC_NOTE_ON = 0x0B,
    IPC_CMD_YC_NOTE_OFF = 0x0C,
    IPC_CMD_YC_PANEL_UPDATE = 0x0D,
    IPC_CMD_YC_SUSTAIN = 0x0E,
    IPC_CMD_YC_ALL_NOTES_OFF = 0x0F,
    IPC_CMD_YC_ROTARY_TARGET = 0x10,
    IPC_CMD_YC_MIDI_CTRL_MODE = 0x11,
    IPC_CMD_YC_PITCH_BEND = 0x12
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

static inline uint16_t ipc_f_to_u16(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return (uint16_t)(f * 65535.0f + 0.5f);
}

static inline float ipc_u16_to_f(uint16_t u) {
    return (float)u / 65535.0f;
}

static inline void ipc_send_flash_lock(void) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_FLASH_LOCK, 0, 0));
}

static inline void ipc_send_yc_note_on(uint8_t note, uint8_t vel) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_NOTE_ON, note, vel));
}

static inline void ipc_send_yc_note_off(uint8_t note) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_NOTE_OFF, note, 0));
}

static inline void ipc_send_yc_panel_update(uint8_t param_id, uint16_t value) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_PANEL_UPDATE, param_id, value));
}

static inline void ipc_send_yc_sustain(uint8_t sustain_on) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_SUSTAIN, sustain_on, 0));
}

static inline void ipc_send_yc_all_notes_off(void) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_ALL_NOTES_OFF, 0, 0));
}

static inline void ipc_send_yc_rotary_target(uint8_t target_speed) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_ROTARY_TARGET, target_speed, 0));
}

static inline void ipc_send_yc_midi_ctrl_mode(uint8_t enabled) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_MIDI_CTRL_MODE, enabled, 0));
}

static inline void ipc_send_yc_pitch_bend(uint16_t bend14) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_YC_PITCH_BEND, 0, bend14));
}

#endif // __IPC_H__
