# PicoFaceRD — Roland MKS-20 / MK-80 emulation on the RP2350

As of 2026-07-18. Code generation: `glm-5.2:cloud` (via Ollama), architecture,
integration and review: Claude (orchestrator).

> **Historical document.** This is the engineering log of the port, written in
> the standalone PicoFaceRD repository. Paths such as `tools/rdpiano`,
> `src/rd_main.cpp` and the second CMake executable no longer exist in the
> collection, and the tool paths have moved to `tools/rd_extract/`. What the
> instrument looks like today is in [ARCHITECTURE.md](ARCHITECTURE.md) and in
> [docs/ARCHITECTURE.md](../../../docs/ARCHITECTURE.md).

## Overview

On the basis of the PicoFaceDX skeleton, a second firmware target
**`picofacerd`** was built which emulates the Roland SA synthesis (MKS-20 /
RD-200 / MK-80, CPU-B board) through the MAME-derived engine from
`tools/rdpiano`. The existing DX target `main` stayed buildable.

## Architecture

```
Core 1 (UI/MIDI)                        Core 0 (audio)
────────────────                        ──────────────
USB MIDI ──> RD_Midi ──┐                i2s_callback_func (DMA IRQ)
Encoder ──> RD_Controller ──┤ SIO FIFO ──> ipc_apply ──> RD_Synth_Bridge
OLED <── rd_ui_draw     ┘   (ipc.h)           │
                                              ├─> Mcu (HD6301 + SA sound chip, ROMs)
                                              └─> RD_VintageFX (DAC filter, EQ,
                                                  tremolo, BBD chorus) ─> I2S
```

## New and changed files

| File | Purpose |
|---|---|
| `src/rd_engine/`, `include/rd_engine/` | engine core copied from `tools/rdpiano` (mcu, sound_chip, mame_utils, mcu_ops, rom_tables plus 4 table files, about 3.0 MB of flash constants, RP2350 attributes already present) |
| `include/RD_Synth_Bridge.h`, `src/RD_Synth_Bridge.cpp` | bridge between audio callback and engine: `fill_buffer_i32()` renders `Mcu::get_next_sample()` per frame through the FX chain; note on/off and sustain -> `sendMidiCmd`; instrument selection -> `loadPatch`; sample rate change flag |
| `include/rd_effects.h`, `src/rd_effects.cpp` | `RD_VintageFX`: 12-bit DAC quantization plus low-pass, bass/treble shelves (±9 dB), tremolo (0.5-8 Hz), BBD-style chorus (2 anti-phase taps, 5 ms base, ±3 ms modulation, 6 kHz BBD low-pass), mono in / stereo out at the native engine rate |
| `include/rd_params.h` | shared parameter IDs (IPC transport 0..255) |
| `include/RD_Midi.h`, `src/RD_Midi.cpp` | MIDI control class: channel filter, note on/off, sustain CC64, program change -> IPC |
| `include/RD_Controller.h`, `src/RD_Controller.cpp` | three-encoder menu controller, shadow values for the display |
| `src/rd_main.cpp` | the lean main program of the RD target (core0: ipc_apply, I2S callback, sample rate tracking; core1: USB/MIDI/encoders/OLED) |
| `lib/audio/audio_subsystem.{h,cpp}` | `init_audio(uint32_t sample_freq = 44100)` (backwards compatible) plus `audio_set_sample_freq()` for switching at runtime |
| `CMakeLists.txt` | a second executable, `picofacerd` |

## Menu mapping (3 encoders, SH1106)

Encoder 1 switches the page (with wraparound), encoders 2 and 3 change the page
parameters:

