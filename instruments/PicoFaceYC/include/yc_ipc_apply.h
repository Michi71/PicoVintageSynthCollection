// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/yc_ipc_apply.h
//
// One ring packet into the engine. Lives in a header of its own so the
// instrument adapter (YC_Instrument.cpp, at the top of every rendered block)
// and the host tests (tools/host_tests/yc_sysex) drain the ring through the
// same switch - a MIDI path that the test sees land in the engine is the path
// the firmware runs.
#ifndef YC_IPC_APPLY_H
#define YC_IPC_APPLY_H

#include "ipc.h"
#include "YC_Synth_Bridge.h"

static inline void yc_ipc_apply(YC_Synth_Bridge& bridge, uint32_t pkt) {
    switch (ipc_type(pkt))
    {
    case IPC_CMD_YC_NOTE_ON:
        bridge.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
        break;
    case IPC_CMD_YC_NOTE_OFF:
        bridge.noteOff(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_PANEL_UPDATE:
        bridge.setParam(ipc_d1(pkt), ipc_d2(pkt));
        break;
    case IPC_CMD_YC_SUSTAIN:
        bridge.setSustain(ipc_d1(pkt) != 0);
        break;
    case IPC_CMD_YC_ALL_NOTES_OFF:
        bridge.allNotesOff();
        break;
    case IPC_CMD_YC_ROTARY_TARGET:
        bridge.setRotaryTarget(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_MIDI_VOLUME:
        bridge.setMidiVolume(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_EXPRESSION:
        bridge.setExpression(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_PITCH_BEND:
        bridge.setPitchBend(ipc_d2(pkt));
        break;
    default:
        break;
    }
}

// Everything queued so far, in order.
static inline void yc_ipc_drain(YC_Synth_Bridge& bridge) {
    uint32_t pkt;
    while (yc_ipc_pop(&pkt)) yc_ipc_apply(bridge, pkt);
}

#endif // YC_IPC_APPLY_H
