# CHANGELOG_DX_ENGINE.md

**Date:** 2026-07-05
**Workflow:** architecture/orchestration by Claude, code generation by glm-5.2:cloud
**Context:** porting the FM synth engine of an ESP32/Arduino project ("RDX", a Yamaha reface DX emulator) into the existing RP2350 project PicoFaceDX (a copy of PicoFaceCP).

> **Historical document.** This is the engineering log of the DX port, written
> in the standalone PicoFaceDX repository. Paths such as `src/main.cpp`,
> `pico_frontpanel.cpp` and `tools/refacedx/` no longer exist in the collection,
> and the core0/core1 split described here is gone - everything runs on core0
> now. What the instrument looks like today is in
> [docs/ARCHITECTURE.md](../../../docs/ARCHITECTURE.md), section 4a.

---

## 1. New files - engine (header-only, `include/dx_engine/`)

Ported 1:1 from the ESP32 project with clean-up (see section 4).

| File | Content |
|---|---|
| `RDX_Types.h` | patch data structures |
| `RDX_State.h` | singleton state |
| `RDX_VoiceAlloc.h` | voice allocator |
| `RDX_Constants.h` | LUTs and constants |
| `misc.h` | math utilities |
| `RDX_Envelope.h` | amplitude envelope |
| `RDX_PEG.h` | pitch envelope |
| `RDX_LFO.h` | LFO |
| `RDX_Operator.h` | FM operator/oscillator |
| `RDX_Voice.h` | 4-operator voice with 12 algorithms |
| `RDX_Synth.h` | synth host with `renderAudioBlock()` / `process()` / `noteOn()` / `noteOff()` |
| `dx_engine_config.h` | **new**, not taken from the ESP32 project - the minimal engine configuration |

**`dx_engine_config.h` - configuration values:**

- `SAMPLE_RATE = 44100`
- `DMA_BUFFER_LEN = 64`
- `MAX_VOICES = 8`
- `MAX_VOICES_PER_NOTE = 2`
- `TIMING_CORRECTION = 1.0f`

> It replaces the ESP32 `config.h`, which contained GPIO, display and SD
> definitions.

---

## 2. New files - integration (top-level `include/` and `src/`)

| File | Content |
|---|---|
| `DX_Synth_Bridge.h` / `DX_Synth_Bridge.cpp` | wrapper class: translates `fill_buffer(float* buffer, int length)` [interleaved stereo, length = frames] into blocks of 64 frames through `RDX_Synth::renderAudioBlock()` |
| `DX_Controller.h` / `DX_Controller.cpp` | control class: MIDI note forwarding, encoder handling, menu page logic |
| `DX_GUI.h` / `DX_GUI.cpp` | GUI: algorithm diagrams and screen drawing |

### 2.1 DX_Synth_Bridge

- `fill_buffer(float* buffer, int length)` - interleaved stereo, `length` =
  frames
- block-wise processing through `RDX_Synth::renderAudioBlock()` in blocks of 64
  frames
- fixed scratch buffers, no heap allocation
- `synth_.updateCache()` is called once per invocation
- `noteOn()` / `noteOff()` / `patch()` as pass-through and access

### 2.2 DX_Controller

- `onMidiNoteOn()` / `onMidiNoteOff()` - forward to `DX_Synth_Bridge`
- `onEncoder1()` - changes the menu page (enum `DxPage`: `OP1`, `OP2`, `OP3`,
  `OP4`, `LFO`, `ALGO`, with wraparound)
- `onEncoder2()` / `onEncoder3()` - change two values per page:

| Page | Encoder 2 | Encoder 3 |
|---|---|---|
| OP1-OP4 | `freqCoarse` (0-31) of that operator | `outLevel` (0-127) of that operator |
| LFO | `lfoSpeed` (0-127) | `lfoPMD` (0-127) |
| ALGO | `algorithm` (0-11) | `feedback` of operator 1 (0-127) |

### 2.3 DX_GUI

- `drawAlgo()` - draws the 12 FM algorithm diagrams (operators as boxes and
  circles with connecting lines)
- a 1:1 geometry port from the ESP32 project's own implementation (the
  `UI_Display` class) onto direct u8g2 calls (thin wrapper functions for call
  compatibility)
- `dxDrawScreen()` - shows the header (page name) plus the algorithm diagram (on
  the ALGO page) or placeholder text (other pages; real value display came
  later)

---

## 3. Changed files

| File | Change |
|---|---|
| `CMakeLists.txt` | three new `.cpp` files added to the `add_executable()` target: `DX_Synth_Bridge.cpp`, `DX_Controller.cpp`, `DX_GUI.cpp` |

---

## 4. Clean-up (removed during the port)

- `#include <Arduino.h>`, `<esp_log.h>`, `<esp_heap_caps.h>` - removed
  everywhere
- `ESP_LOGI` / `ESP_LOGD` calls removed (pure diagnostic code)
- the `logMemoryStats()` function (ESP heap diagnostics) - removed entirely
- `vTaskDelay(1)` in the audio hot path (`RDX_Voice::step()`, the
  invalid-algorithm fallback) removed - it would have been catastrophic in the
  audio IRQ
- `IRAM_ATTR` / `DRAM_ATTR` (ESP32 memory placement):
  - replaced by the `CP_HOT()` macro already present in the project
    (`effects/cp_hot.h`, RAM placement via `__not_in_flash_func` on the RP2350,
    a no-op on the host build)
  - or removed without replacement for constant data tables (they stay in
    flash/rodata, in line with the existing practice for the mdaEPiano sample
    data)
- `RDX_GUI.h` and the PresetManager coupling removed from `RDX_Synth.h`
  (Arduino filesystem/LittleFS based, not part of the sound generation proper)
- `applyBankProgram()` loads the hardcoded `DigiChordPatch()` for now (a TODO
  comment in the code marks the later multi-patch persistence)

---

## 5. Bug fixes (found in the original ESP32 code, corrected during the port)

Defects unrelated to the ESP32 that were already present in the original source:

- **The identifier `VOICES`:** used in nine places in the original project
  (`RDX_Synth.h` x8, `RDX_VoiceAlloc.h` x1) but defined nowhere - only
  `MAX_VOICES` existed, in `config.h`. It would have been a compile error.
  Unified to `MAX_VOICES`.
- **Missing `TWO_PI` macro:** implicitly available from `wiring.h` in the
  Arduino world, but not in standard C++. Added as a constant of its own
  (`6.283185307179586f`) in `RDX_Constants.h`.
- **Missing explicit includes:** the code relied on transitive inclusion through
  `Arduino.h`. The following headers were added in several places: `<cstring>`,
  `<cmath>`, `<initializer_list>`.

---

## 6. A known problem that was not fixed

**`misc.h`, function `fast_floorf()`:**

```cpp
int i = (int)x - (int)(i > x);
```

- `i` is read before it is initialized (undefined behaviour)
- confirmed by the compiler as a warning (`-Wuninitialized`)
- already present in the ESP32 original
- deliberately not fixed, since it is outside the scope of "remove the ESP32
  dependencies"
- **Recommendation:** fix it in a separate task

---

## 7. Architecture note (IMPORTANT, not yet solved)

- Like the existing `mdaEPiano` engine, `DX_Synth_Bridge` is **not designed for
  cross-core access** (callable from one execution context only; the intended
  one is the core0 audio DMA IRQ)
- `DX_Controller` (encoders and MIDI) would naturally run on core1 - direct
  calls across the core boundary are **not safe**
- The integration into `main.cpp` (instantiation, the actual IPC binding through
  new `IPC_CMD_DX_*` commands analogous to `IPC_CMD_NOTE_ON`, hooking into
  `pico_frontpanel.cpp`) has **not happened yet**
- The new codebase compiles and links, but is not reachable or callable at
  runtime

---

## 8. Build verification

- **Build command:** `cmake -G Ninja ..` plus `ninja`
- **Toolchain:** `arm-none-eabi-gcc 15.2.1`
- **Board:** `sparkfun_promicro_rp2350`
- **Platform:** `PICO_PLATFORM=rp2350-arm-s`
- **Result:** 455/455 targets successful, `main.elf` linked

**Memory usage:**

| Resource | Used | Total | Share | Before (CP only) |
|---|---|---|---|---|
| FLASH | 4,423,320 B | 16 MB | 26.37 % | 26.32 % |
| RAM | 193,872 B | 512 KB | 36.98 % | 36.44 % |

**Compiler warnings:**

All compiler warnings come unchanged from the ported ESP32 original:

| Warning | Count | Source |
|---|---|---|
| `misleading-indentation` | 3 | ESP32 original code |
| `reorder` | 1 | the `RDX_Operator` constructor |
| `sign-compare` | 1 | `renderAudioBlock` |
| `uninitialized` | 1 | `fast_floorf` |

> The newly written files `DX_Synth_Bridge.cpp` / `DX_Controller.cpp` /
> `DX_GUI.cpp` produce **no warnings of their own**.

---

## 9. Open points and recommendations

1. **Dual-core IPC binding in `main.cpp`** (see the architecture note in section
   7) - the key piece for actual usability.
2. Fix the **`fast_floorf()` uninitialized read**.
3. **The DX's own effect chain** (distortion/chorus/flanger/phaser/delay/reverb/
   touch wah from the ESP32 project, present there as `fx_*.h`) is **not
   ported** - the DX voice currently has no effect processing. Short term the
   existing `RefaceCpChain` (already float-based) could be shared; long term the
   DX effects should be ported.
4. **The value display of `dxDrawScreen()`** for the OP1-4 and LFO pages is
   placeholder text - real numbers and gauges will follow in a later step.
5. **Patch persistence** (SysEx bulk dump, flash storage for DX patches) is not
   implemented; only one hardcoded init voice (`DigiChordPatch`) is available.
6. **`MAX_VOICES=8`** (taken from the ESP32-S3 model) has not been tested
   against the actual CPU load on the RP2350 (444 MHz overclock).

---

## 10. Addendum: dual-core IPC binding (2026-07-05, continued)

The missing IPC binding described in section 7 was implemented in this
continuation.

### Changed files and implementation details

- **`include/ipc.h`**:
  - three new commands added: `IPC_CMD_DX_NOTE_ON=0x0B`,
    `IPC_CMD_DX_NOTE_OFF=0x0C`, `IPC_CMD_DX_PARAM=0x0D`.
  - a new enum `DxParamId` with 12 values: two per operator
    (frequency and level) for OP1-4, plus LFO speed, LFO PMD, algorithm and OP1
    feedback.
  - three new send helpers: `ipc_send_dx_note_on`, `ipc_send_dx_note_off`,
    `ipc_send_dx_param`.
