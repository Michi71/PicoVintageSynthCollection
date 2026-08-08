# jv_extract - JV-880 ROM extraction and parameter analysis

Host-side toolchain for a planned PicoFaceJV. Nothing here is part of a firmware
image, and no ROM data is in this repository.

The goal is **not** a cycle-exact emulation of the JV-880's CPUs. It is a native
engine that plays the converted PCM data and sounds like the original. That
requires two things from the ROMs: the sample data, and the meaning of the patch
parameters. This directory produces both.

## ROM images

You need your own JV-880 ROM set. Point the tools at a directory containing:

| File | Size | Purpose |
|---|---|---|
| `jv880_rom1.bin` | 32 KB | boot ROM, needed only by the reference emulator |
| `jv880_rom2.bin` | 256 KB | firmware; holds the multisample, sample and patch tables |
| `jv880_waverom1.bin` | 2 MB | wave data, scrambled |
| `jv880_waverom2.bin` | 2 MB | wave data, scrambled |
| `jv880_nvram.bin` | 32 KB | needed only by the reference emulator |

The SR-JV80 expansion boards are deliberately out of scope.

## Wave data format

Each 1 MB ROM page is self-contained:

```
  page + 0x00000 .. 0x07FFF   exponent nibbles, one per 16 samples
  page + 0x08000 .. 0xFFFFF   8-bit signed DPCM deltas
```

A sample is reconstructed by accumulating `delta << exponent`; the nibble for
address `a` sits at `(a & 0xFFFFF) >> 5` with bit 4 selecting high or low nibble.
Both wave ROMs are additionally scrambled by a fixed 20-bit address and 8-bit
data permutation (`descramble()` in `jv_rom.py`).

Verification: all 577 samples decode without a single clipped accumulator,
median peak 7.7 % of 20-bit full scale. Sample 0 (lowest split of "Ac Piano 1",
root key 40) has a loop period of 382 samples, implying 31.5 kHz -- the JV-880's
native rate.

## Tables in rom2

| Table | Offset | Layout |
|---|---|---|
| Multisamples | `0x0004` | 129 x 60 B: 12 B name, 15 split keys, 16 x uint16be sample index |
| Samples | `0x1E41` | 577 x 18 B: start/loop/end (24 bit), root key, tune, level |
| Patches | `0x008CE0` / `0x010CE0` / `0x018CE0` | 3 banks x 64 x 362 B |

A patch is 12 B name + 14 B common + 4 x 84 B tone. See `jv_tone_map.h`.

## Tools

| File | What it does |
|---|---|
| `jv_rom.py` | descrambling, table parsing, DPCM decoding, WAV dump. Importable; run directly for a ROM-set summary. |
| `jv_probe.cpp` | differential probe harness. Reads probe specs on stdin, renders each on the reference emulator, writes one CSV feature row per probe. |
| `jv_analyze.py` | `effects` / `curves` / `shape` views over that CSV. |
| `jv_tone_map.h` | the patch/tone layout, with each field marked VERIFIED or UNVERIFIED. |
| `jv_fx_taps.cpp` | address trace of the effect chip's delay memory. Run it through `build_fx_taps.sh`; see "The reverb, and where it stops". |
| `build_fx_taps.sh` | generates a hooked copy of the emulator's `pcm.cpp` into a scratch directory, builds the trace against it, runs it. |

### Building the probe

`jv_probe.cpp` links against the reference emulator, which is **not** vendored
here (see "Credit" below). Point it at your own checkout of
`giulioz/jv880_juce`; it needs only the emulator core, not the JUCE-coupled ROM
loader, because it brings its own:

```bash
JV=/path/to/jv880_juce/Source/emulator
c++ -O2 -std=c++17 -I"$JV" -I"$JV/resample" -o jv_probe tools/jv_extract/jv_probe.cpp \
    "$JV"/mcu.cpp "$JV"/mcu_opcodes.cpp "$JV"/mcu_interrupt.cpp "$JV"/mcu_timer.cpp \
    "$JV"/pcm.cpp "$JV"/submcu.cpp "$JV"/lcd.cpp "$JV"/resample/*.c
```

The emulator chatters on stderr while it boots; redirect it (`2>/dev/null`) or
the noise lands between the CSV rows.

### Running a sweep

Probe specs are `<label> <tone> <offset> <value>`. Lines labelled `#base` are
applied to the patch before every probe. Offset `-1` means "no modification".
A negative tone index addresses the patch bytes directly instead of a tone.

```bash
{ printf '#base 1 0 0\n#base 2 0 0\n#base 3 0 0\n'
  for o in $(seq 52 66); do for v in 0 16 32 48 64 80 96 112 127; do
    echo "P_${o}_${v} 0 $o $v"      # byte set on the live tone
    echo "C_${o}_${v} 3 $o $v"      # same byte on a switched-off tone
  done; done
} | ./jv_probe /path/to/roms 52 48 100 > sweep.csv

python3 tools/jv_extract/jv_analyze.py curves sweep.csv
```

## Two traps

Both of these produced confident, completely wrong results before they were
found. Anyone extending this work needs to know them.

**1. `SC55_Reset()` does not reset the emulator.** PCM, timer and resampler
state survive it, and so does `samplesError`. Results became order-dependent.
The harness therefore constructs a **fresh `MCU` per probe**. With that, four
repetitions of the same probe render bit-identically -- the noise floor is
exactly zero, so any difference at all is real.

**2. The firmware's patch-parse path takes different amounts of time depending
on which byte was touched**, which shifts the whole render by a few samples.
Summary features (level, pitch, release) cannot tell that apart from a genuine
parameter change: 27 unrelated offsets showed one identical phantom signature.
It surfaced only through a control experiment -- setting the same byte on a
*switched-off* tone produced a byte-identical render, and a silent tone cannot
change the sound.

Hence every measurement is **paired**: the byte is set both on the live tone and
on a switched-off one. The parse-timing shift is identical in both, so a
byte-identical render means the parameter is inert, and any difference is a real
effect. This is what `jv_analyze.py` compares -- never a probe against a global
baseline.

A consequence worth planning for: a parameter can only be measured on a base
patch that actually uses its section. A pipe-organ patch leaves 49 of the 84
tone bytes inert. Pick base patches by section usage, not by name.

## Calibration

Knowing *which* byte is the release time is not enough to build an engine; it
has to know that value 80 means 3.2 seconds. `JV_DUMP=<dir>` makes `jv_probe`
write the raw stereo render of every probe (float32 interleaved, note-off at
frame `JV_HOLD * 32000`), and `JV_HOLD` / `JV_REL` set the render length in
seconds. `jv_calibrate.py` turns those dumps into `jv_calibration.h`.

Results are in [`jv_calibration.h`](jv_calibration.h). The headline findings:

| Quantity | Law | Valid range |
|---|---|---|
| Envelope segment, falling | `t = 56.7 ms * 2^(v/13.98)` to −40 dB, R²=0.996 | v=16…112 |
| Envelope segment, rising | `t = 67.6 ms * 2^(v/14.06)` to 98 %, R²=0.997 | v=16…112 |
| TVF cutoff | `fc = 431 Hz * 2^(v/17.93)`, 67 cents/step, R²=0.984 | v=8…84 |
| LFO rate | `f = 0.122 Hz * 2^(v/18.20)` | whole range |
| LFO delay / fade | `32.3 ms * 2^(v/12.0)` / `17.5 ms * 2^(v/10.5)` | v≳40 |
| TVA level, pan | tables (not a clean power law) | full |

**Envelope shape is direction-dependent** and this matters more than the times:
rising segments ramp *linearly in amplitude*, falling segments decay *linearly
in dB*. Both were measured at ten equal time points across several rates; the
rising shape is identical at every rate.