| Page | Encoder 2 | Encoder 3 |
|---|---|---|
| PATCH | instrument 0-15 (MKS-20: piano 1-3, harpsichord, clavi, vibraphone, e-piano 1-2; MK-80: classic, special, blend, contemporary, a-piano 1-2, clavi, vibraphone) | volume |
| CHORUS | depth (0 % = off) | rate |
| TREMOLO | depth (0 % = off) | rate |
| PHASER | depth (0 % = off) | rate (0.1-5 Hz) |
| EQ | bass (50 % = neutral) | treble (50 % = neutral) |
| VOICES | voice count 8/16/24/32/auto | - (line B shows live `Act <active>/<limit>`) |
| SYS | DAC filter on/off | MIDI RX channel 1-16 / omni |

Display (since P2a in the PicoFaceCP style, module `RD_Display.cpp/h`):

- Boot splash: a 128x64 XBM from `include/rd_logo.h` (placeholder = the
  PicoFaceCP logo; to use your own bitmap just replace the array; image2cpp
  "invert" + "swap bits"), held for 2 s with `tud_task()` - it runs BEFORE
  `init_audio()` and may therefore send blocking.
- Page layout: an inverted header bar "PAGE <rate>k" with the page counter "n/5"
  on the right, a divider, two body lines (8x13B, y=32/48) and the footer
  diagnostics line (6x10, y=62, format unchanged: `<instr> P U D A N`).
- PATCH page: the header shows the BANK instead of "PATCH" ("MKS-20 32k" /
  "MK-80 20k"), the body `<nn> <name without bank prefix>` (the longest case,
  "12 Contemporary", is 120 px, verified on the host to fit); the SYS page shows
  `DAC Flt ON/OFF` and `MIDI Ch 1-16/Omni`.
- `rd_ui_draw()` now only fills the `RdUiModel` (strings); `rd_display_page()`
  draws exclusively into the full buffer - the staged flush in half tile rows
  (g_ui_flush_row) stays the only path to the display, with no SendBuffer during
  operation.
- Layout verified on the host (u8g2 compiled natively, ASCII dump harness in the
  scratchpad).

## Sample rate strategy (no resampling)

The original engine works at **20 kHz or 32 kHz** depending on the patch.
Instead of the libresample path of the desktop wrapper, the **I2S output rate is
matched to the engine rate**:

1. `init_audio(rdBridge.currentSampleRate())` starts with the rate of patch 0.
2. On a patch change `RD_Synth_Bridge::setInstrument()` sets a flag; the I2S
   callback then calls `audio_set_sample_freq()`.
3. On every DMA start `audio_i2s.c` compares
   `producer_pool->format->sample_freq` with `shared_state.freq` and follows the
   PIO frequency automatically (`update_pio_frequency`) - so the switch takes
   effect with the next buffer.

`libresample` and the desktop wrapper `rdPiano.cpp/.h` were deliberately **not**
taken over (std::mutex/vector/iostream in the audio path, heap allocations).

## Inconsistencies found (tools/rdpiano vs. giulioz/rdpiano)

1. `tools/rdpiano/CMakeLists.txt` references `src/effect_chain.cpp` and
   `rdPiano.h` includes `effect_chain.h` - **both files are missing** from the
   skeleton. The class `VintageDACFilter` referenced there is missing too. The
   replacement in this port is `RD_VintageFX` (chorus/tremolo/DAC filter),
   modelled on the BBD chorus and tremolo approximation of the original
   (giulioz/rdpiano, librdpiano).
2. Case mismatch in the CMake file: `src/rdpiano.cpp` versus the actual
   `src/rdPiano.cpp`, which breaks on case-sensitive file systems.
3. `rdPiano.h` documents "40 parameters" (`parameters[40]`), but there are
   `kTotalParameters = 24`.
4. The ROM tables (`*_tables.cpp`) are already decrypted and prepared with
   RP2350 flash attributes (`aligned(4), section(".rodata")`) - consistent with
   `tools/roms` (MKS-20 A/B banks, MK-80, RD-200 program ROM).

## Build and test result

- `cmake --build build --target picofacerd`: **successful** (arm-none-eabi-gcc,
  release).
  - FLASH: 3,108,644 B / 16 MB (18.5 %), including all ROM tables
  - RAM: 300,948 B / 512 KB (57.4 %), including 64 KB of engine RAM and the
    voice memory
  - artifact: `build/picofacerd.uf2`
