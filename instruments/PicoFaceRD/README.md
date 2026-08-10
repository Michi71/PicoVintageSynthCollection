# PicoFaceRD

<p align="center">
  <img src="../../img/picofacerd.png" alt="PicoFaceRD prototype hardware" width="800">
</p>

A Roland **MKS-20 / MK-80** ("S/A synthesis") digital piano clone, one of the
eight instruments in [PicoVintageSynthCollection](../../README.md). Instead of emulating the original hardware cycle-exactly at runtime, it plays a **descriptor-driven re-implementation** of the S/A engine: the original firmware's voice programming was captured note-by-note on a host-side reference emulator, distilled into compact per-note descriptors, and is replayed on-device with chip-exact envelope arithmetic. The result is validated against the reference emulator with a cross-correlation matrix of 1920 cells (median r = 0.9997).

## Features

| Area | Details |
|---|---|
| Sounds | All 16 patches: MKS-20 (Piano 1–3, Harpsichord, Clavi, Vibraphone, E-Piano 1–2) and MK-80 (Classic, Special, Blend, Contemporary, A. Piano 1–2, Clavi, Vibraphone) |
| Engine | Timeline-replay of captured S/A voice programming; 10 parts per voice, chip-exact envelope math, native 20 kHz / 32 kHz per patch (no resampling) |
| Polyphony | 8 / 16 / 24 / 32 voices or **Auto** — a load-adaptive voice governor with active culling (default; the original is 16-voice) |
| Effects | Vintage DAC stage (12-bit requantization + 2-pole reconstruction filter), bass/treble shelves, tremolo, mono 4-stage phaser, stereo BBD-style chorus |
| MIDI | USB and DIN MIDI, implementation modeled on the MK-80 MIDI implementation chart (sounding range 21–108 with octave folding, damper, FX switches CC 92/93/95, reset CC 121, all-notes-off CC 123, program change, pitch bend ±2 semitones) — see [doc/MIDI.md](doc/MIDI.md) and the collection's CC table in [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md), section 6a |
| Tuning | Master tune ±50 cents in 1-cent steps with live A4 frequency display |
| Persistence | All panel settings in a wear-leveled flash append log (versioned, CRC-protected), auto-saved 2 s after the last edit while idle |
| Display | Boot splash, header-bar page UI with percent values (1 % encoder steps) and a live diagnostics footer |
| Clocking | Dual Cortex-M33 @ 480 MHz, flash at 120 MHz QSPI (in spec), dual-core voice rendering |

## Hardware

The board is the same for every instrument in the collection; the full pin map
lives in [core/include/project_config.h](../../core/include/project_config.h),
and [doc/hardware/PROTOTYPE.md](doc/hardware/PROTOTYPE.md) describes a complete
3U/10HP module build (panel, stripboards, wiring).

This instrument is the one that pushes the platform hardest: it runs at 480 MHz
with flash at 120 MHz QSPI, set through `PICOFACE_SYS_CLOCK_HZ` and
`PICOFACE_QMI_M0_TIMING_TARGET` in its `instrument.cmake`. The other seven run
at 444 MHz.

Note: the build uses the SDK board definition `sparkfun_promicro_rp2350` as a
compatible 16 MB stand-in — the Waveshare RP2350 Plus has no board file in the
pinned SDK, and all pins used here are addressed explicitly, so the only thing
taken from the board file is the 16 MB flash size (which matches).