**The time is the segment's DURATION, not a rate.** At T1 = 80 the attack takes
about 3.4 s whether its target level is 64, 96 or 127 — a constant rate would
have given 1.7 / 2.6 / 3.5 s. This matters far beyond the timing: a stage whose
target equals the current level is a *hold*, and reading the time as a rate
collapses it to nothing. Several factory pads (B19 "Beauty Vox", B21 "Pvox
Oooze") have L1 = L2 = 127 and stuttered because of it.

### The measurement setup is the hard part

Two things silently ruined earlier runs, both worth repeating:

* **The base patch's own LFO.** Pipe Organ (A24) carries `lfo1TvaDepth = 255`.
  Its tremolo swamped the amplitude envelope, so attack curves came out
  oscillating instead of rising. Every calibration base must zero the six LFO
  depth bytes (+31…+36).
* **Reverb and chorus sends.** Left at patch defaults they add a tail that gets
  measured as release time. Set +82 and +83 to 0, +81 to 127.

The base used for all of it is a sine wave (multisample 72), single tone, LFOs
off, sends dry, filter open, envelope levels at maximum — so that the one byte
under test is the only thing moving.

### Envelope target levels

The TVA envelope's target levels (+75/+77/+79) do **not** follow the tvaLevel
curve, which the engine assumed at first. Measured by setting every segment time
to 0 and every target to the same value -- the envelope then holds there -- the
two curves differ by up to **9.4 dB** at low levels. Above roughly L=32 the
envelope curve is linear at 0.30 dB per unit.

The TVF envelope's levels are a different quantity again: they scale the cutoff
excursion and are **near-linear in the parameter**, not logarithmic. Normalised
to L=127 they run 0 / 0.09 / 0.20 / 0.28 / 0.46 / 0.56 / 0.72 / 0.83 / 1.0 at
L=0…127, where the dB curve would give 0.09 at L=32 instead of 0.20.

**Four** level-shaped quantities, and no two share a curve:
`JV_TVA_LEVEL_DB` for tone levels, `JV_PATCH_LEVEL_DB` for the patch-common
level, `JV_TVA_ENV_LEVEL_DB` for TVA envelope targets, `JV_TVF_ENV_LEVEL` for
TVF envelope targets. Assuming one curve for all of them was wrong four times
over; each was settled by patching the byte and reading the sustained level.

And a fifth candidate that turned out not to be one: **byte +17 of a sample
record is not a level**. Patched across its whole range, including 0, it moved
the output by 0.00 dB. The engine had been multiplying by it, which wrongly
attenuated the 233 of 577 samples carrying anything other than 127 -- by up to
6.4 dB, and one of them to silence.

### Pan does not stop at 127

`tvaPan` (+68) has three regimes, found by sweeping the whole byte rather than
the 0..127 the field looks like:

| value | behaviour |
|---|---|
| 0…127 | the continuous law: 0 left, 64 centre, 127 right |
| **128** | **alternating** -- the balance jumps between 0.119 and 0.768 from one note to the next |
| 129…255 | centre |

**139 of the 539 active factory tones carry 128.** Clamping it to 127, as the
engine did, threw all of them hard right with the left channel 39 dB down.
"Whistle" has 128 on all four of its tones, which is why it measured 14 dB quiet
-- the mono sum had lost a whole channel.

### Tone summing

Not a factor. Enabling the four tones of "Pipe Organ 1" one at a time, the
machine gains +2.3 / +0.3 / +1.2 dB and the engine +2.8 / +0.5 / +0.4 dB, so
there is no hidden per-tone attenuation; only a constant offset separated them.

### Filter chain, measured against the reference

The low-pass slope is **−10 to −12 dB/octave**, so the two-pole assumption is
right, and the transfer function tracks the reference to 1–4 dB across four
cutoff settings. The filter is not where the remaining timbre error lives.

Worth recording as a method note: an earlier comparison normalised each spectrum
to its own maximum and read single FFT bins, which suggested a 26 dB error at
1 kHz. Measured absolutely and in third-octave bands, the same patch is within
1.8 dB up to 4 kHz and only +3.5/+7.4 dB at 8/12 kHz -- consistent with the
chip's three-coefficient interpolation over the deltas against the engine's
linear interpolation, which is a real but much smaller difference.

### Where it is incomplete

* **Cutoff above v≈84** cannot be measured this way: the corner moves past the
  bandwidth of the white-noise sample used as excitation. A wider excitation
  (or a swept sine) would extend it.
* **Flags bit 5** of either LFO (see "Key sync and offset" above).
* **tvfEnvDepth (+58)** moves the cutoff by 2.4 parameter units per depth unit
  (measured 19.1 / 37.5 / 60.5 at depth 8 / 16 / 24, with the envelope held
  open); above 24 the measurement saturates, not the synth. The field is signed.
* **tvaVelocity (+72) only ATTENUATES.** Measured against sensitivity 0 at the
  *same* velocity -- a comparison the first calibration never made -- every
  setting gives an identical level at velocity 127, and softer notes get quieter
  in proportion: -1.56 / -3.10 / -4.91 dB at velocity 100 for sensitivities
  8 / 16 / 25, and -3.59 / -7.52 / -11.62 dB at velocity 64. The attenuation
  tracks the product `sensitivity * (127 - velocity)`.

  RE-MEASURED on a full grid -- nine sensitivities against eight velocities,
  63 usable points -- after a listening report that middle velocities came out
  too quiet. The product model itself holds: three independent settings near
  product 1500 give 9.82 / 10.33 / 10.51 dB and near 2000 give 13.01 / 13.01 /
  13.51. What was wrong was the curve through them. The old table was built
  from just two velocities, 100 and 64, and ran 1 to 3 dB too steep through the
  middle of the range -- exactly where MIDI files live -- and badly steep at the
  top: 45.25 dB against the machine's 37.78 at product 3969.

  With the new table the engine reproduces the grid to a mean absolute error of
  **0.33 dB** over all 63 points, against roughly 1 dB before.

  Worth being clear about what this did NOT fix. Measured per patch, the drop
  from velocity 127 to 64 is now right on ten patches and slightly too shallow
  on eleven; before it was uniformly a little too steep. The isolated law being
  accurate to 0.33 dB while whole patches still scatter by about a decibel says
  a second mechanism is involved -- most likely the TVF velocity sensitivity,
  which darkens the tone at lower velocity and takes level with it. That is the
  next thing to measure.

  The first model boosted above velocity 64 instead. Because factory patches
  carry sensitivities of 13..42, that spurious boost had been standing in for a
  10.5 dB base offset in the output normalisation, and the two together looked
  acceptable in aggregate while scattering wildly per patch. Fixing the law and
  moving the normalisation cut the spread across 24 patches from 5.1 dB to
  2.7 dB. The lesson is in the comparison that was missing: every velocity
  measurement had been relative to velocity 64 at the same sensitivity, so a
  term that shifts the whole curve was invisible.
* **LFO depths are signed over the whole byte.** 64..127 really is inert, but
  128..255 is -128..-1 and modulates the *other way*: pitch depth -20 gives
  133 cents of swing around a mean above the carrier, +20 gives 136 cents around
  a mean below it. An earlier sweep only covered 0..127 and wrongly concluded
  that everything from 64 up was off. On the TVA the negative side is inaudible
  at full level, because there is no headroom to modulate upward into.
* **Resonance** is calibrated (see the matrix section); the LFO's TVF depth
  is not.

## Pitch: the sample table, resolved

The `tune` word had resisted a structural reading, so it was settled the same
way the patch bytes were — by **patching rom2 itself** and measuring. `jv_probe`
takes `#rom <offset> <value>` lines for exactly this; they are applied before
the MCU starts, so any ROM table can be probed the way patch bytes are.

| Field | Law | Evidence |
|---|---|---|
| root key (+12) | exact semitones | −199.9 / −200.2 cents per +2, five values |
| `tune` (+13/+14, 16-bit BE) | **0.1 cent per unit, neutral at 1024** | low byte 0.098–0.100 cents/unit over six values; high byte 0.1005 |
| `pitchFine` (tone +38) | 1 cent per unit, signed | 0.989–1.001 cents/unit over eight values |
| machine tuning | 9.4 cents flat | constant to 1 cent across 2.5 octaves |

**`end` is inclusive**: a loop spans `end − loop + 1` samples. This was the
thing that made everything else look inconsistent. Treating `end` as exclusive
detunes a sample by 1731/looplen cents — 9 cents on a 193-sample loop but 54 on
a 32-sample one — so the error masqueraded as a per-sample `tune` effect that no
single law could fit.

The two readings are cleanly separable. Solving each sine-multisample zone for
the `tune` reference point gives:

| `end` treated as | reference point per zone | scatter |
|---|---|---|
| exclusive | 1207, 1246, 1303, 1371, 1484, 1671 | 157.7 |
| **inclusive** | 1118, 1119, 1119, 1111, 1120, 1121 | **3.3** |

and 1118 − 1024 = 94 units = 9.4 cents, which is exactly the machine's measured
offset from equal temperament. The neutral value really is 0x400 after all;
what was missing was the off-by-one and the global detune sitting on top of it.

Engine error against the reference afterwards: **0.2 to 0.7 cents** across all
nine zones of the sine multisample, down from +22…+105 cents. On real
multi-tone patches the residual is 5 cents mean / 9 cents worst, which is
dominated by f0 estimation over detuned layers rather than by the model.

### LFO

Waveform comes from bits 0-2 of the flags byte, classified by correlating the
measured dB envelope against ideal shapes: **0/6/7 triangle** (0.990),
**1 sine** (0.990), **2 sawtooth** (0.986), **3 square** (0.984). Values **4 and
5 are not periodic** — only 21 % of their samples sit on a smooth slope against
99-100 % for the rest — so both are treated as sample-and-hold.

Depth is effective for **0..63 only**; every value from 64 up reproduced the
unmodulated signal exactly, so it disables the modulation rather than inverting
it. An earlier note calling these fields "bipolar, split at 64" was wrong.

Both destinations modulate **one-sided downward** from the set value: with
rising TVA depth the peak stayed at −15.0…−15.9 dBFS while the trough fell to
−62.5 dBFS, and the mean pitch dropped from 261 to 194 Hz.

An earlier claim here — that no modulation exists below rate ≈48 — was also
wrong. Re-measured with a 46 dB tremolo over 24 s and counting cycles directly,
the LFO runs at every rate value and matches the fitted law to ~1 % from v=40 up;
below that the cycle count is the limit, not the law. The original measurement
had an autocorrelation locking onto its search bound.

#### Key sync and offset

**Bit 6 is key sync.** Told apart by shifting the note-on in time (`JV_WARM`)
and timing the first square-wave edge. With the bit clear, the edge moves so
that edge+shift stays constant modulo the period — a free-running LFO. With it
set, the edge sits at 227.7–237.7 ms regardless of the shift, scatter 3.4 ms
against ~56 ms for free-running.

Phase 0 is the **unmodulated** end. The 227 ms is a useful cross-check on two
earlier results at once: half a period (194.7 ms at rate 80) plus the 32.3 ms
floor of the delay law.

**Bits 3-4 are an offset that shrinks the swing from the bottom.** The peak does
not move (−15.1…−15.2 dBFS across all four settings) and neither does the phase
(first edge 237.6–239.0 ms, duty 49–50 %); only the trough rises. At TVA depth
63 the swing goes 48.7 / 30.6 / 19.4 / 9.9 dB, and the triangle agrees
(48.1 / 29.5 / 18.5 / 9.0). Stored as a scale factor on the depth.

**Bit 5 is not resolved.** Alone it silences the modulation; combined with
either of bits 3-4 the swing collapses to a fixed 19.4 dB regardless of which,
which no plain offset field explains. The engine ignores it.

#### LFO 2 is the same device

Probed separately rather than assumed. Rate follows the same law (within 1 %
from v=56 up; the 12 % at v=32 is cycle-count quantisation, not a difference),
the four periodic waveforms have the same shapes at the same codes, value 4 is
equally aperiodic, depth follows the same table and is equally disabled from 64
up, key sync behaves the same (scatter 45.4 ms free-running against 3.2 ms
synced), and the four offset settings give **bit-identical** swings to LFO 1
(48.7 / 30.6 / 19.4 / 9.9 dB, difference −0.0 dB throughout).

The two also free-run in lockstep: at equal rates their first square edges land
on the same milliseconds.

### Modulation matrix

Its three sources — mod wheel (CC 1), aftertouch, expression (CC 11) — need
controller traffic, which a bare note-on does not provide, so `jv_probe` gained
`#midi <seconds after note-on> <status> <d1> [d2]`. Messages are queued, sorted
and injected by splitting the hold render at their timestamps.

**Sensitivity is signed with an effective range of ±63, and 64…127 disables it
outright** — the same convention as the LFO depths. This was the whole reason an
early sweep found nothing: it used sensitivity 127. The pitch destination scales
linearly at **19.05 cents per unit**, reaching exactly ±1 octave at ±63
(measured 18.3 / 37.3 / 94.4 / 188.3 / 377.3 / 765.3 / 1200.2 cents for
1 / 2 / 5 / 10 / 20 / 40 / 63).

Destination A is the **low** nibble of the DestAB byte, B the high one — which
is what the factory patches imply too (Dig Rhodes 1 has expression DestAB = 0x20
with SensA = 0 and SensB = 20, so the active destination sits in the high
nibble alongside the active sensitivity).

| Code | Effect | How it was told apart |
|---|---|---|
| 0 | off | — |
| 1 | **pitch** | +376 cents; the ×1.55 brightness is exactly the side effect of the shift on a sine |
| 2 | **cutoff** | corner ×2.50 with the filter enabled |
| 3 | **resonance** | spectral peak +4.4/+8.4/+12.6 dB at sensitivity 20/40/63, against +3.5/+9.0/+12.5 dB for resonance set directly to 30/90/127 |
| 4 | **level** | +4.3 dB, nothing else moves |
| 5, 6 | **LFO 1 / 2 pitch depth** | the pitch wobbles rather than steps; source identified by rate |
| 11-15 | nothing measurable | — |
| 7, 8 | **LFO depth → filter** | oscillation index ×45 / ×34 at the LFO rate |
| 9, 10 | **LFO depth → level** | oscillation index ×73 / ×57 |

The LFO destinations were separated from the static ones by running the LFO at a
known rate with all depths at zero and testing whether the controller introduces
a periodic component at that rate. It is an unambiguous split: the four LFO
codes jump 34–73×, everything else stays at or below 1.1×.

Which LFO each of 7-10 drives was settled by giving the two LFOs different rates
and seeing which one the induced oscillation follows: **7 = LFO 1 filter,
8 = LFO 2 filter, 9 = LFO 1 level, 10 = LFO 2 level**. Adding sensitivity 63 at
full travel reproduces full depth (47.7 dB against the depth table's 46.6), so
the matrix adds straight onto the depth parameter.

Scalings, all with the wheel swept 0 -> 127: pitch **19.05 cents/unit** (exactly
±1 octave at ±63), cutoff **~128 cents/unit**, level non-linear and saturating
near **+6.3 dB** (1.29 / 2.37 / 4.31 / 6.28 dB at sensitivity 5 / 10 / 20 / 40).

Destinations 5 and 6 took two attempts and are worth a warning. Averaged over a
long window their pitch looks like a small static shift, which is what the first
sweep concluded; tracked in 0.12 s windows it plainly wobbles instead of
stepping. At sensitivity 40 the swing reaches 4000 cents, enough to make an
autocorrelation tracker jump octaves and smear the rate across both LFOs — only
at sensitivity 4-8 does the source come out unambiguous. Destination 6 had been
written off as "nothing measurable" for the same reason.

The matrix's pitch-depth contribution is linear **in cents** (55 and 122 cents
at sensitivity 4 and 8), not in depth-parameter units as the level and filter
depths are.

Resonance needed the engine's filter damping calibrated as well: peak gain
against the passband is +1.0 / +3.5 / +6.2 / +9.0 / +12.5 dB at resonance
0 / 30 / 60 / 90 / 127, which fixes k = 1/Q. The first mapping was a guess and
overshot by 7 dB at the top.

Still not established: whether the B/C/D slots behave like A, destinations
11-15, and behaviour on real multi-tone patches — see below.

#### What the engine does and does not reproduce

Against the controlled synthetic base the engine matches: pitch to 0.6-3.8
cents across the whole sensitivity range including the disable above 63, level
exactly, cutoff to 83-173 cents, LFO-depth destinations to 0.1-3.0 dB.

On **real factory patches it is not verified**. The patches that use the matrix
are decaying Rhodes and piano sounds whose own envelope contaminates a
before/after window comparison — the oscillation detector reads a decay ramp as
modulation, so it cannot separate the two. A10 "SA Rhodes" (destination 10,
sensitivity 20, on three tones) is the concrete case: the reference's level
rises 2.4 dB with the wheel, the engine's falls 2.9 dB, and the measurement is
not good enough to say which is right.

### Zone selection, verified

`splits[k]` is the **inclusive upper bound** of zone k: zone 0 covers notes up
to `splits[0]`, zone k covers `splits[k-1]+1 .. splits[k]`, and the last zone
runs to 127. Verified by repointing one zone's entry in the multisample index
table at the white-noise sample (516) and sweeping notes: 34 boundary tests
across all nine zones of the sine multisample, all correct.

Pitch is the wrong observable for this. A first attempt shifted a zone's sample
root by an octave and looked for the jump; it produced a self-contradictory map
(note 87 apparently in a different zone than 86 and 88) because the f0 estimator
was locking onto harmonics. The noise marker has no such ambiguity — the
separation is absolute:

| | measurements | spectral flatness |
|---|---|---|
| note outside the patched zone | 16 | 0.000000, every one |
| note inside it | 18 | 0.000477 … 1.000000 |

The low readings are not weak detections: the noise sample transposed down two
octaves is no longer spectrally flat, but it is still unambiguously not the sine.

### Velocity curves, and a trap worth naming

The seven velocity curves (+55 bits 0-2 for the TVF, +71 bits 0-2 for the TVA)
were first implemented from the icons the manual prints, which put an `x^2` law
on stored curve 0. That quietly took 3 dB off half the factory patches. The
sweep that replaced it -- a tone forced to a flat sustain at velocity
sensitivity 32, read at nine velocities for each of the seven settings -- gives
`JV_VELO_CURVE` in jv_calibration.h and settles three things:

* All seven curves produce **exactly the same level at velocity 127**. That is
  the anchor, the same one the sensitivity law itself uses.
* **Stored curve 0 is the straight line.** Expressing each curve as "the
  velocity curve 0 would need to reach the same level" makes curve 0 an exact
  identity, which is the right footing because `JV_TVA_VELO_ATTEN_DB` was itself
  measured on curve 0.
* The reparameterisation holds across sensitivities. Repeating the sweep at
  sensitivity 16 reproduced curve 1's effective velocities to within 1.5 units
  wherever both are well conditioned, so the curve warps velocity *before* the
  sensitivity law rather than acting on the result.

Checked against the reference on real patches, the drop from velocity 127 to
100 is 8.76 / 8.22 / 13.71 dB on A37, B06 and B50 against 8.98 / 8.18 / 14.14
predicted with the curve and 4.28 / 3.87 / 7.03 without it. A11 lands between
the two, which is right: it is the one of those patches that mixes curve 0 and
curve 1 across its tones.

**The trap.** Fixing this made the aggregate level error look *worse* -- and it
was the third time in this work that a wrong law and the output normalisation
had silently absorbed each other. A velocity-law error is indistinguishable
from a level offset unless the comparison is made at more than one velocity.
Two habits catch it:

* Always compare at both velocity 127 and something well below it. Before the
  curves the engine was flat at one velocity and 4-5 dB out at the other; the
  single-velocity summary showed neither.
* Match the measurement windows. `jv_probe` reports rms over the HELD portion
  only (`N = nHold`), while `jv_engine_test` reports it over hold plus release.
  Comparing the two directly puts a patch-dependent 3 dB bias on every reading,
  which is enough to hide or invent an error of exactly the size being chased.

With the curves in and the normalisation re-trimmed by 1.0 dB (99000 ->
88234), the level error over all 128 factory patches at velocities 127 and 100
is mean +0.15 dB, spread 3.0 dB, mean absolute 2.32 dB -- and the velocity
TRACKING error, which is what the curves actually fix, falls from a mean of
1.22 dB to 0.72 dB with the worst case down from 6.7 to 5.0 dB.

### The machine is at equal temperament, and the 9.4 cents were counted twice

The engine carried a global `kMasterTuneCents = -9.4`, documented here as a
measured property of the machine. It is not one. The reference playing the
ROM's own sine (multisample 72) sits at **+0.11 / +0.18 / +0.08 cents** against
equal temperament at C3, C4 and C5 -- dead in tune. The engine was 9.65 cents
flat, and removing the constant lands it at +0.12 / +0.19 / +0.13.

9.4 cents is real, but it belongs to the tune-word model: it is the residual
left over when the sample tune words are referenced to a neutral of 1024.
Applying it again as a global detune counted it a second time. The whole
instrument played ten cents flat, which is inaudible on its own and obvious
against anything else.

That is the same shape as the two velocity traps below, and the third instance
in this work: a constant belonging to one part of the chain quietly standing in
for an error elsewhere, invisible until something *outside* the chain is
measured. Here the outside reference was equal temperament itself.

### The loop throb: alternating loops, and a test that lied

**Resolved.** Bit 0 of a sample record's flag byte (+11) marks an **alternating
loop**: playback turns around at the end and retraces the loop backwards rather
than jumping to the start. 85 of the 577 samples set it, and they are the long
sustained ones -- median loop length 8998 against 152 for the rest. Playing
those as forward loops makes the sound repeat exactly once per loop, which is
audible as a throb at the loop rate: 2.43 Hz for the sample under B16.

**The return pass is NEGATED.** The chip does not undo the differential steps
on the way back. It keeps integrating in the same direction while the address
walks backwards, so what comes out is `2*v(end) - v(mirrored)`. The loop
endpoints sit on zero crossings -- `v(end)` is exactly 0 for every sample
checked -- so that is the negated mirror of the forward pass.

Retracing the arithmetically "correct" way, `v(a-1) = v(a) - delta(a)`, gives
the un-negated mirror. Against the reference that reads **r = -0.995** through
the whole return pass: perfectly shaped, exactly the wrong sign. It is audible
as a tick at each turn-around, since the waveform jumps to its own negative
there. Adding the delta instead of subtracting it takes the same comparison to
**+0.99**.

With that, the engine tracks the reference sample for sample: aligning on a
window at 200 ms and then measuring 50 ms windows out to 1.8 s gives r = 0.92
to 1.00 throughout, across four turn-arounds, with the alignment drifting only
from +86 to +90 samples (about a cent of rate difference).

The measured signature, engine against reference, envelope correlation at one
loop period across the seven notes of one multisample zone:

| note | reference | forward loop | alternating |
|------|-----------|--------------|-------------|
| 55 | -0.369 | +0.434 | -0.302 |
| 57 | -0.388 | +0.544 | -0.342 |
| 59 | -0.333 | +0.692 | -0.453 |
| 60 | -0.441 | +0.705 | -0.382 |
| 62 | -0.512 | +0.595 | -0.466 |
| 64 | -0.591 | +0.627 | -0.537 |
| 66 | -0.572 | +0.753 | -0.519 |

And directly: comparing one loop pass against the next, the reference matches
its own **reverse** at +0.899 while matching forward at -0.046. The engine gave
+0.871 forward before this and now shows the reference's signature instead.

**The test that lied.** Alternating looping was the second hypothesis tried and
was wrongly discarded, because the first version of the test correlated one
half-period against the reversed next half-period from an *arbitrary* window
start. The turn-around point sits at the loop boundary, so an unaligned window
straddles a reversal and the correlation collapses -- it read +0.076 and sent
the search off for six other explanations. Scanning the window offset instead
of assuming one makes the peak obvious. When a structural hypothesis is
rejected, check whether the test could see it at all before believing the
result.

Bit 1 of the same byte marks a one-shot: all 42 samples carrying it have
`loop == end`, so the forward path already holds on the last value. Bit 2 is
still unidentified, on 15 samples.

### What it was not

B16 was the clean test case: one tone, every LFO depth zero, no random pitch,
no analog feel, no FXM, pan centre -- a single unmodulated sample, so anything
periodic had to come from the playback. These were eliminated on the way, and
each elimination is still worth having:

* **The chorus.** These patches are built around a deep slow chorus that the
  engine does not have, which was the obvious suspect. But switching the chorus
  off on the reference leaves it just as non-periodic (r at the loop period goes
  from -0.34 to -0.30).
* **The DPCM accumulator, and the decode rule.** The loops are authored exactly
  balanced: the drift over one pass is precisely zero for all three samples
  involved, under every plausible variant of the decode step (`s>>1`, `s`,
  truncate-toward-zero, round-half-up). The `refAtLoop` snapshot the engine
  restores on each wrap is therefore a no-op for these samples, and removing it
  would change nothing.
* **A loop-length parity effect.** An odd loop length would flip the alignment
  of the DPCM exponent nibbles every pass, which would explain a two-pass cycle.
  Patching sample 287's `end` in rom2 to make the length even (12779 -> 12778)
  moves the reference's r only from -0.44 to -0.28. Not it.
* **A discontinuity in the engine.** There is none. Replaying the engine's own
  decode and loop logic step by step across the wrap gives
  `... -872, 0, +888, +1768 ...` -- perfectly smooth -- and the rendered
  waveform either side of the boundary is continuous sample for sample. What
  looked like a step in a 10 ms envelope is a fast but continuous swell that
  sits at the boundary because the sample's own amplitude pattern restarts
  there. That is the whole point: it restarts EXACTLY.
* **The loop being twice as long as read.** The reference's best repeat is at
  831 ms, twice the period, which would fit `end` being misread. It is not:
  the next sample record starts 5 bytes after sample 287 ends, so there is no
  data for a longer loop.
* **A fractional-phase effect.** The engine advances only 0.1 to 0.8 output
  samples per pass depending on note, and its correlation shows no relation to
  that fraction; neither does the reference's.

Each of these was a real elimination, and the sample data being provably
periodic while the address returns to exactly the same place is what finally
pointed at the traversal itself rather than the data.

### Still open

* **Cutoff above v≈84** and the **LFO below v≈48** (see Calibration above).
* **tvfEnvDepth (+58)** moves the cutoff by 2.4 parameter units per depth unit
  (measured 19.1 / 37.5 / 60.5 at depth 8 / 16 / 24, with the envelope held
  open); above 24 the measurement saturates, not the synth. The field is signed.
* **tvaVelocity (+72) only ATTENUATES.** Measured against sensitivity 0 at the
  *same* velocity -- a comparison the first calibration never made -- every
  setting gives an identical level at velocity 127, and softer notes get quieter
  in proportion: -1.56 / -3.10 / -4.91 dB at velocity 100 for sensitivities
  8 / 16 / 25, and -3.59 / -7.52 / -11.62 dB at velocity 64. The attenuation
  tracks the product `sensitivity * (127 - velocity)`.

  RE-MEASURED on a full grid -- nine sensitivities against eight velocities,
  63 usable points -- after a listening report that middle velocities came out
  too quiet. The product model itself holds: three independent settings near
  product 1500 give 9.82 / 10.33 / 10.51 dB and near 2000 give 13.01 / 13.01 /
  13.51. What was wrong was the curve through them. The old table was built
  from just two velocities, 100 and 64, and ran 1 to 3 dB too steep through the
  middle of the range -- exactly where MIDI files live -- and badly steep at the
  top: 45.25 dB against the machine's 37.78 at product 3969.

  With the new table the engine reproduces the grid to a mean absolute error of
  **0.33 dB** over all 63 points, against roughly 1 dB before.

  Worth being clear about what this did NOT fix. Measured per patch, the drop
  from velocity 127 to 64 is now right on ten patches and slightly too shallow
  on eleven; before it was uniformly a little too steep. The isolated law being
  accurate to 0.33 dB while whole patches still scatter by about a decibel says
  a second mechanism is involved -- most likely the TVF velocity sensitivity,
  which darkens the tone at lower velocity and takes level with it. That is the
  next thing to measure.

  The first model boosted above velocity 64 instead. Because factory patches
  carry sensitivities of 13..42, that spurious boost had been standing in for a
  10.5 dB base offset in the output normalisation, and the two together looked
  acceptable in aggregate while scattering wildly per patch. Fixing the law and
  moving the normalisation cut the spread across 24 patches from 5.1 dB to
  2.7 dB. The lesson is in the comparison that was missing: every velocity
  measurement had been relative to velocity 64 at the same sensitivity, so a
  term that shifts the whole curve was invisible.
* **LFO depths are signed over the whole byte.** 64..127 really is inert, but
  128..255 is -128..-1 and modulates the *other way*: pitch depth -20 gives
  133 cents of swing around a mean above the carrier, +20 gives 136 cents around
  a mean below it. An earlier sweep only covered 0..127 and wrongly concluded
  that everything from 64 up was off. On the TVA the negative side is inaudible
  at full level, because there is no headroom to modulate upward into.
* **Resonance** is calibrated (see the matrix section); the LFO's TVF depth
  is not.
* **tvfVelocity (+56)** is calibrated. Measured with the TVF envelope held wide
  open over a base cutoff of 8, reading brightness back as an effective depth:

      positive sensitivity   f = 1 - (s/32) * (127 - v)/127
      negative sensitivity   f = 1 - (|s|/32) *  v /127

  clamped to 0..1, fitted over 96 points to a mean error of 0.010. The constant
  is **32, not 63** -- sensitivity 32 already takes the depth to zero at
  velocity 0, so the upper half of the field saturates inside the velocity span.
  Assuming 63 by analogy with the other bipolar fields made the effect half as
  strong as the machine everywhere. Positive sensitivity pivots at velocity 127
  like the TVA law; negative pivots at velocity 0.

  Note the measurement trap: brightness stops resolving above envelope depth 32,
  so the first sweep -- run at depth 40 -- read a flat 0.0187 across the whole
  useful range and looked like no effect at all. Depth 28 keeps every reading on
  the curve.

## What the owner's manual adds

The JV-880 owner's manual (Roland, 244 pages, scanned -- no text layer, so it
has to be read as images) turned out to carry two things the probe could not
produce on its own. Section 10's **parameter address map** (printed 10-38 f.)
lists the SysEx view of a tone: 116 addresses, `0x00`..`0x73`, five of them
nibble pairs, which is the same parameter set as the 84 packed bytes here. That
names every field and gives its range. Section 6 then explains what each one
actually does.

