<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Michi71
-->

# mt_sampleprep — where PicoFaceMT's tapes come from

PicoFaceMT plays ten Mellotron instruments from 2.98 MB of flash. The source
sets are SF2 files with one unlooped 7–8 s recording per key, about 11 MB
each. Two steps get from one to the other.

```
source SF2s ──▶ compress_sf2.py ──▶ compressed SF2s ──▶ sf2_to_cpp.py ──▶ instruments/PicoFaceMT/src/data/
                      │
                      └── ../cp_sampleprep/FindLoopPoints
```

The source SF2 files are not in this repository. They are Taijiguy's
recordings of a 1973 Mellotron M400S (serial #500), licensed
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); `sf2_to_cpp.py`
writes the credit, the licence and the list of changes into every PCM file it
generates (`--credit` for a set from somewhere else).

## Step 1 — `compress_sf2.py`

```bash
../cp_sampleprep/build_loop_finder.sh
python3 compress_sf2.py -o compressed/ /path/to/source/*.sf2
```

- Every recording becomes attack (0.4–0.8 s) plus a short loop.
- Of the 35 recordings one per `--step` keys (default 3) is kept and mapped
  onto its neighbours by key range.
- A file with a single long recording is treated as a one-shot: cut to
  `--oneshot-s` and faded. Recordings under 1 s are copied unchanged.

`FindLoopPoints` compares zero-crossing spacings only, which is not enough on
these sounds, so the script does three things around it:

1. It runs the finder over many window ends and `num_periods` seeds.
2. It moves each returned loop start by at most ±0.6 periods to where the
   waveform best matches the loop end (`--no-refine` turns this off) and
   crossfades the seam.
3. It rates every candidate itself — seam correlation, detuning against the
   period measured by autocorrelation, level against the sustain — and keeps
   the best. Within a key group it keeps the recording with the best loop.

The log marks loops outside the tolerance with `(!)`. On the shipped set that
is 35 of 120, nearly all on ensemble recordings (string section, three
violins, choir), which have no clean period to loop.

## Step 2 — `sf2_to_cpp.py`

```bash
python3 sf2_to_cpp.py --src compressed/
```

Writes one `.cpp` per instrument plus the bank table. Single-recording files
are skipped unless `--all` is given. `instrument.cmake` lists the generated
files by name.

## Checking a new set

`tools/host_tests/mt/build_mt.sh`, then `mt_engine_test out.wav --a <n>
--score hold --set wow=0 --set flutter=0 --set keydrag=0 --set drive=0` renders
held notes that can be compared against any SF2 player on the compressed
file. The shipped set matched FluidSynth to within 0.6 cent.
