#ifndef __IPC_H__
#define __IPC_H__

#include <stdint.h>
#include "pico/multicore.h"

enum IpcCommand : uint8_t {
    IPC_CMD_NOTE_ON      = 0x01,
    IPC_CMD_NOTE_OFF     = 0x02,
    IPC_CMD_CC           = 0x03,
    IPC_CMD_FX_PARAM     = 0x04,
    IPC_CMD_FX_MODE      = 0x05,
    IPC_CMD_VOICE_PARAM  = 0x06,
    IPC_CMD_PROGRAM      = 0x07,
    IPC_CMD_INSTRUMENT   = 0x08,
    IPC_CMD_PITCH_BEND   = 0x09,
    IPC_CMD_FLASH_LOCK   = 0x0A   // Core 1 asks Core 0 to park in RAM during flash write
};

enum FxParam : uint8_t {
    FX_DRIVE      = 0,
    FX_TW_DEPTH,
    FX_TW_RATE,
    FX_CP_DEPTH,
    FX_CP_SPEED,
    FX_DLY_DEPTH,
    FX_DLY_TIME,
    FX_REVERB,
    FX_VOLUME,
    FX_EXPRESSION,
    FX_PRE_GAIN
};

enum FxMode : uint8_t {
    FXM_TW_MODE = 0,
    FXM_CP_MODE,
    FXM_DLY_MODE
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

static inline void ipc_send_note_on(uint8_t note, uint8_t vel) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_NOTE_ON, note, vel));
}

static inline void ipc_send_note_off(uint8_t note) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_NOTE_OFF, note, 0));
}

static inline void ipc_send_cc(uint8_t cc, uint8_t value) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_CC, cc, value));
}

static inline void ipc_send_fx_param(uint8_t fx_id, float value01) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_FX_PARAM, fx_id, ipc_f_to_u16(value01)));
}

static inline void ipc_send_fx_mode(uint8_t fxm_id, uint8_t mode) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_FX_MODE, fxm_id, mode));
}

static inline void ipc_send_voice_param(uint8_t index, float value01) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_VOICE_PARAM, index, ipc_f_to_u16(value01)));
}

static inline void ipc_send_program(uint8_t program) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_PROGRAM, program, 0));
}

static inline void ipc_send_instrument(uint8_t instrument) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_INSTRUMENT, instrument, 0));
}

static inline void ipc_send_pitch_bend(uint16_t bend14) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_PITCH_BEND, 0, bend14));
}

static inline void ipc_send_flash_lock(void) {
    multicore_fifo_push_blocking(ipc_pack(IPC_CMD_FLASH_LOCK, 0, 0));
}

#endif // __IPC_H__
