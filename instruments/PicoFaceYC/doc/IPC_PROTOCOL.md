# Control -> engine ring (include/ipc.h)

## Overview

A same-core single-producer/single-consumer ring of 32-bit words. The control
side (the reface MIDI layer and the front panel, both driven from the core's
`uiTick()` on core0) pushes; the audio producer drains the ring at the top of
every rendered block (`yc_ipc_drain()` in `include/yc_ipc_apply.h`, called from
`YC_Instrument::render()`), so an edit lands at the next block boundary.

Until PicoFaceYC moved to the collection's standard runtime model this was a
cross-core channel over the SIO FIFO; the packet format survived the move, the
FIFO and the flash-park handshake did not. 256 entries; a full ring counts
drops in `yc_ipc_dropped` (shown on the CPU Load screen) instead of blocking.

The same `yc_ipc_apply()` switch is what the host test
[tools/host_tests/yc_sysex](../../../tools/host_tests/yc_sysex/) drains through,
so a MIDI path the test sees arrive in the engine is the path the firmware runs.

## Word format

- Packing: `ipc_pack(type:8bit, d1:8bit, d2:16bit)` -> `[type<<24 | d1<<16 | d2]`
- Decoding: `ipc_type()`, `ipc_d1()`, `ipc_d2()`

## IpcCommand table

| Hex  | Command                  | d1                   | d2                                                              |
|------|--------------------------|----------------------|-----------------------------------------------------------------|
| 0x0B | IPC_CMD_YC_NOTE_ON       | note                 | velocity                                                        |
| 0x0C | IPC_CMD_YC_NOTE_OFF      | note                 | unused                                                          |
| 0x0D | IPC_CMD_YC_PANEL_UPDATE  | param_id (see table) | value (16-bit; mostly lower byte; octave uses the signed byte) |
| 0x0E | IPC_CMD_YC_SUSTAIN       | on (0/1)             | unused                                                          |
| 0x0F | IPC_CMD_YC_ALL_NOTES_OFF | unused               | unused                                                          |
| 0x10 | IPC_CMD_YC_ROTARY_TARGET | target speed (0-3)   | unused (own command: rotary speed has no YcParamId)             |
| 0x11 | IPC_CMD_YC_MIDI_VOLUME   | CC7 value (0-127)    | unused                                                          |
| 0x12 | IPC_CMD_YC_PITCH_BEND    | unused               | 0-16383, centre 8192 (±2 semitones)                             |
| 0x13 | IPC_CMD_YC_EXPRESSION    | CC11 value (0-127)   | unused                                                          |

0x11 used to be `IPC_CMD_YC_MIDI_CTRL_MODE`, reserved for a MIDI Control flag
that never travelled this way (it lives in `RefaceMidi` and in the settings
record); the number was reused.

## YcParamId table (0-based, for IPC_CMD_YC_PANEL_UPDATE d1)

The enum is `YcParamId` in `include/yc_engine/yc_core.h`, the single source
for both the ring and `yc_engine_set_param()`.

| ID | Parameter       |
|----|-----------------|
| 0  | WAVE            |
| 1  | OCTAVE          |
| 2  | FOOTAGE_16      |
| 3  | FOOTAGE_513     |
| 4  | FOOTAGE_8       |
| 5  | FOOTAGE_4       |
| 6  | FOOTAGE_223     |
| 7  | FOOTAGE_2       |
| 8  | FOOTAGE_135     |
| 9  | FOOTAGE_113     |
| 10 | FOOTAGE_1       |
| 11 | PERC_ON         |
| 12 | PERC_TYPE       |
| 13 | PERC_LENGTH     |
| 14 | VIBCHO_SELECT   |
| 15 | VIBCHO_DEPTH    |
| 16 | DISTORTION      |
| 17 | REVERB          |
| 18 | VOLUME          |

## Note

This is a different addressing scheme from the SysEx tone generator addresses
(base 30 00 00) in `doc/MIDI_IMPLEMENTATION.md`. The two tables are not
numbered alike and must not be confused; `midi_reface.cpp` maps between them.