The 16 MB is not decoration here: the image is **5.07 MB**, mostly sample packs,
so this one does not fit a 4 MB board such as a base Pico 2, and there is no
reduced variant. An oversized `.uf2` stops copying without saying why. See
[How much flash an instrument needs](../../README.md#how-much-flash-an-instrument-needs).

## User interface

Encoder **Select** switches the page (with wrap-around), encoders **A** and **B** edit the two page parameters. Continuous values are shown in percent and step 1 % per detent.

| Page | Encoder A | Encoder B |
|---|---|---|
| PATCH | Instrument 1–16 (header shows the bank: `MKS-20` / `MK-80`) | Volume |
| CHORUS | Depth (0 % = off) | Rate |
| TREMOLO | Depth (0 % = off) | Rate |
| PHASER | Depth (0 % = off) | Rate (0.1–5 Hz) |
| EQ | Bass (50 % = neutral) | Treble (50 % = neutral) |
| VOICES | Polyphony 8/16/24/32/**Auto** | — (line B shows live `Act <active>/<limit>`) |
| TUNE | Master tune ±50 cents | — (line B shows the resulting A4 frequency) |
| SYS | Vintage DAC filter ON/OFF | MIDI receive channel 1–16 / Omni |

The footer shows a live diagnostics line: `<instrument> P<peak-load %> U<buffer underruns> D<dropped events> A<active voices> N<note-on count>`.

### Voice governor (Auto mode)

In **Auto**, the polyphony limit follows the CPU load: the base limit is the proven per-rate cap (16 voices for 20 kHz patches, 12 for 32 kHz). When the instantaneous render load reaches 90 % — or a buffer underrun is detected — the governor cuts the limit (down to a floor of 6), and excess voices are faded out within a single 64-sample block (~3 ms, click-free) instead of waiting for their natural decay. Recovery is deliberately slow (+1 voice per 700 ms, only below 70 % load) to avoid pumping. Manual settings bypass the governor entirely.

## Building

Built together with the rest of the collection, or on its own:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceRD
cmake --build build
```

If you already have a shared SDK checkout, export `PICO_SDK_PATH` (and
optionally `PICO_EXTRAS_PATH`) instead of initialising the `lib/pico-sdk` and
`lib/pico-extras` submodules — the in-tree submodules are only the fallback.
The `lib/u8g2/u8g2` submodule is always required.

Flash `build/PicoFaceRD.uf2` via BOOTSEL. Footprint in the collection build:
5,318,096 bytes of flash (the sample banks dominate), 35,420 bytes of static RAM
plus about 44 KB of heap for the pack descriptors.

## Architecture

```
                         Core 0                                Core 1
  ┌───────────────────────────────────────────────┐   ┌─────────────────────────┐
  │ main loop: USB-MIDI · encoders · OLED (staged │   │ RAM-resident render     │
  │ half-tile flush) · settings autosave          │   │ worker: odd-index       │
  │        │  same-core SPSC event ring           │   │ voices, one doorbell    │
  │        ▼                                      │   │ rendezvous per 64-      │
  │ audio producer: drains ring → RdNewEngine     │◄──┤ sample block            │
  │ block render (even voices) → vintage FX →     │   └─────────────────────────┘
  │ softclip → I2S buffer pool → PIO I2S DMA      │
  └───────────────────────────────────────────────┘
```

The sound data pipeline is host-side: a MAME-derived reference emulator (based on [giulioz/rdpiano](https://github.com/giulioz/rdpiano)) plays each (patch, note, velocity) while a capture hook records the firmware's register writes; an analyzer distills them into per-note part descriptors (pitch, wave region, envelope segment chains); a packer emits compact `.rdp` packs that are embedded in the firmware together with losslessly repacked 4-byte sample banks. On-device, `RdNewEngine` replays those descriptors with the same envelope arithmetic as the chip.

Details in [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md). The full engineering log
with the complete debugging history lives in [doc/RD_PORT.md](doc/RD_PORT.md).

RD is the only instrument that uses core1: it keeps a RAM-resident render worker
there and switches the sample rate at runtime. Both go through optional hooks of
the instrument interface, see [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md),
section 8.

## Development & testing

Everything is verified host-first on the Mac/Linux side before it touches the device:

```sh
tools/rd_extract/run_regression.sh
```

builds the host tools, extracts the 16 embedded packs from the firmware sources (so the test covers exactly what ships), runs a six-cell A/B matrix against the reference emulator with frozen expected correlations, and runs stuck-voice stress tests (chord hammering with sustain pedal, single- and dual-thread) — `REGRESSION PASS/FAIL` with exit code. The extraction and analysis toolchain is documented in [tools/rd_extract/README.md](../../tools/rd_extract/README.md).

[tools/rd_midi/](../../tools/rd_midi/) holds MIDI utilities for testing: `midi_keyboard_only.py` strips a Standard MIDI File down to pure keyboard performance (notes, damper, pitch bend) so sequencer dumps can be replayed against the device.

## ROM data & credits

- **[giulioz/rdpiano](https://github.com/giulioz/rdpiano)** — the reverse engineering of the Roland S/A sound generation (MCU emulation, ROM decryption) that this project builds on. The host-side reference emulator is derived from that work and from **MAME**.
- The embedded sample banks and note descriptors are *derived data* from MKS-20 / MK-80 ROM images. Roland is not affiliated with this project; all trademarks belong to their owners. This is a non-commercial educational/preservation project.
- **[u8g2](https://github.com/olikraus/u8g2)** (display), **Raspberry Pi Pico SDK / pico-extras** (platform, PIO I2S audio).
- Sibling instruments in this collection: [PicoFaceCP](../PicoFaceCP/README.md) is where the UI style, the phaser and the veeprom module come from.

## License

GPL-3.0. See [the licensing section of the root README](../../README.md#license).
