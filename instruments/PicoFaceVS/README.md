# PicoFaceVS

A Sequential Prophet VS for the RP2350: four wavetable oscillators per voice,
blended by a joystick position that a five-point envelope moves through the
vector plane, into a low-pass filter and a stereo chorus. Eight voices, two
layers (split and double), arpeggiator.

The engine was written for this project from the original's firmware (OS r1.2),
its service manual and the CEM3379 data sheet; the firmware of the Behringer
Pro VS Mini served as a second reference for a few additions. It first ran as
a host program and is unchanged here.

**Status:** it has run on the board at 32 kHz, where the load peaked at 53 %.
It now runs at 44.1 kHz, which should peak near 73 %. That rate, the edit
pages, the program store in flash, the arpeggiator and chord switches and the
dump transmission have not been tested on the board yet; the host test
(`tools/host_tests/vs/`) covers them as far as a host can.

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

Controls as on PicoFaceM1: **SEL turns the pages, A and B set the two values
shown.**

| Page | A | B |
|---|---|---|
| PROGRAM | program | bank: VS programs or Pro VS Mini patches |
| LEVEL | volume | effect: as programmed, off, chorus, ensemble, VS chorus |
| ARP | arpeggiator off / on / latch | its clock: internal or MIDI clock |
| ARP / CHORD | clocks per arpeggiator step (1-24) | chord set 1-8 (needs the Pro VS Mini data) |
| DIAG | - | press: reset the peak load |

A `*` after the program number means the edit buffer differs from what is
stored, `CMP` that Compare is on. DIAG shows the sounding voices, the CPU load
(current and peak, in percent of the block budget) and whether the bank has
been written to flash.

**A long press on SEL opens the menu:** Edit Program, Write Program, Compare,
Send SysEx, Factory Reset.

- **Edit Program**: two values per screen on A and B, SEL turns to the next
  pair. A short press on SEL lists the groups - OSC, FILTER, FILTER ENV,
  AMP ENV, MIX ENV, LFO 1, LFO 2, PRESSURE, VELOCITY, KEYBOARD, FILT ENV MOD,
  MOD WHEEL, CHORUS, VOICE, PAN, ARP, NAME - and jumps; a long press leaves.
  Edits are heard at once and live in the edit buffer until **Write Program**
  stores them (A picks the number, SEL confirms) or another program is
  selected. **Compare** plays the stored version. The arpeggiator's rate,
  mode, scan, octaves and repeats are program parameters (group ARP), as on
  the VS.
- The Pro VS Mini's patches are played, not edited.
- **Send SysEx**: the program in the edit buffer, or the whole VS bank, on USB
  and DIN.
- **Factory Reset** brings back the banks that were embedded at build time
  (the init program if there were none).

## Program memory

The VS bank - 100 programs - lives in RAM and is mirrored to 12 KB of flash
below the settings store (and below PicoFaceJ6's patch sector and PicoFaceM1's
memory, so switching instruments leaves all of them alone). It is written a
moment after a Write, a received dump or a factory reset, once no voice is
sounding. From the first such write on, the stored bank takes precedence over
the banks embedded in the image; Factory Reset goes back to those. The Pro VS
Mini patch bank is not stored: a received one lives until power-off.

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
| MIDI clock | steps the arpeggiator when its clock is set to MIDI |
| SysEx in | a VS bank dump or single program dump (stored in flash), a Pro VS Mini patch bank (RAM) |
| SysEx out | from the menu: the program in the edit buffer (`F0 01 0A nn`), the VS bank (`F0 01 0A 64`) |

All channels are received. Dump requests are not answered; dumps are sent from
the menu.

## Not there

- The waves 0-31 are the Pro VS Mini's, or empty: the VS's user waves cannot
  be edited or loaded.
- Of the arpeggiator: Layer (overdub) and Split "Dual". The joystick has no
  control; the mix envelope moves the vector position.
- No MIDI channel setting and no parameter changes over MIDI.
