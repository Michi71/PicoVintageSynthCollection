<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Michi71
-->

# cp_sampleprep — where PicoFaceCP's sample sets come from

Five of PicoFaceCP's six voices are not mda-EPiano's. `Rd II`, `Wr`, `Clv`,
`Piano` and `CP` are sample sets of their own, built by this toolchain and
committed as C headers:

| Voice | Header | Flash |
|---|---|---|
| Rd I | `mdaEPianoData.h` | 825 kB — mda-EPiano's original set, not built here |
| Rd II | `Rd_IIData.h` | 864 kB |
| Wr | `WrData.h` | 631 kB |
| Clv | `ClvData.h` | 676 kB |
| CP | `CPData.h` | 511 kB |
| Piano | `PnoData.h` | 280 kB |

Those headers ship in the firmware and account for most of PicoFaceCP's
3.99 MB image. The tools that produce them lived outside this repository until
now, which meant the five generated voices could not be rebuilt from a
checkout. That is what this directory fixes. The sizes above are the sets cut
for a 4 MB flash; how that was done, and how to get the earlier, longer sets
back, is under [Fitting a 4 MB flash](#fitting-a-4-mb-flash).

## What is not here: the source recordings

The WAV files these headers were built from are **not** in this repository and
are not distributed.

They were collected over a long stretch of time, mostly from freely available
internet sources, and some were made by playing an emulator over MIDI and
recording its audio output. Their exact origins can no longer be
reconstructed, so they are not redistributed — the derived headers are what
ships, and this pipeline is what turned one into the other.

Anyone rebuilding a voice therefore brings their own recordings. The pipeline
does not care where they came from; it cares about file naming, which is
described below.

For the record, and because the working copy of this extract has been lost
once already: the committed headers were built from these files of the full
recording sets, one WAV per root note and velocity, and nothing else. Every
committed sample matched its source to within one LSB when the extract was
rebuilt, and the pipeline then reproduced all ten headers byte for byte.

| Voice | Root notes | Velocities |
|---|---|---|
| CP (`yamaha_cp80`) | 42 46 50 53 57 63 65 68 72 80 82 88 91 97 | 065, 115 |
| Clv (`clavinet`) | 42 47 52 57 62 69 74 79 84 89 95 100 | 025, 069, 127 |
| Pno (`piano`) | 30 36 43 48 55 60 67 72 79 84 91 96 104 108 | 127 |
| Rd II (`rhodes_suitcase`) | 28 32 36 … 84 (every fourth) 90 91 94 98 | 030, 094 (90 has only 094, 91 only 030) |
| Wr (`wurlitzer_200a`) | 36 43 48 55 60 67 72 79 84 91 96 | ≈030, ≈074, 125 — the file nearest each, e.g. 028/032, 073/075/076, 124 |

The CP recordings carry a `smpl` chunk that says MIDI note 60 in every file,
so their level normalisation used note 60 throughout; the Rd II files for 64,
68 and 72 (soft layer) say one note lower than their names. Both quirks are
part of what the committed headers sound like, and the pipeline reproduces
them because it reads the chunk first.

## The chain

Two stages, and one C++ helper that the first stage shells out to.

```
your WAVs ──▶ prepare_samples.py ──▶ converted WAVs + _instrument.json
                     │                            │
                     └── FindLoopPoints           ▼
                                          build_instrument.py
                                                  │
                                                  ▼
                                     <name>Data.h + <name>Keygroups.h
```

### Stage 1 — `prepare_samples.py`

Brings arbitrary WAV sets to the format and level of the mda-EPiano samples,
and cuts them to mda's "attack plus short loop" shape.

1. **Downmix to mono** — any channel count is averaged.
2. **Resample to 32 kHz** (`scipy.resample_poly`) — mda's native rate; the
   engine interpolates up to 44.1 kHz at playback.
3. **Normalise to the pitch-dependent mda target peak.** mda's own samples get
   quieter towards the top of the keyboard, and that is part of its character.
   The curve `peak_dBFS(note) = a + b·note` is fitted from mda's root samples
   rather than guessed; on this repository's data it comes out as
   `-8.335 - 0.01738·note`.
4. **Trim**, in one of four modes. `loop` is the default and the interesting
   one: it pre-cuts to a window ending in strong sustain shortly after the
   bloom peak, tries several window candidates, and runs `FindLoopPoints` on
   each. The result is attack plus a short looped sustain region, roughly
   0.25–0.8 s.
5. **Write** 16-bit mono PCM at 32 kHz, plus `_mapping.json` and
   `_instrument.json`.

Loop quality is scored by the C++ tool: ≤0.01 excellent, ≤0.05 good, ≤0.15
acceptable, >0.30 unusable (the sample is then kept without a loop).
Degenerate short loops — half periods, high-partial endings — are rejected via
`min_loop_periods`.

### Stage 2 — `build_instrument.py`

Turns the descriptor into the two headers the engine includes. Every
referenced sample is concatenated once into a single `int16_t` array, global
`pos`/`end`/`loop` are computed in mda's semantics (`while (pos > end) pos -=
loop`), and the keygroup table is emitted with **three velocity layers per
region**, matching the engine's `k += 3` and its thresholds at 48/80.

Regions get `root` and `high` on their first layer only, as mda does. A layer
with no sample of its own points at the nearest available velocity tag — same
data, no extra flash.

### The helper — `FindLoopPoints`

Zero-crossing pattern matching, built from `src/FindLoopPoints.cpp`:

```bash
./build_loop_finder.sh
```

It was reviewed when it was wired into the Python pipeline, and it has three
quirks. None of them affects this use, but all three would bite someone
reaching for it as a general tool:

- `main()` hard-codes `sampleRate = 32000` instead of reading the fmt chunk.
  The search itself works purely in samples, so the rate only reaches the
  ms/Hz display — cosmetic, and the pipeline feeds 32 kHz anyway.
- `readWav()` mis-parses `channels` and `sampleRate` from the fmt chunk
  (skipping 4 bytes where it should skip 2). Those values are never used
  afterwards and the `data` samples are read correctly.
- `estimatedPeriod` is a **half** period — the distance to the previous
  opposite-going zero crossing. Loop lengths are still whole multiples of the
  full period, because loop start and end are the same crossing type, so the
  loops are correct; only the meaning of `num_periods` is halved and the
  displayed Hz doubled.

The finding that shaped the pipeline: the finder takes the **last** zero
crossing as its loop-end reference. On a long decaying note that lands in a
near-silent, inharmonic tail and the loop is miserable. Hence the pre-cut
window ending in strong sustain — the tool is never shown the tail.

## Running it

```bash
./build_loop_finder.sh
python3 prepare_samples.py configs/rhodes_suitcase.json
python3 build_instrument.py build_host/cp_samples/converted/rhodes_suitcase/_instrument.json
```

Run from the repository root, or pass `--repo`. The configs used to point the
loop finder at `tools/FindLoopPoints`, a path that does not exist in this
repository; `prepare_samples.py` reports a missing binary as a warning and
quietly falls back to `transient` mode, which cuts every sample to its bloom
and loops nothing. The configs now name `tools/cp_sampleprep/FindLoopPoints`.
If a rebuilt set comes out with `loop_pending: true` everywhere, that warning
is the first thing to look for.

Paths in the configs are relative to the repository root, and point into
`build_host/`, which is git-ignored. Put your recordings in
`build_host/cp_samples/source/<instrument>/` and the generated headers appear
under `build_host/cp_samples/converted/<instrument>/generated/`.

Dependencies: `pip install soundfile scipy numpy`, plus a C++17 compiler for
the loop finder.

### File naming

`<NNN>-<VVV>.wav` — MIDI note and velocity, e.g. `060-087.wav`. A `smpl` chunk
carrying `Midi Note` and loop points is used in preference when present;
otherwise the note comes from the filename.

The engine has three velocity layers with thresholds at 48/80. A set may carry
one, two, three or more velocity steps; each engine layer takes the sample
whose velocity is closest to that layer's centre (24 / 64 / 104).

> `velocity_thresholds` in a config are the **engine's** switch points, not
> your samples' velocities. Leave them at `[48, 80]` unless you have
> reprogrammed `noteon()`. The sample velocities are read from the filenames.
> `engine.layer_to_tag` overrides the mapping if the automatic choice is wrong
> — `[30, 30, 94]` makes the medium layer play the soft sample instead of the
> hard one.

### Adding a voice

1. Recordings into `build_host/cp_samples/source/<name>/`.
2. Copy a config, adjust `source_dir`, `output_dir` and the `trim.*` fields.
3. Run both stages.
4. Copy the two generated headers into `instruments/PicoFaceCP/include/`,
   add the repository's SPDX lines at the top (the generator does not emit
   them), and wire the voice into `mdaEPianoInstruments.h`.

Note the engine constraint the generator prints: `KGRP kgrp[34]` in
`mdaEPiano.h` sizes mda's own table. A 19-region set needs 57 keygroups, so
the array has to grow.

## Fitting a 4 MB flash

PicoFaceCP was 4.22 MB, and a base Pico 2 has 4 MB of flash. The five built
sets were cut by 441 kB so the image fits with room to spare, and the cut was
made where it changes the least.

**What a sample is, to the engine.** It plays the data once from the start,
and when it reaches the end it jumps back by `loop` samples -- a cycle of a
few periods -- and repeats that cycle for as long as the key is held, under
its own decay envelope. So a sample is an attack that plays once, followed by
one frozen waveform cycle. Everything between the bloom and the loop end is
heard exactly once; the sustain is the cycle. The classic rule cut every
sample at bloom + 350 ms. Cutting at bloom + 225 ms instead saves 60 ms of
sustain that was heard once, and leaves the attack byte for byte as it was.

**What must not change is the cycle.** Its level and spectrum are the
sustain of that note, and they depend on exactly where the loop lands -- a
different window can freeze a different phase of the beating between a
tine's partials, and the first attempt at shorter windows moved single notes
by up to 9 dB. Hence the two-part rule, `loop_shortest_ms` plus a
*reference*:

1. `loop_reference.py` stores a fingerprint of every sample's loop cycle in
   the set being replaced -- level, spectral centroid, six band levels -- as
   [reference/*.json](reference/). The five files in there describe the
   headers that shipped before the cut.
2. `prepare_samples.py` then tries windows from bloom + `loop_shortest_ms`
   upwards in 5 ms steps and takes the **shortest one whose loop is excellent
   (score ≤ 0.02) and whose cycle stays within `reference_tolerance` of the
   fingerprint**: 1 dB in level, 10 % in centroid, 3 dB in any band that
   carries energy. A sample with no such window keeps the window the classic
   rule picks, i.e. exactly what it was; a sample that had no loop before is
   free to take any excellent one.

With `loop_shortest_ms: 225`:

| Voice | Samples | Shorter | Kept | Window, median | Bytes |
|---|---|---|---|---|---|
| Rd II | 36 | 29 | 6 | 382 → 335 ms | 990,208 → 884,794 |
| Wr | 33 | 31 | 2 | 371 → 310 ms | 791,332 → 646,068 |
| Clv | 36 | 26 | 8 | 370 → 307 ms | 797,626 → 691,668 |
| CP | 28 | 20 | 6 | 349 → 274 ms | 593,950 → 522,850 |
| Pno | 14 | 6 | 8 | 339 → 299 ms | 299,746 → 286,234 |

(The counts that do not add up are the samples that came out slightly longer:
five that had no excellent loop before and have one now, three of which had
no loop at all -- CP `097-115`, Clv `042-025`, Pno `108-127` replayed their
whole attack every 370 ms while a key was held.) Loop scores got better, not
worse: no sample scores below its old score, and no voice has a loop failure
any more.

Measured on the engine itself (`tools/host_tests/cp/build_render.sh`, six
notes at two velocities per voice, old headers against new): the output is
**identical to the sample until the new loop point** (211-747 ms into the
note), the sustain level from there on is within ±1 dB on every note, the
spectral centroid within ±10 % (mostly ±2 %), the loop ripple unchanged, and
the release identical. The image went from 4,432,096 to 3,990,856 bytes.

To rebuild the longer sets the headers were cut from, set
`"loop_shortest_ms": null` in the five configs and drop their `reference`
line; that is the classic rule, and it reproduces the pre-cut headers byte
for byte from the extract above. Any future set that has to be cut goes the
same way: build it long, fingerprint it with `loop_reference.py`, then build
it short against that fingerprint.

## What changed on the way in

The pipeline used to read mda's reference peaks from 34 exported WAV files
plus the plugin's `.cpp`. Neither path exists in this repository, which would
have made the tool unusable exactly where it now lives.

`load_mda_curve()` therefore reads the peaks out of `mdaEPianoData.h` and the
keygroup table out of `mdaEPiano.cpp`, both of which are in the repository.
That is not obviously equivalent: the committed data is pre-baked with the
loop crossfade applied, where the original plugin built its array at runtime.
Checked against the old WAVs before the switch — the fitted `a` and `b` agree
to every printed digit, and the resulting target peak differs by **0.000000 dB
across notes 21–108**. The crossfade lives in the loop region; an e-piano
sample's peak is in the attack.

Nothing else was touched. In particular the generators' **output templates are
unchanged**, including their German comments, so regenerating a voice
reproduces the committed headers rather than a reformatted version of them.

An earlier chain — `InstrumentBuilder`, `SamplesToInstrumentTxt`,
`PresetIndexBuilder` and the shell scripts driving them — was superseded by
these two Python stages and is not part of this import. The committed headers
all carry `Auto-generiert von build_instrument.py`.
