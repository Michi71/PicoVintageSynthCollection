// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/settings.h
//
// PicoFaceCP — persisted settings snapshot (virtual EEPROM).
//
// Persisted state:
//   - mdaEPiano instrument index (0..5) and raw engine params (0..1, NPARAMS=12)
//   - UI octave transpose (-2..+2)
//   - RefaceCpChain modes: trem/wah, chorus/phaser, delay
//   - RefaceCpChain FX knobs: drive, twDepth, twRate, cpDepth, cpSpeed,
//                            dlyDepth, dlyTime, reverb, volume, preGain
//   - RefaceMidi SYSTEM common block (RX channel, master tune/transpose,
//     local control, MIDI control, etc.), 32 bytes.
//
// Autosave policy:
//   uiTick() polls every 250 ms, builds a full snapshot, and if it differs
//   from the last saved image and remains stable for 2000 ms, writes one
//   veeprom record ("virtual potentiometer memory").
//
// NOT persisted (deliberately live controllers):
//   - Expression pedal level
//   - Sustain pedal state
//
#pragma once

#include <stdint.h>

#define SETTINGS_VERSION          1
#define SETTINGS_ENGINE_PARAMS   12   // mdaEPiano NPARAMS
#define SETTINGS_SYS_BLOCK_SIZE  32   // RefaceMidi SYSTEM common block (SYS_BLOCK_SIZE)

class mdaEPiano;
class RefaceCpChain;
class RefaceMidi;

// Persisted settings snapshot, version 1.
struct __attribute__((packed)) SettingsV1 {
    uint8_t instrument;                          // 0..5 (mdaEPiano instrument index)
    int8_t  octave;                              // -2..+2 UI octave transpose
    uint8_t twMode;                              // RefaceCpChain trem/wah mode
    uint8_t cpMode;                              // RefaceCpChain chorus/phaser mode
    uint8_t dlyMode;                             // RefaceCpChain delay mode
    uint8_t sysBlock[SETTINGS_SYS_BLOCK_SIZE];   // RefaceMidi SYSTEM common image
    float   engineParams[SETTINGS_ENGINE_PARAMS]; // mdaEPiano params 0..11 (raw 0..1)
    float   drive, twDepth, twRate, cpDepth, cpSpeed;   // FX chain knobs 0..1
    float   dlyDepth, dlyTime, reverb, volume, preGain; // FX chain knobs 0..1
};

static_assert(sizeof(SettingsV1) <= 240, "must fit VEEPROM_MAX_PAYLOAD");

// init(): loads the record and applies it - instrument, engine params, all FX
// knobs/modes, the UI octave and the RefaceMidi SYSTEM block. Must run after
// the default FX init and after refaceMidi.init().
//
// One call since PicoFaceCP moved to the standard runtime model. It used to be
// split into a core0 and a core1 half, with the loaded record parked in a
// static in between, because engine and MIDI layer lived on different cores.
// The core has already run veeprom_init() by the time init() is called.
void settings_boot_restore(mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm);

// Called from uiTick(): every 250 ms gathers a snapshot of all persisted
// values (getters + ui_get_octave + SYSTEM block); when the snapshot differs
// from the last saved image and has been stable for 2000 ms, writes one
// veeprom record ("virtual potentiometer memory" autosave).
void settings_task(mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm);
