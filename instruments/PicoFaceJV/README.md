# PicoFaceJV (work in progress)

A Roland **JV-880** for the collection. It builds a firmware image, but only on
a machine that has a JV-880 ROM set: the ROMs are not distributable, so without
them the instrument removes itself from the build and the other eight are
unaffected. It has not been tried on hardware.

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
| Output level vs the reference | mean −1 dB over 24 patches, but 5.3 dB spread |
| LFO 1 and 2 (pitch, TVF, TVA) | works; rate, depth and waveform matched to the reference |
| LFO key sync, free-run, offset | works; bit 5 of the flags ignored |
| Modulation matrix | works for the identified destinations; see below |
| FXM, portamento | not implemented |
| Firmware adapter, panel UI, MIDI, persistence | built, untested on hardware |
| Reverb / chorus | not implemented |

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

## Hardware and load

The board is the collection's standard one; the pin map lives in
[core/include/project_config.h](../../core/include/project_config.h).

Unlike PicoFaceRD this instrument stays on **core0 at the standard 444 MHz**.
The engine costs roughly 55 M cycles/s at full polyphony and decodes each voice
sequentially, so neither the core1 worker nor the raised clock RD needs applies
here. Firmware image is 4.53 MB, of which 4.25 MB is the ROM blob.

| Page | Encoder A | Encoder B |
|---|---|---|
| PATCH | patch, walking all 192 as one list | bank (User / A / B) |
| VOL | master volume | — |
| VOICES | polyphony cap 1–24 | line B shows live `Act <n>` |
| TUNE | master tune ±50 cents | line B shows the resulting A4 |
| SYS | MIDI receive channel 1–16 / Omni | — |

The footer shows `U<underruns> A<active>/<limit>`. On an underrun the instrument
drops two voices from the cap; the VOICES page raises it again.

MIDI: note on/off, sustain (CC 64), modulation (CC 1), expression (CC 11),
channel aftertouch, pitch bend ±2 semitones, program change within the current
bank, and the panic controllers 120/121/123. CC 1, CC 11 and aftertouch are the
modulation matrix's three sources.

## Building the firmware

The ROM set is not part of this repository. Put these three files in
`instruments/PicoFaceJV/roms/` (gitignored) and configure as usual:

```text
jv880_rom2.bin        256 KB   tables and patches
jv880_waverom1.bin      2 MB   wave data
jv880_waverom2.bin      2 MB   wave data
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceJV
cmake --build build
```

Configure converts the ROMs once into a descrambled blob under the build tree
and embeds it with `.incbin`. Without the ROMs the configure step prints a note
and skips the instrument, so a plain checkout still builds the other eight.

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