The manual is documentation, not measurement: where the two collide the
measurement wins. Its value here was in saying where to point the probe next,
and most of the list below has since been implemented and checked against the
reference. What that pass achieved, over all 128 factory patches:

| metric (vs reference)                  | before | after |
|----------------------------------------|--------|-------|
| level, mean absolute error              | 2.45 dB | 2.33 dB |
| velocity tracking error, 127 -> 100     | 1.22 dB | 0.72 dB |
| brightness at velocity 64, mean abs err | 10.19 dB | 4.93 dB |

**Resolved outright.**

* **The modulation matrix destination table is complete at 13 entries**, 0..12:
  OFF, PITCH, CUTOFF, RESONANCE, LEVEL, PITCH LFO1, PITCH LFO2, TVF LFO1,
  TVF LFO2, TVA LFO1, TVA LFO2, LFO1 RATE, LFO2 RATE. Codes 13..15 do not
  exist, so the sweep that found nothing there was right. Dest 6 and 11/12 read
  as dead for a duller reason: the base patch had LFO2 pitch depth at zero and
  no LFO running, so there was nothing for them to scale.
* **LFO offset has five settings, not four** (-100/-50/0/+50/+100), so it needs
  three bits and **bit 5 of the LFO flags byte is its top bit** -- which is
  exactly why sweeping bit 5 on its own looked inert. That also means the
  remaining flag is fade polarity IN/OUT.
