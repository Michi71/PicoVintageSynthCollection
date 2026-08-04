// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/settings.h
//
// PicoFaceDX — persisted settings snapshot (virtual EEPROM).
//
// Persisted state:
//   - UI octave transpose (-2..+2)
//   - RefaceMidi SYSTEM common block (RX channel, master tune/transpose,
//     local control, MIDI control, etc.), 32 bytes.
//   - Full current DX patch (RDX_Patch: common + 4 operators)
//   - Master volume (0..100 %) -- a device setting, not a patch parameter, so
//     it survives preset changes and SysEx voice dumps.
//
// Autosave policy:
//   settings_task() polls every 250 ms, builds a full snapshot, and if it
//   differs from the last saved image and remains stable for 2000 ms, writes
//   one veeprom record ("virtual potentiometer memory"). Like PicoFaceYC and
//   PicoFaceCP this instrument therefore reports settingsSize() == 0 and keeps
//   its own record instead of handing a buffer to the core: the debounce here
//   is finer than the core's and the record holds a full patch.
//
#pragma once

#include <stdint.h>
#include "dx_engine/RDX_Types.h"

#define SETTINGS_VERSION         3
#define SETTINGS_VERSION_V2      2    // legacy layout, still readable (see below)
#define SETTINGS_SYS_BLOCK_SIZE  32   // RefaceMidi SYSTEM common block (SYS_BLOCK_SIZE)

#define SETTINGS_MASTER_VOLUME_DEFAULT 100   // unity gain

class DX_Synth_Bridge;
class RefaceMidi;

// Persisted settings snapshot, version 2 (version 1 was the reface-CP layout,
// removed). Kept only so a device that already has a V2 record in flash keeps
// its patch across the update; V3 is a pure append, so the V2 image is just
// the leading prefix of a V3 one.
struct __attribute__((packed)) SettingsV2 {
    int8_t    octave;                             // -2..+2 UI octave transpose
    uint8_t   sysBlock[SETTINGS_SYS_BLOCK_SIZE];   // RefaceMidi SYSTEM common image
    RDX_Patch patch;                               // full current DX patch (common + 4 operators)
};

// Persisted settings snapshot, version 3: V2 plus the master volume.
struct __attribute__((packed)) SettingsV3 {
    int8_t    octave;                             // -2..+2 UI octave transpose
    uint8_t   sysBlock[SETTINGS_SYS_BLOCK_SIZE];   // RefaceMidi SYSTEM common image
    RDX_Patch patch;                               // full current DX patch (common + 4 operators)
    uint8_t   masterVolume;                        // 0..100 %, device-level output attenuator
};

static_assert(sizeof(SettingsV3) == sizeof(SettingsV2) + 1,
              "V3 must stay a pure append onto V2 for the migration below");
static_assert(sizeof(SettingsV3) <= 240, "must fit VEEPROM_MAX_PAYLOAD");

// Called from the adapter's init(): loads the record and, if a valid V3 (or
// legacy V2) image exists, applies all of it - patch and master volume
// straight into the engine, UI octave and the RefaceMidi SYSTEM block into
// their owners. Runs before the audio pool exists, so a stored low volume is
// already in effect on the very first rendered block.
//
// Was a pair of functions, one per core, while the UI lived on core1. Call
// after dx->init() and rm->init(dx).
void settings_boot_restore(DX_Synth_Bridge* dx, RefaceMidi* rm);

// Called from uiTick(): every 250 ms gathers a snapshot of all persisted
// values (dx->patch() + ui_get_octave + SYSTEM block); when the snapshot
// differs from the last saved image and has been stable for 2000 ms, writes
// one veeprom record ("virtual potentiometer memory" autosave). The write
// lands on core0 between two audio blocks.
void settings_task(DX_Synth_Bridge* dx, RefaceMidi* rm);
