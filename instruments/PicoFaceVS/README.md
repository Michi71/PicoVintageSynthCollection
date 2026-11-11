# PicoFaceVS

A Sequential Prophet VS for the RP2350: four wavetable oscillators per voice,
blended by a joystick position that a five-point envelope moves through the
vector plane, into a low-pass filter and a stereo chorus. Eight voices, two
layers (split and double), arpeggiator.

The engine was written for this project from the original's firmware (OS r1.2),
its service manual and the CEM3379 data sheet; the firmware of the Behringer
Pro VS Mini served as a second reference for a few additions. It first ran as
a host program and is unchanged here.

**Status: first integration.** It has run on the board at 32 kHz, where the
load peaked at 53 %. It now runs at 44.1 kHz, which should peak near 73 %; that
rate has not been heard on the board yet. The panel shows the load.

## It builds from the repository alone

Nothing of the original is shipped, and nothing of yours is needed:

| Part | From the repository | With your own files in `roms/` |
|---|---|---|
| Waves 32-125 | recomputed from a harmonic table, `tools/vs_data/vs_wave_harmonics.txt` | exact, from a Prophet VS OS r1.2 ROM image |
| Envelope, release, LFO, velocity, glide tables | recomputed from their generating rules | exact, from the same ROM |
| Init program | one of its own, "INIT" | the firmware's "BASIC" |
| Waves 0-31 (user RAM on the original) | empty | the Pro VS Mini's, from its firmware |
| Pro VS Mini ADSR times, chord sets | absent | from the Pro VS Mini firmware |
| Programs | the init program only | every `.syx` in `roms/`, embedded in the image |

The harmonic table describes each wave as a sum of harmonics, magnitudes on a
0.5 dB grid and phases on a 90 degree grid. It is a smoothed description of
the timbres, not a copy of the sample values, but it is derived from the
original waves. Against a build from the ROM, none of 138 test sounds differed
audibly in the A/B measurement (at most 0.46 dB in level, 1.32 dB in timbre).

`roms/` is gitignored. What is there is used, what is not is left out:

- **Prophet VS ROM**: one 64 KiB image or the two 32 KiB EPROM images. The
  file names do not matter; the image is recognized by its contents.
- **Behringer Pro VS Mini firmware**: the QSPI image (waves 0-31, ADSR times)
  and the program image from firmware 2.0 on (chord sets). Images as the
  update tool stores them are encrypted; this repository contains nothing for
  that. A module `pvm_decrypt.py` of your own with a function
  `decrypt(bytes) -> bytes`, placed in `roms/`, is used if present.
- **SysEx banks**: a Prophet VS bank dump (`F0 01 0A 64`, 100 programs), single
  program dumps, a Pro VS Mini patch bank (`F0 00 20 32 ...`, 32 patches; these
  need the Mini's ADSR table).

The generators need Python 3 with numpy. Without it the instrument is skipped
at configure time with a note.

## Panel

| Control | Does |
|---|---|
| Sel | steps through the programs; press to switch between the VS bank and the Mini bank |
| A | effect: as programmed, off, chorus, ensemble, VS chorus |
| B | volume; press to reset the peak load |

The line under the name shows the effect, the sounding voices and the CPU load
(current / peak, in percent of the block budget).

## MIDI

| Message | Does |
|---|---|
| Note on/off, velocity | play |
| Pitch bend | +/- 2 semitones |
| Channel pressure | pressure, routed as the program says |
| CC 1 | mod wheel |
| CC 7 | volume |
| CC 32, program change | bank select LSB 0 = VS programs, 1 = Mini patches |
| CC 64 | hold |
| CC 66 | alternate release (the VS's footswitch) |
| CC 120, 123 | all notes off |
| SysEx | the bank formats above; a received bank lives in RAM until power-off |

## Not there yet

- Editing a program on the panel, and storing programs in flash.
- The arpeggiator and the chord mode are in the engine but have no control.
- Sending dumps.
- A host test of this instrument in `tools/host_tests/`; the engine's own
  tests live with the engine's source project.
- The engine sources and the generators still carry their German comments.
