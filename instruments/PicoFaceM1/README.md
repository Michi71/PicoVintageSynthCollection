# PicoFaceM1

A Korg **M1** for the collection: the plain M1, without the Plus+1
expansion. It builds a firmware image, but only on a machine that has an M1
ROM set. The ROMs are not distributable, so without them the instrument
removes itself from the build and the other instruments are unaffected.

Runs on the hardware (confirmed 2026-11-05): sound, effects, edit pages, the
user memory and SysEx bank loads from presetpatch.com, at about 32 % peak CPU
load with 16 voices.

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
| Scale types: Equal Temp 1/2, Pure Major/Minor with key, User | works (firmware tables and the OS's random detune) |
| User memory (100 programs, 100 combinations, 4 drum kits, global) | in RAM, mirrored to flash; starts as the factory preload |
| SysEx: dumps, requests, write, parameter change, device inquiry | works; pinned by a host test with synthetic dumps and the real preload, see below |
| Edit Program / Combination / Global / Drum kits, Write, Compare | works |
| Out 3/4 as separate outputs | no: the PicoFace has one stereo pair, which is M1 outputs 1/2 |

## Playing it

- **PROG/COMBI page**: encoder A picks the program or combination (0–99),
  encoder B switches between Program and Combination mode. A `*` after the
  number means the edit buffer differs from what is stored, `CMP` that
  Compare is on.
- **Long press on SEL** opens the menu: Edit Program / Edit Combi, Write,
  Compare, Global, Drum Kits, Send SysEx, memory protect, factory reset.
- **Editing** follows the M1's own edit pages (manual TABLE 5/6): two values
  per screen on encoders A and B, SEL turns to the next pair. A short press on
  SEL lists the pages (OSC, Pitch EG, VDF, VDF EG, EG time switches, VDA, MGs,
  After Touch, Joystick, Effects, the two effects' own parameters, the eight
  timbres of a combination ...) and jumps; a long press leaves. Edits are heard
  at once and live in the edit buffer until **Write** stores them (encoder A
  picks the number, SEL confirms) or another program is selected. **Compare**
  plays the stored version.
- **Global**: master tune, key transpose, the pedal settings and the scale:
  Equal Temp 1, Equal Temp 2 (a slight random detune per note, as on the M1),
  Pure Major / Pure Minor on a chosen key, or a user scale in cents.
- **Drum Kits**: kit and instrument on the first page, then sound, key, pan,
  tune (10-cent steps), level and decay.

## Memory and SysEx

The M1's internal memory -- 100 programs, 100 combinations, the four drum kits
and the global data, 27.5 KB -- is kept in RAM and mirrored to 32 KB of flash
below the settings store (and below PicoFaceJ6's patch sector, so switching
instruments loses neither). The mirror is written about 1.5 s after the last
change, once no voice is sounding: an erase stops the CPU for a moment. The
first start, or a factory reset, loads Korg's preload.

SysEx follows the owner's manual (MIDI implementation, sections 2-5 and 3),
on USB and DIN, on the MIDI channel set on the SYS page (Omni = any):

| Received | Function |
|---|---|
| 40 / 49 | program / combination parameter dump into the edit buffer |
| 4C / 4D | all programs / all combinations (internal memory, 100/100 layout) |
| 51 | global data and drum kits |
| 50 | ALL DATA; the sequencer part is skipped |
| 10, 19, 1C, 1D, 0E, 0F | dump requests, answered with the dumps above |
| 11 / 1A | write the edit buffer to a program / combination |
| 12, 4E | mode request / mode change |
| 41 | parameter change (program pages; combination: Multi and effect pages) |
| F0 7E .. 06 01 | device inquiry |

Each received message is answered as the M1 does: data load completed / error,
write completed / error, MIDI-in data format error for a wrong length.
Memory protect refuses bank loads. Bank files from sites like presetpatch.com
are ALL PROGRAM dumps (16 350 bytes) and load directly. The ALL DATA the
PicoFace sends has no sequencer part, which a real M1 would reject; send the
parts separately for one.
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
- Everything runs on core 0, at about 32 % peak load with 16 voices sounding.

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
