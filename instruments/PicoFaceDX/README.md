# PicoFaceDX

<p align="center">
  <img src="../../img/picofacedx.png" alt="PicoFaceDX prototype hardware" width="800">
</p>

A Yamaha reface DX FM synth clone, one of the eight instruments in
[PicoVintageSynthCollection](../../README.md). Four operators, twelve
algorithms, two effect slots.

## Features

| Area | Details |
|---|---|
| Engine | 4-operator, 12-algorithm FM synthesis (ported from an ESP32/Arduino reference) |
| Polyphony | 8 voices (`MAX_VOICES=8`) |
| Sample rate | 44100 Hz |
| DMA buffer | 64 samples (`DMA_BUFFER_LEN=64`) |
| DSP style | Header-only, no dynamic allocation, fixed buffers, single-precision float throughout the audio path |
| MIDI | USB and DIN, both provided by the core - full reface DX MIDI/SysEx |
| Effects | 2 slots, 8 types each (thru/distortion/touch wah/chorus/flanger/phaser/delay/reverb), post-mix |
| Persistence | Virtual EEPROM (wear-levelled flash append log), autosave 2 s after the last change |
| Presets | 32 built-in factory presets (the real reface DX bank, program change 0-31) |
| Display | SH1106 128x64 OLED |
| Controls | 3 rotary encoders (selector / param A / param B) |

## Signal flow / architecture

```
USB MIDI / DIN MIDI (core dispatch)
    │
    ▼
RefaceMidi ──ring──► DX_Synth_Bridge ──► dx_engine (RDX_Synth / RDX_Voice / RDX_Operator /
    ▲                     │              RDX_Envelope / RDX_LFO / RDX_VoiceAlloc) ──► I2S DAC
    │                     │
DX_Controller ──ring──────┘
    │
    ▼
DX_Ui ──► DX_GUI ──► SH1106 OLED
```

Everything runs on core0. The audio producer drains the ring
([`include/ipc.h`](include/ipc.h)) at the start of every block; MIDI dispatch,
SysEx and panel edits push into it: note on/off, CC and pitch bend forwarding,
single patch-byte writes (`IPC_CMD_DX_RAW_WRITE` for SysEx parameter edits), and
a staging area ([`include/dx_patch_stage.h`](include/dx_patch_stage.h)) for
whole-patch transfers - presets and SysEx bulk dumps are too large for a single
ring word.

The audio-rate and mutating methods of `DX_Synth_Bridge` (`init()`,
`fill_buffer_i32()`, `noteOn()`, `noteOff()`, `processCC()`, `updatePB()`,
`patch()`) belong to the producer, i.e. `render()`.

Until this instrument moved into the collection, the UI, USB and MIDI owned
core1 and the ring was a cross-core SIO FIFO with a flash-park handshake. What
that cost and how it was undone is in
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md), section 4a.

## Hardware

The board is the same for every instrument in the collection; the pin map lives
in [core/include/project_config.h](../../core/include/project_config.h). The
DIN MIDI header on GPIO 4/5, unused in the standalone version of this project,
is wired up by the core now.

## Layout

```
instruments/PicoFaceDX/
├── effects/ram_hot.h        RAM_HOT() macro - places hot audio functions in RAM,
│                            no-op on host builds. Used throughout dx_engine/.
├── include/
│   ├── dx_engine/           the FM engine: RDX_Synth, RDX_Voice, RDX_Operator,
│   │                        RDX_Envelope (incl. PEG), RDX_LFO, RDX_VoiceAlloc,
│   │                        types/constants, DX_FXHost (2-slot router) and
│   │                        fx_*.h (the eight effect types)
│   ├── DX_Controller.h      panel pages, encoder handling
│   ├── DX_GUI.h             panel drawing incl. the algorithm diagrams
│   ├── DX_Synth_Bridge.h    engine wrapper for the audio path
│   ├── DX_Ui.h              front panel and menu as a state machine
│   ├── dx_patch_stage.h     staging area for whole-patch transfers
│   ├── ipc.h                same-core ring between control side and producer
│   ├── midi_reface.h        reface DX MIDI layer (channel filter, CC, SysEx)
│   ├── presets.h            the 32 factory voices
│   └── settings.h           persisted snapshot
├── src/                     the matching .cpp files plus DX_Instrument.cpp,
│                            the adapter implementing picoface::Instrument
├── doc/                     MIDI implementation, persistence, presets, changelogs
└── instrument.cmake
```

