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

Twelve factory patches from `assets/installer/.../Patches` of the original,
converted into `include/ob_presets.h`. The .fxp files carry their parameters as
named, normalized values in an embedded XML block, so the conversion is a name
mapping onto `ob_params.h`. What this port does not have - unison, panning,
LFO 2, the modulation matrix, velocity tracking - falls away, and the LFO
waveform is rounded to the nearest of our five fixed positions. A patch that
leans heavily on any of that will not sound identical here.

Reachable under Menu -> Presets.

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