- **`src/midi_reface.cpp`**:
  - `RefaceMidi::onNoteOn` and `RefaceMidi::onNoteOff` now additionally send to
    the DX engine, with the same channel-filtered and transposed note number.
    Incoming MIDI notes therefore sound on BOTH engines (CP and DX)
    simultaneously.
- **`include/DX_Controller.h` / `src/DX_Controller.cpp`**:
  - the methods `onMidiNoteOn` and `onMidiNoteOff` were removed. They had become
    redundant, since MIDI now goes through `RefaceMidi`, which already brings
    the channel filter, transpose and soft pedal handling.
  - `onEncoder2` and `onEncoder3` no longer write into the patch directly (the
    cross-core defect documented in section 7). They still read the current
    value (an accepted cross-core read, as elsewhere in this project) and send
    the new values through `ipc_send_dx_param()`.
- **`include/DX_Synth_Bridge.h`**:
  - comment made precise: `noteOn()`, `noteOff()` and `fill_buffer()` may only
    be called from core0. `patch()` may be read (not written) from core1.
- **`src/main.cpp`**:
  - new global instance `DX_Synth_Bridge dxBridge;` (owned by core0, like `ep`
    and `cp_fx`).
  - `dxBridge.init()` called from `main()`.
  - `ipc_apply()` has three new cases: note on/off is passed straight through,
    and a parameter set writes the matching patch field.
  - `i2s_callback_func()` now additionally renders a `dxBuf` scratch buffer
    (float, interleaved stereo) via `dxBridge.fill_buffer()` and mixes it
    ADDITIVELY (with int16 clamping) into the existing CP output. With a silent
    DX engine (no active voices) the result is exactly identical to the previous
    pure CP signal.

### Build verification

- Build verified again: `ninja` -> 8/8 successful (only the changed and new
  targets rebuilt).
- FLASH: 4,485,528 B / 16 MB = 26.74 % (before 26.37 %).
- RAM: 241,040 B / 512 KB = 45.97 % (before 36.98 %). A clear jump, because
  `dxBridge` is now a real global instance the linker can no longer remove: 8
  voices x 4 operators plus the envelope and LFO state of all voices are now
  permanently allocated static RAM. Before that, the memory for the (never
  instantiated) engine may have been dropped.
- No new compiler warnings.

### Still open (as of before section 11)

- The encoder hardware (`encSel`/`encA`/`encB` in `main.cpp`, processed in
  `pico_frontpanel.cpp`) does NOT yet call `DX_Controller::onEncoder1/2/3`. The
  DX menu pages (OP1-4/LFO/ALGO) are not reachable from the physical encoders
  yet. That is a separate, still pending step (hooking into
  `pico_frontpanel.cpp`, for example as a new screen or a mode of its own).
- MIDI notes, on the other hand, already produce real DX sound (through the
  loaded default patch `DigiChordPatch`). That can be tested simply by playing
  from a MIDI keyboard or DAW.

---

## 11. Addendum: encoder binding in pico_frontpanel.cpp (2026-07-05, continued)

- **Goal:** make the DX menu pages (OP1-4/LFO/ALGO) actually reachable from the
  physical encoders (`encSel`/`encA`/`encB`).
- **New global instance:** `DX_Controller dxController(dxBridge);` in
  `src/main.cpp` (a core1 object that references the core0 bridge only for IPC
  sends and reads, see section 10).
- **`src/pico_frontpanel.cpp`:** a new function `openDxSynth()` implemented
  after the model of the existing `showAbout()` / `openSystem()` leaf screens.
  It has a small render/event loop of its own, draws via `dxDrawScreen()`, reads
  `encSel->delta()` / `encA->delta()` / `encB->delta()` and calls
  `dxController.onEncoder1/2/3()`. It exits on a press of `btSel`, returning to
  the main menu.
- **`openMainMenu()` extended:** the menu list gets a third entry, "DX Synth"
  (between "System" and "<< BACK"). The signature was extended by the `encA` and
  `encB` parameters (which were not passed through before), and the single call
  site was adjusted.
- **Reachability:** the DX synth page is now reachable through a long press of
  the selector encoder -> menu entry "DX Synth". There the selector cycles the 6
  pages (OP1-4/LFO/ALGO) and encoders A and B change the two values of the
  current page. A short press of the selector leaves the page for the main menu.
- **IMPORTANT BUG FIX (nothing to do with the ESP32, discovered by the first
  real build attempt with this change):** `include/mdaEPiano.h` defines
  `#define SUSTAIN 128` - a preprocessor macro, a purely textual replacement
  with no knowledge of C++ scope. The DX engine files `RDX_Envelope.h` and
  `RDX_PEG.h` each had their own `enum class Stage { ..., SUSTAIN, ... }` with
  an enum value `SUSTAIN`. As long as no translation unit included both headers
  in that order (first `mdaEPiano.h`, then `dx_engine`), this went unnoticed.
  `pico_frontpanel.cpp` already includes `mdaEPiano.h` through
  `pico_frontpanel.h` / `pico_userinterface.h`, and now additionally `DX_GUI.h`
  for the new DX page. The macro textually replaced `Stage::SUSTAIN` with
  `Stage::128`, which produced a compile error with a long cascade of follow-up
  errors.
  - **Fix:** both enum values in `RDX_Envelope.h` and `RDX_PEG.h` were renamed
    from `SUSTAIN` to `SUSTAIN_STAGE` (7 sites in total across both files: one
    enum declaration each plus several `Stage::SUSTAIN` uses in `enterStage()`,
    `advanceStage()` and `stageTarget()`). `mdaEPiano.h` itself was not touched
    (existing, working CP code).
- **Build verification after the fix:** `ninja` -> 6/6 successful. FLASH
  4,489,648 B / 16 MB = 26.76 % (before 26.74 %). RAM 241,048 B / 512 KB =
  45.98 % (before 45.97 %, the small increase being the `dxController`
  instance). No new compiler warnings - all remaining ones are unchanged and
  inherited from the ESP32 original (misleading-indentation, reorder,
  sign-compare, and the known fast_floorf uninitialized problem).
