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
* **tvaVelocity (+72)** is a positive magnitude, not a bipolar field centred on
  64: values 8 / 16 / 32 give +2.7 / +5.6 / +10.8 dB at velocity 110 and
  -1.8 / -4.3 / -10.6 dB at velocity 30, both against velocity 64. About
  0.47 dB per unit at full velocity travel.
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

### Still open

* **Cutoff above v≈84** and the **LFO below v≈48** (see Calibration above).
* **tvfEnvDepth (+58)** moves the cutoff by 2.4 parameter units per depth unit
  (measured 19.1 / 37.5 / 60.5 at depth 8 / 16 / 24, with the envelope held
  open); above 24 the measurement saturates, not the synth. The field is signed.
* **tvaVelocity (+72)** is a positive magnitude, not a bipolar field centred on
  64: values 8 / 16 / 32 give +2.7 / +5.6 / +10.8 dB at velocity 110 and
  -1.8 / -4.3 / -10.6 dB at velocity 30, both against velocity 64. About
  0.47 dB per unit at full velocity travel.
* **LFO depths are signed over the whole byte.** 64..127 really is inert, but
  128..255 is -128..-1 and modulates the *other way*: pitch depth -20 gives
  133 cents of swing around a mean above the carrier, +20 gives 136 cents around
  a mean below it. An earlier sweep only covered 0..127 and wrongly concluded
  that everything from 64 up was off. On the TVA the negative side is inaudible
  at full level, because there is no headroom to modulate upward into.
* **Resonance** is calibrated (see the matrix section); the LFO's TVF depth
  is not.

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