* **Offset is a shift, not a scale.** `JV_LFO_OFFSET_SCALE` shrinks the swing;
  the manual shows the waveform being pushed until it sits wholly above (+100)
  or wholly below (-100) the centre, at full amplitude. Applied to pitch with a
  square wave that is a one-sided trill, not a weaker vibrato.
* **Fade OUT** runs the LFO at full depth from note-on and fades it away over
  the fade time -- the engine only implements fade IN.
* **LFO delay and tone delay both have a KEY-OFF setting past 127**, holding the
  LFO (or the whole tone) until the key is released.
* **+54 and +40 are keyfollow pairs.** +54 packs cutoff keyfollow (0..15,
  -100..+200 about C4) with the TVF envelope's time keyfollow; +40 packs pitch
  keyfollow (0..15, +100 = the normal octave per twelve keys) with a TVA time
  keyfollow. Cutoff keyfollow is the consequential one: without it every tone
  filters at the same absolute frequency across the whole keyboard.
* **+71 is tone delay mode plus the TVA velocity curve.** NORMAL delays the tone
  and still lets it sound after release, HOLD drops it if the key goes up first,
  PLAY-MATE uses the gap between the last two note-ons as the delay time.
* **+72's upper half is probably the negative side.** The manual gives TVA
  velocity sensitivity as -63..+63, where negative makes *harder* playing
  quieter. The measurement covered only the positive half and called the rest
  inert -- the same shape the LFO depths had before the signed sweep found
  128..255 modulating the other way. Worth the same sweep.

