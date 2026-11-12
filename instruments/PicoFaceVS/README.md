# PicoFaceVS

A Sequential Prophet VS for the RP2350: four wavetable oscillators per voice,
blended by a joystick position that a five-point envelope moves through the
vector plane, into a low-pass filter and a stereo chorus. Eight voices, two
layers (split and double), arpeggiator.

The engine was written for this project from the original's firmware (OS r1.2),
its service manual and the CEM3379 data sheet; the firmware of the Behringer
Pro VS Mini served as a second reference for a few additions. It first ran as
a host program and is unchanged here.

**Status:** on the board it runs at 44.1 kHz with a peak load of 67 % at
eight voices. The joystick and MIDI pages, the arpeggiator's Layer and Dual,
Edit Waveform mode, the wave store and the answers to dump requests are newer
than that test; the host test (`tools/host_tests/vs/`) covers them as far as a
host can.

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
| JOYSTICK | left / right, between A and C | up / down, between B and D; a press on either centers it |
| ARP | arpeggiator off / on / latch | Layer off / on (a parameter of the program) |
| ARP / CHORD | clocks per arpeggiator step (1-24) | chord set 1-8 (needs the Pro VS Mini data) |
| MIDI | channel: Omni or 1-16 | arpeggiator clock: internal or MIDI clock |
| DIAG | - | press: reset the peak load |

A `*` after the program number means the edit buffer differs from what is
stored, `CMP` that Compare is on. DIAG shows the sounding voices, the CPU load
(current and peak, in percent of the block budget) and whether the memory has
been written to flash.

**The joystick** is the VS's Waveform Mix joystick on two encoders. The page
draws the vector plane as it is printed on the VS - A left, B top, C right, D
bottom - with the position and the share of each oscillator in percent. It
offsets the mix envelope of whatever program plays, and it is a panel
setting, not part of the program.

**The arpeggiator** is the VS's, with its two sequences:

- *Latch* freezes what is being arpeggiated; keys played after that sound
  normally over it. On the VS one holds a chord and presses Latch - here,
  hold it and turn A to Latch. Set to Latch with no key held, it waits for
  the next chord instead and takes it once all its keys are released.
- *Layer* on, with a latched sequence: keys held now are arpeggiated as a
  second sequence beside the first (overdub), for as long as they are held -
  or kept, in the Auto-Latch and Extend modes.
- In a split program the arpeggiator's Split parameter (group ARP of the
  edit pages) picks the side: Off = the whole keyboard, Right, Left, or
  *Dual* - both sides, as two sequences that step together.
- With Rests on, the top C (note 96) is a rest, and with an arpeggiator split
  the lowest C (note 36) too.
- In Mono voicing each sequence has one voice of its own (voices 1 and 5);
  keys played beside it use the others.

**A long press on SEL opens the menu:** Edit Program, Write Program, Compare,
Edit Waveform, Send SysEx, Factory Reset.

- **Edit Program**: two values per screen on A and B, SEL turns to the next
  pair. A short press on SEL lists the groups - OSC, FILTER, FILTER ENV,
  AMP ENV, MIX ENV, LFO 1, LFO 2, PRESSURE, VELOCITY, KEYBOARD, FILT ENV MOD,
  MOD WHEEL, CHORUS, VOICE, PAN, ARP, NAME - and jumps; a long press leaves.
  Edits are heard at once and live in the edit buffer until **Write Program**
  stores them (A picks the number, SEL confirms) or another program is
  selected. **Compare** plays the stored version.
- The Pro VS Mini's patches are played, not edited.
- **Send SysEx**: the program in the edit buffer, the whole VS bank, or all
  user waves, on USB and DIN.
- **Factory Reset** brings back the banks that were embedded at build time
  (the init program if there were none) and empties the user waves.

### Edit Waveform

The VS's own way of making a wave (owner's manual, section 13): up to four
waves, each at a harmonic of its own, mixed by a joystick position, stored as
one of the user waves 0-31. The menu entry leaves the program and plays an
organ whose four oscillators are that mix, so what is heard is what Store
writes.

