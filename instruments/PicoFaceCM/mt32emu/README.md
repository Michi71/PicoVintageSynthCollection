# mt32emu, CM32 port

The synthesis core of [munt](https://github.com/munt/munt)'s mt32emu 2.8.6
(commit 91d2205), LGPL 2.1 or later - see COPYING.LESSER.txt and AUTHORS.txt.
This copy is being reshaped to run on an RP2350. Everything that differs from
upstream is listed here, and marked `CM32 port` in the source.

Left out: FileStream, SampleRateConverter, VersionTagging, the C interface,
the tests, mt32emu.h (include the class headers directly).

Changed:

- `Synth::setExternalPCMData()` / `Synth::unscramblePCMWord()` (Synth.h,
  Synth.cpp): the PCM ROM can be handed over already unscrambled and is then
  read in place. Upstream builds a 1 MiB copy on the heap in open(); on the
  RP2350 the unscrambled words sit in flash.

- Lookup tables as data (Tables.cpp): upstream fills them at start-up with
  log(), log10(), sin() and exp2f(), and the LA32 exponent table changes with
  the C library's last bit. The port includes `TablesData.inc`, which is not
  in the source tree: the build writes it with `tools/cm_extract/cm_make_tables.py` from the same
  formulas and stops if the result is not upstream's (digest in the script;
  `gen_tables.cpp` in the cm32l project asks upstream itself). The port needs
  none of the `MT32EMU_WITH_...` definitions and no libm in the integer path.

- `MT32EMU_MAX_SAMPLES_PER_RUN` can be set from the build (globals.h). At 256
  the renderer's buffers shrink from 48 KiB to 3 KiB; the output is the same.

- Two executors (Synth.h `ParallelHelper`, Synth.cpp `RendererImpl<IntSample>`,
  PartialManager, Partial): the integer renderer no longer runs upstream's one
  loop over the partials. It generates every partial into a buffer of its
  own, then pans and mixes them in upstream's order - the mix saturates sample
  by sample, so the order is part of the result. Generation is shared between
  the calling executor and a second one (core1 on the RP2350); after that one
  mixes the non-reverb partials while the other mixes the reverb partials and
  runs the reverb. What a partial's end changes outside itself (free list,
  poly, part) is put off until generation is over and applied in upstream's
  order. Without a helper the same code runs on one executor.
- Pitch timer jitter per partial (`Synth::setPerPartialPitchJitter`, TVP):
  upstream takes two random bits from the C library's `rand()` whenever a
  partial's pitch timer fires, so a partial's pitch timing depends on which
  partials were rendered before it. With the switch on, each partial has its
  own generator. Off, the port is upstream to the bit; on, it is not and
  cannot be - but then it renders the same on one executor and on two.
- A poly's abort in one run (`Synth::setChunkedAbortRuns`, Synth.cpp
  `doRenderStreams`, `PartialManager::generateAbortingPoly`): when a note
  needs partials that are in use, a sounding poly is faded out and the MIDI
  event waits in the queue until that is done. Upstream finds that moment by
  rendering one sample per run, about a hundred runs for each note taken
  away. With the switch on, the renderer generates the fading poly first,
  sees where it ends and renders the rest up to there in one run; the
  deactivations of such a run are applied in order of time, as the
  single-sample runs would apply them. Needs per-partial jitter.
- The generation is forked to the second executor from 16 partial-samples
  up, mix and reverb from 8 samples up; below that the calling executor does
  it alone.
- `CM32_HOT` (internals.h) marks what runs while the synth plays: the
  per-sample code, the renderer's runs, and the path of a MIDI event up to
  the start of its partials. On the RP2350 those functions are linked into
  RAM; elsewhere the mark is empty. Opening the synth, SysEx and the float
  renderer stay in flash.