**Since implemented.** How many of the 539 active factory tones each one
actually reaches is what decided the order:

1. **TVF envelope velocity (+56)** -- 336 tones. Measured; see "Still open"
   above for the law. This is what moved the brightness error.
2. **Velocity curves** (+55 and +71, bits 0-2) -- 204 tones on the TVF, 168 on
   the TVA. Measured; see the section above.
3. **Cutoff keyfollow** (+54 low nibble) -- 185 tones. At +100 % the corner
   tracks the keyboard one for one about C4, which falls straight out of the
   already-measured cutoff law: 17.93 parameter units per octave.
4. **TVF envelope release level (+66)** -- 95 tones. The TVF envelope releases
   to its own fourth level, not to zero; releasing to zero shut the filter on
   note-off while the TVA tail was still sounding.
5. **Level and panning keyfollow** (+70 low, +39 high) -- roughly 95 tones
   each. Per-semitone amounts are NOT measured, only the table indices.
6. **The pitch envelope (+40..+51)** -- 68 tones, 33 of them at full depth.
   Depth is signed semitones clamped to ±12; the four levels are bipolar where
   the TVA and TVF levels are unsigned. Runs at control rate.
7. **Pitch keyfollow** (+40 low) -- 32 tones. 507 sit at +100 %, the normal
   octave per twelve keys.
8. **Output dry level (+81)** -- 31 tones, six at or near zero. Those are heard
   only through the effects on the hardware, so with no effects yet they drop
   out; no patch loses all four tones that way.
9. **Random pitch (+39 low)** -- 18 tones -- and **analog feel** (patch common,
   119 of 192 patches). Both exist to stop tones phase-locking; the analog-feel
   magnitude is a guess, flagged in jv_calibration.h.
10. **Per-patch bend range** -- 24 patches bend down further than they bend up.
11. **LFO offset as a three-bit field** and **fade polarity** -- five tones and
    two tones respectively, but see the offset note in jv_calibration.h: the
    neutral index is 2, not 0, which is why the depth tables and the offset
    scale had been cancelling each other unnoticed.

12. **FXM (+02)** -- 24 tones. Measured; see jv_calibration.h. The engine's
    sidebands now sit within 0.3 dB of the reference across the depth range
    (-30.8 / -16.8 / -11.5 / -5.9 dB at depth 0 / 4 / 8 / 15 against
    -30.8 / -16.8 / -11.5 / -6.2).

**Still not implemented.**

* **POLY/SOLO and portamento** (11 and 6 patches). Needs a note stack and a
  glide; the engine is poly-only.
* **Resonance mode SOFT/HARD** (+53 bit 7, 58 tones).
* **Tone delay** (+69/+71) including the HOLD and PLAY-MATE modes.
* **Envelope time keyfollow and the T1/T4 velocity fields** (+42, +57, +73,
  plus the high nibbles of +40/+54/+70). Almost all neutral in the factory
  banks: +42 is (7,7) on 537 of 539 tones.

