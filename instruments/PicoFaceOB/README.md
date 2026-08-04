# PicoFaceOB

An Oberheim OB-X emulation, one of the eight instruments in
[PicoVintageSynthCollection](../../README.md). The sound generation is a port of
**OB-Xf** (https://github.com/surge-synthesizer/OB-Xf), the successor of OB-Xd.

## License

**This instrument is GPL-3.0-or-later**, because OB-Xf is - which is also the
licence of the repository as a whole. Concretely:

- `instruments/PicoFaceOB/` as a whole, including the adapter files I wrote for
  this port: GPL-3.0-or-later, see `LICENSE`.
- The built `PicoFaceOB.uf2` is therefore a GPL-3 work.
- The GPL's obligation to provide source is met by this public repository.

The files under `include/obxf/` are original OB-Xf source and carry their
copyright header unchanged. Every ported file additionally has a
`PORTED FOR PicoFaceOB` block listing what was changed.

This instrument keeps a copy of the licence text of its own, next to the
upstream files it applies to; the root `LICENSE` says the same thing for the
repository. For the per-instrument upstreams see
[the licensing section of the root README](../../README.md#license).

## What is ported

| Taken from OB-Xf | Lines |
|---|---|
| Voice, OscillatorBlock, saw/pulse/triangle oscillators with BLEP | ~1,100 |
| filter (2- and 4-pole, Xpander modes) | 241 |
| ADSR envelopes, LFO, noise, smoother, delay line | ~800 |
| BLEP tables | 1,086 |

Replaced by code of my own: `Motherboard.h` and `SynthEngine.h` (776 + 579 lines
of desktop infrastructure) by `OB_Engine` - voice allocation, parameters, render
loop. The parameter ranges are taken from `SynthEngine.h`, so that the control
laws stay the same.

## What is not ported

- **The modulation matrix** (`VoiceMatrix.h`, 785 lines). It cost saving and
  restoring 13 parameters per sample and voice.
- **Unison, MPE, panning, patch banks, tempo sync, MIDI learn, OB-Xd import.**
- **Oversampling.** Upstream can do 2x, and the switch there defaults to off
  anyway.
- **Tuning tables.** Equal temperament, so that `tunedMidiNote()` does not do a
  double-precision calculation per sample.
- 32 voices. Here there are six (`MAX_VOICES` in `include/obxf/ObxfPort.h`).

## The three changes without which it does not run

All three are invisible on a desktop and fatal on a Cortex-M33.

1. **19 unsuffixed floating point literals** in `TriangleOsc.h`, `SawOsc.h` and
   `Lfo.h` (`0.5` instead of `0.5f`). Each of them promotes its whole expression
   to double - in the middle of the per-sample path, emulated in software.
2. **`tan()` and `atan()`** in the filter, also the double variants, once per
   sample and voice each. Replaced by `ob_tan()` / `ob_atan()`.
3. **`getPitch()`** = `440 * exp(ln2/12 * i)`, three times per sample and voice.
   Replaced by `ob_exp2()` (exponent field plus polynomial).

After 1-3, no object of this instrument contains a call into the double runtime
library any more.

## Presets

**351 of the 488 factory patches**, in the original's 18 categories,
converted by `tools/ob_fxp_to_presets/` (committed, rerunnable - see its
README). The .fxp files carry their parameters as named, normalized values in
an embedded XML block; the converter maps them with the exact SynthEngine
control laws and an auto filter skips what the port cannot express: two
audibly active LFOs (114 patches), off-centre master tune (4) and transposes
outside the oscillator range (19). LFO2-only patches are merged onto the
single global LFO, Transpose is folded into the oscillator semitones, and the
invert switches fold into the bipolar parameter signs.

The table costs ~70 KB of flash and 350 bytes of RAM (the category name
pointers); preset data is only touched on load, never in the render path.
Reachable under Menu -> Presets -> category. One patch ("Dreaming Anew")
drives the engine hot enough to clip at the DAC - upstream does the same.

## Status

**First hardware run: it droned.** The cause was the neutral position of the
oscillator coarse tuning. `Voice.h` feeds the oscillators with `midiNote - 93`;
upstream compensates for that in `processOsc1Pitch()`, which maps the normalized
parameter onto `val * 48` - so the centre is at 24, not at 0. `OB_Engine` did
not set `pitch1` at all, it stayed at its declared default of 0, and everything
sounded **two octaves too low**. Fixed; `OB_OSC1_PITCH` is now a parameter of
its own.

**Second run: it plays, peak 91 % with 6 of 6 voices.** That is roughly 2,100
cycles per voice and sample - far more than the arithmetic suggested. The reason
could be looked up in the symbol table: `renderBlock()` was in RAM, but GCC had
not pulled `Voice::ProcessSample` (3.6 KB) and `OscillatorBlock::ProcessSample`
(18 KB) into it and had left them as functions of their own in flash. So the
entire DSP still ran over XIP - and 18 KB of code does not fit into a 16 KB XIP
cache when it is traversed six times per sample.

Both are now marked with `__not_in_flash_func()`, as are the three noise
generators. In addition, two divisions per sample and voice were removed from
the 4-pole filter that the arithmetic already had on hand: `1/(1+g)` is
`1 - lpc`, and `g/(1+g)` is `lpc`.

That costs 22 KB more RAM (code moves out of flash); the overall picture is now
68 KB of flash text, 47.6 KB of `.data` and 39.9 KB of `.bss`.

**Third run: peak 53 % with 6 of 6 voices.** So it was the XIP cache: 38
percentage points.

## Why 44.1 kHz and not more voices

The headroom that freed up goes into the sample rate, not into polyphony. Two
reasons, both of them in the ported code:

- The filter's resonance compensation is written around 44 kHz
  (`sqrt(44000.f / sampleRate)` in `Filter.h`). At 44.1 kHz the filter works at
  its own design point.
- The cutoff frequency is capped at `sampleRate * 0.5 - 120`. At 32 kHz that is
  15.9 kHz - below the 19 kHz the code otherwise allows. So the filter never
  reached its upper end.

Expected load: 53 % x 44100/32000, about 73 %. Measured: **78 % peak with 6 of 6
voices**, and the sound is right. The alternative would have been 8 voices at
32 kHz (about 71 %) - the two together are not possible.

**On the long decay of load and voice count:** not a bug. The envelope times of
the factory patches go through `logsc(v, 8, 60000, 900)`, so up to 60 seconds.
"5 AM Pad" has a 45.7 s release and a 60 s decay on the amp envelope; a voice
lasts that long, and so does the load. With six voices that means anyone playing
more than six notes within one release phase steals voices from themselves. The
allocator then takes the quietest voice that has already been released.

The adjustment screws are one line each: `kSampleRate` in
`src/OB_Instrument.cpp`, `MAX_VOICES` in `include/obxf/ObxfPort.h`.

## The review round (August 2026)

A pass against the OB-Xf sources and the original OB-X manuals (owner's +
service manual) found three real bugs and a set of control laws that had
drifted from upstream. All fixed; `tools/host_tests/ob/` now proves the
audible ones on the host. Settings version is 3 - older records are discarded
on first boot.

**Bugs.** Pitch bend was dead: `Voice.h` multiplies by `par.extmod.pbUp/pbDown`
and nothing ever set them. They are now driven by a new **Bend Range**
parameter with the two positions of the original bend assembly's Narrow/Broad
switch - 2 or 12 semitones, the service manual's calibration points (0.167 V /
1.000 V). Like the hardware switch it is not part of a program; presets leave
it alone. - `OB_LFO_TO_CUTOFF` had upstream's semantics wrong: upstream
`par.lfo1.cutoff` is a routing gain in [-1, 1] and the depth comes from
`amt1`, so the port's `v * 60` was 60x too big AND silently gated by the
LFO-to-pitch depth. The port now gives each LFO target its own depth: the
cutoff term in `Voice.h` drops the `amt1` factor (documented there) and the
parameter maps through upstream's amt1 curve. - The menu opened its
three-entry list with a count of 2, so "<< BACK" did not exist; you left the
menu by waiting five seconds.

**Control laws pulled back to upstream**, so factory patch values mean the
same sound: resonance (reverse-log, not linear - '32 Rez Bass gets its squelch
back), brightness (7000..26000), LFO rate (logsc 0..250 Hz; the original tops
out at 20 Hz anyway, service manual trimmer T5), filter slop (18 semitones)
and level slop (0.67). The voice slop knob now also drives the per-oscillator
tuning scatter (`unisonDetune * tuningSlop`, upstream's curve) - the term was
already in the pitch path, it costs nothing, and that drift IS the old
Oberheim character.

**The modulation lever is additive now**: programmed LFO depth always applies
in full (before, a patch played with the wheel down got 20 % of it), and the
lever adds up to half a semitone of vibrato on top - which is how the
original's lever sits on the panel DEPTH control.

**Also**: power-on default is the 2-pole filter (the OB-X has no 4-pole; that
mode belongs to the OB-Xa/OB-8 side of OB-Xf and stays available), and the
per-voice LFO2 of the upstream engine - which nothing here can drive - is
removed from `ProcessSample`, about 800 bytes less RAM code and measurable
load off the render path (measured: 78 % -> 74 % peak with 6 of 6 voices).

## The preset import (settings version 4)

For the full factory bank the parameter set grew by ten: Env->Pitch and
Env->PW (bipolar, the invert switches fold into the sign - as does the filter
envelope's, so **Env Amt is bipolar now** with 0.5 neutral), the BothOscs
switches for the two, PW Offset (osc2), Noise Colour, LFO->Volume tremolo,
the 2-pole bandpass blend and the two Xpander controls (enable + 15 pole-mix
modes). None of them cost a cycle: every one drives a field the ported
Voice/Filter code has computed since day one - the port had the machinery of
the whole OB-Xf voice on board and simply never plugged these in. Like the
cutoff route before, the tremolo depth is decoupled from the shared amt2 in
`Voice.h` (documented there). The enum was reordered into panel sections at
the same time, hence settings version 4.

The UI gained a category level for the 351 patches: Menu -> Presets lists the
18 categories, each opens its own list, both with "<< BACK" and the idle
timeout back to the panel.