- Regression test of the DX target `main`: **still successful** (the
  audio_subsystem change is backwards compatible).
- Generator errors fixed during review: a missing `<new>` include, the position
  of `alignas`, wrong enum scopes (`RdPage::`), the `-Xlinker` flag, and field
  and member name drift in two glm answers (only the specified minimal
  transformation was taken).

## Bug fix history

**2026-07-18 - "only a quiet howl, parameters have no effect" (first hardware
test):** cause: the bridge never booted the 6301 MCU - neither `Mcu::reset()`
(jump to the reset vector plus 8192 instructions of run-in) nor the MCU
handshake was executed, so the emulated CPU executed garbage data from address
0. Note: the same gap is in the skeleton `tools/rdpiano/src/rdPiano.cpp`, where
`reset()` is missing as well - another inconsistency against the original. Fixed
after the model of `mcuReset()` in `rdpiano_juce/Source/PluginProcessor.cpp`
(original repository): `reset()` -> handshake `0x30, 0xE0, tuneMsb, tuneLsb` ->
1024 warm-up samples -> `loadPatch(0)`. In addition the sample scaling was
corrected from /32768 to /65536 (the original does
`get_next_sample() / 65536.0f`).

## Debugging campaign "crackling and tearing" (2026-07-18 … 2026-07-19, 21 rounds)

A chronology of the actual defects - every single one was real and necessary:

| # | Defect | Symptom | Fix |
|---|---|---|---|
| 1 | MCU boot sequence missing (`reset()` + handshake + warm-up) | a quiet howl instead of sound | boot after the model of the JUCE plugin's `mcuReset()` |
| 2 | `strcpy` overflow in `getPatchName` (names up to 19 characters, a 17-byte buffer) | crash on instrument selection (reproducible on the host) | length-bounded copy in the bridge |
| 3 | The whole engine ran from XIP flash (interpreter, op dispatch table, sound chip, tables) | 270 % CPU load, underruns | moved into RAM: code (`RD_HOT_FUNC`), program ROM, exponent table, dispatch and flag tables; interleaved sample tables (1 flash burst instead of 4); phase increment cache |
| 4 | The interpreter executed whole instructions per "cycle" (3-4x too fast and too expensive) | M share 55 % | the cycle-accurate `m_icount` loop reactivated (more authentic than the original plugin) |
| 5 | `std::queue` (heap!) in the audio IRQ; later: the ring dropped individual bytes | potential crash; the 3-byte command framing could be torn apart | a fixed ring plus an atomic `push3` |
| 6 | Dual-core split: the memory barrier was on the wrong side of the doorbell; IRQ merge order | note-event-correlated corruption, only on the device | `__dmb()` before `req++`, acquire in the worker, ascending voice merge; single == dual proven bit-identical |
| 7 | Active `printf`s in the audio IRQ (`write_byte` unmapped I/O = the front panel latches of the original!, and `update_pio_frequency` on a rate change) | crackling on note events and patch changes; phantom P727 | printfs removed or replaced by counters |
| 8 | `loadSounds` byte loops (160K iterations) on a patch change | the pump stalled -> U counter | `memset` / `memcpy` |
| 9 | NULL pool window: the first DMA IRQ before `ap` was assigned | layout-lottery crash | guard in the callback |
| 10 | **Rendering inside the DMA IRQ**: long IRQ runs blocked the next DMA complete -> PIO TX FIFO stalls (invisible to the buffer underrun counter!) | crackling although U stayed constant; proven with the `FDEBUG.TXSTALL` counter (S) | producer moved into the core0 main loop, IRQ made microscopic |
| 11 | Capacity of the 32 kHz patches | U counted on clavi and friends | 480 MHz (flash 120 MHz, in spec; 504 would not boot), asymmetric split (worker 2/3), IC9 reformulation (boolean cascades -> mask tests, proven exhaustively over 2^19 addresses) |

