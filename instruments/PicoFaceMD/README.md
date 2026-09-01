# PicoFaceMD — Moog Minimoog Model D

An emulation of the Moog Minimoog Model D, one of the nine instruments in
[PicoVintageSynthCollection](../../README.md).

**Status:** running firmware for the RP2350, plus a macOS host test that
compiles the identical engine sources.

The engine follows the Minimoog operation manual and the schematic in the
service manual — Moog's documents, not shipped with this repository — section by
section as the panel is laid out:

```
CONTROLLERS      Tune, Glide, Modulation Mix (osc 3 ↔ noise), oscillator
                 modulation switch, osc 3 keyboard switch, pitch and mod wheel
OSCILLATOR BANK  three oscillators; Range (LO/32'/16'/8'/4'/2') and Waveform
                 (6 positions) each, plus a ±7 semitone Frequency control on
                 oscillators 2 and 3
MIXER            volume and on/off for osc 1–3, noise (white/pink) and the
                 external input — which on a Model D is what you patch the
                 output back into
MODIFIERS        24 dB/oct transistor ladder low-pass: Cutoff, Emphasis,
                 Amount of Contour, filter modulation switch, two keyboard
                 tracking switches; one contour generator (attack / decay /
                 sustain) each for filter and loudness
OUTPUT           main volume, A-440 tuning tone
```

The essential point: the Model D is **monophonic**, and that is not a
limitation to be quietly designed around. The keyboard produces one control
voltage, and the manual says exactly what happens when you hold more than one
key — *"If more than one key is held down, only the lowest one has effect."*
Together with the single-trigger contours, which do **not** restart when a
second key goes down while the first is held, that is most of how the
instrument plays. Both are modelled here.

## Where the code comes from