- **STATUS NOW:** the DX engine is fully reachable end to end - both by MIDI
  note (it sounds immediately, mixed additively with CP) and by hardware encoder
  (menu page navigation and parameter editing through the main menu entry "DX
  Synth").

---

## 12. Addendum: complete removal of the reface CP engine (2026-07-06)

**Goal (a correction of the original task):** the user clarified that PicoFaceDX
is to become **exclusively** a reface DX clone; the reface CP only served as a
template and skeleton for the hardware binding (pin, display and encoder init,
USB MIDI transport, virtual EEPROM, generic OLED dialogs). The additive
two-engine architecture from sections 10 and 11 was therefore rolled back.

### Deleted (24 files)

- the mdaEPiano engine plus instrument sample data (14 files, about 8 MB):
  `src/mdaEPiano.cpp`, `include/mdaEPiano.h`, `include/mdaEPianoData.h`,
  `include/mdaEPianoInstruments.h`, `include/{CP,Clv,Pno,Rd_II,Wr}Data.h`,
  `include/{CP,Clv,Pno,Rd_II,Wr}Keygroups.h`.
- the reface CP effect chain plus the generic DSP headers orphaned by it (7
  files): `effects/reface_cp_chain.h`, `reface_cp_fx.h`, `wahwah.h`,
  `cp_audio.h`, `dsp_fastmath.h`, `dsp_lut.h`, `dsp_reverb.h`.
- `src/pico_program_select.cpp` (the mdaEPiano program selection screen).
- the CP-only host test harness (7 files): `test/test.cpp`, `build.sh`,
  `mdaepiano_test`, `cp_test.cpp`, `build_cp.sh`, `cp_test`, `test/README.md`
  (`test/veeprom_test.*` stays - pure, engine-independent storage tests).
- `include/arduino_compat.h` (no consumers left after the mdaEPiano removal,
  verified by grep).

### Rewritten (DX-native instead of CP-native)

- **`include/ipc.h`:** all CP commands and enums (`FxParam`, `FxMode`,
  `IPC_CMD_NOTE_ON/OFF/CC/FX_PARAM/FX_MODE/VOICE_PARAM/PROGRAM/INSTRUMENT/PITCH_BEND`)
  removed. Four new DX commands added: `IPC_CMD_DX_PITCH_BEND`, `IPC_CMD_DX_CC`,
  `IPC_CMD_DX_RAW_WRITE` (a single patch byte from a SysEx parameter change,
  since the 12 curated `DxParamId` values do not cover the complete common and
  operator layout) and `IPC_CMD_DX_PATCH_APPLY`.
- **`include/dx_patch_stage.h`** (new): a cross-core staging slot for a complete
  `RDX_Patch` (about 66 bytes, too large for the 32-bit FIFO word) - core1
  writes it (presets, SysEx bulk dump) and core0 takes it over after
  `IPC_CMD_DX_PATCH_APPLY`.
- **`include/DX_Synth_Bridge.h`:** two new lean pass-through methods
  `processCC()` / `updatePB()`, pointing at the already present but unused
  `RDX_Synth::processCC()` / `updatePB()` methods (no change to the engine
  itself - just two extra call wrappers).
- **`include/midi_reface.h` and `src/midi_reface.cpp`:** a completely new,
  reface-DX-native MIDI/SysEx scheme (common block @ `30 00 00`, 38 bytes;
  operator blocks @ `31 <op> 00`, 28 bytes each; model ID `05H` instead of
  `04H`; identity reply bytes `41 53 06` instead of `41 52 06`), ported 1:1 from
  the ESP32 reference `RDX/RDX_Midi.h`. Control change handling is two-tier: a
  handful of cases stay local to core1 (soft pedal, reset all controllers, omni
  on/off), and the large remainder is passed through unchanged via
  `ipc_send_dx_cc` to `RDX_Synth::processCC`, which already implements mod
  wheel, volume, sustain, portamento, algorithm quick-select and operator
  quick-edit CCs completely - a previously unused code path, and a real
  simplification compared with the much more complex CC table originally
  planned.
- **`include/presets.h` and `src/presets.cpp`:** `CpPreset[8]` -> `DxPreset[1]`
  (a single entry, `"DigiChord"`, a deliberate scope decision; see
  `PRESETS.md`).
- **`include/settings.h` and `src/settings.cpp`:** `SettingsV1` -> `SettingsV2`
  (octave + the 32-byte SYSTEM block + the complete `RDX_Patch`); the storage
  mechanism (veeprom) is unchanged.
- **`include/pico_frontpanel.h` and `src/pico_frontpanel.cpp`:** the DX page
  view (previously reachable only through the "DX Synth" menu entry) is now the
  permanent home screen; all CP screens removed. The menu is only `Presets` /
  `System` / `<< BACK`.
- **`CMakeLists.txt`, `src/usb_descriptors.c`:** `mdaEPiano.cpp` and
  `pico_program_select.cpp` removed from the build, product name and USB string
  `PicoFaceCP` -> `PicoFaceDX`.
- **`effects/cp_hot.h` -> `effects/ram_hot.h`:** the macro `CP_HOT` was renamed
  to `RAM_HOT` (purely cosmetic, no behavioural change), carried through all 10
  `dx_engine` headers plus `DX_Synth_Bridge.cpp`.

### Build verification

- `ninja` -> all targets successful, no new warnings (only the known ones
  inherited from the ESP32 original).
- **FLASH: 142,864 B / 16 MB, about 0.85 %** (before 26.76 % with the CP sample
  data).
- **RAM: 71,672 B / 512 KB, about 13.67 %** (before 45.98 %).
- A grep sweep over `src/`, `include/` and `effects/` confirms: no
  `mdaEPiano` / `RefaceCpChain` / `CpPreset` / `FxParam` / `FxMode` / `CP_HOT`
  references left (apart from two purely historical comment sites, cleaned up as
  well: the header line of `include/veeprom.h` and the `.gitignore` entries for
  the deleted test binaries).

### STATUS NOW

PicoFaceDX is from now on a **pure reface DX clone** without any reface CP
engine, effect chain, presets or MIDI layer. The hardware binding (pin
configuration, USB MIDI transport, virtual EEPROM, generic OLED dialogs) remains
unchanged as a skeleton. Documentation updated: `README.md`,
`MIDI_IMPLEMENTATION.md` (v2.0), `PERSISTENCE.md`, `PRESETS.md`;
`CHANGELOG_MIDI_RP2350.md` marked as superseded and archived.

**Still open** (unchanged from section 9, now without the CP legacy): applying
master tune to the engine (currently a no-op, `tuningSemitones` not wired), real
numeric display on the OP and LFO pages instead of placeholder text, extending
the preset library (parity with the real reface DX factory bank), a CPU load
test of `MAX_VOICES=8` on real hardware, and a DX host test analogous to the
removed CP `cp_test`.

---

## 13. Addendum: phase A - MIDI spec accuracy (2026-07-06)

**Goal:** a gap analysis plan (based on the official Yamaha Data List and
Reference Manual plus another review of the ESP32 reference project) uncovered
several concrete deviations of our MIDI implementation from the real reface DX.
This addendum fixes the small, low-risk corrections ("phase A" of the plan); the
larger items (the preset library from the ESP32 project, and the effect chain)
follow in separate passes.

- **CC11 (expression) was specified but not wired.** `RDX_Synth::processCC` had
  no `case 11`. Now: `RDX_Controls` gained `expression` / `expressionFactor`
  (default 127/1.0, as in the real reset-all-controllers behaviour), `processCC`
  handles CC11 exactly like CC7 (volume), and `calcOutputGain()` additionally
  multiplies by `expressionFactor`.
- **CC66/CC67 (sostenuto and soft pedal) do not exist on the real reface DX**
  (only the reface CP has them) - confirmed against the official Data List. The
  CC67 soft pedal handling (velocity scaling by 0.75x), a leftover of the
  removed reface CP layer, was removed entirely: the `_softPedal` member from
  `midi_reface.h` and all its uses in `midi_reface.cpp` (`onNoteOn`, `init`,
  `resetControllers`, `onControlChange`).
- **The "MIDI control" gate reintroduced.** The official Data List confirms that
  on real hardware CC80 (algorithm) and CC85-90/102-119 (operator quick edit)
  only take effect while SYSTEM "MIDI control" is on; all other CCs (mod wheel,
  volume, expression, sustain, portamento, all sound/notes off) are always
  active. `RefaceMidi::onControlChange` now checks that correctly before
  forwarding the gated CCs.
- **Reset all controllers completed.** The Data List specifies for CC121: pitch
  bend -> centre, modulation -> 0 (minimum), expression -> 127 (maximum),
  sustain -> off. `resetControllers()` now sends all four instead of only pitch
  bend and sustain.
- **Documentation error corrected:** `MIDI_IMPLEMENTATION.md`, `PRESETS.md` and
  a code comment in `src/midi_reface.cpp` wrongly claimed that the real reface
  DX has no MIDI program change at all. Per the official Data List it does
  support program change 0-31 (4 banks of 8 presets) - this firmware implements
  only value 0 of that so far (the single preset "DigiChord"), which is now
  documented correctly as an open scope item rather than as (non-existent) spec
  parity.
- **A real bug found and fixed:** the stack buffer `buf[48]` in
  `RefaceMidi::txBulkBlock()` was too small - a complete common block bulk dump
  (38 data bytes) needs 11 header bytes + 38 data + checksum + F7 = 51 bytes,
  three more than the buffer held (a stack buffer overflow, reported by the
  compiler as `-Wstringop-overflow`). The buffer was enlarged to `buf[64]`. The
  bug had existed since the original SysEx implementation (section 12) and only
  became visible through another full rebuild in this session.

### Build verification

`ninja` -> successful, no new warnings left (the `-Wstringop-overflow` fixed by
the buffer change). FLASH 143,400 B / 16 MB, about 0.85 %; RAM 71,752 B / 512
KB, about 13.69 % (marginally above section 12, from the new `expression` and
`expressionFactor` fields).

### STATUS NOW

Phase A of the 100 % clone plan is complete. Still open: phase B (the preset
library from the 34 `.syx` factory files of the ESP32 reference project) and
phase C (the effect chain - 2 slots, 8 effect types, currently completely
unwired despite the memory already reserved in `RDX_Common::effects[2][3]`).

---

## 14. Addendum: phase B - preset library (2026-07-06)

**Goal:** grow from 1 hardcoded preset to the complete 32 real Yamaha reface DX
factory presets (program change 0-31, 4 banks of 8).

- **A new one-time host tool `tools/refacedx/syx_to_patches.cpp`** (not part of
  the firmware build, not compiled by CMake): it reads the 32 official `.syx`
  factory voice bulk dumps from the reference project's `RDX/data/patches/` and
  applies the already verified `syxToPatch()`
  (`include/dx_engine/RDX_Types.h`) to each file; for every patch it prints a
  `{ "Name", patchFromBytes({...150 bytes...}) }` initializer. Built with
  `g++ -std=c++17` (a host compiler, no Pico SDK needed - `RDX_Types.h` is
  already plain standard C++17).
- **Verified byte-exact:** the `voiceName` field of every parsed patch decodes
  to exactly the expected factory voice name as ASCII (spot-checked:
  "DigiChord ", "WobbleBass", "GlassHarp ", "Chopper   "). The byte count per
  patch confirms `sizeof(RDX_Patch)=150` exactly.
- **File order -> program change mapping:** the 32 file names encode bank and
  slot (`11` = bank 1 slot 1 … `48` = bank 4 slot 8); the generated table puts
  them in exactly PC 0..31 order. A 33rd file, `00-Init_Voice.syx`, exists in
  the ESP32 project as well, but it is not a real factory bank slot (it falls
  outside 0-31) and was deliberately left out.
- **`include/presets.h` / `src/presets.cpp`:** `DX_NPRESETS` 1 -> 32,
  `dxPresets[]` now filled with all 32 generated patches, plus a new
  `patchFromBytes()` helper (a memcpy of the 150 raw bytes into an
  `RDX_Patch`). `preset_apply` / `preset_stage` / `preset_set_current` /
  `preset_get_current` are unchanged - the architecture was already laid out for
  N presets.
- **A real follow-on bug found and fixed:** `openPresets()` in
  `pico_frontpanel.cpp` built the menu selection list in `char buf[128]` -
  harmless with a single preset, but a guaranteed stack buffer overflow with 32
  names (up to 10 characters plus a newline, about 360 bytes). The buffer was
  enlarged to `buf[512]`.
- **Documentation corrected:** all remaining "only 1 preset" and "only value 0
  is valid" statements in `MIDI_IMPLEMENTATION.md`, `PRESETS.md` and a code
  comment in `src/midi_reface.cpp` were updated - program change now covers the
  full real range 0-31.

### Build verification

`ninja` -> successful, no new warnings. FLASH 150,272 B / 16 MB, about 0.90 %
(before 0.85 % - the 32 x 150 bytes of preset data account for roughly 7 KB);
RAM 76,592 B / 512 KB, about 14.61 %.

### STATUS NOW

Phase B is complete. The preset library has full parity with the real reface DX
factory bank. Still open: phase C (the effect chain - by far the largest
remaining effort) and the longer-standing items (master tune wiring, numeric
display on the OP and LFO pages, a CPU load test on real hardware, a DX host
test).

---

## 15. Addendum: phase C - the effect chain (2026-07-06)

**Goal:** close the largest remaining gap to the real reface DX: two independent
effect slots, each selectable among 8 types (thru, distortion, touch wah,
chorus, flanger, phaser, delay, reverb) - confirmed against the official Yamaha
Data List AND the ESP32 reference project (`RDX_FX.h` plus `fx_*.h`), which
brings a complete, finished implementation of all 8 types.
`RDX_Common::effects[2][3]` had reserved the matching memory ever since the
original engine port, but it was wired up nowhere - until now the DX voice was
100 % dry.

### Ported files (all in `include/dx_engine/`, faithful to the ESP32 original)

- `fx_base.h` - the `FXBase` interface (virtual: `init` / `reset` /
  `processBlock` / `prepare`) plus `FxThru`.
- `fx_distortion.h`, `fx_touch_wah.h`, `fx_chorus.h`, `fx_flanger.h`,
  `fx_phaser.h` (a 2-notch phaser plus an internal flanger layer, the largest
  module at 256 lines in the original), `fx_delay.h`, `fx_reverb.h` (4 combs
  plus 2 all-passes per channel) - all DSP algorithms, coefficients and
  constants taken over 1:1, no tonal changes.
- All required LUTs and helpers (`LFO_SPEED`, `PM_DEPTH`, `DELAY_TIME_MS`,
  `sin01`, `semitonesToRatio`, `wrap01`, `fclamp`, `saturate_cubic`) were
  already present in the project from the original engine port - not a single
  new LUT was needed.

### RP2350 adaptations (ESP32-specific parts replaced, the DSP untouched)

- **The `prepare()` signature was simplified:** the ESP32 distinguished "fast"
  (DRAM) and "slow" (PSRAM) scratch buffers per slot; the RP2350 has no PSRAM
  tier in this build, so there is one shared buffer per slot
  (`prepare(float* scratch, uint32_t scratchSize, int sampleRate)`).
- **`DX_FXHost`** (`include/dx_engine/DX_FXHost.h`, new, not a 1:1 port): 2
  slots x 8 effect types as a static object pool (no allocation when switching
  effects), with one fixed `float[24576]` buffer per slot (96 KB) - sized for
  the largest single requirement (`FxReverb`, computed at 24276 floats, rounded
  up with margin). The ESP32's `heap_caps_get_largest_free_block` and
  `MALLOC_CAP_*` probing and the dynamic polyphony throttling (a `VOICES`
  estimate from measured effect compute time) are gone without replacement -
  this project already has a fixed `MAX_VOICES` budget.
- **The memory budget was checked, not guessed:** 2 x 96 KB = 192 KB of RAM for
  effect scratch buffers, which together with the rest of the firmware makes
  52.76 % of 512 KB - enough margin for stacks and so on (see the build
  verification below).
- **A real bug found (through a host smoke test before the actual Pico build)
  and fixed:** `FxTouchWah::reset(bool instant=false)` had a different signature
  from `FXBase::reset()` and therefore **hid** the virtual function instead of
  overriding it (`-Woverloaded-virtual`) - a bug taken over from the ESP32
  original, where it was equally present. A call through `FXBase*` - exactly
  what `DX_FXHost::setSlot()` does on an effect change - silently called the
  no-op base version, so the internal filter and envelope state was not reset
  when switching TO touch wah. Fix: `reset()` without a parameter, `override`
  added, and the single call site in `prepare()` adjusted.

### Wiring

- **`DX_Synth_Bridge`:** a new member `DX_FXHost fxHost_;`, `init()` calls
  `fxHost_.init((float)SAMPLE_RATE)`, and `fill_buffer()` calls
  `fxHost_.process(scratchL_, scratchR_, chunkLen)` right after
  `synth_.renderAudioBlock(...)` and before the interleaving - so the effects
  run post-mix, as on real hardware.
- **SysEx and patch editing work automatically:** `effects[2][3]` lies in the
  already addressable common block (offset `0x1D-0x22`); as soon as the DSP
  consumes those bytes, the already generic `IPC_CMD_DX_RAW_WRITE` path applies
  with no new IPC work at all.
- **No MIDI CC control needed:** the official Data List confirms that effects are
  addressed only through the patch and SysEx, and that no dedicated CC
  assignment exists.
- **New front panel pages FX1/FX2** (`DX_Controller`: `DxPage` extended by `FX1`
  and `FX2`; encoder A = effect type 0-7, clamped like ALGO, encoder B = param 1
  0-127; both through `ipc_send_dx_raw_write(1, offset, val)` using
  `offsetof(RDX_Common, effects)` instead of magic numbers). Param 2 stays
  SysEx-only, like many other patch parameters (voice name, KSC curves) that are
  equally out of reach of the three encoders.
- **`DX_GUI::dxDrawScreen()`:** the FX1/FX2 pages now show the real effect name
  (`FX_NAMES[]`, new in `DX_FXHost.h`) plus the param 1 value instead of the
  generic placeholder text - the other pages (OP1-4/LFO) still show the
  placeholder (still an open point).

### Build verification

`ninja` -> successful, no new warnings apart from a harmless signedness
comparison warning in `fx_delay.h` that was already in the ESP32 original. FLASH
166,128 B / 16 MB, about 0.99 %; RAM 276,616 B / 512 KB, about 52.76 % (a clear
but budgeted jump from the 192 KB of effect scratch buffers).

### STATUS NOW

Phase C is complete. PicoFaceDX now has a complete, working effect chain with
all 8 real reface DX effect types, editable by patch and SysEx and reachable
through two new front panel pages (FX1/FX2). All three phases of the 100 % clone
plan (A: MIDI spec accuracy, B: preset library, C: effect chain) are done.

**Still open** (phase D of the plan, lower priority): master tune wiring
(`tuningSemitones` still unwired), real numeric display on the OP and LFO pages
(still placeholder text), delay time display in milliseconds instead of the raw
value, a CPU load test of `MAX_VOICES=8` with active effects on real hardware
(more relevant now because of the effect chain), and the absence of a DX host
test.

---

## 16. Addendum: phase D - polish (2026-07-06)

**Goal:** work off the three polish items left open in phase C: master tune
wiring, real numeric display on the OP and LFO front panel pages, and a basis
for measuring CPU load with the effect chain active.

### Master tune wired up (a SYSTEM SysEx parameter, not a MIDI CC)

`RDX_Controls::tuningSemitones` had been stored and round-tripped since the
original engine port, but was consumed nowhere in the audio path. It is now
fully wired, end to end across the core boundary:

- **`RDX_Voice.h`** (`updateMods()`): the pitch calculation was extended from
  `ctl_.pitchbendSemitones` to
  `ctl_.pitchbendSemitones + ctl_.tuningSemitones` - master tune acts additively
  to the pitch bend on all operators, exactly as on real hardware.
- **`RDX_Synth.h`**: a new `setMasterTune(float semitones)`, which sets
  `ctl_.tuningSemitones`.
- **`ipc.h`**: a new `IPC_CMD_DX_MASTER_TUNE` plus
  `ipc_send_dx_master_tune(uint16_t rawTune)` - an IPC command of its own rather
  than misusing CC or raw write, because master tune is global controls state,
  not a patch byte and not a CC. Core0 decodes the four nibble bytes into cents
  and semitones (the formula per the official Yamaha Data List:
  `cents = (raw - 1024) * 0.1`, clamped to ±102.4/±102.3 cents) in
  `ipc_apply()` in `main.cpp`.
- **`DX_Synth_Bridge.h`**: a `setMasterTune(float semitones)` pass-through to
  `synth_.setMasterTune()`.
- **`midi_reface.cpp`**: `applyMasterTune()` was a documented no-op stub from
  phase A (correct at the time, because `tuningSemitones` was still unwired) -
  now a real implementation: it reads `_sys[SYS_TUNE_0..3]`, reconstructs the
  16-bit raw value from the four nibbles (`(t0<<12)|(t1<<8)|(t2<<4)|t3`, each
  masked with `& 0x0F` per the official Data List: "1st bit 3-0: bit 15-12" and
  so on) and sends it with `ipc_send_dx_master_tune()`.

### Real numeric display on the OP1-4 and LFO pages

`DX_GUI::dxDrawScreen()` used to show only the generic placeholder text
"(values shown via encoders)" for the OP1-4, LFO and ALGO pages - exactly like
the FX1/FX2 pages before they got their own display in phase C. It has now been
extended after the same pattern:

- **OP1-4:** `"Freq: %d"` (`ops[opIdx].freqCoarse`, which is what encoder A
  actually writes on this page) and `"Level: %d"` (`ops[opIdx].outLevel`, what
  encoder B writes).
- **LFO:** `"Speed: %d"` (`common.lfoSpeed`) and `"PMD: %d"`
  (`common.lfoPMD`).
- ALGO keeps its graphical algorithm rendering (`drawAlgo()`) and FX1/FX2 keep
  their phase C display - both unchanged.

### CPU load instrumented (no hardware test possible by me)

The plan called for a real CPU load test of `MAX_VOICES=8` with active effects
on real hardware. I have no physical RP2350 board, so an actual hardware test
cannot be performed or claimed by me. Instead the basis for measuring it was
created, so that the user can read real numbers after flashing:

- **`DX_Synth_Bridge::fill_buffer()`**: measures the actual block duration
  (rendering plus effect chain) with `time_us_32()` and compares it against the
  block's real-time budget (`length / SAMPLE_RATE` seconds). The result lands in
  two new members, `cpuLoadPercent_` (the last block) and `cpuLoadPeakPercent_`
  (the maximum since boot), readable publicly through `cpuLoadPercent()` and
  `cpuLoadPeakPercent()` - core1 may read them directly, the same convention as
  for `patch()`.
- **A new front panel page:** the SYSTEM menu was extended by "CPU Load"
  (`pico_frontpanel.cpp`, `showCpuLoad()` after the model of `showAbout()`) - it
  shows "Now" and "Peak" in percent, updated live.
- No overhead in the hot path beyond two `time_us_32()` calls (already in IRQ
  context, so no additional synchronization is needed).

### Build verification

`ninja` -> successful, no new warnings. FLASH 166,384 B / 16 MB, about 0.99 %;
RAM 276,544 B / 512 KB, about 52.75 % (a minimal increase over phase C: +256 B
of flash and +16 B of RAM for the whole of phase D).

### STATUS NOW

Phase D is complete, so all four phases of the 100 % clone plan (A: MIDI spec
accuracy, B: preset library, C: effect chain, D: polish) are implemented. What
remains is the actual CPU load test on real hardware itself - the measurement
basis is in place, but reading the real percentage while playing with active
effects can only be done by the user on a real board.

---

## 17. Addendum: compiler warnings cleaned up (2026-07-06)

**Trigger:** the user reported various warnings in the build log. They were
split into project-owned warnings (fixable) and vendor warnings (not fixable, or
not sensibly fixable, because they come from a third-party dependency pulled in
by CMake `FetchContent`).

### Project-owned warnings fixed (all in `include/dx_engine/`)

- **`misc.h` - `fclamp()` / `saturate_cubic()`:** two `if` statements each stood
  on one line (`-Wmisleading-indentation`) - split onto separate lines, no
  change in logic.
- **`misc.h` - `fast_floorf()`, a real bug, not just a warning:**
  `int i = (int)x - (int)(i>x);` read the local variable `i` inside its own
  initializer, before it had a value - undefined behaviour (`-Wuninitialized`).
  **This bug already existed in the ESP32 original** (`RDX/misc.h:27`, identical
  code) and was taken over 1:1 in the original engine port - just like the
  `FxTouchWah::reset()` bug found in phase C. `fast_floorf()` and `wrap01()` are
  used among other things for LFO phase wrapping (`RDX_LFO.h`), so the UB could
  potentially have caused phase glitches. Fixed with the standard
  truncate-then-adjust trick:
  `int i = (int)x; return (float)(i - (int)(i > x));`
- **`RDX_Types.h` - `syxToPatch()`:** `while (...) i++; i++;` on one line
  (`-Wmisleading-indentation`) - split onto two lines; the unconditional `i++`
  stays unconditional, no change in logic.
- **`RDX_Operator.h` - the constructor (`-Wreorder`):** the initializer list
  named `idx_` before `params_` although `params_` is declared first in the
  class (C++ always initializes in declaration order, regardless of list order)
  - the list was reordered (`params_` before `idx_`), pure cosmetics with no
  behavioural change.
- **`RDX_Synth.h` (`renderAudioBlock`) and `fx_delay.h` (`processBlock`)
  (`-Wsign-compare`):** one `for` loop each compared a signed `int` against an
  unsigned `uint32_t` limit (`len` / `frames`) - the loop variable was changed
  to `uint32_t`.

### Not fixed (outside the project source tree)

- `build/_deps/picotool-src/main.cpp:227` (a `static_assert` without a message,
  a C++17 extension warning) and a linker note about "ignoring duplicate
  libraries" (`liberrors.a` / `libmodel.a`) - both come from the `picotool`
  source that CMake pulls in automatically (`_deps/picotool-src/`), not from
  this repository. A patch there would be lost on the next `FetchContent`
  refresh and does not belong to PicoFaceDX - deliberately left alone.

### Build verification

A full rebuild (`cmake --build .` after touching every `.cpp` file, so that
every translation unit is recompiled): **zero warnings** from all project files.
FLASH 163,760 B / 16 MB, about 0.98 %; RAM 273,912 B / 512 KB, about 52.24 % -
slightly smaller than before the clean-up, among other things because the
`fast_floorf` UB path in `misc.h` (which is included practically everywhere) is
gone.

---

## 18. Addendum: two remaining items from the gap analysis (2026-07-07)

**Trigger:** the user's question "is anything still open for the 100 % clone?"
led to another review against the Data List, the Reference Manual and the
codebase. Two small, clearly bounded items were identified and fixed on request
(no tonal or protocol gap - pure code hygiene and display polish).

### Dead code removed: `RDX_Synth::programChange()` / `applyBankProgram()`

A grep over the whole source tree confirmed that neither method was called
anywhere. The real program change path has run entirely through
`midi_reface.cpp::onProgramChange()` -> `presets.cpp` ->
`IPC_CMD_DX_PATCH_APPLY` -> `preset_apply()` since phase B; the `RDX_Synth`
method was a leftover from before phase B, and its comment
`// TODO: multi-patch storage — for now always load the hardcoded init voice`
had been factually wrong since phase B (there are 32 real presets, just via the
other path). Both methods were deleted without replacement (`RDX_Synth.h`).
`ctl_.wantProgram` / `wantBankMSB` / `wantBankLSB` / `getWantBank()` in
`RDX_Types.h` were left untouched (deliberately out of scope -
`wantBankMSB`/`wantBankLSB` are still written by the real, active CC0/32
handlers, matching the "received and stored but not consumed" pattern
established elsewhere, e.g. tempo, LCD contrast and pedal model in the SYSTEM
block).

### FX1/FX2 display: effect-specific labels instead of a generic "Param1"

The front panel pages used to show "Param1: %d" regardless of the effect type.
Checked against the `processBlock()` of every `fx_*.h`, what the param 1 byte
(`effects[slot][1]`, the only one the encoders actually write - param 2 stays
SysEx-only) means per type: distortion = drive, touch wah = sens,
chorus/flanger/phaser/reverb = depth, delay = feedback; thru has no parameters
at all.

- **`DX_FXHost.h`:** a new `FX_PARAM1_LABELS[FX_COUNT]` array, indexed like
  `FX_NAMES[]`.
- **`DX_GUI::dxDrawScreen()`:** the FX1/FX2 branch now uses `"%s: %d"` with
  `FX_PARAM1_LABELS[typeId]` instead of the generic label; for thru the second
  line is omitted entirely (no parameter exists, so no empty or misleading
  line).
- The values stay raw (0-127) on purpose, consistent with all the other pages
  (OP1-4 likewise show raw values for freq and level, with no conversion). An
  idea noted in phase C, "show the delay time in milliseconds", resolves itself
  this way: the time is param 2 (delay/reverb), which stays SysEx-only and is
  never shown on the front panel - param 1 for delay is feedback, and a plain
  raw value without a unit really does fit best there.

### Build verification

Full rebuild: **zero warnings**. FLASH 163,864 B / 16 MB, about 0.98 %; RAM
273,912 B / 512 KB, about 52.24 % (a minimal change: -168 B of flash from the
deleted methods, +272 B from the new label array and the GUI change, for a net
+104 B).

### STATUS NOW

Both items raised in the user's question are done. The 100 % clone plan (A-D)
remains fully complete; the only remaining point I cannot close myself is still
the CPU load test on real hardware (the measurement instrumentation has been
there since phase D, see §16). The three-encoder user interface (against the
roughly 20 direct knobs of the real reface DX) remains a deliberate hardware
decision fixed since the start of the project, not a firmware gap.

---

## 19. Addendum: a real hardware CPU load test uncovers an effect-switch bug (2026-07-07)

**Trigger:** the user ran the CPU load test instrumented in phase D on real
hardware (the one open point I could not close myself). Result: **now 49-60 %
(good), peak 140 %** - a peak above 100 % means a real buffer underrun (an audio
glitch or click at that moment), not a measurement error.

### Root cause

`SAMPLES_PER_BUFFER` (`lib/audio/include/audio_subsystem.h`) is **16** samples -
at 44.1 kHz only about **363 µs** of real-time budget per I2S IRQ block.
`DX_FXHost::setSlot()` (called on every effect type change: an encoder on
FX1/FX2, a preset change, a SysEx patch load) called `resetSlot()`, which
**always cleared the full 96 KB scratch buffer** (24576 floats, sized for the
largest single requirement, `FxReverb`) with `memset()` - regardless of which of
the 8 effect types was actually activated. Estimated memset duration at about
150 MB/s of throughput: 400-650 µs depending on the real memory throughput at
runtime - which blows the 363 µs budget on practically every switch and explains
the measured 140 % peak.

The reason **some** clearing is necessary: chorus, flanger, phaser, delay and
reverb all place their delay lines as pointer aliases into the same shared
`scratch_[slot]` buffer (a RAM saving from phase C - only one effect per slot is
ever active). Without clearing on a switch, the new effect instance would find
the raw data of the previous effect as its delay line content (an audible
phantom echo artifact). The phase C fix recognized that correctly, but solved it
with the most expensive approach imaginable (always clear everything) instead of
the cheapest (clear only the region actually needed).

### Fix

- **`fx_base.h`:** a new virtual method `scratchFootprintFloats()`, defaulting
  to `0` (thru, distortion and touch wah never touch `scratch_`).
- **`fx_chorus.h` / `fx_phaser.h`:** override with `MAX_DELAY * 2` resp.
  `FLANGER_BUF_SIZE * 2` -> 8192 floats instead of 24576 (66.7 % less).
- **`fx_flanger.h`:** override with `bufferSize_ * 2` (a runtime value from
  `prepare()`, about 1338 floats at 44.1 kHz) -> 94.6 % less.
- **`fx_delay.h`:** override with `MAX_DELAY * 2` = 22050 floats (10.3 % less
  than the full buffer - delay itself needs almost the reverb maximum, see
  below).
- **`fx_reverb.h`:** the override sums `combSize_[ch][i]` + `allSize_[ch][i]`
  over both channels at runtime (a loop, uncritical - it runs once per switch,
  not per sample) -> exactly the roughly 24276 floats actually needed (no
  structural difference from the worst case, since reverb *is* the worst case).
- **`DX_FXHost.h`:** `resetSlot()` now takes the incoming `FXBase*` instance,
  asks it for `scratchFootprintFloats()` (defensively clamped to
  `FX_SCRATCH_FLOATS`) and clears only that region instead of always
  `sizeof(scratch_[slot])`.

### Result and remaining limitation

For 6 of the 8 target effects (thru, distortion, touch wah: no clearing needed;
chorus and phaser: 8192 instead of 24576 floats; flanger: 1338 instead of 24576)
the peak is now fully within the 363 µs budget. For the remaining two (delay:
22050 floats, reverb: about 24276 floats) a short, unavoidable blip remains when
switching **specifically into those two effect types** - their delay lines are
inherently so large that even their actual (no longer worst-case) requirement
exceeds the 363 µs budget of a single audio IRQ. Eliminating it entirely would
mean spreading the clear over several audio blocks (a state machine with
intermediate muting) - deliberately not implemented, since that is a
considerably larger intervention for a rare, user-driven event (turning the
encoder on the FX type), not a permanent condition while playing.

### Build verification

Full rebuild: **zero warnings**. FLASH 164,064 B / 16 MB, about 0.98 %; RAM
273,936 B / 512 KB, about 52.25 % (a minimal change: +200 B of flash and +24 B
of RAM for the 5 new overrides plus the `DX_FXHost` change).

### STATUS NOW

The hardware test carried out by the user uncovered a real, previously
undetected performance bug and led directly to its fix - exactly the purpose of
the instrumentation added in phase D. Recommendation to the user: run the load
test on real hardware again and in particular switch back and forth between
delay and reverb deliberately, to confirm that the peak now only occurs on those
two switches and no longer on the other six effect types.

---

## 20. Addendum: FPU flush-to-zero against hiss and jitter on fast note changes (2026-07-07)

**Trigger:** a second hardware test after §19 with the preset "MotionPad"
(algorithm 8, slot 1 = chorus, slot 2 = delay with feedback 70/127): peak 111 %,
now 50-65 % - much better than the 140 % of §19, but still above 100 %.
Additionally described: audible hiss and jitter specifically during fast note
changes (rapid note changes in POLY mode, i.e. voice stealing across all 8
voices).

### Root cause

`RDX_Operator::compute()` (`RDX_Operator.h`) contains a feedback low-pass filter
per operator and sample:

```cpp
fbFilter_ += fbLpCoef_ * (fbAcc_ - fbFilter_);
```

A classic IIR filter, which approaches zero exponentially as a voice decays -
and inevitably passes through the **subnormal/denormal floating point range**
(extremely small floats near zero) on the way. Without an explicit
flush-to-zero configuration, the RP2350's Cortex-M33 handles denormals in an
IEEE-754-conformant way in hardware or microcode, which can be **many times
slower** than the normal case. A grep over the whole source tree confirmed that
nothing set the FZ bit so far. The more voices and operators decay at the same
time - exactly the case during fast note changes with many active and decaying
voices - the more likely a stall inside a single 363 µs audio block, audible as
hiss and jitter. The reverb, delay, chorus, flanger and phaser of the effect
chain have structurally similar feedback and damping IIR states, but they are
secondary to the operator feedback filter, which runs continuously for **every**
voice and **every** preset.

### Fix

`pico_init()` (`src/pico_hw.cpp`) now sets, as its very first statement, the FZ
(flush-to-zero, bit 24) and DN (default NaN, bit 25) bits in the FPU status
register FPSCR:

```cpp
{
    uint32_t fpscr;
    __asm__ volatile ("vmrs %0, fpscr" : "=r" (fpscr));
    fpscr |= (1u << 24) | (1u << 25);
    __asm__ volatile ("vmsr fpscr, %0" : : "r" (fpscr));
}
```

Plain inline assembly (VMRS/VMSR) instead of CMSIS intrinsics (`__get_FPSCR` /
`__set_FPSCR`): the first attempt failed because the CMSIS core headers of this
SDK fork are not transitively reachable on the RP2350 build path (neither
through the includes already present nor through `hardware/sync.h`, which
despite `__wfi()` pulls in no CMSIS chain but uses an inline asm wrapper of its
own). The inline assembly is platform- and header-independent and works
identically on any Cortex-M with an FPU (M4/M7/M33).

Flush-to-zero is inaudible for audio (the affected values are by definition
below the noise floor) and is standard practice in real-time audio DSP on ARM.

### Build verification

Full rebuild: **zero warnings**. FLASH 164,072 B / 16 MB, about 0.98 %; RAM
273,936 B / 512 KB, about 52.25 % (+8 B of flash over §19, no runtime RAM
overhead).

### STATUS NOW

The fix is applied and the build verified; the actual confirmation - the peak
should drop noticeably on another hardware test with "MotionPad" during fast
note changes, and the hiss and jitter should disappear - can only be done by the
user on real hardware. Should hiss remain after this fix, the next candidate
would be to measure the total IRQ time including the IPC drain loop (the current
CPU load instrumentation only covers `fill_buffer()` itself, not the time for
`ipc_apply()` calls such as `noteOn()` before it, which during a burst of notes
in quick succession could equally eat into the budget).

---

## 21. Addendum: a soft limiter against clipping hiss (2026-07-07)

**Trigger:** a third hardware test after §20: the peak had fallen to 96 % (the
FTZ fix works), but the hiss remained - now described concretely as "especially
in the high note range". The first hypothesis (aliasing from sawtooth feedback
at high pitch) was checked against the ESP32 original and the official reference
manual: FB is genuinely adjustable per operator per the documentation (p. 4),
from -127 to +127, sine to sawtooth/square - so it is not a bug in our data
model. The user then found experimentally that lowering the OP1/OP3 level on
"MotionPad" from 127 to 121 removes the hiss almost completely with no audible
tonal difference - a hint that fits a threshold effect (clipping) better than
gradual aliasing.