**Diagnostic infrastructure** (stays in the build): OLED line 4
`<instr> P<peak%> U<underruns> S<pio-stalls> N<noteons>`; host tools in the
scratchpad pattern: `rd_host_test` (bit identity regression), `rd_gliss_test`
(glissando plus dual-core thread mode), `rd_burst_test` (USB-like event bursts).
Method: every engine change is checked on the host for bit identity - or for a
documented, explainable deviation - before it goes onto the device.

**Final state:** 16 voices on all patches, dual core (core0: interpreter + FX +
1/3 of the voices + UI/USB; core1: 2/3 of the voices), 480 MHz, 6 buffers of
lead (about 19 ms at 20 kHz). P peaks around 125 %, absorbed by the lead; U and
S are practically stationary while playing. The remaining "cut off" notes during
extreme runs are the authentic 16-voice voice stealing of the original, present
in host renderings as well.

## Strategy "way 2" (decided 2026-07-19) - status: phases 1-3 implemented

The engine was rewritten. The core idea: **the original firmware is the
parser**. A host-only capture hook in `SoundChip::write` records what the
firmware programs into the chip per (patch, note, velocity); from that come part
descriptors (pitch LUT, wave region, loop, envelope segment chains with
timestamps), which the new `RdNewEngine` plays back by **timeline replay** -
with the unchanged, bit-exact part arithmetic (IC19/IC9/IC8), but without the
6301 interpreter, without the 64-slot scan, and without the firmware.

**Toolchain** (`tools/rd_extract/`): `rd_extract` (capture, with a primer note
against the fresh-boot quirk and a 4 s window for long decays) ->
`rd_analyze.py` (descriptors; note the recycled-voice heuristic) -> `rd_pack.py`
(.rdp binary packs, about 170 KB per patch) -> `rd_embed_packs.py` (flash C
arrays, all 16 about 2.7 MB) -> `rd_ab_test` (A/B against the reference
emulator, lag-scan correlation ±300).

**Firmware target `picofacerd2`** (RD_ENGINE_V2): the same rd_main/UI/MIDI/FX,
bridge v2 (`RD_Synth_Bridge_v2.*`) with RdNewEngine (24 voices), sustain pedal
logic in the bridge. Static RAM requirement: **31 KB instead of 450 KB** (no Mcu
memory, no RAM copies of the tables); flash 6.3 MB including the packs.

**Validation:** an A/B matrix (30 notes x 4 velocities x 16 patches) against the
reference emulator; the first round gave a median r of 0.97. Data traps found
and fixed: the fresh-boot kill of the first sweep note, and window truncation of
long decays.

**The v2.2 finale (2026-07-20):** after an adversarial multi-agent review of the
hot path (18 verified findings, among them a segAt flash peek per sample, GCC
rodata promotion of addrTbl, split imbalance, a flash-resident core1 loop), plus
4-byte sample banks (proven lossless, halving the XIP miss stream and 1.5 MB of
flash) and a block doorbell (1 rendezvous per 64 samples,
voice-outer/sample-inner): **P max 98 during extreme playing**, flash 4.8 MB,
RAM 100 KB. Stuck-note hardening: IPC ring 256 (plus a D counter in the
display), CC 123 all notes off as a rescue anchor (bridge plus the engine's
`allNotesOff()`), because single hanging notes during extreme playing were
traced to event loss in the 64-entry ring (not reproducible on the host in
engine, bridge or dual-core stress).

The long-term goal is unchanged: the same player back end for further Roland
ROMs (SC-55).

## Extension phase P1 (2026-07-20)

- **P1a made the vintage DAC filter audible** (`rd_effects.cpp/h`): the old code
  ran the reconstruction low-pass *unconditionally* - the DAC toggle only
  switched the inaudible 12-bit quantization, which is why it seemed to do
  nothing. Now the block is fully gated: real 12-bit requantization (±1.0 ->
  4096 steps) plus a 2-pole low-pass at about 6 kHz (12 dB/oct, capped at
  `0.35*sr` at 20 kHz) only while the DAC filter is ON; OFF gives a clean
  signal. ON sounds audibly softer and duller (vintage DAC character); the
  default stays ON.