- `HotPath.cpp` compiles LA32Ramp, LA32WaveGenerator, TVA, TVF, TVP and
  Partial as one translation unit, so that the dozen small calls each sample
  of each partial goes through are inlined. Those six files are not compiled
  on their own. (For that, two file-level pointers have other names than
  upstream: `rampExp9` in LA32Ramp.cpp, `tvfEnvLogarithmicTime` in TVF.cpp.)
  On the host this alone makes the port a fifth faster than upstream.
- The wave generator (LA32WaveGenerator), three things that leave every
  sample as it was:
  the exponent that turns a log value into a linear one is interpolated for
  all 4096 arguments once (`exp12`, 8 KiB) instead of at least twice per
  sample and partial;
  `setCutoff()` returns at once unless the cutoff has moved far enough to
  change anything derived from it - the ramp moves it with nearly every
  sample, the derived values look at it shifted by 9 or 10 bits;
  the square and the resonance wave are not worked out when the terms known
  beforehand already put them below what comes out as a linear zero, which
  is the case for the resonance wave of most partials most of the time and
  for both in the long end of a release.
  `unlogAndMixWGOutput()` is inlined, and so is the check in `setCutoff()`.
  The mix (`Partial::mixOutput`) saturates with ARM's SSAT instruction where
  there is one.
  Counted on a Cortex-M4 under QEMU (`hosttest/armcount` in the cm32l
  project): a synth partial takes about 295 instructions per sample instead
  of 405, a PCM partial 165 instead of 211. On the RP2350 the same step took
  a synth partial from 1230 ns to 900 ns and a PCM partial from 730 ns to
  560 ns, which is what the count had said.
- `Synth::getRenderRunCount()`, `setJobClock()` / `takeJobTimes()`, for
  diagnostics: runs rendered, and generation time split into synth and PCM
  partials.

Built with `-DMT32Emu=CM32Core`, so this copy and upstream mt32emu can be
linked into one program and rendered against each other.

## Tables and the firmware

Three of the six tables are in the CM-32L control ROM v1.02, byte for byte:

| Table | Size | Control ROM |
|---|---|---|
| pulseWidth100To255 | 101 | 0x128C |
| envLogarithmicTime | 256 | 0x1371 |
| levelToAmpSubtraction | 101 | 0x1471 |

`exp9` and `logsin9` (512 words each) model tables inside the LA32 chip and
have no counterpart in the ROM.

`masterVolToAmpSubtraction` differs from the firmware on purpose, upstream's
purpose: the CM-32L ROM holds the variant clamped to 8 (indices 68..100 match
at 0x3D1E, below that the ROM has 8 where the table goes on rising), which
belongs to an output VCA that mt32emu does not model. Upstream therefore uses
the MT-32 table for all models. Left as is; it is the one place found so far
where the port knowingly does not follow the firmware.

## State

Checked on the host, in the cm32l project this copy is developed in. Its
`hosttest/ab.sh` renders 25 event sets - a demo, single notes, rhythm keys,
and four generated pile-ups that saturate the mix and exhaust the partials -
and compares byte for byte (digital output, integer renderer):

- upstream against this copy on one thread: the same;
- this copy with per-partial jitter, aborts sample by sample against aborts
  in one run: the same;
- that against the same on two threads: the same.

MIDI goes through the synth's queue in all of them, as it has to: an event
played with `playMsgNow()` that sets off an abort is lost, and the renderer
then crawls along sample by sample.

A thread sanitizer build of the two-thread render reports nothing. The
generated tables are checked against upstream's own table code.

Heap taken by open(), measured there with `cm32-measure`: about 134 KiB with
external PCM words and `MT32EMU_MAX_SAMPLES_PER_RUN=256` (upstream: 1.21 MiB).
Nothing is allocated while rendering.

## Known, not yet done

- `Synth` keeps its own 64 KiB copy of the control ROM (`controlROMData`).
- The float renderer, ROM identification by SHA-1 and the analogue output
  models other than "digital" are still compiled in.
- Render time on the RP2350 is unmeasured.