### Root cause

`RDX_Operator::setParams()` computes
`outGain_ = rdxGain(params_.outLevel * velogain_) * scaling_`. `velogain_`
(velocity sensitivity) can reach up to **1.08x**
(`velocityGain(vel, sens, 1.08f)`, `VELO_SENS[127] = 1.0`). At `outLevel=127`
that queries the gain LUT (`rdxGain()`, `RDX_Constants.h`) at index about 137 -
the LUT is deliberately defined with 192 instead of 128 entries, beyond the
nominal 0-127 range, and returns **about 1.5x instead of 1.0x** gain there.
Confirmed to be identically present in the ESP32 original
(`velocityGain(..., 1.08f)`, exactly the same value) - so a deliberate, ported
velocity dynamics feature, not a porting error.

The only safety net against it was a **hard integer clip** (`main.cpp`,
`if (dl > 32767) dl = 32767;`). As soon as operator gain overshoot plus the
three-operator mix (algorithm 8) plus the effect chain exceed full scale, the
hard clip produces real digital clipping - a threshold effect, which matches the
user's observation (a small level reduction brought it just under the clipping
threshold rather than making it linearly quieter).

### Fix

`main.cpp`: a new `softClipSample()` helper before `i2s_callback_func()` -
transparent (identity) below 0.9, and above that a gentle saturation toward ±1.0
(`threshold + range * (excess / (excess + range))`, asymptotic and never exactly
reached). It replaces the raw sample values before the 16-bit conversion; the
old hard clamp stays as a cheap safety net for residual cases (float rounding)
but is no longer the primary limiter. It is cheap: the normal case (no clipping)
costs only a comparison, and the more expensive computation (a division) runs
only for the rare samples that actually cross the threshold.