- **P1b regression runner** `tools/rd_extract/run_regression.sh` (one command,
  details in `tools/rd_extract/README.md`): builds the host tools, dumps the 16
  embedded firmware packs (`rd_dump_packs.cpp` - so the test covers what ships),
  checks 6 A/B cells against frozen r reference values (±0.002) plus an rmsRatio
  window, and 3 stuck-voice stress cases (tailRMS < 0.001). First run after P1a:
  **REGRESSION PASS (9 checks)**, all r bit-identical - which proves the DAC
  rework does not touch the engine.
- **P1c USB product name** in `usb_descriptors.c`: "PicoFaceDX" -> "PicoFaceRD".
- Firmware build still green: FLASH 4.78 MB (28 %), RAM 100 KB (19 %).

## Extension phase P2 (2026-07-20)

- **P2a GUI framework** (`RD_Display.cpp/h`, `rd_logo.h`): splash, header bar and
  footer in the PicoFaceCP style; details in the "menu mapping" section above.
- **P2b percent UI** (`RD_Controller.cpp`, `rd_ui_draw`): the shadow values are
  now in UI units - percent 0-100 for continuous parameters, 0/1 for toggles.
  The encoder step is exactly 1 % per detent; the only wire conversion happens
  when sending (`toWire`: toggles 0/255, percent `(v*255+50)/100`). Mapping
  verified: 0->0, 40->102, 50->128, 80->204, 100->255, strictly monotonic (every
  detent changes the engine value). The percent defaults mirror the engine's FX
  defaults exactly, so the boot state is bit-identical to before. Display:
  `Depth 50%`, `Volume 80%`, `DAC Flt ON/OFF`, `MIDI Ch 1-16/Omni`.

## Extension phase P3a: phaser (2026-07-20)

- **Mono 4-stage phaser** (a port of the PicoFaceCP `CpPhaser`, `rd_effects.cpp`
  section "4b"): a first-order all-pass cascade, feedback with a `fastTanh`
  limiter (0.2-0.75 depending on depth), sweep 750 Hz ±0.6-2.2 octaves, rate
  0.1-5 Hz (`0.1·10^(n·1.699)`), coefficient update every 8 samples (exp2f and
  fastTan are too expensive per sample and inaudible with an LFO of 5 Hz or
  less), LFO through the existing `sinApprox` instead of the CP LUT. **Mono**
  instead of CP's stereo (half the cost, classic in the Phase 90 sense): it sits
  between tremolo and the chorus split and is fully gated (OFF = one branch).
- New menu page PHASER (between TREMOLO and EQ, six pages now): depth with
  auto-ON like chorus and tremolo, plus rate; parameters
  `RD_PARAM_PHASER_ON/RATE/DEPTH`.
- Verification: host harness, 10 s at depth and rate 100 % at 20 kHz and 32 kHz -
  the output is finite and bounded (maxAbs 0.69, no feedback runaway), with a
  clear difference from bypass. Regression after integration: PASS (9 checks,
  engine bit-identical, default OFF). FLASH +3 KB, RAM +1.5 KB. Estimated CPU
  cost below 0.5 % (it runs once per sample after the voice mix, independent of
  voice count) - the real P value on the device still to be confirmed.

## Extension phase P3a.5: voice governor (2026-07-20)

- **Voice count parameter** (page VOICES): manually 8/16/24/32 or **auto**
  (default). `kVoices` 16 -> 32 (+8.6 KB RAM for array capacity; the LIVE limit
  is still set by `voice_limit_` with clean stealing).