| Page | A | B |
|---|---|---|
| WAVE A ... WAVE D | wave 0-126 | harmonic 1-32: the wave runs at that multiple of the pitch |
| WAVE MIX | left / right | up / down; a press centers it |
| STORE WAVE | destination 0-31; **SEL stores** | how many programs of the bank use that wave |

A long press on SEL goes back to the program. A stored wave can itself be a
component of the next one. The mix is computed with the firmware's own
arithmetic; where it would exceed the twelve bits of a user wave - the
reconstructed waves overshoot them slightly, the Pro VS Mini's are hotter -
it is scaled down as a whole instead of clipped.

A user wave slot that has never been written plays what the build put there:
the Pro VS Mini's wave of that number, or silence.

## Memory

What the VS kept in its battery-backed RAM lives in RAM here and is mirrored
to 20 KB of flash below the settings store (and below PicoFaceJ6's patch
sector and PicoFaceM1's memory, so switching instruments leaves all of them
alone): the VS bank of 100 programs, and the 32 user waves. It is written a
moment after a Write, a stored wave, a received dump or a factory reset, once
no voice is sounding. From the first such write on, the stored bank takes
precedence over the banks embedded in the image; Factory Reset goes back to
those. The Pro VS Mini patch bank is not stored: a received one lives until
power-off.

## MIDI

| Message | Does |
|---|---|
| Note on/off, velocity | play |
| Pitch bend | +/- 2 semitones |
| Channel pressure | pressure, routed as the program says |
| CC 1, CC 4 | mod wheel (the VS takes the foot pedal as the wheel) |
| CC 2 | pressure (the VS takes breath as pressure) |
| CC 7 | volume |
| CC 16, CC 17 | joystick x and y, 64 = center (their LSBs, CC 48 and 49, are ignored) |
| CC 32, program change | bank select LSB 0 = VS programs, 1 = Mini patches |
| CC 64, CC 69 | alternate release (rate 4A instead of 4), as on the VS's footswitch |
| CC 66 | hold (not on the VS) |
| CC 120, 123 | all notes off |
| MIDI clock | steps the arpeggiator when its clock is set to MIDI |

The channel is set on the MIDI page: one of 1-16, or Omni. SysEx is not tied
to a channel.

| SysEx | Direction | |
|---|---|---|
| `F0 01 0A nn` + 164 nibbles + `F7` | in, out | one program, nn = 0-99; in: stored at nn |
| `F0 01 0A 64` + 100 x 164 nibbles + `F7` | in, out | all programs |
| `F0 01 0A 7F` + 32 x 384 nibbles + `F7` | in, out | all user waves, as the VS's 192-byte records |
| `F0 01 00 nn F7` | in | dump request: program nn, all programs (nn = 64h) or all waves (7Fh) |
| `F0 7E cc 01 ...`, `F0 7E cc 02 ...` | in, out | one user wave in the sample dump standard, 128 words of 12 bits |
| `F0 7E cc 03 ss 00 F7` | in | sample dump request for user wave ss |
| `F0 00 20 32 ...` | in | Pro VS Mini patch bank (RAM only) |

Dumps go out from the menu or on request, on USB and DIN.

The sample dump standard is done the way the VS does it (firmware @0x4C80,
@0x5878). The twelve bits of a sample travel as they are in the wave memory,
which is two's complement, because that is what the firmware's code does: a
file made for a Prophet VS fits, while a generic sample dump, whose data the
standard defines as unsigned, comes out with its top bit inverted. Coming in, the first 128 words are the wave and the
rest is cancelled; other word sizes are accepted and cut to their upper
twelve bits. Going out it is the VS's header and three packets of 60 words.
Two things are done by the standard rather than by the VS: packets carry a
real checksum (the VS sends 00, which is accepted here too), and a received
header and its packets are acknowledged, so a sender does not have to wait
out its timeouts.

## Not there

- Parameter changes over MIDI (the VS's parameter select and data entry
  controllers), MIDI mode 4, separate channels for the two halves of a
  split, overflow, the arpeggiator channel.
- The VS answers a sample dump request for a ROM wave too; here only the
  user waves go out.
- Transposing a latched arpeggio from the keyboard, and the ROM and RAM
  cartridges.
