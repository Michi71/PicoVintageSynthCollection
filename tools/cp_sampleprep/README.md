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
| Rd II | `Rd_IIData.h` | 967 kB |
| Wr | `WrData.h` | 773 kB |
| Clv | `ClvData.h` | 779 kB |
| CP | `CPData.h` | 580 kB |
| Piano | `PnoData.h` | 293 kB |

Those headers ship in the firmware and account for most of PicoFaceCP's
4.22 MB image. The tools that produce them lived outside this repository until
now, which meant the five generated voices could not be rebuilt from a
checkout. That is what this directory fixes.

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