**Confirmed, no change needed.** The TVA envelope's shape (T1→L1, T2→L2,
T3→L3, hold, then T4→0 with no L4, while the TVF envelope does carry L4);
velocity range lower/upper as a per-tone window; pan L64..63R with 128 as the
setting the panel calls RND; patch level, tone level and the sends all being
independent 0..127 controls.

## The chorus

Measured by isolating it: the tone's dry level at 0 and its chorus send at 127
makes the output nothing BUT the chorus, and cross-correlating that against a
dry render of the same note recovers the delay as a function of time. The ROM's
own white noise (multisample 74) is the source; the sine (72) is better for
anything to do with pitch.

It is a **stereo modulated delay**, base delay 578 samples (18.06 ms), the two
channels sweeping in **antiphase** with a common minimum -- the modulation only
ever lengthens. Depth sets the delay SLOPE, rate sets the LFO period, and the
excursion follows:

    slope     = 1.738 * (depth + 18) samples/s   (252 at full depth)
    f         = 126 / (183 - 1.344 * rate) Hz    (0.69 Hz at 0, 10.2 at 127)
    excursion = slope / (2f)

That reproduces all sixteen measured rate points and four depth points to about
2 %, and correctly predicts that the period does not depend on depth (760 ms at
rate 64 for every depth, measured). Level is exactly linear in the parameter
with a full scale of 1.30; feedback is a comb with `g = 0.72 * fb/127`; bit 7 of
the level byte routes the chorus into the reverb instead of the mix, which
measured as exact silence with the reverb turned down.

Verified against the reference on a sine carrier: peak-to-peak pitch deviation
13.6 / 20.7 / 32.3 cents at depth 32 / 64 / 127 against 13.5 / 20.9 / 32.5, and
level within 0.07 dB.

Three traps, all of which cost time here:

* **The reference's effects DSP needs about 1.5 s from reset.** With the usual
  1 s warm-up the chorus appears to start 454 ms after note-on; with
  `JV_WARM=4` it starts after 20. Every effect measurement needs the longer
  warm-up.
* **Counting zero crossings is a bad period estimator.** It gave alternating
  values that read as a depth-independent constant slope, which is wrong.
  Autocorrelation with proper peak finding -- first local maximum after the
  correlation goes negative -- gives a clean answer.
* **Correlating white noise needs a lag step of 1 and a NORMALISED score.**
  Stepping the lag by 4 misses the peak entirely, and maximising the
  unnormalised correlation tracks the local energy of the dry signal instead of
  the delay. Both produced "no peak anywhere" against a chorus that was in fact
  correct.

**CHORUS2 is the same delay, deeper and faster.** It was written off earlier as
"not a plain modulated delay" because cross-correlation against the dry signal
found no peak at any lag from 0.3 to 125 ms. That was the measurement, not the
effect: at these slopes the delay moves 24 samples inside a 16 ms analysis
window, which smears the peak away entirely. A sine carrier settles it, because
it does not care how fast the delay sweeps -- only where the pitch ends up.
Measured at depth 127: 195.6 cents of peak-to-peak deviation against CHORUS1's
32.5 at every rate, and a period of 576 / 416 / 224 ms against 1120 / 768 / 448
at rate 32 / 64 / 96 -- half, to within 4 %.

Two notes on getting the engine to match it. The slope multiplier that works is
8, not the 6 the cents ratio implies, judged by energy-weighted spectral spread:
10.14 and 15.80 Hz against the reference's 9.27 and 14.79 at depth 64 and 127.
Spread is the measure to trust here -- it needs no peak tracking and reproduces
CHORUS1 to within 8 %, where the pitch tracker gave answers that changed with
the analysis window. And the delay buffer had to grow from 768 to 1280 samples:
CHORUS2 sweeps three times as far as CHORUS1, so the excursion clamp was biting
silently and costing it a third of its deviation.

And one real bug it exposed: reading the delay line at `pos - delay` truncates
toward zero when that goes negative, which yields a NEGATIVE interpolation
fraction and makes the interpolator extrapolate backwards. With the write
pointer cycling 0..767 and the delay at 578..761 it happened three quarters of
the time. Biasing the read position up by a whole buffer length fixes it.

## The reverb, and the two delays hiding in it

Same isolation as the chorus -- dry level 0, reverb send 127, chorus off -- but
with a 50 ms burst and a five second tail, so the render is essentially an
impulse response. All six reverb algorithms come out with the two channels
DECORRELATED: |L/R correlation| < 0.04 over the tail, every type.

**The two delay types are exact.** Measured, not modelled:

    delay time = 2.0 + 3.843 * time ms    (2 / 63 / 124 / 185 / 250 / 311 /
                                           372 / 433 / 490 at time 0..127)
    feedback   = fb/128 - 1/64            (per-repeat ratios 0.234 / 0.484 /
                                           0.734 / 0.984 at fb 32/64/96/127)

PAN-DLY is **two taps on one line, not one tap that alternates sides**. At
feedback 0 the reference already produces two echoes -- 62 ms left and 124 ms
right at time 32 -- so the half-period tap feeds the left channel and the
full-period tap the right, with the feedback going round the full period. That
is why the levels arrive in equal pairs. The engine now reproduces the whole
train: 64 / 126 / 188 / 250 / 316 / 378 ms alternating sides against the
reference's 62 / 124 / 184 / 246 / 306 / 368, decay ratio 0.484 against 0.485,
and peak level 0.1660 against 0.1659.

**The six reverbs are matched, not reproduced.** RT60 is measured per type at
five time settings (`JV_REVERB_TIME_MS`) and the engine runs a Schroeder
comb/allpass network sized to hit it, landing within 1 % everywhere:

| type | t=0 | t=64 | t=127 |
|------|-----|------|-------|
| ROOM1 | 281 [276] | 618 [614] | 1130 [1124] |
| STAGE2 | 322 [318] | 1362 [1354] | 5460 [5420] |
| HALL1 | 527 [524] | 1203 [1196] | 3603 [3583] |
| HALL2 | 407 [404] | 1303 [1296] | 6625 [6572] |

The topology is not the chip's, and it shows in the residual: tail level is
2.4 dB out on average (3.4 dB absolute), worst on the room types at very short
decay, where the reference's level falls away faster than a Schroeder network's
does. Reverb time in the factory banks has a median of 80 and only 12 of 125
patches sit below 32, so the fit is good where it is used.

**The reverb type is bits 0-2, not the low nibble.** The manual gives the range
as 0-7 and bit 3 of patch-common +12 is something else, set on 40 of the 192
factory patches. Masking with `0x0F` handed 67 of them a type of 6 or 7 -- DELAY
or PAN-DLY -- so a third of the bank played a hard echo where it should have had
a room or a hall. It sounded exactly like that, and it survived every level and
decay measurement because those were all made by setting the type byte
explicitly to 0..5.

**The tail level is right in isolation and low on real patches.** With the level
table fitted, the isolated reverb matches the reference to 0.0 dB across every
type and time. On A04, whose piano samples decay through the note, the engine's
tail sits about 13 dB low. The reverb is linear, so this is not a gain error: a
comb bank driven by a decaying input accumulates less than the chip's network
does. Same root cause as the burst measurement being 12-41 dB quiet while a
flat-sustain note was 5.7 dB loud -- the build-up differs, and no single gain
fixes both. Open.

Two things worth carrying forward:

* **The two channels need genuinely different comb sets, not a stereo spread.**
  Nudging the right channel's combs by a dozen samples (809/823, 877/887, ...)
  left the engine at +0.85 correlation, because both sides see the same input
  and nearly the same delays. Separate sets -- 809..1049 against 1123..1381 --
  bring it to +0.01.
* **`jv_engine_test` renders whatever hold you ask for now** (`--hold`,
  `--tail`). It used to be fixed at 2 s + 2 s, and comparing that against a
  50 ms burst on the reference smeared every echo into one blob. It looked
  exactly like PAN-DLY putting both taps on both channels, which sent the
  implementation off after a bug that was not there.

## Playing material that was not written for the JV

The machine's velocity response is steep -- across 25 factory patches the median
drop from velocity 127 to 64 is 11.7 dB, and A43 Syn Strat drops 19.6. That is
faithful, and it is also awkward: a sequencer file whose velocities sit between
60 and 100 comes out thin.

The firmware therefore has a VELO page. It scales the incoming velocity toward
127 before the engine sees anything, so 100 % passes it through untouched, 50 %
halves the distance to 127 and 0 % makes every note full strength:

| setting | v=48 | v=64 | v=80 | v=100 | level at v=64 |
|---------|------|------|------|-------|---------------|
| Orig | 48 | 64 | 80 | 100 | -- |
| 75 % | 68 | 80 | 92 | 107 | +3 to +4 dB |
| 50 % | 87 | 95 | 103 | 113 | +6 to +8 dB |
| 25 % | 107 | 111 | 115 | 120 | +11 dB |
| 0 % | 127 | 127 | 127 | 127 | +15 to +17 dB |

