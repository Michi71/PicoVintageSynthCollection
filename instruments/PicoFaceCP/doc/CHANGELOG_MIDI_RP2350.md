# Changelog: MIDI implementation + RP2350 optimization

Date: 2026-07-02 · Workflow: architecture/orchestration by Claude, code
generation by `glm-5.2:cloud`
Build verified: `cmake --build build -j4` -> FLASH 26.32 % / RAM 36.44 %
(release, no new warnings)

> **Historical document.** This log describes the state of the standalone
> PicoFaceCP repository. File names such as `main.cpp`, `pico_frontpanel.cpp`
> and the core0/core1 split no longer match the collection - the front panel is
> `CP_Ui` now and everything runs on core0. See
> [docs/ARCHITECTURE.md](../../../docs/ARCHITECTURE.md), section 4a.

## 0. Addendum after the live hardware test: a new SYSTEM screen

After the hardware test succeeded, two additional controls were added:

* **Pre-gain** (`effects/reface_cp_chain.h`): a new parameter `_preGain`
  (0..1, default 1.0), applied as the very first stage in
  `RefaceCpChain::process()` - **before** drive and the rest of the FX chain.
  Purpose: some effects (drive and wah in particular, at a high level) tend to
  overdrive; pre-gain lets the input level of the FX chain be lowered
  independently of the engine volume. The multiplication is skipped at unity
  (>= 0.9999), so there is no extra cost in the normal case. New: `FX_PRE_GAIN`
  in `include/ipc.h`, an `ipc_apply` case in `main.cpp`
  (`cp_fx.setPreGain(v)`), operable locally from the panel (no reface CP CC
  equivalent, hence no MIDI TX/RX binding).
* **MIDI channel display and setting**: `RefaceMidi` gained `getRxChannel()` /
  `setRxChannel()` (inline resp. in `src/midi_reface.cpp`). Since `RefaceMidi`
  and the front panel UI both ran on **core1**, access is direct and needs no
  IPC - unlike every audio-affecting parameter, which goes through the core0
  FIFO.
* **New screen `SCR_SYSTEM`** (`src/pico_frontpanel.cpp`, the last page before
  wrapping back to VOL/OCT): param A = MIDI receive channel (1-16 or "All" for
  omni, reset button -> All), param B = pre-gain in % (reset button -> 100 %).

## 1. New files

| File | Content |
|---|---|
| `include/midi_reface.h` | class `RefaceMidi`: the reface CP compatible MIDI protocol layer (declaration, SYSTEM and TG address enums) |
| `src/midi_reface.cpp` | implementation: channel filter, CC map, channel mode messages, SysEx (identity, parameter change/request, bulk dump/request including checksum), active sensing (TX 200 ms / RX timeout 350 ms), master transpose/tune, soft pedal velocity, panel CC TX |
| `doc/MIDI_IMPLEMENTATION.md` | separate MIDI document (equivalent to Data List pages 11-14) |
| `doc/CHANGELOG_MIDI_RP2350.md` | this document |

## 2. Changed files

### `include/midi_input_usb.h` / `src/midi_input_usb.cpp` (transport rewrite)
* The parser moved from fixed 3-byte reads to **USB MIDI 4-byte event packets**
  (`tud_midi_packet_read`, CIN dispatch) - before that, SysEx messages and pitch
  bend were lost or threw the stream out of step.
* New callbacks: pitch bend (14 bit), realtime byte (0xF8-0xFF), complete SysEx
  message (64-byte assembler, overflow-safe), activity (for the active sensing
  supervision).

### `include/ipc.h`
* New: `IPC_CMD_PITCH_BEND` (0x09), `FX_EXPRESSION`, `ipc_send_pitch_bend()`.

### `effects/reface_cp_chain.h`
* New: `setExpression()` / `getExpression()` (CC 11), multiplied with the master
  volume in the process loop (one extra multiplication per sample, arranged for
  inlining).

### `include/mdaEPiano.h` / `src/mdaEPiano.cpp` (engine)
* **Pitch bend** (±2 semitones): `VOICE.dbase` stores the unbent sample
  increment; `setPitchBend(0..16383)` scales the `delta` of all active voices by
  `exp2f(semis/12)`; `noteOn` starts new voices with the current bend factor.

### `src/main.cpp` (integration, core assignment unchanged)
* Global instance `RefaceMidi refaceMidi` (core1).
* The MIDI callbacks forward to `refaceMidi.on*()`; four new callbacks (pitch
  bend, realtime, SysEx, activity) registered; `refaceMidi.init(&ep, &cp_fx)` in
  `core1_main`.
* Octave transposition (`apply_octave`) moved into `RefaceMidi::transposeNote()`
  (combined with the SysEx master transpose); the CC 1 special case moved into
  the protocol layer.
* `ui_poll_usb()` additionally calls `refaceMidi.tick()` (active sensing).
* `ipc_apply` (core0, audio IRQ): new cases `IPC_CMD_PITCH_BEND` ->
  `ep.setPitchBend()` and `FX_EXPRESSION` -> `cp_fx.setExpression()`.

### `src/pico_frontpanel.cpp`
* Panel changes now additionally send the reface CC (wrappers
  `fp_send_fx_param/mode/instrument` = IPC + `RefaceMidi::tx*`; only while MIDI
  control is on).

### `CMakeLists.txt`
* `src/midi_reface.cpp` added to the target.

## 3. RP2350 optimizations (task 2)

Analysis finding: the most critical path is `mdaEPiano::noteOn()`, because it
runs through `ipc_apply` **inside the audio DMA IRQ on core0**. There - and in
`update()` / `getVolume()` - the engine computed in **double** precision. The
Cortex-M33 only has a single-precision FPU, so doubles go through the slower
software/DCP emulation.

* `exp` -> `expf` (8 sites, among them the decay/release calculation per note
  on/off, the LFO rate and the treble filter), `pow` -> `powf` (velocity curve),
  `sqrt` -> `sqrtf`, `fabs` -> `fabsf` plus float literals; the `(double)note`
  casts removed. Numerically equivalent within float range.
* Side finding fixed: `getVolume()` cast the `sqrt` result to `uint8_t`
  needlessly inside a float assignment.
* Existing measures verified and kept: FX hot path in RAM (`CP_HOT` /
  `__not_in_flash_func`), int16 delay lines, bare-metal dual core with FIFO IPC,
  release build (`-O3`, `-ffast-math`), samples flash-resident (XIP).
* The new MIDI layer runs entirely on **core1**; the audio IRQ only ever sees
  the existing 32-bit FIFO packets, so there is no additional latency or jitter
  in the audio path. The SysEx buffer is static (64 B), there is no heap
  allocation and no printf in the MIDI path.

## 4. Behaviour and compatibility

* Existing functions (UI, presets, instrument selection, FX operation)
  unchanged.
* Newly controllable from outside: all reface CP CCs, SysEx parameters, bulk
  dump/restore, identity reply, active sensing, pitch bend, expression, soft
  pedal, master transpose/tune, receive channel (including omni on/off via
  CC 124/125).
* Deviations from the original are documented in
  `doc/MIDI_IMPLEMENTATION.md` §8.

## 5. Open points and recommendations

* Hardware test: enumeration, CC RX/TX with a DAW, bulk dump round trip (e.g.
  MIDI-OX or SysEx Librarian).
* Optional: persist the SYSTEM block in flash (RAM-only at the time, defaults at
  boot).
* Optional: show the `_sys` settings (RX channel, MIDI control) in the OLED
  SYSTEM menu.
