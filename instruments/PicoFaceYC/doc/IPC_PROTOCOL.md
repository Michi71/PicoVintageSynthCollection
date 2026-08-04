# Cross-Core IPC Protocol (include/ipc.h)

## Overview
The protocol uses a lock-free 32-bit word FIFO (SIO FIFO). Core 1 (Controller/MIDI) sends to Core 0 (Synth Bridge). Core 0 processes the messages in `ipc_apply()` (in `main.cpp`) inside the Audio-DMA-IRQ.

## Word Format
- Packing: `ipc_pack(type:8bit, d1:8bit, d2:16bit)` -> 32-bit word `[type<<24 | d1<<16 | d2]`
- Decoding functions: `ipc_type()`, `ipc_d1()`, `ipc_d2()`

## IpcCommand Table (Hex values)

| Hex  | Command                    | d1                     | d2                                                                      |
|------|----------------------------|------------------------|-------------------------------------------------------------------------|
| 0x0A | IPC_CMD_FLASH_LOCK         | unused                 | unused (Parks Core 0 during Core 1 flash write operation / persistence) |
| 0x0B | IPC_CMD_YC_NOTE_ON         | note                   | velocity                                                                 |
| 0x0C | IPC_CMD_YC_NOTE_OFF        | note                   | unused                                                                   |
| 0x0D | IPC_CMD_YC_PANEL_UPDATE    | param_id (see table)   | value (16-bit; mostly lower byte relevant; Octave uses signed pattern)   |
| 0x0E | IPC_CMD_YC_SUSTAIN         | on (0/1)               | unused                                                                   |
| 0x0F | IPC_CMD_YC_ALL_NOTES_OFF   | unused                 | unused                                                                   |
| 0x10 | IPC_CMD_YC_ROTARY_TARGET   | target_speed (0-3)     | unused (Separate command as rotary speed has no own YcParamId)          |
| 0x11 | IPC_CMD_YC_MIDI_CTRL_MODE  | reserved/unused        | unused (MIDI Control mode is Core 1 local state in RefaceMidi)          |
| 0x12 | IPC_CMD_YC_PITCH_BEND      | reserved/unused        | unused (Engine does not implement pitch bend yet)                      |

## YcParamId Table (0-based, for IPC_CMD_YC_PANEL_UPDATE d1)

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
This is a different addressing scheme than the SysEx Tone Generator addresses (30 00 00h basis) in `doc/MIDI_IMPLEMENTATION.md`. The two tables are NOT identically numbered and must not be confused.
