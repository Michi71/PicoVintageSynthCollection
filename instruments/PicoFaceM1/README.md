# PicoFaceM1

A Korg **M1** for the collection: the plain M1, without the Plus+1
expansion. It builds a firmware image, but only on a machine that has an M1
ROM set. The ROMs are not distributable, so without them the instrument
removes itself from the build and the other instruments are unaffected.

**Untested on hardware.** Everything below was checked in host renders only.

## What this is, and is not

It is not an emulation of the M1's chips. The M1 runs its envelopes, LFOs and
voice allocation in software on a V50, and that part is re-implemented here
from the OS code. The sound chips are replaced with code that does what the
analysis found they do:

| M1 | here |
|---|---|
| TG MB87402/403, 16 channels, 4.12 pitch register | 16 voices, sample read forward from the 4 MB PCM in flash |
| IC30 interpolation kernel, 8 taps, 4096 phases | the same kernel, from the user's IC30 |
| two MB87404 VDF chips, coefficient ROMs IC26/IC20 | four one-pole stages, two per chip, from both tables of each chip (topology assumed, 24 dB/oct) |
| VDA in the second VDF chip | multiplicative amplitude, linear ramp per 20.8 ms update |
| voice pan register to buses A/B/C/D | the same register words (DS:0854) |
| MDE MB87405, a programmable DSP with 64 K words of delay RAM | an algorithmic port of all 33 effect types, 2 × 32 K words of int16 delay RAM |

The tables come from the user's ROM files at configure time: multisounds,
drum sounds, interpolation kernel, VDF coefficients, EG, LFO, velocity, pitch
and pan tables, the effect coefficients, and the factory programs,
combinations and drum kits. The repository contains no ROM data.

How the engine works, which parts are firmware-exact and which are assumed is
in [doc/ENGINE.md](doc/ENGINE.md).

## State

| Part | State |
|---|---|
| Multisounds 0–99 (PCM and DWGS), key splits, NT, tune and trims | works |
| IC30 kernel, VDF, VDA, EG core | matches the Python reference within 2.6·10⁻³ of peak on all 26 single factory programs |
| Pitch, VDF, VDA, EG times, MGs, controllers | firmware formulas from the driver code (EF83); a few constants assumed, see ENGINE.md |
| Single, Double, Mono, Hold, OSC 2 delay start | works |
| Drum programs (three factory kits) | works, keymap as the OS builds it |
| Combinations: 8 timbres, key and velocity windows, transpose, detune, level, pan | works |
| Voice allocation (queue, stealing order, damper) | as the OS does it |
| 33 effect types, serial/parallel, balances, Out-3/4 panpots | see "Effects" in ENGINE.md |
| Scale types other than equal temperament | not implemented |
| SysEx, MIDI program/combi dump | not implemented |
| Out 3/4 as separate outputs | no: the PicoFace has one stereo pair, which is M1 outputs 1/2 |

## Playing it

- **PROG/COMBI page**: encoder A picks the program or combination (0–99),
  encoder B switches between Program and Combination mode.
- MIDI: notes, pitch bend (joystick X), CC 1 (joystick +Y), CC 2 (joystick −Y),
  channel aftertouch, CC 64 damper, CC 65 rotary speaker slow/fast, CC 7
  volume, program change (programs in Program mode, combinations in
  Combination mode).
- All 100 factory combinations have every timbre on the global channel. The
  PicoFace takes the channel it listens on (or any channel in Omni) as the
  global channel; per-timbre MIDI channels are not used.

## Hardware and load

- **Flash**: about 4.3 MB (4 MB of PCM). It needs a board with more than 4 MB
  of flash; there is no reduced build.
- **RAM**: 128 KB for the effect delay lines (the MDE's 64 K words), 32 KB for
  the interpolation kernel and 32 KB for the VDF coefficient tables, so that
  only the PCM is read from flash.
- **Sample rate**: 31 250 Hz, the M1's own (32 MHz / 1024). The I2S clock
  divider handles it like the RD's 20/32 kHz; the PCM5102 accepts it.
- Everything runs on core 0. The CPU load has not been measured on hardware.

## Building the firmware

Put these files into `instruments/PicoFaceM1/roms/`:

| File | What |
|---|---|
| `IC36.bin` … `IC39.bin` | PCM, low bytes (4 × 512 KB) |
| `IC40.bin` … `IC43.bin` | PCM, high bytes (4 × 512 KB) |
| `IC30.bin` | coefficient ROM (64 KB; an `IC20.bin` or `IC26.bin` dump is the same chip contents) |
| the two halves of the M1 OS | the original OS, tested with Rev 19 (`IC32` even bytes, `IC23` odd bytes); the file names do not matter, the converter finds the pair by content |
| `*.syx` | an M1 ALL DATA dump, normally Korg's factory preload `M1preld.syx` |

The Plus+1 OS is laid out differently and is refused. Then:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target PicoFaceM1
```

The configure step runs `tools/m1_extract/m1_make_blob.py`, which writes
`build/PicoFaceM1_rom/` (the PCM blob and the table header). It reruns when
the converter or the dump changes.

## Host renders

```bash
tools/host_tests/m1_engine_test/build.sh build/PicoFaceM1_rom
tools/host_tests/m1_engine_test/m1_render build/PicoFaceM1_rom prog 1 60 100 1.0 2.5 piano.wav
tools/host_tests/m1_engine_test/m1_render build/PicoFaceM1_rom fxcheck
```

`compare_reference.py` compares the engine against the Python reference of
the analysis; it needs that reference and its ROM set outside this
repository. Like the JV and D5 renders, none of this is in the CI: it needs
the ROMs.

## Licence

The code is GPL-3.0-or-later like the rest of the instruments; the converter
is MIT like the other tools. Korg M1 is a trademark of Korg Inc.; this project
is not affiliated with Korg.
