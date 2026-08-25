# PicoFaceRD — Architecture

This document describes how PicoFaceRD produces the Roland MKS-20 / MK-80
("S/A synthesis") sound on an RP2350, and how the project is verified.
The complete engineering log (in German, including the full debugging
history) is in [RD_PORT.md](RD_PORT.md).

## 1. Two-stage approach: reference emulator + descriptor replay

A cycle-exact emulation of the original hardware (Hitachi 6301 CPU +
custom S/A sound chip, as reverse-engineered by
[giulioz/rdpiano](https://github.com/giulioz/rdpiano) and MAME) runs at
the limit of what an RP2350 can do. PicoFaceRD therefore splits the
problem:

1. **Host side (offline)** — the MAME-derived reference emulator plays
   every (patch, note, velocity) combination while a capture hook records
   all register writes the firmware makes to the sound chip: pitch LUT
   index, wave region, per-part envelope segment programming with exact
   sample timestamps.
2. **Device side (runtime)** — `RdNewEngine` replays those captured
   *descriptors*: it starts each envelope segment at its captured time
   and computes the envelope with the same integer arithmetic as the
   chip. The sample ROMs themselves are played back directly.

The reference emulator is **not** in this repository. It is a separate
checkout (`RDPIANO_REF`, Michi71/librdpiano) that the capture and the A/B
harness are pointed at; it is the **ground truth** for every engine change,
and it is also what produced the descriptors in the first place. See
[`tools/rd_extract/README.md`](../../../tools/rd_extract/README.md).

### Data pipeline (tools/rd_extract)

```
rd_extract          capture:  (patch, note, vel) -> register-write JSONL
     │              (plays a primer note first: the firmware kills the
     ▼               very first high note after a fresh boot)
rd_analyze.py       distill:  JSONL -> per-note part descriptors
     │              (pitch LUT, wave region, envelope segment chains,
     ▼               release chains; keeps the firmware's pre-attack
rd_pack.py           env pulse -- see "Lessons" below)
     │              pack:     descriptors -> compact .rdp binary
     ▼
rd_embed_packs.py   embed:    16 packs -> rd_packs.bin + .S (~3.3 MB)
```

Sample ROM data is repacked from the original interleaved 8-byte entries
into 4-byte words (`gen_pk4.cpp`; bits[13:0] = exponent, [14] = exp sign,
[23:15] = delta, [24] = delta sign — provably lossless over all banks).
This halves the dominant XIP cache-miss stream and 1.5 MB of flash.

### .rdp pack container format (version 1)

One pack per patch, parsed in place from flash by `RdNewEngine::loadPack`
(all multi-byte fields little-endian):

```
offset  size  field
0       4     magic "RDP2"
4       4     version (u32, currently 1)
8       1     patch_id (0..15)
9       1     bank (0 = MKS-20 A, 1 = MKS-20 B, 2 = MK-80)
10      2     entry_count (u16)
12      ...   entry_count entries, back to back
```

Each entry: `u8 note, u8 vel, u8 part_count`, followed by `part_count`
parts. Each part: an 8-byte header (`u8 flags, u8 env_offset,
u16 pitch_lut, u8 wave_loop, u8 wave_high, u8 nseg, u8 nrel`) followed by
`nseg` attack segments and `nrel` release segments, 6 bytes each
(`u32 t, u8 dest, u8 speed`). The release chain sits immediately after
the attack chain, so the engine stores a single segment pointer per part.
`rd_pack.py` writes this format; `rd_dump_packs` extracts the embedded
packs back out of the firmware sources for regression testing.

## 2. The engine (RdNewEngine)

- **Voice = 10 parts**, each part has its own pitch increment, wave
  region and envelope segment chain — the structure of the S/A chip.
- **Timeline replay is time-driven, not state-driven**: the firmware
  reprograms segments *before* their predecessors finish, so segments
  are scheduled by captured timestamps (`next_t` cache avoids per-sample
  flash reads).
- **Chip-exact envelope math** (the IC19/IC9/IC8 datapath), including
  FREEZE segments (`speed & 0x7F == 0` → envelope holds).
- **Note lifecycle safety net**: release chains that end on a FREEZE
  segment would ring forever when replayed with a different velocity
  layer; a generalized guard force-fades any audible, released,
  chain-exhausted part.
- **Voice capacity 32**, live limit set by the voice governor (below).
  Voice stealing prefers the oldest releasing voice.
- **Pitch factor**: pitch bend (±2 semitones) and master tune (±50
  cents) combine into one Q16 factor applied to all part phase
  increments; at center the math is exactly identity, so the default
  state is bit-identical to an unbent engine.

### Dual-core rendering

- Core 0 runs the audio **producer** in the main loop (not in an IRQ):
  it drains the event ring, calls `renderBlock()`, applies FX and fills
  the I2S buffer pool. The I2S DMA IRQ body is microscopic.
- Core 1 runs a **RAM-resident worker loop**. Per 64-sample block there
  is exactly ONE rendezvous: core 0 publishes the block via a doorbell
  (with a data memory barrier before the store), both cores render their
  half of the voices (split by voice-index parity), core 1 signals done.
  Voice-outer/sample-inner loops keep part state in registers and make
  sample-ROM reads sequential.
- All voice-state mutations (note on/off, limit changes, pitch factor)
  happen on core 0 *between* blocks — the worker only observes state
  inside the rendezvous, so no locks are needed.

### Why the flash discipline matters

The RP2350 runs at 480 MHz here with flash at 120 MHz QSPI (in spec).
There is no D-cache; every stray flash access in the hot path costs an
XIP miss. Hard-learned rules: hot functions (including the core-1 spin
loop) live in RAM (`__not_in_flash_func`); per-sample flash reads are
forbidden (descriptors are parsed in place, but scheduling state is
cached in RAM); GCC silently promotes never-written non-`const` statics
to `.rodata` (flash!) — verified with `nm`, fixed with explicit runtime
copies to `.bss`.

## 3. Voice governor

The VOICES page offers fixed polyphony (8/16/24/32) or **Auto**:

- Base limit = per-rate cap (16 @ 20 kHz, 12 @ 32 kHz patches).
- **Cut**: instantaneous render load ≥ 90 % → limit −6 (floor 6).
  A detected buffer underrun (already audible) drops straight to the
  floor.
- **Active culling**: lowering the limit marks excess voices; the block
  renderer fades each one out over a single 64-sample block (~3 ms
  linear ramp, click-free) and frees it. CPU relief arrives within one
  block instead of after the natural decay — without this, a limit cut
  is nearly a no-op under sustained load.
- **Recovery**: +1 voice per 700 ms of rendered samples, only while load
  < 70 % (the 70–90 % dead zone prevents pumping). All governor timing
  is sample-count based, which makes it deterministic and host-testable.

## 4. Effects chain

`engine output → vintage DAC stage → bass shelf → treble shelf →
tremolo → phaser → chorus (stereo split) → volume → soft clip`

- **Vintage DAC stage** (switchable): true 12-bit requantization plus a
  2-pole ~6 kHz reconstruction low-pass — the darker, softer character
  of the original converter generation.
- **Phaser**: mono 4-stage first-order all-pass ladder with tanh-limited
  feedback (port of the PicoFaceCP `CpPhaser`, mono for half the cost);
  coefficient update every 8 samples.
- **Chorus**: stereo two-tap BBD-style modulated delay with
  reconstruction low-passes.

The FX chain runs once per sample after the voice mix, so its cost is
independent of polyphony.

## 5. Control plane

- **UI, USB-MIDI, encoders and the display all live on core 0** in the
  main loop, decoupled from audio through a same-core SPSC event ring
  (256 entries; dropped-event counter visible in the diagnostics
  footer).
- The OLED is never sent in one blocking transfer (~25 ms I2C would
  starve the producer); the frame buffer is streamed out in half-tile
  rows (~1.5 ms each) between audio fills.
- The MIDI front-end implements the MK-80 implementation chart
  ([MIDI.md](MIDI.md)).
- **Persistence**: all panel state (17-byte versioned record) goes into
  a wear-leveled two-sector flash append log (`veeprom`), auto-saved 2 s
  after the last encoder edit and only while no voice is active. In this
  architecture nothing needs to be parked for a flash write: the save
  runs on core 0 between fills, core 1 spins in RAM, and the writer
  disables IRQs and restores the QMI flash timing itself. MIDI-driven
  changes (e.g. program changes from a sequencer) are deliberately
  session-transient — like the original, only panel state is memorized.

## 6. Verification methodology

- **Host-first**: every engine change is validated on the host against
  the reference emulator before flashing. `renderSample()` remains as a
  single-sample wrapper so host tools can drive the engine directly.
- **A/B metric**: lag-scan cross-correlation (±300 samples) between
  reference and replay, because onset alignment alone is hypersensitive
  at high frequencies. The reference side always plays a primer note
  first (fresh-boot quirk of the firmware).
- **Full matrix**: 1920 cells (16 patches × 30 notes × 4 velocities).
  After the P4a analyzer fix: median r = 0.9997, 98.9 % of cells ≥ 0.9.
- **Regression runner** (`tools/rd_extract/run_regression.sh`): one
  command that rebuilds the host tools, dumps the packs *embedded in the
  firmware sources* (testing exactly what ships), checks six frozen
  reference correlations and runs stuck-voice stress tests. Exit code
  0/1.
- **On-device diagnostics**: the OLED footer
  (`P U D A N` = peak load, underruns, dropped events, active voices,
  note-ons) turned out to be the single most valuable debugging tool of
  the project.

## 7. Lessons learned (short list)

1. **Measure, don't guess** — every real breakthrough in this project
   came from a counter, a WAV diff or a host reproduction, never from a
   plausible theory.
2. **The firmware programs envelopes before pitch**: a per-part
   pre-attack pulse gives the envelope a head start. Dropping it is
   inaudible on low notes and catastrophic on high ones (r = 0.23).
3. **FREEZE segments** at the end of captured release chains create
   immortal voices when replayed with nearest-velocity descriptors —
   sustaining patches (Clavi) are the canary for note-lifecycle bugs.
4. **Segment scheduling must be time-driven**; the firmware reprograms
   parts before `end_reached` fires.
5. **A voice-limit without active culling does not reduce load** — the
   voices you already have keep rendering; you must fade them out.
6. **Barriers before doorbells**; render in the main loop, not in the
   IRQ; keep the IRQ body microscopic and watch PIO TXSTALL, not just
   pool underruns.
