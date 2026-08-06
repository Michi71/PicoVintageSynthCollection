# PicoFaceJV (work in progress)

A Roland **JV-880** for the collection. Unlike the other instruments in this
repository, **this one does not build a firmware image yet** — what exists is
the sound engine and a host harness for it. There is no `instrument.cmake`, no
adapter, no controller or display, and no UF2.

## What this is, and is not

It is **not** an emulation of the JV-880's hardware. Emulating the H8/532, the
sub-MCU and the PCM chip costs roughly 400 M cycles/s and, worse, 5.1 M
*random* flash reads per second against an 8 KB XIP cache — the RP2350 would
choke on the flash, not the arithmetic.

Instead the engine reads the same ROM data — multisample table, sample table,
patch parameters — and runs its own voice chain: sequential DPCM decode, pitch,
TVF, TVA, pan. Sequential decoding is what keeps the flash access pattern
cache-friendly (~0.9 M reads/s, nearly all cache hits).

The ROM formats, the parameter map and the calibration are documented in
[tools/jv_extract/](../../tools/jv_extract/README.md), which also holds the
tooling that produced them.

## State

| Part | State |
|---|---|
| Sequential DPCM decode with looping | works |
| Multisample key splits | works, zone mapping verified |
| TVA envelope, 4 stages | works, calibrated |
| TVF low/high-pass with envelope | works (TPT topology), envelope depth uncalibrated |
| Pan, tone level, velocity sensitivity | works, calibrated |
| Voice allocation with oldest-voice stealing | works |
| Pitch accuracy | 0.2-0.7 cents on single tones, 5 cents mean on layered patches |
| LFO 1 and 2 (pitch, TVF, TVA) | works; rate, depth and waveform matched to the reference |
| LFO key sync, free-run, offset | works; bit 5 of the flags ignored |
| Modulation matrix | works for the identified destinations; see below |
| FXM, portamento | not implemented |
| Reverb / chorus | not implemented |
| Firmware adapter, UI, MIDI, persistence | not started |

Pitch is resolved. Two things had to be right together, and getting one wrong
made the other look unexplainable: `end` in the sample table is **inclusive**
(a loop spans `end - loop + 1` samples), and `tune` is a fine-tune of **0.1 cent
per unit, neutral at 1024**. Reading `end` as exclusive detunes short loops far
more than long ones, which looked exactly like a per-sample `tune` law that no
single formula could fit. Both were settled by patching rom2 itself and
measuring — see the jv_extract README.

The modulation matrix reaches pitch, cutoff, resonance, level and all six LFO
depths — every destination the reference responds to except 11-15, which
produced nothing measurable and are no-ops.

Verifying it turned up two bugs of my own and one design mistake. Positive TVA
modulation was being discarded, because the gain was only applied on the
negative branch that the LFO uses — the entire level destination did nothing.
`tvfEnvDepth` was read as bipolar around 64, turning a patch's "no envelope
depth" into full negative depth; it follows the same 0..63-then-disabled
convention as the LFO depths and the matrix sensitivities. And the filter was a
plain Chamberlin SVF, which is only accurate to about sr/6: at 32 kHz it erred
by a third of an octave at cutoff parameter 56 and had stopped filtering by 64,
squarely inside the range the matrix reaches. It is now a topology-preserving
SVF, accurate to 17-92 cents against the calibration table up to parameter 56.

One more thing worth recording because it cost real time: a plain DPCM integrator
does not return to the same value after a loop pass. Sample 504's loop drifts by
−728 per turn, which walks a sustained tone into the 20-bit clamp and destroys
it. The original chip integrates without correction; a native engine does not
have to, so the engine snapshots the accumulator at the loop point and restores
it on every wrap.

## Building the host harness

No ROM data is in this repository; bring your own JV-880 ROM set.

```bash
c++ -O2 -std=c++17 -Iinstruments/PicoFaceJV/include -Itools/jv_extract \
    -o jv_engine_test tools/host_tests/jv_engine_test/jv_engine_test.cpp \
    instruments/PicoFaceJV/src/jv_engine/jv_engine.cpp -lm
./jv_engine_test <romdir> 1 24 60 100 organ.wav
```

Arguments are `<romdir> [bank 0-2] [patch 0-63] [note] [velocity] [out.wav]`,
plus `--set tone:offset:value` to patch tone bytes before the note (mirroring
`jv_probe`'s `#base` lines, so both sides can be driven identically) and
`--trim <ratio>` for a global pitch trim.

## Licence

The engine is original code and carries the repository's licence. It contains no
emulator code: the reference emulator used to measure against is host-side only
and is never vendored here — see the licence note in
[tools/jv_extract/README.md](../../tools/jv_extract/README.md).