- **Auto governor** (in the bridge, core0, `governorTick` at the end of
  `fill_buffer_i32`): the base is the proven per-rate cap (16 at 20 kHz, 12 at
  32 kHz). It cuts 4 voices when the instantaneous load reaches 95 % (floor 6),
  plus a **U emergency brake** (the main loop calls
  `voiceGovernorEmergency()` on an underrun delta). Recovery is +1 voice per
  400 ms of rendered samples, and only while the load is below 80 % (the dead
  zone 80-95 % is hysteresis against pumping). All timers are **sample-based**,
  so they are deterministically testable on the host. Manual modes bypass the
  governor entirely (a deliberate user choice; 24 or 32 on 32 kHz patches is the
  overload experiment P4b).
- Signal choice: P (the instantaneous `cpuLoadPercent_`) as the forward-looking
  primary signal, U as the emergency brake; A is a consequence, N is activity,
  and D is a different problem entirely (IPC event loss - the voice brake does
  not help against that).
- Host verification (`gov_check`, 14 checks PASS): 24 active voices in 32-voice
  mode (the array bump works), tails -> 0, the emergency cascade 16->12->8->6
  with its floor, recovery pacing exactly 4.00 s for 10 steps, auto follows the
  rate on a patch change, manual modes survive a patch change. Regression: PASS
  (9 checks, engine bit-identical). RAM 110 KB (21 %).

### P3a.5 fix: active culling (2026-07-20, after the device test P116/U69/D0)

Root cause of the ineffective governor: `setVoiceLimit()` only set the
variable - running voices kept rendering. The limit only affected new notes, and
`allocVoice` steals at the limit anyway. The actual load therefore only fell as
notes decayed naturally, far too late against the 19 ms of buffer lead.

- **Active culling in the engine core:** `setVoiceLimit()` now calls
  `cullToLimit()` - excess voices get `killing=1` (victim choice as in
  allocVoice: the oldest releasing one, otherwise the oldest) and fade out
  linearly over ONE 64-sample block (about 3 ms, click-free, int64 ramp), then
  are freed. The CPU relief arrives after one block instead of after seconds.
  Host-verified: 24 active -> 8 in 6.4 ms. Dual-core safe: `killing` is set
  between blocks (producer context, published through the existing doorbell
  barrier), each core clears its own parity voices, and stolen slots clear
  `killing` in noteOn.
- **More aggressive tuning:** cut threshold 95 -> 90 %, cut step 4 -> 6, and the
  U emergency brake jumps DIRECTLY to the floor of 6 (an underrun is already
  audible - no point being timid); recovery 400 -> 700 ms per voice and only
  below 70 % load (dead zone 70-90 %). Recovery pacing host-verified: 7.01 s for
  10 steps.
- Note on the peak display: `P` is a peak hold since boot and is never reset -
  P116 may have been a single spike. For watching the governor, use the VOICES
  page (`Act n/limit`).

## Extension phase P3b: MIDI per the MK-80 chart (2026-07-20)

