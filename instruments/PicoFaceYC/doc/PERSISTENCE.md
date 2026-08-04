# PicoFaceYC Persistence

## Overview
PicoFaceYC targets the RP2350 Dual-Core Bare-Metal environment, dividing tasks between Core 0 (Audio) and Core 1 (USB+UI). Persistence is handled via a Virtual EEPROM, which restores the complete panel state after power-up.

## Persisted State
The persisted state is defined in `yc_panel_state_t`, contained within `SettingsV2` in `include/settings.h`. The structure size is 22 bytes, enforced using `#pragma pack(push,1)`.

Persisted fields include:
* `wave` (0-4)
* `octave` (-2..2)
* `footage[9]` (0-6 each)
* `perc_on`
* `perc_type`
* `perc_length` (0-4)
* `vibcho_select`
* `vibcho_depth` (0-4)
* `rotary_speed` (0-3)
* `distortion` (0-127)
* `reverb` (0-127)
* `volume` (0-127)
* `midi_ctrl_mode` (0/1)

**Not persisted:** Sustain pedal state and active notes/voices.

## Flash Layout
The flash layout is unchanged from PicoFaceDX, utilizing the last 2x 4KB sectors with 256-byte records and Ping-Pong-Erase (see `include/veeprom.h`).

The record header structure is: `{magic 0x50434650, seq u32, version u16, len u16, crc32(payload)}`.
The current configuration uses `SETTINGS_VERSION=2`.

## Design Decisions
Originally, `settings.h` included an additional `sysBlock[32]` field, operating under the assumption of a DX-like SYSTEM-Common byte array. The actual built `RefaceMidi` (for YC), however, does not use such an array, relying instead on individual fields. Consequently, `sysBlock` was removed, and `SettingsV2` consists exclusively of `yc_panel_state_t`.

## Autosave Mechanism
Autosave operates on Core 1 via `settings_task(YC_Synth_Bridge*, RefaceMidi*)` in `ui_poll_usb`. It uses a 250ms poll interval and triggers a write only after 2000ms of stability (defined as no field value change).

## Boot Restore
The boot restore process is split across both cores:

* **Core 0:** `settings_boot_restore_core0(YC_Synth_Bridge*)` is called in `main()`. It reads `yc->state()` fields directly during the Single-Core phase using XIP reads. This function MUST run before `multicore_launch_core1()`.
* **Core 1:** `settings_boot_restore_core1(RefaceMidi*)` is called in `core1_main()` after `refaceMidi.init()`. It restores only `midi_ctrl_mode` via `rm->setMidiControlEnabled()`, as `RefaceMidi` is strictly a Core-1 object.

## Multicore Flash Safety
Flash safety uses the identical mechanism from PicoFaceDX. During flash erase or programming by Core 1, Core 0 parks in RAM. This is implemented via `veeprom_set_lock_hooks` in `main.cpp` utilizing the `IPC_CMD_FLASH_LOCK` command.

## Testing
There is no YC-specific unit test for the persistence layer itself. The `veeprom.cpp` mechanics are directly inherited from PicoFaceDX, meaning the existing PicoFaceDX tests remain valid.

**On-Device Test Procedure:**
1. Change a parameter value.
2. Wait for >2s.
3. Perform a power-cycle.
4. Verify that the parameter state is correctly restored.
