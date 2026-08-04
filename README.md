# PicoVintageSynthCollection

Eight vintage synthesizer emulations for the RP2350, built from one shared
codebase. Same board, same core, one firmware image per instrument.

<p align="center">
  <img src="img/picofacedx.png" alt="PicoFace prototype hardware running PicoFaceDX" width="800">
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
USB PID. Six of them run on the hardware; PicoFaceOB and PicoFaceDX are not yet
tested there.** Open points are listed in
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
| PicoFaceYC | [OpenB3 / BeatrixCPP](https://github.com/pantherb/setBfree) (tone generation concepts) | AGPL-3.0 - see the note below |
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

> **Open question, PicoFaceYC.** The tree the YC drawbar engine goes back to
> (OpenB3 / BeatrixCPP) is **AGPL-3.0**, not GPL-3. The AGPL adds an obligation
> that GPL-3 does not, and it cannot be dropped by relicensing. Whether it
> reaches this repository depends on something only the author can answer: the
> YC README describes the tone generation as *concepts* "reimplemented for the
> RP2350 header-only architecture", and no file under
> `instruments/PicoFaceYC/include/yc_engine/` carries upstream code or an
> upstream copyright header - but the feature table also says "derived/ported
> from setBfree (BeatrixCPP)". If code was copied rather than the behaviour
> reimplemented from reading, `instruments/PicoFaceYC/` belongs under AGPL-3.0
> and should get its own `LICENSE` the way PicoFaceOB has. If it was a clean
> reimplementation, copyright does not follow the ideas and GPL-3 is right.
