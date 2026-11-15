<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Michi71
-->

# PicoFaceMT — tape replay keyboard

A Mellotron-style instrument: every key plays a tape recording, three tracks
(A/B/C) lie side by side on the tape, and a selector picks one or rests
between two. All tapes run off one capstan, so wow and flutter move every
note together. The output is mono, as on the instrument.

```
voices (looped tapes, one capstan speed) -> track selector gains
   -> high pass -> tape saturation -> tone low pass -> volume
```

**Status:** the first bank ran on hardware with a CPU peak below 5 %. The
current bank with long loops builds and passes the host tests; it has not
been heard on hardware yet.

| | |
|---|---|
| Keys | 35, MIDI 43–77 (G2–F5) |
| Polyphony | 16 voices; a selector position between two tracks uses two per key |
| Sample rate | 44.1 kHz, recordings at 22.05 kHz |
| Flash / RAM | 7,885,404 / 16,152 bytes, of which 7.78 MB is the sample bank; needs the 16 MB flash |
| USB PID | 0x105D |

## Tapes

Ten instruments, each cut down to twelve looped recordings (one per three
keys, about 1.5 s: attack plus a loop of about a second with a 250 ms
crossfade): Gc3 Brass, M300A Violins, M300B Violin, M400
Cello, M400 Cmb Choir, M400 String Section, M400 Woodwind2, MKII 3 Violins,
MKII Brass, MKII Flute. Any of them can sit on any track; the default frame is
flute, three violins, cello.

The recordings are by Taijiguy, sampled from a 1973 Mellotron M400S (serial
#500), and are used under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
They were changed for this instrument: each recording is cut to its attack
plus a crossfaded loop, and one recording is kept per three keys. The
PCM files in `src/data/` carry that licence; the rest of the instrument is
GPL-3.0-or-later like the repository.

The bank in `src/data/` is generated — see
[tools/mt_sampleprep](../../tools/mt_sampleprep/README.md). A first bank with
loops of a few periods fitted into 3 MB and a 4 MB flash, but on hardware the
loop seam was plainly audible: it came round 7 to 38 times a second. The
loops now repeat about once a second. `compress_sf2.py --mode short` still
builds the small bank.

## Panel

SEL turns the pages, its button returns to the first; the two parameter
encoders edit the two values of the page.

| Page | Left | Right |
|---|---|---|
| Mellotron | Track (A, A+B, B, B+C, C) | Volume |
| Tapes A B | tape on track A | tape on track B |
| Tape C | tape on track C | Click: level of the key click at every key-down, 0 = none (default 40) |
| Tape | Limit: note runs out after 8 s, as a real tape does (off by default) | Rewind: time a fully played tape takes to spring back, Off to 2 s (default 600 ms) |
| Tone | Tone (treble cut) | Drive (tape saturation) |
| Motor | Wow | Flutter |
| Keys | Attack | Release |
| Feel | Drag (pitch dip while a tape comes up to speed) | Velocity sensitivity |
| Tune | Pitch, ±200 cents | Bend range, 1–12 semitones |
| Diagnostics | CPU peak, underruns, dropped events, voices | |

A key has one tape. While the key is down the tape runs; when it is let go a
spring pulls it back, and that takes time in proportion to how far it ran. A
key struck again before its tape is home plays from wherever the tape is: the
attack is missing, and with Limit on there is that much less tape left. With
Rewind off every note starts at the beginning. The rate is this project's
choice, not a measurement of the instrument.

Every key-down also plays the key click, a 12 ms recording of the mechanism,
untransposed and outside the track selector - also when a tape with Limit on
has run out and the key itself is all there is to hear.

The panel shows the tapes by a short name of at most eight characters - the
value cell is 50 pixels wide - so "M400 String Section" reads "Strings" and
"MKII 3 Violins" "3Violins".

A new tape or track position applies to notes struck after the change; a held
note keeps the track it started on, fading if the selector leaves it.

Everything on the panel is stored (settings record version 3, 19 bytes) once
no note is sounding.

## MIDI

Omni: the channel is ignored.

| Message | Effect |
|---|---|
| Note on/off | 43–77 sound; velocity only matters if Velocity is turned up |
| Pitch bend | ± bend range |
| CC 3 | Track selector |
| CC 7 | Volume |
| CC 12 / 13 | Wow / Flutter |
| CC 71 | Drive |
| CC 72 / 73 | Release / Attack |
| CC 74 | Tone |
| CC 64 | Sustain pedal |
| CC 120 / 123 | All notes off |
| Program change 0–9 | tape on track A |

## Source

| Path | What |
|---|---|
| `src/MT_Instrument.cpp` | adapter: MIDI, panel, settings, event ring into the audio block |
| `src/mello/engine.cpp`, `include/mello/` | the engine; portable C++, also built by `tools/host_tests/mt` |
| `src/data/` | generated sample bank |

The engine's constants — wow and flutter rates and depths, filter corners,
saturation curve, envelope ranges — are this project's own choices, set by
ear. Nothing in it comes from another product's code or data.

## To test on hardware

- Sound on all three tracks and between them; no clicks when the selector moves.
- CPU peak on the diagnostics page with 16 voices (eight keys at Track A+B).
- Underruns stay at zero while turning encoders.
- Settings survive a power cycle.
- USB and DIN MIDI: notes, pitch bend, the controllers above.