The mapping happens before the engine, so the velocity curves, both velocity
sensitivities and the per-tone velocity windows all act on the same value.
That last one is the side effect to know about: compressing upward will also
bring in tone layers a patch reserves for hard playing.

## MIDI: what the machine receives

The manual's MIDI implementation (printed 10-32 f.) lists exactly what the
JV-880 accepts, and the firmware now covers that list rather than the handful
of controllers it started with:

| CC | what it does | note |
|----|--------------|------|
| 0 | bank select, MSB only | 80 user, 81 preset; latched until a program change |
| 1 | modulation | matrix source |
| 5 | portamento time | overrides the patch |
| 6 / 38 | data entry | for RPN |
| 7 | volume | on the machine's own tone-level curve |
| 10 | pan | constant power, 0 left / 64 centre / 127 right |
| 11 | expression | matrix source |
| 64 | hold-1 | |
| 65 | portamento switch | overrides the patch |
| 91 | Effect1 depth | reverb send, scales the whole bus |
| 93 | Effect3 depth | chorus send |
| 100 / 101 | RPN | 0 bend sensitivity, 1 fine tune, 2 coarse tune |
| 120 / 123 | all sound / notes off | |
| 121 | reset all controllers | returns every override to the patch |
| 124-127 | omni / mono / poly | mono forces SOLO, poly forces it off |

Program change reaches all 192 patches: bank 80 gives the 64 user patches, bank
81 splits into A for programs 0-63 and B for 64-127. Before this it could only
move within whichever bank was already selected.

The overrides are deliberately three-state -- CC5, CC65 and mono/poly hold -1
for "use the patch", so a reset-all-controllers hands the decision back rather
than freezing whatever the last CC said.

## Resonance mode and tone delay

**Resonance mode** is bit 7 of +53: clear SOFT, set HARD. 58 of the 539 active
tones set it, though 44 of those sit at resonance 9 or below where it barely
matters. Measured with white noise through the low-pass at cutoff 48, peak
against the 300-600 Hz passband: HARD sits above SOFT by 0.0 / 2.8 / 5.1 / 8.3 /
12.1 dB at resonance 0 / 30 / 60 / 90 / 127. That is what doubling the exponent
of the damping law gives -- `exp(-0.0104 * res * (R-1))` reproduces all five
points at R = 2.05.

Measuring it also showed the damping law's leading constant was 1.2 to 2.4 dB
loose, engine against reference by the same method. Re-fitting 0.893 to 1.125
brings the mean absolute error over both modes and five resonance settings from
2.4 dB to **0.44 dB**, and the brightness error over 128 factory patches from
4.95 to **4.68 dB**.

**Tone delay** (+69) is exactly linear at 16.0 ms per unit: 0 / 130 / 260 / 380
/ 510 / 770 / 1020 / 1280 / 1540 / 1790 / 2030 ms at value 0 / 8 / 16 / 24 / 32
/ 48 / 64 / 80 / 96 / 112 / 127. The engine reproduces it to within one 10 ms
analysis frame. 34 tones use it.

The mode lives in bits 3-4 of +71 and the factory banks use NORMAL on 535 tones
and PLAY-MATE on 4; HOLD does not occur at all. PLAY-MATE takes the gap between
the last two note-ons instead of the parameter, scaled so that a parameter of 64
reproduces the gap and 127 roughly doubles it. Past 127 the panel shows KEY-OFF,
which starts the tone when the key is released; one tone carries it and the
engine clamps to the longest delay rather than modelling it, since a voice that
starts on note-off has no note-off left to end it.

## Key assign and portamento

Patch common +24 holds the voice-assignment flags -- key assign SOLO in bit 7
(11 of 192 patches), portamento switch in bit 6 (6 patches), solo legato in bit
5 (16 patches), portamento mode in bit 4 -- and +25 the portamento time with its
type in the top bit.

Measured by playing a sine in SOLO and sending a second note-on an octave up one
second in, then tracking the pitch through the glide. **The glide is linear in
cents**: at time 50 it covers 193 / 251 / 248 / 254 / 230 cents in successive
50 ms windows, constant to the resolution of the tracker. Duration for one
octave:

    t = 13.6 ms * 2^(value / 12.3)

**TIME and RATE differ exactly as the names say.** At time 56 a glide of 5, 12
and 19 semitones took 300 / 300 / 290 ms in TIME mode -- the same duration
whatever the interval -- and 120 / 300 / 490 ms in RATE mode, the same 25 ms per
semitone throughout. One law serves both: the fit above is the duration of an
octave, and RATE scales it by the interval.

The engine reproduces both within 4 %: 30 / 110 / 290 / 750 / 1200 ms against
30 / 110 / 300 / 740 / 1150 at time 24 / 40 / 56 / 72 / 80, and the interval
behaviour of each mode exactly.

SOLO keeps a stack of held keys and plays the last of them, falling back to the
one before when a key is released -- which is what makes a trill under one
finger work. Solo legato retunes the sounding voices in place instead of
retriggering; the multisample zone is deliberately not re-selected, since
re-picking it would restart the sample, and not restarting is the whole point.

One harness note: `jv_engine_test` grew `--note2` and `--note2at`, because
nothing about portamento or legato is visible with a single note. The first
version placed the second note-on with `at > ctlAt`, and the obvious test value
of 1.0 s is exactly `ctlAt` -- so the note never sounded and the engine appeared
to have no portamento at all.

## Two more sources, and what they were each good for

**Roland's "JV Master Class" supplemental notes** (SN08, 1996, a Keyboard
Magazine article, 7 pages with a text layer) is a tutorial, not a
specification, but it contains two facts nothing else stated:

* **FXM "uses a square wave to modulate the selected waveform".** That one
  sentence turned FXM from unmeasurable into a targeted experiment -- knowing
  the modulator is a square wave says to go looking for evenly spaced sidebands,
  and they are there at a fixed 125 Hz.
* **Analog feel "produces irregular variations in pitch and level"**, not pitch
  alone.