Host-side pieces live outside the instrument: the virtual EEPROM unit test in
[`tools/host_tests/veeprom/`](../../tools/host_tests/veeprom/) and the one-time
`.syx` conversion tool in
[`tools/dx_syx_to_patches/`](../../tools/dx_syx_to_patches/).

> **Host demo gap:** there is no host-buildable audio demo for the DX engine.
> The engines of CP, J6, MD and SM have one under
> [`tools/host_tests/`](../../tools/host_tests/README.md); a DX equivalent does
> not exist yet.

## Building

Built together with the rest of the collection, or on its own:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceDX
cmake --build build
```

### Editor compatibility: the reface DX USB identity

Yamaha's [Soundmondo](https://soundmondo.yamahasynth.com/) voice library filters
MIDI ports by USB descriptor, not by the SysEx Identity Reply - it does not see
the device while it enumerates as `PicoFaceDX`. An opt-in build option makes the
DX enumerate as the real thing (VID `0x0499`, PID `0x1624`, "Yamaha Corp." /
"reface DX"):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_DX_REFACE_USB_IDENTITY=ON
cmake --build build
```

Confirmed working: with it, Soundmondo connects and loads voices directly.

It is **off by default** and meant for a session with such an editor, not for
distribution: it borrows Yamaha's vendor ID, and it gives up the per-instrument
PID that lets a host tell the eight images of this collection apart. The option
is cached, so switching it back off needs a reconfigure *and* a rebuild:

```bash
cmake -S . -B build -DPICOFACE_DX_REFACE_USB_IDENTITY=OFF
```

The PID `0x1624` is not verified - it is the value the ESP32 reference uses,
derived there from the reface identity range `0x51..0x54`.

Footprint in the collection build: 170,052 bytes of flash, 218,508 bytes of RAM.
The RAM figure is dominated by the effect chain's fixed scratch buffers (2 slots
x 96 KB, sized for the largest single effect, `FxReverb`) - budgeted
deliberately, with comfortable headroom left for stacks and other buffers.

## Controls

The home screen **is** the DX page view; no menu entry is needed to reach it.

### Encoder assignments

| Encoder | Function |
|---|---|
| **Selector** | Rotate to page through OP1 -> OP2 -> OP3 -> OP4 -> LFO -> ALGO -> FX1 -> FX2 (8 pages). A long press opens the menu. A short press is a no-op, available for future use. |
| **Param A** | Rotate to edit the first value on the current page. |
| **Param B** | Rotate to edit the second value on the current page. |

### Pages

| Page | Param A | Param B |
|---|---|---|
| OP1 | operator 1 frequency (coarse) | operator 1 output level |
| OP2 | operator 2 frequency (coarse) | operator 2 output level |
| OP3 | operator 3 frequency (coarse) | operator 3 output level |
| OP4 | operator 4 frequency (coarse) | operator 4 output level |
| LFO | LFO speed | LFO pitch mod depth (PMD) |
| ALGO | algorithm (0-11) | operator 1 feedback |
| FX1 | effect slot 1 type (0-7) | effect slot 1 param 1 |
| FX2 | effect slot 2 type (0-7) | effect slot 2 param 1 |

Effect param 2 (TONE for distortion, RATE for chorus/flanger/phaser, TIME for
delay/reverb) is SysEx-only, matching how several other patch parameters (voice
name, KSC curves) are already out of reach of a three-encoder panel.

Every page shows its two live values on the OLED: OP1-4 show freq/level, LFO
shows speed/PMD, FX1/FX2 show the effect type plus a per-type label for param 1
("Drive" for distortion, "Sens" for touch wah, "Depth" for
chorus/flanger/phaser/reverb, "Feedback" for delay - thru has no parameters, so
only its type is shown), and ALGO renders the actual algorithm diagram. The
diagram renderer in `DX_GUI` covers all 12 FM topologies and is ported
byte-for-byte from the reference project's geometry tables.

### Menu (long press of the selector)

| Entry | Description |
|---|---|
| **Presets** | all 32 factory presets, opening on the one currently loaded |
| **System** | master volume, About, CPU load (live and peak %, underruns, dropped ring packets) |
| **&lt;&lt; BACK** | back to the page view |