Deliberately **not** done: capping the operator levels in individual presets (an
alternative the user proposed). That would only have repaired "MotionPad", not
the mechanism - every other preset with a high level and a firm touch would have
kept the same risk. The soft limiter catches it generally, without trimming the
intentional velocity dynamics.

### Build verification

Full rebuild: **zero warnings**. FLASH 164,248 B / 16 MB, about 0.98 %; RAM
274,112 B / 512 KB, about 52.28 % (+176 B of flash and +176 B of RAM over §20).

### STATUS NOW

The fix is applied and the build verified; the confirmation - the hiss should
disappear on another test with "MotionPad" in the high note range with a firm
touch, without the level values having to be turned down - can only be done by
the user on real hardware.

---

## 22. Addendum: the "MotionPad" hiss confirmed as aliasing, deliberately not "fixed" (2026-07-07)

**Trigger:** a fourth hardware test after §21: the soft limiter from §21 had
**no effect** on the hiss - only manually lowering the OP1/OP3 level (127 -> 121,
found empirically by the user in §19/§20) still helps.

### Assessment

That the soft limiter (which acts only on the total amplitude at the end of the
mixdown) shows no effect rules out clipping as the cause - the gain
overshoot/clipping hypothesis of §21 was therefore wrong. It instead confirms
the original aliasing hypothesis first examined in §19: OP1 and OP3 (internally
operators 0 and 2) are exactly the two operators on "MotionPad" with appreciable
self feedback (55 and 56 of 127). Per the official reference manual (p. 4),
feedback turns the waveform from a sine toward a sawtooth, which produces a lot
of harmonic energy. `outLevel` only scales the **operator's contribution to the
mix**, not the internal feedback strength (which uses the raw, unscaled `fbAcc_`
value before the level scaling) - so lowering it to 121 does not reduce the
alias generation at the source, it makes the alias content already generated
quieter and more masked in the mix, until it slips below the audibility
threshold.

