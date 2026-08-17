# PicoVintageSynthCollection

Ten vintage synthesizer emulations for the RP2350, built from one shared
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
| [instruments/PicoFaceJV](instruments/PicoFaceJV/README.md) | Roland JV-880 (native engine over the original PCM data) | local build only |
| [instruments/PicoFaceD5](instruments/PicoFaceD5/README.md) | Roland D-50 (native LA engine over the original PCM data) | local build only |

The JV-880 and the D-50 are the exceptions to "download and flash": they only
build where the respective ROM set is present, and those ROMs are not
distributable, so neither is in the release binaries. Without them the
configure step skips them and the eight above are unaffected. Both run on the
hardware; see the flash note below for the one build option the JV-880 needs
on a small board.

## Hardware

- RP2350 with 16 MB of flash
- I2S audio (PCM5102 DAC)
- 128x64 OLED over I2C (SH1106)
- Three rotary encoders with push buttons
- USB MIDI and DIN MIDI

The pin map is the same for every instrument and lives in
[core/include/project_config.h](core/include/project_config.h).

**On the board setting.** The build defaults to `PICO_BOARD=sparkfun_promicro_rp2350`,
and that is a statement about flash size rather than about hardware: the
prototype runs a Waveshare RP2350-Plus, and the SparkFun definition was picked
because it declares 16 MB where the plain `pico2` definition declares 4. Any
RP2350 board with 16 MB works if the pins above are reachable on it. For a
Pico-format board the honest pair is

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DPICO_FLASH_SIZE_BYTES=16777216
```

Without that second flag, `pico2` caps the image at 4 MB and PicoFaceCP
overflows it by 5.5 %. Nothing else in the SparkFun definition reaches the
firmware: the flash timing and the system clock are set by the project itself in
[core/src/pico_hw.cpp](core/src/pico_hw.cpp), and `PICO_FLASH_SPI_CLKDIV` is
identical in both. The one thing it declares that the prototype does not have is
an 8 MB PSRAM region on GPIO 19 — nothing is placed there, so it costs nothing,
but PSRAM is not a thing to reach for on this hardware without checking the
board first.

### Switching instruments

One board, ten firmware images, so choosing a different instrument is something
you do rather than something a factory does once. **Hold all three encoder
buttons together.** After half a second the display takes over and counts down
from two; let go and nothing happens. Hold it out and the board silences itself,
drains the audio it had already queued, and reappears as the `RPI-RP2` drive —
drop the next `.uf2` on it.

No button combination in any instrument uses all three at once, so it cannot
happen by accident, and the BOOTSEL button on the module is never needed for
this. Keep that button reachable anyway: if a firmware is too broken to reach
its own user interface, BOOTSEL plus a power cycle is the way back.

### How much flash an instrument needs

The reference board carries 16 MB. That is not a requirement of the design, but
three instruments ship sample data and will not fit the 4 MB that a base
Raspberry Pi Pico 2 and many other RP2350 boards provide:

| Instrument | Image | On a 4 MB board |
|---|---|---|
| YC, DX, J6, MD, SM, OB | 90-190 KB | fits anywhere |
| PicoFaceCP | 4.22 MB | does not fit |
| PicoFaceJV | 4.34 MB | only with `-DPICOFACEJV_4MB=ON` (3.76 MB, banks A+B) |
| PicoFaceD5 | 0.86 MB | fits |
| PicoFaceRD | 5.07 MB | does not fit |

CP carries the Rhodes, Clavinet and E-piano sample sets, RD the MKS-20 sample
packs. Neither has a reduced variant: there is no subset to drop the way the
JV's three wave banks can become two. Anything from 8 MB up holds all ten. The
D-50 is the one sample-based instrument that fits everywhere: its whole sample
ROM is 512 KB.

An oversized `.uf2` fails to copy in a way that names no reason: the file
transfer stops or the drive rejects it, with nothing on screen about flash size.
That is the symptom, and it is not a corrupt download.

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

### If the very first cmake call dies with an architecture error

On macOS a mixed package manager prefix produces this, and it reads like a
problem with the project:

```
dyld: Library not loaded: /opt/local/lib/libarchive.13.dylib
  Referenced from: /opt/local/bin/cmake
  Reason: (mach-o file, but is an incompatible architecture (have 'arm64', need 'x86_64'))
Abort trap: 6
```

It is not. The complaint comes from `dyld` about loading `cmake` itself, before
any of this repository is read, and "need 'x86_64'" means the `cmake` binary is
an Intel build sitting next to Apple Silicon libraries — a MacPorts or Homebrew
prefix that was installed under Rosetta, or carried over from an Intel Mac
without the migration. Check with:

```bash
uname -m                 # arm64 on Apple Silicon
file $(which cmake)      # must say the same
```

If they disagree, reinstall the toolchain for the machine's own architecture
(MacPorts documents a migration procedure for exactly this). Apple Silicon
itself is fine: the collection is developed on it.

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

**All ten instruments build from a single configure run, each with its own USB
PID, and all ten run on the hardware.** Eight are in the release binaries; the
JV-880 and the D-50 need a local ROM set and are therefore built locally only.
Open points are listed in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
section 8.

## License

**GNU General Public License, version 3 or later** - see [LICENSE](LICENSE).
Copyright (C) 2026 Michi71.

The collection was assembled in August 2026 from seven single-instrument
repositories -- YC, CP, DX, RD, J6, MD and SM; OB, JV and D5 were written here
afterwards, which is why the number is seven and not ten. Each of those seven
already shipped the identical GPL-3 licence text, and every instrument README
states GPL-3. It is also the lowest common denominator of what the engines are
built on. Where an instrument derives from someone else's work:

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
| PicoFaceJV | own work, over the machine's own PCM data | - |
| PicoFaceD5 | [munt](https://github.com/munt/munt) - the LA32's wave generation and the Boss reverb topology as that project *documents* them, no code | LGPL-2.1-or-later upstream, does not reach here; see below |

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

**On PicoFaceD5 and munt.** The D-50 and the MT-32 share the LA32 sound chip,
and the MT-32 emulator [munt](https://github.com/munt/munt) has read that chip
out and written down what it does: how a cutoff becomes the width of a cosine
edge, how the resonance decays, and - from the same era's Boss reverb chip,
whose data lines were traced by Lord_Nightmare, balrog and Mok - the topology
the reverb here follows. munt is LGPL-2.1-or-later. Nothing was copied: the
engine under `instruments/PicoFaceD5/include/d5_engine/` was written for this
project from that written description and from the D-50's own firmware, which
is why no file there carries an upstream copyright header. The description is
what mattered, and a description is not the program.

**On PicoFaceYC and the AGPL.** OpenB3 / BeatrixCPP, whose tone generation
concepts the YC drawbar engine follows, is AGPL-3.0 - stricter than GPL-3, and
not something that can be dropped by relicensing. It does not apply here: the
engine under `instruments/PicoFaceYC/include/yc_engine/` was written for this
project and contains no upstream code, which is why no file there carries an
upstream copyright header. Copyright covers the expression, not the concepts,
so GPL-3 is the right licence for it.