## MIDI

Full reface DX MIDI/SysEx, on USB and DIN alike:

- note on / note off
- pitch bend (forwarded to `RDX_Synth::updatePB`)
- control change: mod wheel, volume, expression, sustain, portamento, algorithm
  quick-select, operator quick-edit CCs (forwarded to `RDX_Synth::processCC`);
  the algorithm and operator quick-edit CCs are gated by the SYSTEM "MIDI
  control" setting, matching the real hardware
- program change 0-31, selecting the corresponding factory preset
- active sensing (350 ms RX timeout silences all voices)
- identity reply
- parameter change / request
- bulk dump / request (addressing system / common / operator blocks)
- master tune (SYSTEM SysEx parameter, ±102.4/±102.3 cents), applied additively
  to pitch bend on all operators

Outgoing traffic is active sensing, the program change sent when a preset is
picked, and SysEx replies. Panel edits are deliberately not mirrored as CC - the
three encoders only reach a handful of the patch parameters.

Full spec: [`doc/MIDI_IMPLEMENTATION.md`](doc/MIDI_IMPLEMENTATION.md).

## Persistence

The virtual EEPROM (wear-levelled flash append log) persists:

- the UI octave
- the MIDI SYSTEM block
- the full current DX patch
- the master volume

Stored as `SettingsV3` ([`include/settings.h`](include/settings.h)); a legacy V2
record without the master volume is still read, so a device coming from the
standalone firmware keeps its patch. Autosave happens 2 s after the last change,
on core0 between two audio blocks - the multicore flash-park handshake this
instrument used to need is gone.

Full spec: [`doc/PERSISTENCE.md`](doc/PERSISTENCE.md).

## Presets

All 32 real Yamaha reface DX factory-bank voices (program change 0-31, 4 banks
of 8), parsed byte-exact from the official `.syx` factory dumps of the reference
project via a one-time host tool
([`tools/dx_syx_to_patches/`](../../tools/dx_syx_to_patches/)) and the verified
`syxToPatch()`. Full parity with the real factory bank.

Full spec: [`doc/PRESETS.md`](doc/PRESETS.md).

## Effects

Two independent slots, each selectable among 8 types: thru, distortion, touch
wah, chorus, flanger, phaser, delay, reverb - the real reface DX effect set,
confirmed against the official Yamaha Data List and ported from the reference
project (`fx_*.h`). Processing happens after the voice mix, before the I2S
output.

- Patch/SysEx-addressable only, matching the real hardware - no dedicated MIDI
  CC control exists for effects.
- Front-panel access via the FX1/FX2 pages (type + param 1); param 2 is
  SysEx-only.
- RP2350 adaptation: the ESP32 original probed heap_caps/PSRAM for scratch
  buffers at runtime and throttled polyphony from measured effect CPU time. This
  build uses one fixed static scratch buffer per slot (96 KB, sized for
  `FxReverb`) and no throttling, since the voice budget is fixed anyway.
- Switching a slot's type only clears the scratch region the incoming effect
  actually needs (`FXBase::scratchFootprintFloats()`), not the full 96 KB - found
  through an on-hardware CPU-load measurement that showed a 140 % peak (buffer
  underrun) on every effect switch; see `doc/CHANGELOG_DX_ENGINE.md` §19.
  Switching into delay or reverb can still cause one brief blip of under a
  block, since their delay lines are inherently close to the worst-case size.

## Design notes

- **Header-only DSP** in `dx_engine/` - no dynamic allocation, fixed buffers.
- **`RAM_HOT()`** (`effects/ram_hot.h`) places hot audio functions in RAM to
  avoid XIP jitter. No-op on host builds.
- **Single-precision float** throughout the audio path, effects included.
- **CPU load instrumentation**: `fill_buffer_i32()` times itself against its
  real-time budget and exposes current and peak percentages, readable from the
  System -> CPU Load screen. Since the producer sits in the main loop this is
  wall-clock time, so it includes preemption by the DMA and USB interrupts -
  which is the number that matters, because that is what the buffer lead has to
  absorb.