Implemented after the MIDI implementation chart (the recognized side, MK-80
owner's manual pages 46-48), logic in `RD_Midi.cpp`:

| Message | Behaviour |
|---|---|
| Note on/off | true voice 15-113, outside that octave folding (identical for on and off); note on with v=0 is a note off |
| CC 64 damper | >= 64 = ON (as before; half damper 1-63 not modelled) |
| CC 92/93/95 | tremolo/chorus/phaser switches: 0-63 OFF, 64-127 ON (onto `RD_PARAM_*_ON`) |
| CC 121 reset all controllers | hold -> off, pitch bend -> centre (modulation n/a) |
| CC 123 all notes off | as before |
| Program change | chart 0-63; mapped modulo 16 onto our instruments |
| Pitch bend | ±2 semitones (the MK-80 bender depth default); the engine scales all part `phase_inc` values with a Q16 factor (`setPitchBend`), recomputed from the base table so no error accumulates, and the identity at centre is exact (regression bit-identical) |

Deliberately left out: CC 1 modulation (the engine has no vibrato path), active
sensing (a USB transport concern) and SysEx model 2FH (it edits MK-80 patch RAM;
our packs are ROM). UI note: the MIDI FX switches change the engine state behind
the UI shadow's back - the next touch of an encoder re-sends the shadow state.

Verification (`midi_check`, host): 18 mapping checks PASS (folding, v0, CC map,
the CC121 double packet, channel filter, PC mod 16) plus a bend frequency
measurement over zero crossings: up 1.116 / down 0.889 (targets
2^(±2/12) = 1.122/0.891). Regression PASS (9 checks). FLASH 4.79 MB, RAM 111 KB.

### P3b addendum: master tune (2026-07-20)

In the JUCE port `masterTune` is declared but never wired up; here it is
complete: ±50 cents in 1-cent steps (as usual for Roland), default 0. It is
implemented as a second Q16 factor multiplied with the pitch bend
(`applyPitchFactor()`: combined factor `(bend·tune)>>16`, exact identity at the
default, so the regression stays bit-identical). New page **TUNE** (between
VOICES and SYS, eight pages now): encoder 2 = cents, line B shows the A4
reference live (`A4 441.3Hz` = 440·2^(c/1200)). On the wire,
`RD_PARAM_MASTER_TUNE` carries cents+50 (0-100), routed in `ipc_apply` to
`bridge.setMasterTune()`; the getter `masterTuneCents()` is there for the coming
veeprom persistence. Host-verified by zero-crossing measurement: ±50 cents ->
x1.024/0.976 (targets 1.029/0.971). No MIDI mapping, since the MK-80 chart does
not recognize a tune request. FLASH 4.79 MB, RAM 111 KB.

## Extension phase P3c: veeprom persistence (2026-07-20)

- **The veeprom module** was reactivated from `deleted/` (the proven ping-pong
  append log, 2 x 4 KB at the end of flash, 256-byte records with
  magic/seq/version/CRC32, host test simulation). The core is unchanged except
  for ONE critical adjustment: the QMI timing restore after
  `flash_range_erase/program` still used the DX define
  `PICOFACE_QMI_M0_TIMING_OC` (0x60007303, CLKDIV 3 = 160 MHz flash at 480 MHz -
  out of spec!). A new single-source define `PICOFACE_QMI_M0_TIMING_RD`
  (0x60007304) is now used by both `pico_hw.cpp` (boot) AND `veeprom.cpp`
  (post-write restore) - otherwise the device would run with unstable flash
  timing after the first save.
- **The lock hooks are trivial in v2** (instead of the DX core parking
  handshake): the save runs on core0 between two audio fills; the core1 worker
  is RAM-resident and only reads flash inside the render rendezvous. veeprom
  disables interrupts internally and restores the QMI timing itself.
- **Record v1** (17 bytes, `rd_settings.h`, in UI units): instrument, volume,
  chorus/tremolo/phaser (on + rate + depth), bass, treble, DAC, MIDI channel,
  voice mode, master tune. Import clamps defensively, so corrupt or foreign
  records cannot escape.
- **Save policy:** only ENCODER edits mark the state dirty (MIDI changes such as
  program changes are deliberately session-transient - the original only
  remembers the panel state either). The write happens 2 s after the last edit
  and only while A=0 (no active voice, so the flash stall is inaudible); the
  save-induced U delta is swallowed so the governor's emergency brake does not
  fire. A sector erase only happens on a ping-pong switch (every 16 saves),
  otherwise it is a 256-byte program (about 1-2 ms, inside the buffer lead).
- **Boot restore** goes through the normal IPC path (`importSettings` re-sends
  everything; the sample rate change of a restored 32k instrument uses the
  existing mechanism).
- Host verification (`settings_check`, 15 checks PASS): veeprom round trip,
  latest-wins after 40 saves, ping-pong erase of both sectors, version and CRC
  reject, import/export identity, 16 IPC packets per restore, clamping of an
  0xFF record. Regression PASS (9 checks). FLASH 4.79 MB, RAM 112 KB.

## Extension phase P4a: matrix fine-tuning (2026-07-20)

**Root cause of the weak cells found - no re-capture needed.** The baseline
matrix (1920 cells, notes 21-108 in steps of 3, velocities 40/80/110/127) had a
median r of 0.9725, but 15.2 % of the cells were below 0.9 - clustered at high
notes (p3 harpsichord n102-108 down to r=0.23, p7/p11 n87-93 at v110/127).
Diagnosis by WAV comparison: pitch exact, but the attack about 40 % too quiet
and the decay shifted - an envelope problem, not a pitch problem.

**The cause is in `rd_analyze.py`:** the filter "envelope writes only from the
first pitch write onwards" (meant to guard against kill writes of recycled
voices) discarded the **pre-attack pulse of the firmware**. The firmware
programs TWO envelope writes per part BEFORE the pitch (for example dest=31
fast, then dest=0 speed=0 = freeze): the envelope starts its attack with a head
start of about 31/255. On low notes, with their long attack windows, that is
inaudible; on high notes with tight segment timing, a replay without the head
start never reaches the attack target before the next segment is reprogrammed.
Fix: for parts with a pitch, keep ALL envelope writes of the window (sweep
captures are single-note windows, and there are no recycling kills there).