It also independently confirms the matrix destination count ("level ... and 11
other parameters"), pan RND as a per-note random position, and that release
velocity drives the T4 time of all three envelopes.

**[charlesvestal/schwung-jv880](https://github.com/charlesvestal/schwung-jv880)**
wraps the same Nuked-derived emulator for the Ableton Move, so its DSP is the
reference this harness already drives. Its value is a hand-built table in
`src/dsp/jv880_plugin.cpp` giving a byte offset, bit shift and mask for 89 tone
parameters. As an independent check it confirms exactly the inferences that had
been least certain here:

* `lfo1offset` at +23 **shift 3, mask 0x07** -- the three-bit offset field
* `lfo1fadepolarity` at +23 **bit 7**
* `levelkeyfollow` at +70 **low** nibble, `tvaenvtimekeyfollow` **high**
* `panningkeyfollow` at +39 high, `randompitchdepth` low
* `tvfenvvelocitycurve` at +55 **bits 0-2**, `tvfenvvelocitylevelsense` at +56
* `tvfenvlevel4` at +66, and the pitch envelope's four levels as signed

It disagrees on three nibble assignments, and in all three the factory data or
a measurement decides against it:

| byte | that table | here | what decides it |
|------|-----------|------|-----------------|
| +40 | pitch keyfollow = high nibble | low nibble | The low nibble is 12 on 507 of 539 tones, and index 12 is +100 %, the normal octave per twelve keys. The high nibble is 7 on 534 tones, which in the same table is +20 % -- nothing would play in tune. |
| +71 | TVA velocity curve = high nibble | bits 0-2 | The high nibble is 12 or 13 on every one of the 539 tones, outside the valid 0-6 range. Sweeping bits 0-2 produces a clean seven-member family of velocity responses. |
| +54 | cutoff KF and TVF-env time KF both listed as the whole byte | low / high nibble | They cannot both be the whole byte. The low nibble reaches 15, which only the 16-entry cutoff table allows, and is 0 % at its mode; the high nibble stops at 14 and is 0 % at its mode in the 15-entry table. |

Neither source changed a measured number. Both were worth reading anyway: one
supplied the missing premise for an experiment, the other independently
corroborated six inferences that had rested on argument alone.

## Fitting a 4 MB board, without touching a sample

The full instrument is 4.33 MB of flash, so it needs a 16 MB board. Most people
who would like to try it own a base Pico 2 with 4 MB. The question was whether
something could be selected away, and the honest answer had to come from the
data rather than from a guess about what "sounds unimportant".

Reachability, measured rather than assumed: a tone names a multisample, a
multisample names up to 16 samples across its zones, and a tone with bit 7 of
its flag byte clear is off. Walking that from all 192 patches reaches **538 of
577 samples** and **93 of 129 multisamples**. So 39 samples are addressed by
nothing at all — the ROM also serves the JV-880's rhythm sets, which this
instrument does not implement. Their bytes are 0.36 MB that no build needs.

That alone does not get there. The banks split as follows, after relocation and
packing:

| kept | samples | wave data | blob | firmware | free on a 4 MB board |
|---|---|---|---|---|---|
| A | 331 | 1.60 MB | 1.67 MB | 2.00 MB | 2040 KB |
| A + B | 516 | 3.29 MB | 3.43 MB | 3.76 MB | 234 KB |
| A + B + User | 538 | 3.52 MB | 3.65 MB | 3.99 MB | 7 KB |

Keeping everything and dropping only the dead bytes misses by a hair — 7 KB is
not a margin, it is a build that breaks on the next commit. Dropping the user
bank costs 64 patches and 22 samples and buys 234 KB, so that is where the line
is.

Three constraints shape the relocation, all of them consequences of how the
chip finds the exponent nibble for a byte at address `a`:

```text
nibble_byte = wave[(a & 0xF00000) | ((a & 0xFFFFF) >> 5)]
nibble      = high half if a & 0x10 else low half
```

A sample must keep its address **modulo 32**, or it reads its neighbours'
exponents. It must stay inside **one 1 MB page**, because that is what supplies
its nibbles. And the first 32 KB of every page *are* nibbles, so a page holds
992 KB of sample data, not 1 MB.

Two things were easy to get wrong here and both were caught by measurement
rather than by reasoning:

* **Samples share bodies.** The 577 samples span 4.34 MB of ranges inside a
  4 MB ROM — several differ only in loop points over the same data. Relocating
  them individually duplicates about half a megabyte, which is more than the
  whole exercise saves. Overlapping ranges have to be merged and moved as one
  block, and those blocks then cannot be split to fill a page, because the
  samples inside them are tied to each other's offsets.
* **Packing order matters more than it looks.** Filling pages in address order
  strands ~130 KB at the page ends — over half the final margin. First-fit
  decreasing gets it back.

The check is the strong one, and it is worth stating in the form it takes: the
claim is not "close enough", it is that banks A and B are **bit-identical** to
the full build. Nothing is resampled, requantised or shortened; only moved.
`tools/host_tests/jv_blob_test` renders all 128 patches out of both blobs and
compares sample by sample. It reports `0/128 patches differ`.

One engine change fell out of this. `Engine::init()` refused any wave blob
under 4 MB, and `selectPatch()` then walked off a null `rom2` — the compacted
blob crashed before it played a note. The size floor was never the right check:
every address is bounds-checked in `sampleFor()` anyway, which is also what
makes a dropped sample safe. Zeroing its table entry fails `start < loop` there,
so a tone naming it falls silent instead of playing whatever now sits at that
address.

## The reverb, and where it stops

The reverb is the one part of the engine that is matched rather than
reproduced: the type, time and level laws are measured, but the network behind
them is a Schroeder bank of my own, and its tails run about 13 dB low on
material that decays. This is what came of trying to settle it.

The effects sit inside the PCM chip, whose delay memory the reference emulator
models as `eram`, 0x4000 words -- 512 ms at 32 kHz. Every effect slot addresses
it as `eram[(base + tv_counter) & 0x3fff]`, with `tv_counter` a free-running
pointer, so logging each access and taking `(addr - tv_counter) & 0x3fff`
recovers the base the firmware programmed. That is the tap position, and it
does not have to be guessed. `jv_fx_taps.cpp` does exactly that.

The same route was the one munt took for the MT-32's reverb, where the buffer
sizes came from tracing the reverb RAM address lines of the real chip. Its
constants are of no use here -- a different chip, five years earlier, four modes
against the JV's six plus two delays -- but the method carries over, and in an
emulator the address lines are function arguments.

**What came out.** One fixed delay network, shared by all six reverb types:

```
reverb        917 1295 1406 1424 1483 2149 4063 4877 5462 6772
              7217 7960 8438 9510 10097 10879 11441 12700 13128
DELAY/PAN     0 1 2 3 4 5 6 7 | 8192 8193 8194 8195
chorus        4 to 6 taps just below the wrap, 82-189 samples back
```

Three things are settled by that. The six reverb types do **not** each get their
own network -- they share one and differ only in coefficients. The time setting
moves no tap at all, so it too is a coefficient, which means the measured RT60
law is the right level of description. And a handful of taps sit in every
configuration, reverb or delay alike, a short distance behind the write pointer:
that is the chorus, which runs on the same chip. The reverb proper has 19 taps
and the delays 12.

Checked across seven patches of bank A (0, 7, 19, 31, 44, 55, 63) at three time
settings each: the reverb's 19 and the delays' 12 are **identical every time**,
so the geometry belongs to the chip and not to the patch. The chorus taps are
the exception and vary from patch to patch -- four of them for some, six for
others, anywhere from 82 to 189 samples back -- which is what a modulated delay
looks like when it is sampled at one instant. Reading a single patch would have
suggested a fixed 153-188, and that would have been wrong.

**Where it stops.** The coefficients are not reachable this way: the chip walks
32 slots in rotation and reuses `ram2[28..30]` per slot, so a register snapshot
catches one arbitrary moment of the cycle and reports identical values for every
reverb type. It looks like an answer and is not one, and the first version of
this tool was thrown away for it. The address trace above is immune to that
because it records every access rather than one instant.

Neither is the decay, and for a blunter reason: **there is no decay**. The
effect section moves signal through the delay memory -- the addressing is real,
which is why the tap trace works -- but it does not recirculate. What comes out
is a delayed copy of what went in, and it stops when the input stops.

Isolate the effect by rendering a note twice, once with the per-tone reverb send
at 127 and once at 0 (`+82` in `jv_tone_map.h`), and subtract. The difference is
exactly zero, then present, then exactly zero again. Vary how long the key is
held and the window moves with it:

```
key down  60 ms   difference present from 375 ms to  625 ms   (275 ms)
key down 500 ms   difference present from 375 ms to 1150 ms   (800 ms)
```

The onset stays at 375 ms -- the propagation time to the output taps -- while
the length follows the note. A reverb tail would outlast its input and decay;
this ends with it. Nothing changes with the reverb type or the time setting,
because there is no loop for them to act on.

**Consequences for the engine.** `jv_reverb_rt60_ms` and `jv_reverb_level` were
fitted here, and there is no reverb tail here to fit to. The note in
`Engine::Reverb` records the measurement as taken "in context on A04", with the
dry note still sounding, and describes a decay that steepens from 7 to
15.6 dB/100 ms where a comb bank falls at a constant 8.9. That shape is what a
dry release plus a delayed copy looks like when the two are measured together --
first the note dominating, then dropping away. So the ~13 dB deficit is not a
tuning error in the Schroeder bank: the curve it is tuned to does not describe a
reverb.

**This emulator cannot settle it.** What the JV-880's reverb actually does is
not reachable from here, and no further sweep of it will help. Recordings from
the hardware would be needed, and until there are any, the honest position is
that the reverb is matched to a curve of unknown provenance and the geometry
above is the only measured thing about it.

Three lessons, each of which cost a wrong conclusion here. A differential
measurement is only as good as its control: patch common 13, the reverb return,
does nothing in this emulator, so toggling it gives exactly the same silence as
a missing feature -- prove the setup on a control known to work before its
silence means anything. The check meant to validate the harness, the same
measurement aimed at the chorus, used the same wrong kind of byte and so
confirmed the setup instead of testing it. And a difference that is merely
non-zero proves nothing either: seeing one through the send, this section was
briefly rewritten to say the reverb worked after all, when what had been found
was a delay line. Ask what the difference *is*, not whether there is one.

What is left, then, is not a measurement but a decision. The tap geometry above
is real and the gains are not obtainable here, so a rebuild of `Engine::Reverb`
on the measured geometry would still be guessing its gains -- against a decay
law that, as it stands, describes a dry release rather than a reverb. Better
than today, but not by as much as the geometry alone suggests, and worth doing
only alongside a decay from hardware.

Until such a recording exists, leaving the reverb as it is and saying so
plainly, as `README.md` and `Engine::Reverb` both do, beats rebuilding it around
numbers of unknown provenance.

## Credit

The patch and tone field layout comes from
[giulioz/jv880_juce](https://github.com/giulioz/jv880_juce)
(`Source/dataStructures.h`, by Giulio Zausa), which also carries the reference
emulator this harness drives -- itself derived from
[NukeYKT's Nuked-SC55](https://github.com/nukeykt/Nuked-SC55).

That emulator is under a licence forbidding sale and commercial use, which is
compatible with neither MIT nor GPL-3.0. It is therefore **not vendored into
this repository**, and no emulator code will enter a PicoFaceJV firmware image.
The reference emulator is a host-side measuring instrument only; the decoder,
the extractor and the eventual engine are written from scratch, with the ROM
formats documented above serving as the specification.
