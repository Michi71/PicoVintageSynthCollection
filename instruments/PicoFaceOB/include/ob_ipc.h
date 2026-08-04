// ob_ipc.h - same-core SPSC ring between MIDI/panel and the audio producer,
// modelled on instruments/PicoFaceMD/include/md_ipc.h. Both sides run on
// core0; render() drains the ring at block start.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).
#ifndef OB_IPC_H
#define OB_IPC_H

#include <stdint.h>

enum ObIpcCommand : uint8_t {
    IPC_CMD_OB_NOTE_ON  = 0x01,
    IPC_CMD_OB_NOTE_OFF = 0x02,
    IPC_CMD_OB_PARAM    = 0x03,
    IPC_CMD_OB_SUSTAIN  = 0x04,
    IPC_CMD_OB_PITCHBEND = 0x05,
    IPC_CMD_OB_ALL_NOTES_OFF = 0x06,
    IPC_CMD_OB_MODWHEEL = 0x07
};

static inline uint32_t ipc_pack(uint8_t type, uint8_t d1, uint16_t d2) {
    return ((uint32_t)type << 24) | ((uint32_t)d1 << 16) | (uint32_t)d2;
}

static inline uint8_t ipc_type(uint32_t p) { return (uint8_t)(p >> 24); }
static inline uint8_t ipc_d1(uint32_t p) { return (uint8_t)(p >> 16); }
static inline uint16_t ipc_d2(uint32_t p) { return (uint16_t)(p & 0xFFFF); }

// Parameters travel as 0..65535 and are normalised back to 0..1 in the engine,
// which is the range every OB-Xf parameter setter expects anyway.
static inline uint16_t ipc_f_to_u16(float f) {
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    return (uint16_t)(f * 65535.f + 0.5f);
}

static inline float ipc_u16_to_f(uint16_t u) { return (float)u * (1.f / 65535.f); }

// C++17 inline variables: exactly ONE ring across all translation units.
inline volatile uint32_t ob_ipc_buf[256];
inline volatile uint16_t ob_ipc_head = 0;
inline volatile uint16_t ob_ipc_tail = 0;
inline volatile uint32_t ob_ipc_dropped = 0;

static inline void ob_ipc_push(uint32_t pkt) {
    uint16_t next_head = (ob_ipc_head + 1) & 255;
    if (next_head != ob_ipc_tail) {
        ob_ipc_buf[ob_ipc_head] = pkt;
        ob_ipc_head = next_head;
    } else {
        ob_ipc_dropped++;
    }
}

static inline bool ob_ipc_pop(uint32_t* pkt) {
    if (ob_ipc_head != ob_ipc_tail) {
        *pkt = ob_ipc_buf[ob_ipc_tail];
        ob_ipc_tail = (ob_ipc_tail + 1) & 255;
        return true;
    }
    return false;
}

static inline void ipc_send_ob_note_on(uint8_t note, uint8_t vel) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_NOTE_ON, note, vel));
}
static inline void ipc_send_ob_note_off(uint8_t note) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_NOTE_OFF, note, 0));
}
static inline void ipc_send_ob_param(uint8_t id, float v01) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_PARAM, id, ipc_f_to_u16(v01)));
}
static inline void ipc_send_ob_sustain(uint8_t on) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_SUSTAIN, on, 0));
}
static inline void ipc_send_ob_pitchbend(uint16_t bend14) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_PITCHBEND, 0, bend14));
}
static inline void ipc_send_ob_all_notes_off(void) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_ALL_NOTES_OFF, 0, 0));
}
static inline void ipc_send_ob_modwheel(uint8_t v) {
    ob_ipc_push(ipc_pack(IPC_CMD_OB_MODWHEEL, v, 0));
}

#endif // OB_IPC_H
