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

**Unsettled - this repository has no root LICENSE file yet.** The facts, so the
decision can be made on them:

| Part | Upstream it derives from | That upstream's license |
|---|---|---|
| core, cmake, tools | own work | - |
| PicoFaceYC | [setBfree](https://github.com/pantherb/setBfree) / BeatrixCPP (concepts) | GPL |
| PicoFaceCP | [mda-EPiano](https://sourceforge.net/projects/mda-vst/) (engine, in-tree) | GPL-3 |
| PicoFaceDX | an ESP32 reface DX emulation (engine) | see that project |
| PicoFaceRD | [giulioz/rdpiano](https://github.com/giulioz/rdpiano) + MAME (reference emulator) | GPL |
| PicoFaceJ6 | [junox](https://github.com/dzannotti/junox) (patch table, scaling) | GPL-3 |
| PicoFaceMD | [BelaMiniMoogEmulation](https://github.com/lbros96/BelaMiniMoogEmulation) (ladder filter) | stated by its author to be under no copyright |
| PicoFaceSM | [string-machine](https://github.com/jpcima/string-machine) (DSP models) | Boost Software License 1.0 |
| PicoFaceOB | [OB-Xf](https://github.com/surge-synthesizer/OB-Xf) (engine) | GPL-3.0-or-later |

All seven single-instrument repositories this collection was assembled from
shipped a GPL-3 LICENSE file, and each instrument's README still states GPL-3
(the exceptions above are the *upstream* licenses of individual components, not
of the instrument as a whole). An earlier version of this README described the
repository as "MIT except PicoFaceOB"; that does not match the sources, so it
has been removed rather than carried forward.

Every instrument builds into its own binary, so per-instrument licensing is
possible - but it needs to be decided and written down, and a root `LICENSE`
file added.