The engine follows the panel structurally. The ladder filter is Aaron
Krajeski's variant of the Huovilainen model, taken from
[BelaMiniMoogEmulation](https://github.com/lbros96/BelaMiniMoogEmulation)
(stated by its author to be under no copyright); the block structure follows
[moogvst](https://github.com/grimtraveller/moogvst). Each file header under
`include/moog/` and `src/moog/` names its specific source.

| File | Original circuit | Contents |
|---|---|---|
| `moog_osc.h` | Oscillator Bank | six waveforms, six ranges, polyBLEP on every discontinuity |
| `moog_ladder.h` | Modifiers: the filter | four one-pole sections in a saturating feedback loop, 24 dB/oct |
| `moog_env.h` | the two contour generators | RC attack curve, sustain, release governed by the DECAY switch |
| `moog_dsp.h` | — | one-pole, biquad, DC block, saturators, noise, drift |
| `moog_voice.{h,cpp}` | Controllers + Mixer + keyboard + output | note priority, glide, modulation mix, mixer overdrive, feedback, output stage |
| `moog_params.{h,cpp}` | the front panel itself | one table row per control: name, type, MIDI CC, panel scale |
| `moog_presets.{h,cpp}` | — (the original has no memory) | 25 factory panel settings |
| `moog.{h,cpp}` | — | parameters, presets, MIDI, sample rate |

## Signal flow

```
 osc 1 ──┐
 osc 2 ──┤                                        contour ──┐
 osc 3 ──┼─► mixer ──► ladder low-pass (24 dB/oct) ──► amplifier ──► output
 noise ──┤     ▲            ▲                              │         stage
         │     │            │                          contour       │
 feedback┘─────┘        keyboard tracking (⅓ / ⅔ / full)             │
         ▲                  ▲                                        │
         └──────────────────┴────────────────────────────────────────┘
                            ▲
              modulation mix (osc 3 ↔ noise) × mod wheel
              routed to the oscillators (switch A) and/or the filter (switch J)
```

Three things about this shape are worth stating, because they are what make it
sound like the instrument rather than like a generic subtractive synth:

- **The mixer is meant to be overdriven.** Five sources, each with its own
  volume, summing into a stage that saturates. Turning everything up does not
  make it louder, it makes it growl. The `Drive` control sets how hard that
  stage is pushed.
- **The saturation is inside the filter's feedback loop, not after it.** So
  overdriving the mixer changes the shape of the resonance rather than merely
  adding distortion at the end.
- **Oscillator 3 is a control source whether or not it is audible.** The
  manual: *"Switch (G) does not affect the control signal produced by
  Oscillator 3 via the Modulation Mix."* With its keyboard switch off and its
  range on LO it is the only LFO the instrument has.

## Parameters and the menu

62 parameters, each `0.0 … 1.0`. See `enum MoogParam` in
`include/moog/moog_params.h`; the table that gives each one its name, type,
panel scale and MIDI controller is `src/moog/moog_params.cpp`, and the
firmware, the host test and the table below all read it — there is one place
that says what a control is called and which CC moves it.

Forty-odd controls laid out flat would be nearly thirty screens to page
through to reach the filter, so the panel is a **two-level menu** whose
sections are the ones printed on the instrument:

| Section | Pages |
|---|---|
| `PRESET` | the preset list |
| `CONTROLLERS` | tune, bend range, glide, modulation mix, mod wheel, the two mod switches |
| `OSCILLATOR` | range and waveform per oscillator, plus a detune page |
| `MIXER` | volume and switch per source, noise colour, feedback |
| `MODIFIERS` | filter, contour amount, keyboard tracking, both contour generators |
| `OUTPUT` | main volume, A-440 |
| `EFFECTS` | the two slots, then chorus, delay and reverb |
| `VINTAGE` | drift, drive, tone, note priority, trigger mode |
| `SYSTEM` | receive channel, transposition |

Three encoders, as in the master project:

| | |
|---|---|
| encoder 1 | the section, at the top level; the page within it below |
| button 1 | down into the section under the cursor, and back out again |
| encoder 2 | the left-hand parameter of the page |
| encoder 3 | the right-hand parameter |

The section list and the preset list are drawn as three rows with the cursor
kept in the middle; parameter pages show two labelled values. The section and
page tables are plain data at the top of `src/MD_Controller.cpp` — reordering
means moving a line.

Values are printed the way the panel is marked: knobs run `0.0 … 10.0`, the
Cutoff control `−4.0 … +4.0`, and the Frequency controls of oscillators 2 and
3 `−7.0 … +7.0`, which on those two is also the number of semitones. Rotary
switches print the name of their position (`8'`, `Narrow`, `Pink`).

Continuous values move in steps of 1 % and snap to the percent grid on the
first click. Preset values do not sit on that grid (the cutoff of *Fat Bass*
is 0.30, that of *Reso Sweep* 0.12), so without snapping the round values
would be unreachable from some presets. Switches and rotary switches step one
position per click — turning right is on, left is off — rather than toggling,
so the direction of the encoder keeps its meaning.

## Presets

The original has no memory: the sound is where the knobs are, and a patch is a
sheet of paper. The 25 factory entries in `src/moog/moog_presets.cpp` are the
replacement for that sheet. Selecting one writes **every** parameter, so a
preset is a starting point and never a layer — turn a knob afterwards and only
that knob moves.

```
 1 Fat Bass       7 Whistle       13 Snare        19 Reso Sweep    25 Shine On
 2 Lead Solo      8 String Pad    14 Wind         20 Bell
 3 Taurus Bass    9 Hard Lead     15 Organ        21 Space Drone
 4 Brass         10 Wobble Bass   16 Clav         22 Trumpet
 5 Funk Bass     11 Vibrato Lead  17 Sub Bass     23 Detune Stack
 6 Flute         12 Percussion    18 Growl Bass   24 Sixth Source
```

*Sixth Source* is straight out of the manual: *"When the EMPHASIS control is
set to 10, the filter breaks into oscillation, and produces a pure sine wave
tone. It is thus available as a sixth sound source."* All mixer switches off,
both keyboard control switches on so that it plays in tune — measured, the
self-oscillating filter tracks the keyboard to within 15 cents over two
octaves.

*Shine On* is the four-note theme, entered from a patch sheet. Two sawtooths
detuned by four cents, a filter kept fairly closed with just enough emphasis
to go nasal, oscillator 3 down in LO doing nothing but vibrato, and the
external input fed from the output — the feedback trick, which is where the
warmth comes from. Measured against the sheet: the LFO runs at 5.00 Hz and the
two oscillators beat at 0.50 Hz, which is what four cents comes to at A3.

The filter contour departs from the sheet on purpose. Its 200 ms attack and
500 ms decay put a slow sweep across the front of every note — measured, the
spectral centroid climbed from 246 to 542 Hz over 230 ms and then sagged back
to 449 Hz, which reads as a growl. This part wants a horn: one that speaks at
once and then holds still. Attack 25 ms, decay 150 ms, sustain 8.5, with the
cutoff dropped from −0.5 to −0.9 to pay for the higher sustain, leaves the
steady-state cutoff at 788 Hz either way — the colour the sheet asks for —
while the centroid now settles inside 46 ms and stays between 420 and 450 Hz.

Two values the sheet leaves open are noted in the preset's comment: the
oscillator 3 frequency (the sheet only says the range switch is on LO) and the
modulation wheel, which on the instrument is played by hand rather than set.
It ships at about ±7 cents — a shimmer rather than a wail — and CC 1 brings in
as much as the part wants.

Worth knowing before reaching for the Modulation Mix to clean up a vibrato
that is too prominent: it will not help. Measured at equal depth, oscillator 3
smears the spectrum to 20 dB harmonic-to-rest while the noise source only
reaches 46 dB, so mixing *more* noise in makes the tone **cleaner**, not
rougher. What sets how much the vibrato is heard is the modulation wheel.

## MIDI

Program Change selects a preset. Pitch Bend travels as far as the `Bend`
parameter says (default two semitones; the original is fixed at about half an
octave). Velocity is ignored — the keyboard of a Model D produces a gate, not
a velocity, and every key sounds at the same level however it is struck.

Notes are accepted across the **full MIDI range**. A Model D has 44 keys, but
its pitch comes from a control voltage and nothing in the circuit stops that
voltage going past the ends of its own keyboard. (PicoFaceSM folds stray notes
back into its manual, because a divider organ genuinely has no note outside
it. Doing the same here would take the bottom octave of a 61-key controller
away from a synth whose main job is bass.)

Controller numbers follow the usual meanings wherever one exists — CC 1
modulation, CC 5 and 65 portamento, CC 7 volume, CC 71 resonance, CC 73/75
attack and decay, CC 74 cutoff, CC 91 and 93 reverb and chorus depth — so a
generic controller does something sensible before anyone opens a mapping
editor. Everything else sits on
controllers the MIDI specification leaves undefined, never on a reserved one.

| Section | Control | CC | | Control | CC |
|---|---|---|---|---|---|
| **Controllers** | Tune | 3 | | Mod Wheel | **1** |
| | Glide | **5** | | Osc Modulation | 14 |
| | Glide switch | **65** | | Osc 3 Keyboard | 15 |
| | Mod Mix | 9 | | Bend range | 20 |
| **Oscillator 1** | Range | 21 | | Waveform | 22 |
| **Oscillator 2** | Range | 23 | | Waveform | 25 |
| | Frequency | 24 | | | |
| **Oscillator 3** | Range | 26 | | Waveform | 28 |
| | Frequency | 27 | | | |
| **Mixer** | Osc 1 volume | 29 | | Osc 1 on | 102 |
| | Osc 2 volume | 30 | | Osc 2 on | 103 |
| | Osc 3 volume | 31 | | Osc 3 on | 104 |
| | Noise volume | 85 | | Noise on | 105 |
| | Noise colour | 106 | | | |
| | Feedback volume | 86 | | Feedback on | 107 |
| **Modifiers** | Cutoff | **74** | | Filter modulation | 108 |
| | Emphasis | **71** | | Keyboard control 1 | 109 |
| | Amount of Contour | 87 | | Keyboard control 2 | 110 |
| | Filter attack | 88 | | Filter decay | 89 |
| | Filter sustain | 90 | | Decay switch | 111 |
| | Loudness attack | **73** | | Loudness decay | **75** |
| | Loudness sustain | 76 | | | |
| **Output** | Volume | **7** | | A-440 | 112 |
| **Vintage** | Drive | 70 | | Note priority | 113 |
| | Drift | 77 | | Trigger mode | 114 |
| | Tone | 78 | | Transpose | 115 |
| **Effects** | Slot A | 116 | | Slot B | 117 |
| Chorus | Rate | 12 | | Depth | 13 |
| | Mix | **93** | | Feedback | 16 |
| Delay | Time | 17 | | Feedback | 18 |
| | Mix | 19 | | Tone | 79 |
| Reverb | Size | 80 | | Damping | 81 |
| | Mix | **91** | | Width | 82 |

Channel messages: CC 64 sustain pedal, CC 120 all sound off, CC 123 all notes
off, CC 121 reset all controllers. A parameter arriving over MIDI updates the
display as well as the engine, so the screen never shows a value the sound no
longer has.

Press `m` in the host test to print this map from the table itself.

## Effects

A Model D has none. These sit behind the output stage as a section of their
own, and every factory preset but one leaves both slots empty, so the dry
instrument stays what the firmware sounds like. The exception is *Shine On*,
where a delay is part of the sound rather than a decoration on it: slot A a
430 ms delay whose repeats darken as they go, slot B a hall under it. That
preset measures P31 on the device against P21 for a dry one. They are also the only thing
in the signal path that produces a stereo image at all — the voice is mono, as
the original is.

Three effects, **two slots**. Each slot is empty or holds one of them, and the
signal runs slot A then slot B, so the order is the player's choice: a chorus
into a reverb is not the same thing as a reverb into a chorus. There is
exactly one instance of each effect, which halves the memory a two-slot design
would otherwise need and makes the same effect in both slots impossible rather
than merely discouraged (the second slot is skipped).

| | |
|---|---|
| Chorus | three modulated lines around a 14 ms base delay, their LFOs a third of a cycle apart. Rate, Depth, Mix, Feedback |
| Delay | one mono line up to 750 ms, two taps, the right one reading 8 % earlier for width. The tone control sits **inside** the feedback loop, so each repeat returns darker than the last. Time, Feedback, Mix, Tone |
| Reverb | eight comb filters into four all-passes per channel, the Schroeder arrangement by way of Freeverb. Size, Damping, Mix, Width |

Two slots and not three is what bounds the cost. Measured against the peak
load the firmware reports on the device, where the dry engine sits at 21 %:

| Slots | Host | → board |
|---|---|---|
| dry | 0.185 % | **21 %** — measured on the device |
| Delay | 0.208 % | 23 % |
| Chorus | 0.238 % | 27 % |
| Reverb | 0.256 % | 29 % |
| Delay → Reverb | 0.269 % | **31 %** — measured on the device |
| Chorus → Reverb | 0.301 % | 34 % |

The two marked rows are readings from the display footer, the rest are scaled
from the host figures against them. Worth recording that the method holds: the
delay-into-reverb row was extrapolated at 30 % before anyone played a note on
the hardware, and the device then reported 31 % playing the *Shine On* part.

Memory is the larger cost: 255 kB of delay lines, which takes the firmware
from 19 kB of RAM to 276 kB of the 512 kB available. A third simultaneous
effect would have bought very little and spent the headroom that keeps the
audio glitch-free while the display is being pushed out.

Nothing the panel can be set to reaches the hard clip in the I2S conversion.
Two effects in series each with their own wet level can sum past full scale —
a delay at maximum feedback into an undamped reverb measured 1.63 — so the
section ends in a limiter that is exactly linear below 0.70 and asymptotic to
1.0 above it. Every pair of slots at maximum settings is part of the self
test, watched for twenty seconds of tail after the note is released.

## Building the firmware (RP2350)

Built together with the rest of the collection, or on its own:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceMD
cmake --build build
```

Result: `build/PicoFaceMD.uf2`. Target platform `rp2350-arm-s`, board
`sparkfun_promicro_rp2350`, **444 MHz**. Scaffolding, hardware layer, audio
subsystem, MIDI and display come from the shared core.

| | |
|---|---|
| Flash | 99,168 bytes |
| RAM | 268,624 bytes, of which 255 kB is effect delay lines |
| of which sound engine | 27.5 kB code and rodata, 6.4 kB of that the preset table |

Core 0 runs the audio producer in the main loop together with USB, MIDI, the
controls and the display; the DMA IRQ stays microscopic. Unlike PicoFaceRD
there is no voice worker on core 1 — a Model D is one voice, and even
oversampled it costs well under half a core. Core 1 is free.

### Build options

The standalone repository had three CMake options — `MD_DOUBLE_RESET`,
`MD_OVERSAMPLE` and `MD_SAFE_MODE`. They did not survive the merge into the
collection, which knows only the keywords of `picoface_add_instrument()`.

What is left of them: `MOOG_OVERSAMPLE` still exists as a macro with a default
of 2 in `include/moog/moog_defs.h` and can be set through `DEFINES` in
`instrument.cmake`. The safe mode and the double-reset switch are gone; see the
section below for what the latter now does.

### Oversampling

The oscillators are band-limited by polyBLEP, so they alias very little on
their own. What aliases is the two saturating stages — the overdriven mixer
and the `tanh` in the ladder. Both are soft, so their harmonics decay quickly,
and measuring all three settings shows where the knee is:

| | sawtooth | narrow pulse | compute |
|---|---|---|---|
| 1× | −48.5 dB | −44.3 dB | 748× real time |
| **2×** | **−64.7 dB** | **−58.5 dB** | **508× real time** |
| 4× | −64.7 dB | −57.9 dB | 255× real time |

(worst non-harmonic component relative to the fundamental, across notes C4 to
C7, filter wide open.)

2× buys 16 dB over no oversampling. 4× buys nothing at all — past that point
what is left is no longer aliasing but the noise floor of the decimation
filter, and it costs twice the compute to not improve. Hence the default.

Whatever is left above 22 kHz is removed by a 6th-order Butterworth at 15 kHz
before decimation. That corner is not a compromise: the audio path of the
original rolls off in the same region, so the anti-alias filter doubles as the
vintage bandwidth the sound is expected to have.

### The 480 MHz boot failure, and what it actually was

Worth recording, because it cost real time and the obvious explanation was
wrong. For a while the firmware would not start at 480 MHz while 444 MHz was
rock solid, which looked like the chip simply running out of silicon margin.
It was not. The cause was the flash timing during the *transition*.

`pico_init()` writes a conservative QMI `M0_TIMING` before raising `clk_sys`
and the aggressive one after. Between those two writes the flash briefly runs
at the new, high system clock with the *old* divider and `RXDELAY`:

| target | transitional flash clock | steady-state flash clock |
|---|---|---|
| 444 MHz (old code) | 111 MHz @ `RXDELAY=2` | 148 MHz @ `RXDELAY=3` |
| 480 MHz (old code) | **120 MHz @ `RXDELAY=2`** | 120 MHz @ `RXDELAY=3` |

`RXDELAY` compensates a round-trip delay that is fixed in nanoseconds, so the
value needed grows with `clk_sys`. `RXDELAY=2` was just enough at 111 MHz and
just short at 120 MHz — the core hung on the first instruction fetch after the
clock switch. Note the giveaway: the 480 MHz build ran the flash *slower* in
steady state (120 MHz) than the 444 MHz build that worked (148 MHz), so the
flash chip was never the limit. Only the transitional window was. 444 MHz was
on the working side of that edge, but with no margin worth the name.

The fix is `PICOFACE_QMI_M0_TIMING_SAFE` (`CLKDIV=8`, `RXDELAY=2`), which puts
the window at 55 MHz. With it, 480 MHz booted reliably — the diagnosis was
confirmed on the device rather than argued. 480 MHz was then removed again:
peak load stays under a third of one core even with 2× oversampling, the
Model D does not need the headroom, and at a core voltage of 1.60 V the slower
clock is the kinder choice when it costs nothing.

### What the datasheet made computable

The section above worked the flash budget out as far as it could without
numbers for the pads, and settled for "no margin worth the name". The RP2350
datasheet has the numbers. Table 1292, worst case over process, voltage and
temperature at VDDIO 3.3 V: **system clock to QSPI output 2.5 ns, QSPI input to
system clock 1.5 ns**. A 133 MHz QSPI part specifies 6 ns clock-to-output. So
the requirement is 10.0 ns, and at 444 MHz:

| name | CLKDIV | RXDELAY | SCK | budget | left for the device |
|---|---|---|---|---|---|
| `SAFE` | 8 | 2 | 55.5 MHz | 11.26 ns | 7.26 ns |
| `CD4` | 4 | 5 | 111.0 MHz | 10.14 ns | **6.14 ns** |
| `RX4` | 3 | 4 | 148.0 MHz | 7.88 ns | 3.88 ns |
| `OC` | 3 | 3 | 148.0 MHz | 6.76 ns | **2.76 ns** |

`OC` was the default for a while and leaves 2.76 ns where the part asks for 6.
It has run on every board tested here, because real pads and a real flash at
room temperature are far better than their worst-case numbers -- but that is
exactly the profile of a setting that works on one board and not the next, and
two issues report boards that will not boot. `RX4` is the default now, and the
ladder is walked at boot rather than handed out by e-mail; both are below.

**Getting to a default took three tries and a lesson about the measuring
instrument.** The arithmetic alone made `CD4` look right, and it
was the default briefly. Then the cost was measured on the D5 -- the most
XIP-bound of the ten, because it reads its PCM from flash. Boot benchmark,
four voices:

| | SCK | RXDELAY | B | for the device |
|---|---|---|---|---|
| `OC` | 148 MHz | 3 | **51** and **53** on different runs | 2.76 ns |
| `RX4` | 148 MHz | 4 | 53, 54 | 3.88 ns |
| `CD4` | 111 MHz | 5 | 59 | 6.14 ns |

**Read the spreads before the differences.** `bootBenchPercent()` reports the
*worst* of 94 blocks, not the mean, and a maximum picks up whatever transient
landed in one of them — so it swings by two points or more between runs of the
same image. That is the right statistic for the diagnostic it is (it shows the
worst case the governor has to survive) and the wrong one for comparing timings
two points apart.

An earlier version of this section claimed, on one `OC` run against one `RX4`
run, that RXDELAY costs about five percent of throughput at constant SCK —
which would have been a finding the datasheet does not carry. A second `OC` run
came back at 53 and took it away. **Withdrawn.** `OC` and `RX4` overlap and
nothing measured here separates them.

`CD4`'s cost does stand: 59 against 51–53 is six to eight points, well outside
the spread, and about **1.3 voices** before the governor starts trimming tails.

Which leaves which rung to ship. **`RX4` is the default**, and it is a free
change: same 148 MHz, and its benchmark numbers sit inside `OC`'s own spread, so
nothing measured separates them on cost. What separates them is where in the
valid window the sample point lands. A bit is good from the device's
clock-to-output until the next bit replaces it -- at 148 MHz, 6.00 to 12.76 ns.
`OC` samples at 6.76, which is 11 % in and hard against the leading edge; `RX4`
samples at 7.88, 28 % in. Free margin is worth taking.

There is some history in that sentence. `RX4` was briefly the default, then
reverted to `OC` on a measurement that RXDELAY costs about five percent of
throughput. That measurement was withdrawn one PR later -- it was noise in a
maximum -- but the default never followed the retraction, and stayed on `OC` for
a reason that no longer existed.

`CD4` itself was wrong when it was added: `RXDELAY=4` gives 9.01 ns, a nanosecond
short. It had been chosen against the part's 133 MHz rating rather than against
this sum, which was not computable until the pad delays turned up.

**And the A4 question is closed.** The datasheet says of that stepping: *"This
stepping has no hardware changes."* It is identified by `CHIP_ID.REVISION` 0x8
and differs from A3 only in bootrom. The hardware delta a reporter comparing A2
against A4 actually sees belongs to **A3** -- GPIO leakage, a QFN-60 NSMASK fix,
USB and OTP mitigations, and changed reset states for the clock registers --
and none of it touches flash timing. Of the 28 errata, 13 still affect A4 and
not one concerns QMI or booting from flash. The datasheet puts the ceiling
squarely on the board instead: *"the maximum SCK frequency is constrained by the
limits of the attached QSPI device, the signal integrity afforded by the PCB
layout, and IO delays in the pads."*

### The steady-state flash timing, and the boards it does not fit

The section above ends on "the flash chip was never the limit". That was true
of *that* failure, on *this* board. Issue #107 is the other case, and it is
worth having both written down next to each other.

A user with third-party 16 MB RP2350 boards could not get any instrument to
start, while official Pico 2 boards worked. The boards carried the newer A4
stepping, which is what the report suspected. A4 is documented by Raspberry Pi
as a drop-in replacement for A2, so that is unlikely on its face -- and the
decisive observation is in the report itself: lowering `clk_sys` to 300 MHz
made the boards boot. That change is usually read as "the core cannot hold the
overclock", but `M0_TIMING` encodes a *divider* of `clk_sys`, so lowering the
core clock lowers the flash clock with it. The 300 MHz test moved the flash
from 148 MHz to 100 MHz. Nothing distinguishes the two readings yet.

`RXDELAY` counts half `clk_sys` cycles, and a value of 0 samples on the SCK
edge that launched the command. In SPI mode 0 the device presents its data
half an SCK period before that, so the time a flash part has to get data back
through the pads is `(half an SCK period) + RXDELAY`:

| build | SCK | budget |
|---|---|---|
| 444 MHz, `OC` (`CLKDIV=3`, `RXDELAY=3`) | 148.0 MHz | **6.76 ns** |
| 480 MHz, `RD` (`CLKDIV=4`, `RXDELAY=3`) | 120.0 MHz | 7.29 ns |
| 444 MHz, `RX4` (`CLKDIV=3`, `RXDELAY=4`) | 148.0 MHz | 7.88 ns |
| 444 MHz, `CD4` (`CLKDIV=4`, `RXDELAY=4`) | 111.0 MHz | 7.88 ns |
| 300 MHz, `OC` (the reporter's working test) | 100.0 MHz | 10.00 ns |
| 444 MHz, `SAFE` (`CLKDIV=8`, `RXDELAY=2`) | 55.5 MHz | 11.26 ns |

So the shipping 444 MHz build has the tightest flash timing in the collection
-- tighter than the 480 MHz one -- and a W25Q128JV is specified at 6 ns
clock-to-Q before the pad round trip is counted. There is no margin there at
all; whether a given part makes it is a property of that part. That is the
straightforward reason this project fails on a board where, as the report puts
it, other RP2350 firmware runs fine: none of it drives the flash 11 % past the
nominal 133 MHz.

This is a hypothesis with a clean experiment behind it, not a conclusion. The
experiment is to hold `clk_sys` at 444 MHz and change only the flash timing,
which no test so far has done -- both variables moved together every time. If
a `SAFE` build boots on those boards, the stepping is exonerated.

The second half of the report is separate and needs no new explanation: at
300 MHz the DX froze after menu navigation and the D5 and MD made no sound.
These engines are budgeted for 444 MHz -- the D5's own load figures are about
7 % of a block per voice plus 11 % fixed -- so at 68 % of the design clock they
simply do not finish their blocks. That is the downclock, not a second fault.

Groundwork landed with this: `PICOFACE_SYS_CLOCK_HZ` and
`PICOFACE_QMI_M0_TIMING_TARGET` now live in `core/include/project_config.h`
rather than in `pico_hw.cpp`, because the boot is not the only place that
writes `M0_TIMING`. Every flash write re-inits boot2 and clobbers the register,
and the three restore sites (`core/src/veeprom.cpp`,
`instruments/PicoFaceRD/src/veeprom.cpp`,
`instruments/PicoFaceJ6/src/j6_patchstore.cpp`) each held their own copy of the
value. A `-DPICOFACE_QMI_M0_TIMING_TARGET=...` therefore reached the boot and
nothing else, and the device reverted to the `OC` timing at the first settings
save -- which would have quietly spoiled exactly the experiment above. All four
sites now read one macro.

The same defect was present in PicoFaceRD, PicoFaceCP, PicoFaceDX and
PicoFaceYC, which share this hardware layer — today they share it as
`core/src/pico_hw.cpp`. RD ran at 480 MHz
unconditionally and showed exactly this symptom — it would not restart after
the USB cable was unplugged. All four have been fixed and confirmed.

Two smaller things were corrected in the same place:

- `__dsb()` / `__isb()` around the timing writes. The register write leaves
  over APB while the instruction fetches that follow reach the same peripheral
  over the XIP path — two routes to one endpoint, not ordered against each
  other. The same barrier was missing in `veeprom.cpp`, where only a compiler
  barrier guarded the post-flash-write timing restore.
- `set_sys_clock_hz()` is called with `required=false` and its result was
  discarded. On an unreachable target it silently leaves `clk_sys` at 150 MHz;
  the aggressive flash timing is now only applied if the switch succeeded.


### What the boards actually reported, and the mode we never checked

The experiment above was run. A diagnostic image reads QMI `M0_TIMING`,
`M0_RFMT` and `M0_RCMD` as the bootrom left them -- captured in `pico_init()`
before this project touches the flash -- and puts them on the display. On
RP2350 that is the only way to know: `boot_stage2` is compiled but never runs,
the bootrom configures the flash, and what it chose was never visible.

| | working A2 board | reporting board |
|---|---|---|
| `M0_RCMD` prefix | `EB` (quad I/O) | **`BB` (dual I/O)** |
| addr / data width | 4 bit | **2 bit** |
| `DUMMY_LEN` | 4 | **0** |
| `M0_TIMING` | `0x60007203` | `0x60007023` as reported |

The timing value is almost certainly a transcription slip: the digits were read
off a screen that could not be photographed and typed by hand, and
`0x60007023` is one adjacent-digit transposition away from `0x60007203` --
which is exactly the working board's value. It is treated as such here.

The other two are not slips. `0x00009154` is not a mistyping of `0x000492A8`,
and the two registers cross-validate: `9154` decodes to precisely the widths
and turnaround a `BB` dual-I/O read requires. A slip of the pen does not
accidentally produce a coherent alternative configuration. **The dual mode is
real.**

That exposes a defect independent of any boot failure. `pico_init()` wrote its
tuned `M0_TIMING` over whatever the bootrom had established, without ever
looking at what that was. The tuned value is measured for quad reads with four
dummy cycles; a dual read with none is a different proposition at the same
divider -- half the bandwidth per clock, and a turnaround carried entirely by
the mode byte. We had been overwriting a flash configuration we had never
looked at.

The fix reads `M0_RCMD` and `M0_RFMT` first and applies the tuned value only
when the bootrom actually established quad -- prefix `EB` and 4-bit widths.
Otherwise every field the bootrom chose is kept and only `CLKDIV` is rescaled,
so the flash keeps the clock it already had across the core-clock change:

| bootrom | flash before | flash after, core at 444 MHz |
|---|---|---|
| `CLKDIV=3` @ 150 MHz | 50.0 MHz | 49.3 MHz (`CLKDIV=9`) |
| without this fix | | 148.0 MHz |

The splash line carries the mode as a `Q` or `D` (`A2 Q 148MHz`), so a board
running dual is visible in one glance instead of invisible for a fortnight.
The three post-flash-write restore sites write the value the boot actually
settled on rather than the compile-time macro, or the first settings save
would undo the caution.

**This is a correctness fix, not a proven cure.** Two things must be said
against reading it as the answer to #107. Under the corrected timing value the
reporting board's bootrom picked the *same* divider as the working one, so the
flash divider is not what separates them. And the clock ladder does not order
by flash clock at all: `SAFE`'s 55.5 MHz failed at a 444 MHz core, while `OC`'s
100 MHz works at a 300 MHz core. No monotone function of flash clock produces
that; core clock produces it immediately. The prime suspect is therefore the
core clock, and this fix earns its keep by making the next experiment clean --
it moves the flash to 49 MHz while leaving the core at 444, which no previous
build did. The reported chip revision was also `A3`, not the `A4` of the issue
title, and is hand-transcribed like the rest.


### Measuring the flash instead of prescribing it

Everything above is an argument about which single timing to ship, and every
version of that argument has the same hole in it: the right value is a property
of the board, and the boards are not ours. Waveshare changes flash suppliers
between production runs; the same part number, bought twice, need not carry the
same die. The honest hardware requirement for a fixed 148 MHz would read *"a
QSPI flash that beats its own datasheet by more than a factor of two"*, and
nobody can shop for that.

Two things are worth separating before the fix, because the dual-mode finding
above invites the wrong conclusion:

- **Dual is not slower to sample than quad.** Same pins, same clock, same
  clock-to-output; a 133 MHz part is rated 133 MHz either way. The budget table
  applies unchanged. **A dual flash can be clocked exactly as high as a quad
  one** -- what dual costs is *bandwidth*, two bits per clock instead of four.
- **Dual is usually not a property of the chip either.** Practically every 16 MB
  SPI NOR supports quad I/O. Coming up in dual normally means the bootrom could
  not *confirm* quad, because the quad-enable bit sits in a different
  status-register bit per manufacturer. It is a probe outcome, not something
  printed on the package -- which is the second reason a hardware requirement
  would not have helped.

So the timing is measured at boot instead of asserted. Checksum 32 KB of flash
at the timing the bootrom itself was running -- the chip booted with it, so it
is the one configuration known good -- raise the clock, then walk the ladder
fastest-first and keep the first rung that reproduces the checksum:

| rung | SCK | outcome |
|---|---|---|
| `RX4` | 148 MHz | kept if it verifies; every board tested lands here |
| `CD4` | 111 MHz | first fallback, the first value inside the worst-case sum |
| `SAFE` | 55.5 MHz | second fallback |
| bootrom's own, `CLKDIV` rescaled | whatever it chose | last resort, taken on trust |

**This must never slow down hardware that already works, and it does not:** a
board that verifies at `RX4` is left exactly where it was. The ladder only
rescues boards that would otherwise not start, which is the entire point.

The part that makes it safe rather than merely clever is that the probe, the
per-rung retry and the ladder all execute **from SRAM**. While a candidate
timing is in force every flash read is suspect, so a wrong one must corrupt
*data*, never the instruction stream -- code in flash would fetch garbage and
fault instead of returning a mismatch. Two details fall out of that and both
are checked in the built ELF rather than assumed:

- `__not_in_flash_func()` sets the section and **nothing else**. Under this
  file's `#pragma GCC optimize("Ofast")` the compiler inlined all three routines
  into `pico_init()`, which lives in flash -- the design was silently gone and
  the build looked identical. They carry an explicit `noinline` now, and the
  first version of this section was written against an ELF in which the symbols
  did not exist at all.
- The rung constants reach the routine as arguments rather than as a table. A
  flash-resident array would have to be read while a candidate timing is in
  force, which is the one thing this code may not do. In the built image the
  literal pool sits inside the RAM function (`60007403`, `60007504`, `60007208`
  at `0x200002a8`), every `bl` targets a RAM address, and the probe reads
  through `0x14000000` -- the no-cache window, because a cached second pass
  would be answered from SRAM and would "verify" a broken timing.

Cost is about 5 ms at boot on a quad board and under 100 ms on a slow dual one,
against a splash screen that is held for two seconds.

**What it does not cover:** the check runs at boot temperature, and timing
margin shrinks as the die warms. A rung that verifies cold could in principle
fail hot. Taking the rung *below* the fastest that verifies would cover it and
was rejected -- it would cost every working board 1.3 voices to insure against
something not yet observed. If a board is ever reported to fail after warming
up, that is the knob.

### Double-tap RESET, and why it used to be disabled

In the standalone repository `pico_bootsel_via_double_reset` was deliberately
**not** linked in. With a Waveshare Pico Audio board driving 3 W speakers, the
inrush current on plug-in dips the supply, the chip performs a brownout reset —
and the library reads that as a double tap and enters BOOTSEL mode instead of
running the program. On the RP2350 the flag lives in the POWMAN register
`chip_reset.DOUBLE_TAP`, which survives the dip, so shortening the detection
window does not help. With headphones instead of speakers it does not occur.

In the collection the library is linked by default - it is what keeps a board
without an accessible BOOTSEL button reflashable - but
`picoface_add_instrument()` takes a `NO_DOUBLE_RESET` keyword, and this
instrument's `instrument.cmake` sets it. So the standalone behaviour is
preserved: no double-tap RESET here, double-tap RESET everywhere else.

The BOOTSEL button keeps working regardless.

## Host test (macOS)

```bash
tools/host_tests/md/build_moog.sh
tools/host_tests/md/moog_test
```

Requires PortMidi (`brew install portmidi`). Opens a virtual MIDI input named
`moog` and plays through CoreAudio; the key bindings are documented at the top
of `tools/host_tests/md/moog_test.cpp`. This compiles the same `src/moog/`
sources as the firmware, with `-DMOOG_HOST_BUILD` replacing the Pico audio
subsystem.

```bash
tools/host_tests/md/moog_test --selftest
```

runs without an audio device, a MIDI port or a terminal: every preset is
checked for level and for NaN, the panel is driven to both extremes to prove
the engine cannot be made to blow up, and the controller map and the full
front panel are printed. This is the form to reach for when something sounds
wrong and the question is whether the engine or the wiring is at fault.

## Playing behaviour

**Monophonic**, as the original is. Which held key sounds is the `Priority`
setting: `Low` is the Model D's own rule and the default, `High` and `Last`
are offered because a mono synth played from a MIDI keyboard is a different
proposition from one played from its own 44 keys.

**Single trigger** by default: holding one key and pressing another moves the
pitch without restarting the contours. That legato behaviour is a large part
of how the instrument phrases. `Trigger` switches it to `Multi`, where every
new key retriggers.

A retrigger does **not** start from zero. There is nothing in the circuit to
discharge the contour capacitor when a new key goes down, so it simply starts
charging again from wherever it is — which is why fast repeated notes on a
Model D swell rather than restarting cleanly.

The `DECAY` switch governs both contours at once, as on the instrument: off, a
released key stops almost immediately; on, it falls at the decay time. That
one switch is why a Model D can play a tight bass line and a long pad without
anything else being touched.

**Glide is two controls, not one.** `Glide Sw` enables it, `Glide` sets the
time — and at a time of zero the switch has nothing to do, so it does nothing.
That is how the instrument behaves, but it makes for a switch that looks
broken, so **every preset carries a usable glide time (60–270 ms) even where
its switch ships off**. Flipping the switch on therefore always changes
something. The two controls share one page (`CTL GLIDE`) so both are visible
at once.

Glide only exists between two *different* pitches. The control voltage
persists across silence, as it does in the original, so a glide is heard even
when the previous key was released before the next one was struck — but a
repeated note has nothing to glide to.

## Measurements (host, Apple M4)

| | |
|---|---|
| Tuning (drift at 0) | A4 = 440.02 Hz (+0.08 ct), C4 = 261.57 Hz (−0.38 ct) |
| Aliasing, worst waveform and note, filter wide open | −58.5 dB below the fundamental (narrow pulse, C7) |
| Aliasing, sawtooth | −64.7 dB |
| Filter self-oscillation, keyboard tracking over 2 octaves | within 15 cents |
| Peak level of the 25 presets | −17.8 to −4.8 dBFS |
| Every parameter at maximum, note held | 0.46 peak — the engine cannot be driven into clipping from the panel |
| Compute | 510× real time, 0.196 % of one core |
| Relative to the PicoFaceSM engine (10 keys held) | 0.77× |

The last line is the one that matters for the target: the Solina peaks at
30–40 % on this hardware, so this engine should land at roughly **23–31 %** —
to be confirmed by the `P` value in the display footer on the device.

## Deliberate deviations from the original

1. **Note priority is selectable.** The original has one rule, lowest note,
   and that is the default. `High` and `Last` are additions.
2. **The external input is a feedback path.** The Model D has a microphone
   preamp on a rear jack, and the thing players actually do with it is patch
   the output back in. There is no input jack on this hardware, so the mixer's
   fifth channel is that feedback loop directly.
3. **Feedback is tapped before the main volume.** On the instrument the loop
   runs through the volume control, so the character changes every time the
   volume moves. That is a trap rather than a feature; here the tap sits after
   the amplifier and before the volume.
4. **Pitch bend defaults to two semitones.** The manual gives the wheel *"as
   much as half an octave up or down"*. A MIDI controller expects two, so the
   travel is a parameter (`Bend`, 0–12) rather than fixed.
5. **`Drift`, `Drive` and `Tone` are panel controls.** In the original these
   are component values, trimmers and the plain physics of a warm circuit
   board. Drift covers both a slow random walk per oscillator and a fixed
   per-oscillator tuning error, and both fade out together as the control is
   turned down — at zero the instrument is mathematically in tune, which no
   Model D has ever been but which is occasionally what you want.
6. **Presets, and a transposition control.** The original has neither.
7. **The modulation wheel is squared.** It reaches 7.2 semitones of pitch
   modulation at the top, which is what it is for — sirens are a Model D
   sound. But linearly, a musical vibrato of 20 cents sits at wheel position
   0.03, so the first perceptible movement and "unusable" are three per cent
   of the travel apart. Squaring puts 20 cents at 0.17 and leaves the full
   depth at the top: the useful range becomes the first fifth of the wheel
   instead of the first thirtieth. The panel still reads 0…10 linearly, as a
   volume control does while its pot is tapered.
8. **Pitch modulation runs at the oversampled rate, filter modulation at the
   sample rate.** Oscillator 3 aimed at the other two at audio rate is a sound
   this instrument is known for, so that path is worth the fifth-order 2^x it
   costs. Filter modulation is used for sweeps and wah at LFO rates, where
   44.1 kHz is already a very high control rate.
9. **The waveforms are not ideal shapes.** The three rectangular positions use
   duty cycles of 0.48, 0.29 and 0.14 rather than 0.5, 0.25 and 0.125: the
   "square" of a Model D is not 50 %, and that asymmetry is audible. The
   narrow positions are level-compensated, because otherwise the waveform
   switch would double as a volume control.
10. **The triangle and sawtooth-triangular waveforms get no polyBLEP.** Both
   are continuous — only the slope steps, not the value — so their harmonics
   fall off at 1/n² and the eleventh harmonic of a note at the top of the
   keyboard is already 40 dB down. A polyBLAMP would cost real cycles to
   correct something sitting under the noise floor of the original. Measured
   aliasing for the triangle is −64.7 dB.
11. **`tanh` is a Padé approximant**, `x(27+x²)/(27+9x²)`, with the input
    clamped to ±3. Above that the expression grows again instead of
    saturating, which inside a feedback loop is not a rounding error but an
    explosion; at exactly ±3 it evaluates to ±1, so the clamp is continuous.
    It matches `tanh` to better than 0.3 % over the range the filter uses, for
    one division instead of a call into libm.
12. **The voice is mono, duplicated to both channels.** This is faithful — the
    Model D is a mono instrument — and is called out only because the other
    projects in this family are stereo.
13. **An effects section.** The original has none. Two slots holding a
    chorus, a delay and a reverb between them, off in every factory preset
    but *Shine On*, and the only thing in the path that makes the instrument
    stereo. See the section above.

## Licence

GPL v3. The ladder filter model is adapted from
[BelaMiniMoogEmulation](https://github.com/lbros96/BelaMiniMoogEmulation),
which its author has stated is under no copyright. See
[the licensing section of the root README](../../README.md#license).
