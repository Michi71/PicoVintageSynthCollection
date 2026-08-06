# PicoVintageSynthCollection

Eight vintage synthesizer emulations for the RP2350, built from one shared
codebase. Same board, same core, one firmware image per instrument.

<p align="center">
  <img src="img/picofacedx.png" alt="PicoFace prototype hardware running PicoFaceDX" width="800">
</p>

## Demo

<p align="center">
  <a href="https://youtu.be/cG6gyhaqCxE">
    <img src="https://img.youtube.com/vi/cG6gyhaqCxE/maxresdefault.jpg" alt="PicoVintageSynthCollection demo video" width="800">
  </a>
  <br>
  <a href="https://youtu.be/cG6gyhaqCxE">&#9654; Demo video on YouTube</a>
</p>

## Instruments

| Folder | Instrument | Binary |
|---|---|---|
| [instruments/PicoFaceYC](instruments/PicoFaceYC/README.md) | Yamaha reface YC (drawbar organ) | PicoFaceYC.uf2 |
| [instruments/PicoFaceCP](instruments/PicoFaceCP/README.md) | Yamaha reface CP (electric piano, mdaEPiano) | PicoFaceCP.uf2 |
| [instruments/PicoFaceDX](instruments/PicoFaceDX/README.md) | Yamaha reface DX (4-operator FM) | PicoFaceDX.uf2 |
| [instruments/PicoFaceRD](instruments/PicoFaceRD/README.md) | Roland RD / MKS-20 (sample piano) | PicoFaceRD.uf2 |
| [instruments/PicoFaceJ6](instruments/PicoFaceJ6/README.md) | Roland Juno-6 | PicoFaceJ6.uf2 |
| [instruments/PicoFaceMD](instruments/PicoFaceMD/README.md) | Minimoog Model D | PicoFaceMD.uf2 |
| [instruments/PicoFaceSM](instruments/PicoFaceSM/README.md) | ARP/Eminent Solina String Ensemble | PicoFaceSM.uf2 |
| [instruments/PicoFaceOB](instruments/PicoFaceOB/README.md) | Oberheim OB-X (engine ported from OB-Xf) | PicoFaceOB.uf2 |

A ninth is in progress: [instruments/PicoFaceJV](instruments/PicoFaceJV/README.md)
(Roland JV-880). It is not in the release binaries and not yet confirmed on the
hardware, and it only builds where a JV-880 ROM set is present — the ROMs are
not distributable. Without them the configure step skips it, so the eight above
are unaffected.

## Hardware

- RP2350
- Board `sparkfun_promicro_rp2350`
- I2S audio
- 128x64 OLED over I2C
- Three rotary encoders with push buttons
- USB MIDI and DIN MIDI

The pin map is the same for every instrument and lives in
[core/include/project_config.h](core/include/project_config.h).

## Building

```bash
git clone --recurse-submodules https://github.com/Michi71/PicoVintageSynthCollection.git
cd PicoVintageSynthCollection
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The artifacts are then in `build/<instrument>.uf2`.

Building a single instrument:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceMD
cmake --build build
```

## Repository layout

```text
core/            shared runtime: audio pipeline, hardware, USB/DIN MIDI, GUI, persistence
cmake/           picoface_add_instrument() and the SDK import helpers
lib/             third-party static libraries (audio, encoder, u8g2) and the SDK submodules
instruments/     one folder per instrument: instrument.cmake, src/, include/, doc/, README.md
tools/           host-side tools; not part of any firmware image
docs/            documentation shared by all instruments
img/             photos of the prototype hardware
```

## Documentation

Shared:

- [Architecture](docs/ARCHITECTURE.md) - how core and instruments fit together, and why
- [Adding an instrument](docs/ADDING_AN_INSTRUMENT.md) - the three steps needed
- [Host tools](tools/README.md)

Per instrument, under `instruments/<name>/doc/`: MIDI implementation charts,
persistence formats, preset tables and the engineering logs of the individual
ports. The per-instrument README links them.

Manufacturer manuals and service documentation are **not** part of this
repository. Where an emulation follows a specific manual or schematic, the
instrument's README names the document so it can be obtained separately.

## Status

**All eight instruments build from a single configure run, each with its own
USB PID, and all eight run on the hardware.** Open points are listed in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), section 8.

## License

**GNU General Public License, version 3 or later** - see [LICENSE](LICENSE).
Copyright (C) 2026 Michi71.

That is what every one of the seven single-instrument repositories this
collection was assembled from already declared: all seven shipped the identical
GPL-3 licence text, and every instrument README states GPL-3. It is also the
lowest common denominator of what the engines are built on. Where an instrument
derives from someone else's work:

| Part | Upstream it derives from | That upstream's license |
|---|---|---|
| core, cmake, tools | own work | - |
| PicoFaceYC | [OpenB3 / BeatrixCPP](https://github.com/pantherb/setBfree) - tone generation *concepts* only, no code | AGPL-3.0 upstream, does not reach here; see below |
| PicoFaceCP | [mda-EPiano](https://sourceforge.net/projects/mda-vst/) (engine, in tree) | GPL-3.0-or-later |
| PicoFaceDX | an ESP32 reface DX emulation (engine) | see that project |
| PicoFaceRD | [giulioz/rdpiano](https://github.com/giulioz/rdpiano) + MAME (reference emulator, host side only) | GPL |
| PicoFaceJ6 | [junox](https://github.com/dzannotti/junox) (patch table, parameter scaling) | GPL-3 |
| PicoFaceMD | [BelaMiniMoogEmulation](https://github.com/lbros96/BelaMiniMoogEmulation) (ladder filter) | stated by its author to be under no copyright |
| PicoFaceSM | [string-machine](https://github.com/jpcima/string-machine) (DSP models) | Boost Software License 1.0 |
| PicoFaceOB | [OB-Xf](https://github.com/surge-synthesizer/OB-Xf) (engine) | GPL-3.0-or-later |

`instruments/PicoFaceOB/` additionally carries its own `LICENSE`, identical in
text, because that instrument's engine is a direct port of OB-Xf and its files
keep the upstream copyright headers. Every instrument builds into its own
binary, so a stricter licence on one of them stays confined to that binary.

Source files carry a two-line SPDX header rather than the full notice, to keep
it out of the way of the code. Three kinds exist:

- own work: `GPL-3.0-or-later` plus the copyright line;
- ported trees (`instruments/PicoFaceDX/include/dx_engine/`,
  `instruments/PicoFaceRD/**/rd_engine/`): the licence line plus a note that
  copyright is shared with the upstream authors - no sole claim is made there;
- upstream files (`instruments/PicoFaceOB/include/obxf/`, CP's `mdaEPiano.*`,
  MAME's `mame_utils.h` and `mcu_ops.h`, the SDK-derived `usb_descriptors.c`,
  `get_serial.*` and `tusb_config.h`): untouched, they keep the header they
  came with. The last four are MIT and BSD-3-Clause, not GPL.

**On PicoFaceYC and the AGPL.** OpenB3 / BeatrixCPP, whose tone generation
concepts the YC drawbar engine follows, is AGPL-3.0 - stricter than GPL-3, and
not something that can be dropped by relicensing. It does not apply here: the
engine under `instruments/PicoFaceYC/include/yc_engine/` was written for this
project and contains no upstream code, which is why no file there carries an
upstream copyright header. Copyright covers the expression, not the concepts,
so GPL-3 is the right licence for it.
