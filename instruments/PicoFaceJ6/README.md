# PicoFaceJ6 — Roland Juno-60

An emulation of the Roland Juno-60, one of the nine instruments in
[PicoVintageSynthCollection](../../README.md).

**Status:** running firmware for the RP2350, plus a macOS host test that
compiles the identical engine sources. Six voices, 48 factory sounds and 56
writable memories, arpeggiator, chorus. Measured on the device, the peak load
stays at or below **30 %**.

The engine follows the Roland Juno-60 service notes and the PG-JU60 manual,
section by section as the panel is laid out. Neither document is shipped here —
they are Roland's, not mine. Every value taken from them is recorded with its
source in `include/juno/juno_defs.h`, so the numbers stand on their own:

```
LFO      rate, delay time
DCO      one per voice: range (16'/8'/4'), sawtooth, pulse with PWM,
         sub-oscillator, noise -- all available at once
HPF      the four-position high-pass, one for all six voices
VCF      IR3109: cutoff, resonance to self-oscillation, contour amount and
         polarity, LFO amount, keyboard follow
VCA      contour or gate
ENV      attack, decay, sustain, release (IR3201)
CHORUS   off / I / II / I+II -- two MN3009 bucket-brigade lines
ARP      up / up&down / down, one to three octaves, 1.5 .. 50 Hz
```

Two details of the signal chain are easy to get wrong, and the block diagram on
page 3 settles both: **the high-pass filter sits after the voices are summed**,
not in each voice, and **the patch's VCA level sits before the chorus**, so a
loud patch drives the bucket-brigade lines harder and distorts them more.

## Where the code comes from

Three sources, and where they disagree the order of precedence is recorded at
the value in `include/juno/juno_defs.h`:

| Source | What it settles |
|---|---|
| Service notes | IC types and component values. Nothing overrides them. |
| PG-JU60 manual | What the controls are called and what they are for. |
| [Juno60](https://github.com/pendragon-andyh/Juno60) (MIT) | Measurements off a real instrument — envelope times and slopes, chorus rates and delay ranges, HPF corners. Where a measurement contradicts the specification, the measurement wins and both numbers are recorded. |
| [junox](https://github.com/dzannotti/junox) (GPL v3) | Structure, parameter scaling, and the 48 patches. |

| File | Original circuit | Contents |
|---|---|---|
| `juno_dco.h` | the DCO | sawtooth, pulse, sub and noise from one phase, polyBLEP on every edge |
| `juno_filter.h` | IR3109 and the switched HPF | four OTA sections in a feedback loop; the high-pass as four fixed positions |
| `juno_env.h` | IR3201, and the LFO | fixed-duration segments with measured curves; LFO with delay and fade-in |
| `juno_arp.h` | the arpeggio clock | pattern, range, gate per step |
| `juno_fx.{h,cpp}` | the chorus board | two bucket-brigade lines, one triangle LFO, right channel inverted |
| `juno_dsp.h` | — | one-pole, biquad, DC block, saturators, noise, fast `2^x` |
| `juno_voice.h` | a voice card | DCO into filter into amplifier, with the contour on both |
| `juno_params.{h,cpp}` | the panel itself | one row per control: name, type, MIDI CC, panel scale |
| `juno_presets.{h,cpp}` | — | 48 patches |
| `juno.{h,cpp}` | — | voice assignment, the shared sections, MIDI, output stage |

### Where this departs from junox

junox supplied the structure and the patches, and two of its choices are not
followed:

- **Its DCO quantises the period to whole audio samples** (`round(sampleRate /
  frequency)`). That looks like the instrument's digital clock but is not one:
  the real counter runs in the megahertz, so its steps sit far below one audio
  sample. Reproducing it at 44.1 kHz would put the top octave tens of cents
  out — a note at 4 kHz lands on a period of 11 samples, which is 4009 Hz. A
  float accumulator is used instead, which is both correct and cheaper. The
  stability that makes a DCO a DCO comes for free.
- **Its filter is a diode ladder.** The service notes name the part: six
  IR3109s, IC2, 5, 8, 11, 14 and 17, with a BA662 setting the feedback around
  each. That is four OTA integrator sections in series — the same class of
  circuit as a transistor ladder, and about half the cost of the zero-delay
  diode ladder junox uses. That junox offers a Moog filter as an alternative in
  the same patch format suggests its authors were not certain either.

The audible difference between an IR3109 and a Moog ladder is the bass: a
transistor ladder takes its feedback from inside the ladder, so the low end
drains away as the resonance comes up, and an OTA cascade with a separate
feedback amplifier does not. That is the `gComp` term, set to 0.85 here against
0.5 for the Model D. A Juno with the resonance up is never thin.

## Polyphony

**Six voices, fixed.** The schematic shows six IR3109 filters and six BA662
amplifiers, and what happens on the seventh note is part of how the instrument
plays.

Free voices are handed out in round-robin order rather than always from the
top, which is what gives a Juno its characteristic cycling: hold a chord,
release it, play it again and a different set of cards responds. When none is
free the quietest voice that is no longer held goes first, and only if every
voice is still held does the oldest get taken.

The display footer shows `A` — how many cards are sounding. On an instrument
with a hard limit of six, seeing the stealing happen beats wondering about it.
The self test holds eight notes and checks that six sound and no seventh
appears.

## Patch parameters and instrument settings

The parameter list is in two halves, and the split is how the instrument works
rather than a convenience.

A Juno-60 stores the **sound** in its 56 memories. The arpeggiator switches and
the output volume are live panel controls read from the CPU's switch matrix,
and no patch change touches them. So:

| | |
|---|---|
| **29 patch parameters** | written in full whenever a patch is selected |
| **6 instrument settings** | arpeggiator on, mode, range, rate, HOLD, master volume — no patch change touches them |

Which is also the only sensible behaviour: a patch change in the middle of a
performance must not stop the arpeggio or move the volume.

Both halves are stored in the settings record, so both survive power-off, and
both appear in the menu, the CC map and the display through the same table.

## Menu

A two-level menu whose sections are the ones printed on the instrument:

| Section | Pages |
|---|---|
| `PATCH` | the patch list — 48 factory sounds and 56 memories, as three rows with a cursor — plus pages for naming a sound and writing it to a memory |
| `LFO` | rate, delay |
| `DCO` | range and LFO, sawtooth and pulse, PWM and its mode, sub, noise |
| `HPF` | the four positions |
| `VCF` | cutoff and resonance, contour amount and polarity, LFO and key follow |
| `VCA` | level, contour or gate |
| `ENV` | attack/decay, sustain/release |
| `CHORUS` | off / I / II / I+II |
| `ARP` | on and HOLD, mode and range, rate |
| `OUTPUT` | master volume |
| `SYSTEM` | receive channel and transposition, tune and bend range, LFO trigger |

Three encoders: the first picks the section and then the page within it, its
button steps in and out, and the other two edit the page's two parameters. The
push button of the third carries out the action on the `PATCH WRITE` page, and
does nothing anywhere else.

A `*` in front of the title means the sound no longer matches the patch it was
loaded from — `*VCF`, `*PATCH`. It is there because the patch list replaces the
sound on every detent: without it there is no sign that a turn of the encoder is
about to throw work away. It clears when the sound is written, when another
patch is loaded, and when the change is simply turned back.

What counts as a change is decided at the resolution a patch is stored at, per
mille, so the marker means exactly "writing this would give something other than
what is stored". The arpeggiator and the master volume are instrument settings
rather than part of a sound, so they never light it.

Values are printed the way the panel is marked — sliders 0.0 … 10.0, the tune
control in cents, rotary switches by the name of their position (`8'`, `1 Oct`,
`Up/Dn`, `I+II`).

## The arpeggiator

Specifications page: ARPEGGIO RATE 1.5 … 50 Hz. Panel Board A carries the
on/off switch, a MODE switch and a RANGE switch; the rear panel has an ARPEGGIO
CLOCK input, which there is no socket for here.

It sits between the keyboard and the voice allocator: keys join a pattern, and
the arpeggio clock decides when they sound. Measured with C–E–G held:

```
Up    3 Oct: 48 52 55 60 64 67 72 76 79  48 52 …
Up/Dn 3 Oct: 48 52 55 60 64 67 72 76 79 76 72 67 64 60 55 52  48 …
Down  3 Oct: 79 76 72 67 64 60 55 52 48  79 …
```

Up-and-down turns around without sounding either end twice. Each step is a
**gate**, not a held note — the gate closes at 55 % of the step so the next one
retriggers the contours. With the gate held open across steps it would sound
like one note sliding around instead of a sequence.

**HOLD latches the keys, not the sound.** Let go of the chord with HOLD on and
the arpeggio keeps running; play another key and it joins the figure, which is
how the instrument is actually used.

## Patches

A Juno-60 stores 56 of its own, eight per bank across seven banks, and the
factory set is not in the service notes. The 48 here come from the patch table
of junox, which uses the same parameters and supplied the names.

Three values depart from that set, each because the sound and the name
disagreed:

| Patch | | |
|---|---|---|
| Piano I | DCO LFO 0.4 → 0 | forty cents of vibrato at five hertz, on a piano |
| Clavichord I | DCO LFO 0.4 → 0 | the same, and the same reason |
| Brass | VCA level 0.7 → 1.0 | the timbre was right and the level sat six decibels under everything else |

The other fourteen patches that use the DCO LFO keep it: ten to twenty cents on
a violin, a clarinet or an oboe is what those instruments do.

Measured across all 48, holding a four-note chord: rms −39.8 to −14.6 dBFS with
a median of −23.0, peak 0.74. Nothing clips, nothing is silent, and every one is
part of the self test.

### The 56 user memories

The instrument's own count, eight per bank across seven banks. They sit after
the factory sounds, so the patch list is a fixed 104 entries and a Program
Change always means the same thing — with the list shortened to the occupied
memories the numbering would shift under a sequencer every time one was
written. 104 fits inside Program Change's 128.

**The destination is chosen here, not in the patch list.** That matters more
than it looks: the list loads whatever the cursor passes over, one sound per
detent, so scrolling down to a memory to "pick where to save" replaces the sound
that was about to be saved — arrive at `U01` from patch 40 and what gets stored
is patch 48, the last one scrolled through. Encoder 2 on this page moves the
destination without touching the sound at all. To make the detour pointless, the
destination starts on the first free memory at power-on, so there is nothing to
go hunting for.

Writing is on a page of its own, `PATCH WRITE`:

| | |
|---|---|
| encoder 2 | the destination memory, with whatever it currently holds |
| encoder 3 | the name about to be written, or `Erase >` |
| button 3 | carries it out |

The two lines read together:

```
U01 Init
Test >
```

— into `U01`, which currently holds `Init`, goes `Test`. The upper line is what
gets overwritten, the lower one is what replaces it, so scrolling encoder 2 for a
`-free-` memory still works while a rename is visible before it is committed.
Showing only the destination's own name meant a rename could not be confirmed
anywhere before writing: the one place a name appeared still said the old one,
which reads as the rename not having taken.

It is deliberately not reachable anywhere else. Either action erases a flash
sector, which stops the audio for a moment, so it should not be possible to
trigger by accident — the controller checks which page is open and ignores the
button everywhere else. After the button, the lower line shows `written`, `freed`
or `FAILED`, and any further input clears it — including input on another page,
or naming a sound would come back to a stale `written` covering the name it was
about to store.

| | |
|---|---|
| Storage | one 4 kB flash sector, immediately below the two the veeprom uses |
| Record | 72 bytes: a marker, a twelve-character name and the 29 patch parameters |
| Total | 4032 of 4096 bytes |

Twelve characters and not sixteen because at sixteen the 56 records would not
fit in one sector, and a write would have to erase two — doubling the time the
audio is stopped.

**Only the patch half is stored.** The arpeggiator and the master volume are
instrument settings, and putting them in a sound would mean recalling one could
silence the instrument or stop the arpeggio. They live in the veeprom record
instead.

### Naming

A page of its own, `PATCH NAME`, editing the name one character at a time:

| | |
|---|---|
| encoder 2 | the position, shown as `Stri[n]gs I` |
| encoder 3 | the character at that position |

Eleven characters — the twelve-byte field less its terminator. The name gets the
whole display line, which is why it carries no label: eleven characters plus the
cursor brackets need thirteen of the fifteen a line holds.

The character set steps space, `A-Z`, `a-z`, `0-9`, then `. - + ' / & #`. The 48
factory names use nothing but letters and spaces; the rest is for names of your
own. Trailing spaces are dropped, interior and leading ones kept — `Space Sound`
needs them. Blanking every character is allowed, but a memory saved with no name
left is stored as `Init` rather than as a blank row in the list.

Editing the name changes the name of the *loaded sound*, so it is what the next
write stores, and loading another patch replaces it. This is also the way to
repair a truncated one: 17 of the 48 factory names are longer than eleven
characters, so a memory written from `Harpsichord I` arrives as `Harpsichord`.

Without editing, a memory takes the name of the sound that was loaded, so `U03
Strings I` says what it grew out of.

The name comes from the *loaded sound*, not from the patch number the cursor is
on. Those part company as soon as you browse the list looking for a free memory:
moving onto a free one deliberately loads nothing, so the sound keeps playing
while the number has already moved. Reading the name from the number meant
asking an empty memory what it was called, and everything saved that way came
out named `Init`. A sound whose origin genuinely cannot be traced — restored
from the settings while sitting on a free memory — is still called `Init`, which
is the honest answer rather than an invented one.

A free memory is shown as `U07 -free-` and selecting one leaves the sound alone:
an empty memory has nothing to recall. It can still be selected, because that is
how you reach it in order to write to it.

**Freeing a memory again** is what the `Erase` action is for. The instrument has
no equivalent — all 56 of its memories always hold something and you overwrite —
but once a free state exists in the display it has to be reachable, or `-free-`
would describe something that can never be free again. Freeing does not touch
the sound that is playing, even when the memory being freed is the one it came
from: the sound is still there, its stored copy is not. Freeing an
already-free memory does nothing at all, and in particular does not spend a
sector erase and a break in the audio clearing a marker that is already clear.

## MIDI

Program Change selects a patch. Velocity is ignored — the keyboard of a Juno-60
produces a gate, not a velocity. Notes are accepted across the full MIDI range:
the instrument has 61 keys, but its pitch comes from a control voltage and
nothing in the circuit stops that voltage going past the ends of its own
keyboard.

The Juno lines up unusually well with the MIDI sound controllers, so most of the
filter and envelope fall where a sequencer already looks.

| Section | Control | CC | | Control | CC |
|---|---|---|---|---|---|
| **LFO** | Rate | 3 | | Delay | 9 |
| **DCO** | Range | 21 | | LFO | 22 |
| | PWM | 23 | | PWM mode | 24 |
| | Sawtooth | 25 | | Pulse | 26 |
| | Sub | 27 | | Sub level | 28 |
| | Noise | 29 | | | |
| **HPF** | Position | 30 | | | |
| **VCF** | Cutoff | **74** | | Resonance | **71** |
| | Contour amount | 31 | | Polarity | 102 |
| | LFO | 85 | | Key follow | 86 |
| **VCA** | Level | 106 | | Mode | 103 |
| **ENV** | Attack | **73** | | Decay | **75** |
| | Sustain | **76** | | Release | **72** |
| **Chorus** | Mode | **93** | | | |
| **System** | Tune | 87 | | Bend range | 20 |
| | LFO trigger | 104 | | Transpose | 105 |
| **Arp** | On | 107 | | Mode | 108 |
| | Range | 109 | | Rate | 110 |
| **Output** | HOLD | **69** | | Master | **7** |

`CC 7` drives the master volume and not the patch's VCA level: Channel Volume
belongs to the instrument rather than to a stored sound, and a patch change
must not fight a sequencer for it. `CC 69` is Hold 2, which is what the panel
HOLD switch is.

Channel messages: CC 64 sustain pedal, CC 120 all sound off, CC 123 all notes
off, CC 121 reset all controllers. A parameter arriving over MIDI updates the
display as well as the engine.

Press `m` in the host test to print this map from the table itself.

## Compute budget

Settled by measurement rather than guesswork, and the guesswork was wrong twice
in opposite directions — worth recording, because the method is now calibrated.

Timing this engine on a Mac and scaling against what PicoFaceMD reports on the
device (its dry engine is 0.185 % of one host core and P21 on the RP2350):

| | fixed | per voice | chorus | six voices + chorus |
|---|---|---|---|---|
| no oversampling | P8 | P6.2 | P2 | **P48** |
| 2× oversampled | P10 | P10.0 | P2 | **P72** |

A prototype voice written before the engine predicted P42 and P59, so it was
optimistic by 13 points at 2×. The device then reported **no more than P30**
across all patches played — the P48 figure is six voices held continuously with
chorus, which is not what playing looks like. So: the extrapolation is worth
having, and worth treating as a range rather than a number.

Aliasing, one voice, sawtooth, filter wide open and no resonance — the only
configuration that can be measured honestly, since a resonant peak below the
fundamental is not at a harmonic and swamps the figure:

| 1× | 2× | 4× |
|---|---|---|
| −45.2 dB | **−55.4 dB** | −53.7 dB |

2× buys 10 dB and 4× buys nothing, the same shape of result as the Model D.
**1× is the default**, because it fits one core with room to spare. Going to 2×
means either living at P72 — which leaves only a quarter of the core for USB,
MIDI and pushing the display out — or splitting the voices over both cores the
way PicoFaceRD does. Core 1 is free either way.

## Building the firmware (RP2350)

Built together with the rest of the collection, or on its own:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceJ6
cmake --build build
```

Result: `build/PicoFaceJ6.uf2`. Target platform `rp2350-arm-s`, board
`sparkfun_promicro_rp2350`, **444 MHz**. Scaffolding, hardware layer, audio
subsystem, MIDI and display come from the shared core — including the RP2350
clock and flash-timing work and the reasons for it, which are written up in
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) and in the PicoFaceRD and
PicoFaceMD READMEs.

| | |
|---|---|
| Flash | 104,056 bytes |
| RAM | 19,188 bytes, plus 4 kB of the patch sector mirrored for reading |
| Settings record | 72 of 240 bytes |
| Patch sector | 4032 of 4096 bytes |

### Build options

The standalone repository had three CMake options — `J6_DOUBLE_RESET`,
`J6_OVERSAMPLE` and `J6_SAFE_MODE`. They did not survive the merge into the
collection, which knows only the keywords of `picoface_add_instrument()`.

What is left of them: `JUNO_OVERSAMPLE` still exists as a macro with a default
of 1 in `include/juno/juno_defs.h` and can be set through `DEFINES` in
`instrument.cmake`. The safe mode is gone. Double-tap RESET into BOOTSEL is on for
this instrument: the collection links `pico_bootsel_via_double_reset` unless an
instrument opts out with `NO_DOUBLE_RESET`, which J6 does not — see the
PicoFaceMD README for the brownout case that made MD and SM opt out.

## Host test (macOS)

```bash
tools/host_tests/j6/build_juno.sh
tools/host_tests/j6/juno_test
```

Requires PortMidi (`brew install portmidi`). Opens a virtual MIDI input named
`juno` and plays through CoreAudio; the key bindings are documented at the top
of `tools/host_tests/j6/juno_test.cpp`. This compiles the same `src/juno/`
sources as the firmware, with `-DJUNO_HOST_BUILD` replacing the Pico audio
subsystem.

```bash
tools/host_tests/j6/juno_test --selftest
```

runs without an audio device, a MIDI port or a terminal: all 48 patches are
checked for level, for NaN and for stereo width, the panel is driven to both
extremes to prove nothing can be made to blow up, the six-voice limit is
verified by holding eight notes, and the controller map and the full front panel
are printed. This is the form to reach for when something sounds wrong and the
question is whether the engine or the wiring is at fault.

**Note the flag.** Without it the same binary opens a MIDI port and an audio
device and waits for a keypress — which, piped into something that swallows its
output, looks exactly like a hang.

### Panel and memories

```bash
tools/host_tests/j6/build_ui.sh
tools/host_tests/j6/j6_ui_test
```

No audio, no MIDI, no terminal, and no PortMidi to install: it drives
`J6_Controller` directly and prints. Covers the menu, the patch list, writing,
freeing, naming, the edited marker and the first-free destination, and every
group in it exists because something was once wrong there — the list that
stopped at 48, the memory that came out named `Init`, the write result that
stayed on screen across pages.

Two checks are structural rather than about any one feature. Every screen it
visits is composed the way `j6_main.cpp` composes it and measured against the
fifteen characters a display line holds, which is how `To U21 Synthetiser` was
caught at eighteen. And every page of every section is visited with both
encoders turned, which is how the pseudo-parameters were caught overlapping the
arpeggiator.

On the host the patch store is backed by RAM rather than flash. That covers
writing, reading back and the slot bookkeeping, which is where the bugs were;
the flash path itself can only be tested on the device.

The navigation helpers live in `tools/host_tests/j6/j6_ui_harness.h` and address nothing by
number — sections and pages by name, the cursor by reading back where it is.
Four separate test bugs in this project were miscounted encoder steps, and each
one looked like a firmware fault first.

## Deliberate deviations from the original

1. **The keyboard spans the full MIDI range** rather than the instrument's 61
   keys. The pitch comes from a control voltage and nothing stops it going
   further.
2. **Velocity is ignored.** Faithful: the keyboard produces a gate.
3. **Pitch bend defaults to two semitones**, adjustable, where the instrument
   has a lever with a fixed range. Two is what a MIDI controller expects.
4. **The contour opens the filter by up to 10 octaves.** This was 6 to begin
   with and that was simply wrong: a Juno patch routinely leaves the Cutoff
   slider near zero and lets the contour do all the work — "Brass" has Cutoff
   at 0 and Env at 0.8. With 6 octaves it opened to 557 Hz and settled at 147,
   which is quiet, dull and nothing like brass. The cutoff range itself spans
   log2(18000/20) = 9.8 octaves and on the instrument the contour at full can
   take the filter from shut to open, so it has to cover essentially that whole
   range.
5. **The DCO LFO is worth one semitone at full depth.** Seven was a guess and
   badly wrong: "Piano I" has the slider at 0.4, which at seven semitones is a
   wobble of nearly three — a ghost, not a piano. junox settles it by
   construction, applying `2^(freqMod/12)` with `freqMod` running to 1.
6. **The VCA level is linear, not squared.** A squared taper looked like the
   more slider-ish choice and cost every patch up to 3 dB; the 48 patches were
   authored against a linear mapping, so bending the curve underneath them
   misrepresents all of them at once.
7. **No delay-dependent loss in the bucket-brigade lines.** An MN3009 has 256
   stages, so the measured 1.66 … 5.35 ms delay puts its clock between roughly
   24 and 77 kHz and its own Nyquist limit never drops below about 12 kHz — high
   enough at a 44.1 kHz sample rate that modelling the variation buys very
   little. A fixed reconstruction filter stands in for it. The 12 dB low-pass
   *ahead* of the line is modelled, because that one audibly rounds the
   sawtooth.
8. **Envelope times follow the measurements, not the specification.** The sheet
   says decay and release reach 12 s; a real instrument measured 19.8. Both
   numbers are recorded in `juno_defs.h`.
9. **The chorus I+II rate is 9.75 Hz.** The service notes label it 1 Hz, which
   is a typo — Juno60 measured it, and building from the printed figure would
   have made that setting ten times too slow.
10. **The 48 factory sounds are read-only.** The instrument has no factory ROM
    at all -- all 56 of its memories are writable. Here the imported set stands
    in for a factory bank and cannot be overwritten, which costs nothing and
    means a mistake is always recoverable.
11. **Patch names are eleven characters.** The instrument has no names at all,
    only numbers. Eleven is what fits once 56 records share one flash sector, and
    it truncates 17 of the 48 imported factory names -- which is part of why they
    can be edited.
12. **A memory can be freed again.** The instrument cannot do that -- all 56 of
    its memories always hold something. The free state only exists here because
    the imported factory bank means not all 56 start out occupied, so it has to
    be reachable in both directions.

## Licence

GPL v3. The patch table and parts of the parameter scaling are adapted from
[junox](https://github.com/dzannotti/junox), also GPL v3. The measurements that
the envelope, chorus and high-pass values rest on come from
[Juno60](https://github.com/pendragon-andyh/Juno60), MIT. See
[the licensing section of the root README](../../README.md#license).