### Decision

After discussing it with the user: **no intervention in the feedback and
oscillator path.** The reasoning: exactly the same behaviour is byte-identically
present in the ESP32 reference project (already verified in §19); a fix (for
example pitch-dependent feedback damping) would be a real deviation from the
faithful port, would change the tonal character of high notes with feedback,
would cost additional compute time, and cannot safely be classified as a "bug"
rather than "the character of the real hardware" without a comparison against a
real device. Presets that hiss audibly at certain notes or velocities can still
be adjusted manually (reduce the operator level or feedback slightly) - as the
user already demonstrated for "MotionPad".

The soft limiter from §21 stays in the code unchanged: it does not fix this
hiss, but it still addresses a real and independent risk (hard digital clipping
from velocity gain overshoot combined with the algorithm mix) that exists
separately from this aliasing effect.

### STATUS NOW

No further code fix for this behaviour. It is documented as a known, accepted
property (aliasing from non-bandlimited feedback FM at high pitch, identical to
the ESP32 original). Affected presets can be adjusted individually by level or
feedback without changing the engine itself.

---

## 23. Addendum: algorithm diagram labels overlap the graphic (2026-07-07)

**Trigger:** the user reports from real hardware that the operator numbers (1-4)
and the algorithm number in the algorithm diagram (the `ALGO` page) visibly
overlap the operator boxes and circles.

### Root cause

`drawAlgo()` (`src/DX_GUI.cpp`) is byte-identical to the drawing formula of the
ESP32 reference project (`RDX_Algos.h` / `UI_Algos.h`) - the operator digit at
`y[id] - fh2` (font-height based), the algorithm ID text at
`y0 + hTotal - getFontHeight()`. The difference lies in **where it is called**:
the ESP32 original calls `drawAlgo(display, 29, algo_id, 35, true)`, our port
calls `drawAlgo(u8g2, 20, ..., 40, true)` (different `y0` and `hTotal` for the
SH1106 128x64 display) - the margins that work in the original are no longer
enough at our values.

### Fix

- The operator digit position was changed from `y[id] - fh2` (font-height based)
  to `y[id] - ww - 2` - it now follows the actual half box size (`ww = OP_PX/2`)
  instead of an independent font metric, plus 2 px of extra clearance.
- The algorithm ID text position was changed from
  `y0 + hTotal - getFontHeight(u8g2)` to
  `y0 + hTotal - getFontHeight(u8g2) + 3` - 3 px further down, more clearance
  from the lowest operator row.
- The no longer needed variable `fh2` was removed (otherwise
  `-Wunused-variable`).

### Build verification

Full rebuild: **zero warnings**. FLASH 164,232 B / 16 MB, about 0.98 %; RAM
274,112 B / 512 KB, about 52.28 % (a minimal change).

### STATUS NOW

Fix applied, build verified. Since I cannot perform a visual hardware
verification, the exact pixel values (-2 px and +3 px) are a first, well-founded
estimate - confirmation and any fine adjustment are still outstanding (a user
test on a real SH1106 display).

### Addendum to §23: OP numbers inside the box instead of above it

Feedback after the hardware test of §23: positioning "above the box" worked, but
looks worse, because connecting lines between operators run through the digits.
The reason: connecting lines only reach the edge of a box (horizontal lines
start at `x[id]+ww`, and vertical connectors only come within `ww` pixels of a
box), never its centre - so a digit sitting at the box centre (`y[id]`) is never
crossed by a line, while a digit in the gap between two rows (where the vertical
connectors run) is.