**Result** (analyze and pack from the existing captures, full matrix in
parallel): median 0.9725 -> **0.9997**, mean 0.9479 -> 0.9925, cells below 0.9:
292 -> **22 (1.1 %), so the goal of >95 % at r >= 0.9 is met at 98.9 %**; only
one cell got worse by more than 0.02 (p3 n99 v80: 0.898 -> 0.867). The remaining
cells (0.79-0.89) are p3 n102/105 at v40/80, MK-80 bass at v127 and p5 bass at
v40 - baseline level. Reference cells for example p0n60 0.953 -> 0.9996, p8n60
0.905 -> 0.9992.

Follow-up work: packs +17 % (2.75 -> 3.32 MB, FLASH 5.36 MB = 32 %, RAM
unchanged), regression reference values re-frozen (documented in the runner),
and a safeguard for the freeze lesson: rd_stress2 additionally on p3/p5/p15
(vibraphone/harpsichord) - all tails 0.000000, so the additional freeze segments
in attack chains do not create immortal voices. Optionally still open: more
velocity layers (6-8) against audible layer jumps when playing BETWEEN the
captured velocities - that would need a re-capture (about 2x the sweep time) and
about +0.8 MB of flash; it is not measurable from the matrix and is purely a
listening decision.

## Known limitations and next steps

*(As of 2026-07-31, after the refactoring pass; older entries of this list are
done: the hardware runs, veeprom persistence exists, and the CPU load is in the
diagnostics footer as `P`.)*

- `sinApprox()` has delivered the full ±1 since 2026-07-31 (previously ±0.5):
  the tremolo reaches unity gain and its full modulation depth again, and the
  phaser its full octave sweep. A listening test on 2026-07-31 on the hardware
  confirmed both effects sound right, with no need to readjust the depth
  mapping; peak load after the refactoring is below 60 %.
- Since 2026-07-31 note folding follows the pack range 21-108 (88 keys) instead
  of the MK-80 chart's 15-113: the packs contain no descriptors for 15-20 or
  109-113, so those notes would have been silent. The faithful alternative is to
  extend the capture sweep to 15-113 (about 12 % more pack data).
- MK-80-specific velocity curves and SysEx are not implemented (the engine uses
  the original CPU-B ROM routines for velocity).
- Vcore 1.60 V is not calibrated (never bisected); with hardware available:
  lower it in 50 mV steps with regression plus a full-polyphony soak, and keep
  one step of margin (see the comment in `pico_hw.cpp`).
- Optional micro-optimization (measure first, then decide): WFE/SEV instead of
  spin polling in the block rendezvous - it would save power and bus slots and
  change nothing audible; the spin protocol was deliberately left alone.