- **FPU flush-to-zero enabled at boot** (`pico_init()`, now in the core's
  `pico_hw.cpp`): found through an on-hardware load test showing hiss and jitter
  during fast note changes - the operator feedback low-pass filter and other IIR
  state in the effects chain decay into the subnormal float range on every voice
  release, and the Cortex-M33's software denormal path is far slower, occasionally
  blowing the audio time budget. See `doc/CHANGELOG_DX_ENGINE.md` §20.
- **Soft-clip limiter on the final mix** (`DX_Synth_Bridge::softClipSample()`):
  velocity sensitivity can push an operator about 1.5x above unity at high output
  level and velocity - an intentional dynamics feature, present in the reference
  too - and the only safety net was a hard integer clamp, which clipped audibly
  once level, velocity, algorithm mix and effects stacked up. The soft clipper is
  transparent below 0.9 and saturates smoothly toward ±1.0 above it. See §21.
- **High-note hiss with strong operator feedback is expected FM aliasing, not a
  bug**: feedback turns the operator's sine toward a sawtooth per the official
  spec, and at high pitch the harmonics above Nyquist alias back as noise. It is
  inherent to non-bandlimited feedback FM and byte-identical to the reference.
  Deliberately not "fixed", since that would mean deviating from a faithful port;
  presets that hit it audibly can have their operator level or feedback tuned
  down. See §22.

### RP2350 optimizations, wave 1

- **Audio block size aligned:** `SAMPLES_PER_BUFFER` 16 -> 64
  (= `DMA_BUFFER_LEN`). This fixed an LFO timing error - the block-level LFO was
  calculated for 64 samples per block while only 16 were rendered per IRQ, so it
  ran 4x too fast - and lowered the audio DMA IRQ rate from about 2744 Hz to
  about 686 Hz.
- **OLED I2C bus sped up:** 400 kHz -> 1 MHz (SH1106 fast mode). Display refresh
  about 2.5x faster, noticeably better encoder response.
- **Voice skipping:** `RDX_Synth::process()` and `renderAudioBlock()` skip idle
  voices via an `isActive()` check. Releasing voices, which are still audible,
  are not skipped. The saving is largest at typical polyphony: 1-4 notes held
  means 4-7 idle voices skipped.
- **Encoder PIO debounce corrected:** `freq_divider` 1 -> 444, taking the state
  machine clock from about 444 MHz to about 1 MHz, which gives the roughly 490 µs
  hardware debounce the PIO design intended. Before that the debounce interval
  was about 1.1 µs and effectively did nothing.

### RP2350 optimizations, wave 2

- **Per-sample LUTs moved from flash to RAM:** `sinTable` (4.1 KB),
  `SEMITONE_LUT` (1 KB) and `levelLUT` (1.5 KB) are read every sample by the
  RAM-resident audio functions (`sin01`, `semitonesToRatio`, `rdxGain`). They
  used to live in XIP flash (`.rodata`); `RAM_HOT` moves functions, not data, so
  every LUT access caused XIP cache jitter in the audio path. The tables now sit
  in `.time_critical.<name>` in RAM. Storage went `constexpr` -> `const` (the
  builders stay `constexpr`), plus `inline` (C++17) and COMDAT folding so only
  one copy of each table lands in RAM instead of one per translation unit.
- **Audio output path fused:** `fill_buffer(float*)` became
  `fill_buffer_i32(int32_t*)`. After the effects, soft clip, `float -> int32`,
  clamp and interleave now happen in a single loop writing straight into the DMA
  buffer. That removed the static `dxBuf` (512 bytes of RAM) and a whole extra
  pass over the block.
- **`semitonesToRatio` fast path in `RDX_Operator::compute()`:** when
  `phaseModSemitones == 0.0f` - the common idle case with no pitch bend,
  portamento, LFO PM or PEG - the code computes `phase_ += phaseInc_` instead of
  a LUT lookup plus multiplication. Semantically identical, since
  `semitonesToRatio(0) == 1.0` exactly.

## Acknowledgements

The FM engine was ported from the open ESP32/Arduino reface DX emulation project
[RDX-Reface-DX-emu](https://github.com/copych/RDX-Reface-DX-emu). That reference
tree is not vendored here; see [tools/README.md](../../tools/README.md).

Code for the DX port was developed with an LLM-assisted workflow: architecture
and review by the maintainer, code generation via glm-5.2, matching the existing
project conventions.

## License

GPLv3. See [the licensing section of the root README](../../README.md#license).