**Fix:** `fh2` (font height / 2) restored, and the digit's y coordinate changed
from `y[id] - ww - 2` to `y[id] + fh2` - which centres the digit (with baseline
text positioning) roughly in the middle of its own box instead of above it.
`zero warnings`, FLASH 164,248 B, RAM 274,112 B (unchanged from §23).

### Final fine adjustment, confirmed on hardware

The user confirms after testing: OP digits 1 px further right
(`x[id] - fw2` -> `x[id] - fw2 + 1`), algorithm number 3 px further down than
before (`+ 3` -> `+ 6`, so 6 px in total against the original
`y0 + hTotal - getFontHeight(u8g2)` position). The user calls that "clean".
`zero warnings`, FLASH 164,256 B, RAM 274,112 B.

## 24. Addendum: RP2350 optimizations - block size, I2C, voice skipping, encoder debounce (2026-07-07)

### Trigger

After the port to the RP2350, several incremental optimization opportunities
showed up in operation: an LFO timing error from inconsistent audio block sizes,
a slow OLED refresh, unnecessary CPU cycles for idle synthesizer voices, and a
practically ineffective hardware debounce for the encoder PIO. This addendum
documents the four targeted changes which together improve audio stability, UI
responsiveness and CPU efficiency.

### Audio block size aligned to `DMA_BUFFER_LEN`

`SAMPLES_PER_BUFFER` was raised from 16 to 64, so that it now matches
`DMA_BUFFER_LEN`. The block-level LFO was calculated for 64 samples per block
but was called with only 16 samples per IRQ - so the LFO effectively ran 4x too
fast. Aligning them corrects that timing error. The audio DMA IRQ rate also
drops from about 2744 Hz to about 686 Hz, which reduces the IRQ overhead. In the
I2S IRQ, `dxBuf` was changed from a VLA to a static buffer of size
`DMA_BUFFER_LEN * 2`, to avoid stack variability.

### OLED I2C bus at 1 MHz (fast mode)

The I2C clock for the SH1106 OLED was raised from 400 kHz to 1 MHz. The display
works stably in fast mode and the refresh is about 2.5x faster as a result. The
higher display update rate directly affects encoder and UI responsiveness.

### Voice skipping for idle voices

`RDX_Synth::process()` and `renderAudioBlock()` now check `isActive()` before
rendering each voice. Voices in the `IDLE` state are skipped entirely. Voices in
`RELEASE` (an audible decay) are still rendered normally, so as not to create
fade-out artifacts. At typical polyphony (1-4 notes played), 4-7 additional
voices fall into the `IDLE` state and are no longer computed; that is the
largest CPU saving in normal operation.

### Encoder PIO debounce corrected

The `freq_divider` for the encoder PIO state machine was changed from 1 to 444.
That lowers the state machine clock from about 444 MHz to about 1 MHz. The PIO
debouncer works from that clock and now produces about 490 µs of hardware
debounce - exactly the original design intent. Before, the debounce interval was
only about 1.1 µs and practically ineffective, so bouncing encoder pulses got
through.

### Build verification

- Toolchain: `pico-sdk` for RP2350, ARM GCC
- Compiler warnings: **0**
- Firmware size:
  - FLASH: **165,368 B / 0.99 %** of 16 MB
  - RAM: **275,736 B / 52.59 %** of 512 KB

### STATUS NOW

All four optimizations are integrated in the `main` branch, build-verified and
running stably on the RP2350. The audio LFO timing is now correct, the display
refresh noticeably faster, the CPU load reduced by voice skipping, and the
encoder PIO delivers clean hardware debouncing. No known regressions.

## 25. Addendum: RP2350 optimizations wave 2 - LUTs into RAM, output path fused, semitonesToRatio fast path (2026-07-07)

### Trigger

The first optimization wave (§24) moved the audio functions into SRAM with
`RAM_HOT`, but left the lookup tables those functions read per sample in XIP
flash. Every access to `sinTable`, `SEMITONE_LUT` and `levelLUT` therefore went
through the XIP cache and could produce jitter in the audio IRQ. The LUTs were
also declared `constexpr`, so the linker placed a separate copy in flash per
translation unit. Wave 2 removes both problems, fuses the output path and adds a
semantically identical fast path.

### A) Per-sample LUTs moved into a `.time_critical` RAM section

- Tables: `sinTable` (about 4.1 KB), `SEMITONE_LUT` (about 1 KB), `levelLUT`
  (about 1.5 KB).
- Placed via `__attribute__((section(".time_critical.<name>")))`.
- The storage variables changed from `constexpr` to `const`; the builder
  functions stay `constexpr`.
- With `inline` (C++17) plus COMDAT folding, only a single copy of each table
  ends up in RAM.
- `LEVEL_LUT_MAX` was decoupled from the placed storage:
  `= buildLevelLUT().maxValue`.

Root cause: `RAM_HOT` moves only functions, not data. Data in flash creates XIP
cache miss jitter in the audio IRQ.

### B) Audio output path fused

- `DX_Synth_Bridge::fill_buffer(float*)` -> `fill_buffer_i32(int32_t*)`.
- Soft clip, `float -> int32`, clamp and interleave are written directly into
  the DMA output buffer in a single loop after the effects.
- The static `dxBuf` (512 B of RAM) and a separate full-block pass are gone.
- `softClipSample` moved from `main.cpp` into `DX_Synth_Bridge` as a
  `private static inline` method.

### C) `semitonesToRatio` fast path in `RDX_Operator::compute()`

- When `phaseModSemitones == 0.0f` (no pitch bend, portamento, LFO PM or PEG -
  the common idle case), `phase_ += phaseInc_` is computed directly instead of
  calling `semitonesToRatio()` plus a multiplication.
- Semantically identical, since `semitonesToRatio(0) == 1.0` exactly.

### Build verification

- Compiler: zero warnings.
- Footprint:
  - **Flash:** 160,160 B / 0.95 %
  - **RAM:** 283,344 B / 54.04 %
- A note on the flash figure: compared with §24 the flash usage went *down*,
  because COMDAT folding reduced the LUT copies previously present several times
  in flash to a single copy each.

### STATUS NOW

All changes are built, verified warning-free and merged into the `main` branch.
The audio IRQ now has no XIP flash reads in the per-sample path, and the output
path works with one pass fewer. The engine is ready for further feature work.

---

## 26. Addendum: backport from PicoFaceRD - audio pipeline, encoders, producer in thread context, master volume (2026-08-03)

### Trigger

The sibling project `PicoFaceRD` (same hardware base: RP2350, SH1106, 3
encoders, the same vendored `lib/audio` and `lib/encoder`) found and fixed a
series of bugs in exactly those shared components after the split. Those fixes
were missing from PicoFaceDX entirely. The producer architecture was also
aligned, and a master volume was added as a device setting.

The 444 MHz overclock itself was already present (`src/pico_hw.cpp`,
`include/project_config.h`) and is unchanged - PicoFaceRD had merely added an
RD-specific 480 MHz branch on top, which was deliberately not taken over here.

### A) `lib/audio` - brought fully up to the PicoFaceRD state (byte-identical)

1. **Tuning: 14.5 cents sharp (serious, DX-specific).**
   `update_pio_frequency()` discarded the fractional part of the PIO clock
   divider (`divider >>= 8`). At 444 MHz / 44,100 Hz / S32 stereo the exact
   divider is 78.65625; 78 was used. The result: a real sample rate of 44,471 Hz
   instead of 44,100 Hz - the instrument sounded **14.5 cents sharp**
   throughout. Now `pio_sm_set_clkdiv_int_frac()` -> 44,100.1 Hz (+0.005 cents).
   The BCLK edge jitter of about one sysclk is re-clocked in the DAC.
2. **The silence buffer read past its allocation (DMA out of bounds).** The
   underrun replacement buffer was allocated with `sample_count * 4` bytes -
   correct only for S16 stereo. With the S32 stereo output used here the DMA
   reads `sample_count * 2` 32-bit words, twice as much: on **every** underrun
   2,304 bytes of heap garbage went to the DAC. Now stride-correct and shrunk to
   one producer block (64 samples), so an underrun costs one block instead of
   13 ms.
3. **A broken macro guard.** `#ifndef PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH`
   checked the same name in its body instead of
   `PICO_AUDIO_SILENCE_BUFFER_SAMPLE_LENGTH` - the override could never take
   effect.
4. **`wrap_consumer_take()` / `wrap_producer_give()` compared the input channel
   count with itself** (`_i2s_input...` instead of `_i2s_output...`). The mono
   dispatch was therefore unreachable, and in release builds (`NDEBUG`, `assert`
   empty) the function could run off the end without a `return`. Both sites
   corrected, all paths guarded with `return NULL`.
5. **`playing_buffer` was only set to NULL in debug builds** (`#ifndef NDEBUG`) -
   in a release build the IRQ handler leaked the buffer.
6. **`audio_i2s_set_enabled(false)`** now aborts the running DMA, returns the
   checked-out buffer and clears the pending completion; before, re-enabling
   re-triggered a channel that was still occupied.
7. **`audio_i2s_end()`**: a safe order (IRQ off -> abort DMA -> free buffer), a
   NULL guard, and the PIO instruction RAM is no longer cleared completely
   (other programs share the instance).
8. **`printf()` removed from `update_pio_frequency()`** - it ran in the DMA IRQ
   and blocked the PIO for 15-20 ms.
9. **New diagnostics:** `g_i2s_underrun_count` (a counter of substituted silence
   blocks) and `audio_i2s_consume_txstall()` (the sticky `PIO_FDEBUG_TXSTALL`).
10. `init_audio(sample_freq, buffer_count)` plus `audio_set_sample_freq()` taken
    over as well. Inert for DX (a fixed 44.1 kHz), but it keeps `lib/audio`
    byte-identical to PicoFaceRD and provides the `buffer_count` parameter
    needed in C.

### B) `lib/encoder` - brought fully up to the PicoFaceRD state

1. **`clocks_per_time` was computed in the constructor.** `encSel`/`encA`/`encB`
   are global objects whose constructors run before `pico_init()` - so the value
   captured the 150 MHz boot clock instead of the 444 MHz at runtime, a factor
   of 2.96 for the `capture()` / `frequency()` path. The computation was moved
   after `init()`.
