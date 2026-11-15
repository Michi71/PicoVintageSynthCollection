<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Michi71
-->

# mt_sampleprep — where PicoFaceMT's tapes come from

PicoFaceMT plays ten Mellotron instruments from 7.78 MB of flash. The source
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
python3 compress_sf2.py -o compressed/ /path/to/source/*.sf2
```

- Every recording becomes attack plus loop.
- Of the 35 recordings one per `--step` keys (default 3) is kept and mapped
  onto its neighbours by key range. Within a group the middle recording is
  preferred, which keeps the transposition to one semitone, unless its loop
  is poor or the tape stands out from its neighbours in brightness, octave
  balance or level (`--centre-bonus`, `--w-*`). An earlier bank picked by loop
  quality alone; on the choir a bright recording transposed down one semitone
  then sat next to a dull one transposed down two, and D4 sounded higher than
  E4.
- A file with a single long recording is treated as a one-shot: cut to
  `--oneshot-s` and faded. Recordings under 1 s are copied unchanged.

### `--mode long` (default, what ships)

About 1.5 s per recording: the loop is about a second long and ends in a
250 ms crossfade into the stretch before its start. For a set of loop ends the
script looks for the start where the waveform lines up best, and prefers
pairs whose level and band spectrum agree, since a difference there comes
round as a slow wobble. The crossfade gains follow the measured correlation
of the two stretches, so the level does not dip on ensemble recordings.

On the shipped set all 120 loops are within tolerance (2 dB level, 3 dB mean
band difference); the seam differs from its surroundings no more than any
other point of the loop does, and comes round about once a second.

### `--mode short`

Loops of a few periods, 0.4-0.8 s per recording, a bank of 3 MB that fits a
4 MB flash. This was the first bank, and on hardware its seams were plainly
audible: a loop of 27-138 ms repeats 7 to 38 times a second, and on ensemble
recordings (string section, three violins, choir) that is a pulse. Needs
`../cp_sampleprep/build_loop_finder.sh`.

`FindLoopPoints` compares zero-crossing spacings only, so the script runs it
over many window ends and `num_periods` seeds, moves each returned loop start
by at most ±0.6 periods to where the waveform best matches the loop end
(`--no-refine` turns this off), crossfades the seam, and rates every
candidate itself - seam correlation, detuning, level. The log marks loops
outside the tolerance with `(!)`; on this set that was 35 of 120.

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
