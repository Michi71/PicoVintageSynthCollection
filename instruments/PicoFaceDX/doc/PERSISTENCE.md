# PicoFaceDX Persistence (Virtual EEPROM)

## Overview
- **Target:** RP2350 dual-core bare-metal (Core 0: audio, Core 1: USB+UI).
- **Feature:** Virtual EEPROM restores full panel state after power-up.
- **Persisted State:**
  - Octave (-2..+2 UI transpose).
  - 32-B MIDI SYSTEM block (RefaceMidi SYSTEM common image).
  - Full current DX patch (`RDX_Patch`: common + 4 operators, ~66 B).
- **Not Persisted:** Expression pedal, sustain.

## Flash Layout
- **Location:** Last two 4 KB sectors of 16 MB flash.
- **Format:** Append log of 256-B records.
  - Header: `{magic 0x50434650, seq u32, version u16, len u16, crc32(payload)}`.
  - Payload: `SettingsV2` (<=240 B).
- **Load:** Highest valid `seq` selected.
- **Save:** Next slot written; sector erase on entry (ping-pong).
- **Endurance:** ~100k cycles x 32 saves/erase-pair.

## Autosave
- **Execution:** Core 1 `settings_task(DX_Synth_Bridge* dx, RefaceMidi* rm)` in `ui_poll_usb`.
- **Trigger:** 250 ms snapshot poll; write after 2 s stability.
- **Boot Protection:** Baseline after boot restore prevents boot-saves.
- **Error Handling:** Failed saves auto-retry.

## Boot Restore
- **Core 0:** `settings_boot_restore_core0(DX_Synth_Bridge* dx)` in `main()` (single-core phase).
  - Uses XIP reads, direct setters, defaults as fallback.
- **Core 1:** `settings_boot_restore_core1(RefaceMidi* rm)` after `refaceMidi.init`.
  - Restores octave + sanitized SYSTEM block, reapplies master tune.
  - `applyMasterTune()` reconstructs the 16-bit tune value from the 4 stored nibbles and sends it to Core 0 via `IPC_CMD_DX_MASTER_TUNE`, which applies it additively to pitch bend on all operators (see `doc/CHANGELOG_DX_ENGINE.md` §16).

## Multicore Flash Safety
- **Constraint:** Erase/program kills XIP; SDK `flash_safe_execute` unusable (lockout uses SIO FIFO = our IPC channel).
- **Custom IPC Flow:**
  1. Core 1 sends `IPC_CMD_FLASH_LOCK`.
  2. Core 0 parks in `__not_in_flash_func` spin (IRQs off, 2 s timeout) inside audio DMA IRQ.
  3. Core 1 acks max 100 ms, writes with IRQs off, releases.
- **Performance Impact:**
  - Audio pause: ~0.5 ms/program.
  - Sector erase: ~50 ms (every 16th save).
- **444 MHz overclock caveat:** the SDK's `flash_range_erase/program` end with a
  boot2 re-init that resets QMI `M0_TIMING` to a value unstable at 444 MHz. The
  whole erase+program sequence therefore runs in one RAM-resident function
  (`flash_write_locked`) that restores `PICOFACE_QMI_M0_TIMING_OC`
  (project_config.h, same value pico_init sets) before any flash fetch happens.
- **Files:** `include/veeprom.h`, `src/veeprom.cpp`, `include/settings.h`, `src/settings.cpp`, `main.cpp` hooks.

## Testing
- **Unit Tests:** `test/veeprom_test.cpp` via `./test/build_veeprom.sh`.
  - 9 cases: blank, roundtrip, latest-wins, crc32 vector, sector wrap, corruption fallback, torn write, oversize, 1000-save wear.
- **Integration:** Firmware build.
- **On-Device:** Turn knob, wait >2 s, power-cycle, verify state restored.