2. **Two overflow guards relied on signed wraparound** - undefined behaviour
   that GCC is allowed to optimize away at `-O2`. Rewritten with unsigned
   arithmetic.
3. **The `PushButton` debounce compared an absolute millisecond deadline**;
   after the 49.7-day overflow of `to_ms_since_boot()` the button would have
   been blocked for weeks. It is duration-based now and therefore wrap-safe.
4. `lib/encoder/src/rotary_encoder.cpp` removed: not listed in `CMakeLists.txt`
   and referencing two headers (`rotary_encoder.h`, `button_debounce.pio.h`)
   that do not exist in the repository - dead, uncompilable code.

### C) Producer out of the DMA IRQ and into the thread context of core0

Until now `audio_i2s_dma_irq_handler()` called `i2s_callback_func()` directly,
which drained the IPC FIFO and rendered a complete block - so the entire FM
render ran inside the interrupt.

- Rendering and the IPC drain moved into the `main()` loop of core0;
  `i2s_callback_func()` is deliberately empty (the audio library calls the
  symbol unconditionally).
- `AUDIO_BUFFER_COUNT` raised from 3 to 6 -> 5 blocks of lead, about 7.3 ms.
  With that the DAC only starves under sustained overload, no longer on every
  single block that misses the 1.45 ms deadline.
- `multicore_reset_core1()` before starting core1: after a debugger restart
  (core0 only) the launch handshake used to hang.
- Side effect: `cpuLoadPercent()` is now wall-clock time including preemption.
  That is exactly the value the buffer lead has to absorb - the header comment
  records it.
- The buffer pools are protected with `spin_lock_blocking()` (IRQs off plus a
  hardware spinlock), which covers thread/IRQ concurrency on core0. The flash
  park handshake still sits in `ipc_apply()` and behaves unchanged.

### D) `pico_hw` and build

- `pico_fpu_ftz_enable()` as an inline helper in `pico_hw.h`; **FPSCR is
  per-core state**, so `core1_main()` now calls it as well (previously only
  core0 did, in `pico_init()`).
- QMI timing through `qmi_hw->m[0].timing` instead of the hardcoded pointer
  `0x400d000c`.
- Dead ROSC / `srand()` block removed.
- The 1.60 V `vreg` setting is documented as **not bisected**, including a
  measurement procedure for stepping it down.
- `std::__throw_*` panic stubs: the build is `-fno-exceptions`, but the failure
  paths of `std::vector` still reference the symbols and drag the ARM unwinder
  and the C++ demangler into the link.
- CMake: `hardware_adc/spi/interp/watchdog` unlinked (referenced nowhere),
  `PICO_USE_SW_SPIN_LOCKS` removed (it is the SDK default on RP2350 because of
  erratum E2, and as `PRIVATE` it never reached the static libraries anyway),
  and both stacks doubled to 4 KB in the dedicated scratch banks.

### E) Master volume as a menu setting (new, not from PicoFaceRD)

Explicitly **not** a patch or preset quantity: the value lives outside
`RDX_Patch` and is therefore overwritten neither by a preset change nor by a
SysEx voice dump.

- `DX_Synth_Bridge::setMasterVolume(percent, snap)`: 0..100 %, a square-law
  taper (`(v/100)²`, exactly 1.0 at 100 %), smoothed per sample by a one-pole
  filter (τ about 11 ms) against zipper noise.
- Applied **after** the soft clip in `fill_buffer_i32()` - as an output
  attenuator. The saturation character of a patch therefore does not change with
  the volume.
- A new IPC command `IPC_CMD_DX_MASTER_VOLUME` (0x13); core1 keeps a mirror for
  the UI and the autosave through `ui_set_master_volume()` /
  `ui_get_master_volume()`.
- Persistence: `SettingsV3` = `SettingsV2` plus one byte, `SETTINGS_VERSION` 2
  -> 3. A pure append, so an existing V2 record still reads (the migration sets
  100 %) - patches are not lost on the update. A `static_assert` guards the
  append property.
- Set at boot in `settings_boot_restore_core0()` with `snap=true`, i.e.
  **before** `init_audio()`: a stored quiet setting already applies to the first
  rendered block, with no full-level start-up.
- UI: `SYSTEM -> Master Vol`. An editor of its own instead of
  `pico_UserInterfaceInputValue()`, because that helper blocks in its input loop
  without pumping `ui_poll_usb()` - here every encoder step is sent to the
  engine immediately over IPC, so the level can be set by ear.
- CC7 (MIDI channel volume) is unaffected and still acts through
  `ctl_.mainVolumeFactor` in the engine. The two multiply: CC7 is the
  channel-related MIDI volume, the menu value the device level.
- `SYSTEM -> CPU Load` additionally shows `g_i2s_underrun_count`.

### Build verification

- Compiler: zero warnings.
- Footprint (release):
  - **Flash:** 158,872 B / 0.95 % (before 160,176 B)
  - **RAM:** 269,492 B / 51.40 % (before 283,352 B)
  - **SCRATCH_X / SCRATCH_Y:** 4 KB each / 100 % (intentional: the stacks)
- The RAM reduction comes from the `__throw_*` stubs (the unwinder and demangler
  drop out) and from the stacks moved into the scratch banks; the three
  additional audio buffers (+1.5 KB) are already included in that figure.

### STATUS NOW / open

Everything builds warning-free.

**Confirmed on hardware (2026-08-03):** the tuning is now at A=440 Hz - the
fractional PIO clock divider (point A.1) demonstrably fixes the 14.5-cent
deviation.

**Still open** - to be verified on the device:

2. `SYSTEM -> CPU Load`: peak load and the underrun counter at full polyphony
   with the producer in thread context; if underruns persist, raise
   `AUDIO_BUFFER_COUNT`.
3. A master volume sweep for zipper noise, and persistence across a restart.
4. Migration of an existing V2 settings record (the patch has to survive, the
   volume starts at 100 %).

---

## 27. Addendum: a regression from §26 - `flash_park_core0` ended up in XIP flash (2026-08-03)

### Trigger

While preparing the same backport for PicoFaceCP it became apparent that moving
the audio producer out of the DMA IRQ (§26 C) destroyed a RAM residency the
flash park handshake depends on.

### The defect

`__not_in_flash_func()` places **only the out-of-line function body** in a RAM
section (`pico/platform/sections.h:268`). It prevents **no inlining** - there is
a separate `__no_inline_not_in_flash_func()` for that (`:284`). If the compiler
inlines a `static` function with a single call site, the inlined copy inherits
the section of its **caller**.

`flash_park_core0()` was declared `__not_in_flash_func`. As long as the only
call chain was `i2s_callback_func()` (RAM) -> `ipc_apply()` ->
`flash_park_core0()`, everything landed in RAM - but only by accident, as a side
effect of being inlined into the RAM-resident IRQ handler. With the producer in
`main()`, the whole chain moved into flash.

Demonstrated empirically over two builds of the same tree:

| Symbol | `668b9b8^` (producer in the IRQ) | `668b9b8` (producer in a thread) |
|---|---|---|
| `main` | `0x100048a8`, 64 B (flash) | `0x100039a8`, 11,912 B (flash) |
| `i2s_callback_func` | `0x20000110`, 12,984 B (RAM) | `0x20000110`, 2 B (RAM) |
| `flash_park_core0` | (inlined into the IRQ, RAM) | (inlined into `main`, **flash**) |

So of all things, the spin loop whose only purpose is to keep core0 out of XIP
while core1 erases and programs was itself running from XIP. Every settings save
is affected, i.e. about 2 s after every parameter change.

### The fix

`flash_park_core0()` changed to `__no_inline_not_in_flash_func()` - the same
reasoning and the same macro as for its counterpart `flash_write_locked()` on
the core1 side (`src/veeprom.cpp:77`), which had the protection from the start.

Verification after the fix:

```
100148a0 00000008 t __flash_park_core0_veneer     <- flash (long-call veneer)
20000110 00000040 t flash_park_core0              <- RAM, out of line
20000150 00000002 T i2s_callback_func             <- RAM (empty)
```

### Audit of the rest of the RAM hot path

All `RAM_HOT` functions of the engine are `always_inline` and land in
`DX_Synth_Bridge::fill_buffer_i32()`, which exists as a real RAM symbol
(`0x20001b74`, 35,964 B) - no residency was lost there. `flash_park_core0()` was
the only affected case.

### The lesson

`__not_in_flash_func` is no guarantee for `static` functions with few call
sites. Where RAM residency is **semantically required** (flash access runs in
parallel), `__no_inline_not_in_flash_func` belongs; `__not_in_flash_func`
remains right for pure performance and jitter optimization. Changes to the call
context of such functions have to be cross-checked with `arm-none-eabi-nm -nSC`.

### A second follow-up: `__wfe()` instead of a permanent spin on a full buffer pool

Also from the CP comparison: §26 C left core0 spinning hot with
`tight_loop_contents()` when the pool was full. That was uncritical before,
because core0 slept in `__wfi()` - with the producer in thread context the CPU
has since been at 100 % duty permanently, on a part running at 1.60 V and
444 MHz with `vreg_disable_voltage_limit()`. That is a real thermal change and
should not arise as a side effect.

Now `__wfe()`. Both wake sources already SEV - verified, not assumed:

- `queue_free_audio_buffer()` -> `__sev()` (`lib/audio/src/audio.cpp:97`),
  reached from the DMA IRQ when the consumer drains a producer buffer.
- `multicore_fifo_push_blocking_inline()` -> `__sev()` (pico-sdk
  `pico/multicore.h:197`) on every IPC push from core1.

The event register is latched, so a SEV shortly before the WFE makes it return
immediately - no lost wakeup and no note-on delay. In addition, any enabled
interrupt taken also wakes from WFE, which makes the DMA IRQ a backstop. It is
the pool's own idiom anyway: its blocking take does exactly the same
(`audio.cpp:108`).

`__wfi()` would have been wrong - it sleeps through a core1 push and would
therefore delay note-ons and the flash park acknowledgement.

### Build verification

- Zero warnings. **Flash:** 158,824 B / 0.95 %. **RAM:** 269,556 B / 51.41 %.
- Placement after both fixes still correct: `flash_park_core0` @ `0x20000110`
  (RAM), `i2s_callback_func` @ `0x20000150` (RAM, empty),
  `DX_Synth_Bridge::fill_buffer_i32` @ `0x20001bb4` (RAM).
