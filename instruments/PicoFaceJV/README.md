# PicoFaceJV

A Roland **JV-880** for the collection. It builds a firmware image, but only on
a machine that has a JV-880 ROM set: the ROMs are not distributable, so without
them the instrument removes itself from the build and the other eight are
unaffected. Both the full and the 4 MB build run on hardware.

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
| Output level vs the reference | mean 0 dB over 24 patches, 2.7 dB spread |
| LFO 1 and 2 (pitch, TVF, TVA) | works; rate, depth and waveform matched to the reference |
| LFO key sync, free-run, offset | works; bit 5 of the flags ignored |
| Modulation matrix | works for the identified destinations; see below |
| Velocity curves, TVF velocity | works, all seven curves measured |
| FXM, alternating loops, tone delay, resonance mode | works, calibrated |
| Poly / Solo, portamento, legato | works, calibrated |
| Chorus (both types) and the two delay types | works, calibrated |
| Reverb | matched, not reproduced: the type, time and level laws are measured, the topology is a Schroeder network of my own. Tails on material that decays run ~13 dB low |
| Firmware adapter, panel UI, MIDI, persistence | works on hardware |

Open, and documented as such: per-patch velocity steepness still scatters about
1 dB, most likely because TVF velocity takes level with it and that was never
measured for level; the per-tone Volume Switch and Hold-1 Switch bits are
unresolved; the envelope time keyfollows and the T1/T4 velocity fields sit
near-neutral throughout the factory banks and were never exercised.

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

Unlike PicoFaceRD this instrument stays on **core0 at the standard 444 MHz**,
and decodes each voice sequentially, so neither the core1 worker nor the raised
clock RD needs applies here. Firmware image is 4.33 MB, of which 4.25 MB is the
ROM blob.

Measured on hardware with B33 Brass Combo at full polyphony: **69 % peak at 24
voices**. Splitting that against the host profile puts the fixed cost — chorus,
reverb, block overhead, all of which run once per block regardless of how many
voices sound — at about 5 %, and each voice at about 2.7 %. The effects are
cheap; it is the voices.

The cap stays at 24 rather than the machine's 28. 28 would land near 80 % in the
middle of the keyboard, but per-voice cost varies about 15 % with register — a
high note decodes more DPCM steps per output sample — so the bottom of the
keyboard would reach ~92 %, and the display, encoders and MIDI parsing draw on
the same core0 time that this figure does not include. The 31 % is not spare
capacity, it is the margin that keeps the underrun count at zero.

| Page | Encoder A | Encoder B |
|---|---|---|
| PATCH | patch, walking all 192 as one list: A, then B, then User | bank |
| VOL | master volume | — |
| VOICES | polyphony cap 1–24 | line B shows live `Act <n>` |
| TUNE | master tune ±50 cents | line B shows the resulting A4 |
| VELO | incoming-velocity scaling, 0–100 % | line B shows where velocity 64 lands |
| SYS | MIDI receive channel 1–16 / Omni | — |

VELO exists because the machine is faithful and sequencer files are not written
for it: a typical patch drops about 11 dB from velocity 127 to 64. 100 % passes
velocity through untouched; lower values pull it toward 127. Worth knowing that
it acts before the engine sees anything, so compressing upward will also bring
in tone layers a patch reserves for hard playing.

The footer shows `P<load>% U<underruns> A<active>/<limit>` — the load is the
worst render block since the last decay, measured the same way PicoFaceRD
measures its own. On an underrun the instrument drops two voices from the cap;
the VOICES page raises it again.

MIDI: note on/off, pitch bend with the **patch's own** up and down ranges, and
CC 0 (bank select: 80 selects the user bank, 81 the presets), 1 modulation,
5 portamento time, 6/38 and 100/101 for RPN 0-2 (bend range, fine and coarse
tune), 7 volume, 10 pan, 11 expression, 64 sustain, 65 portamento switch,
91 reverb send, 93 chorus send, plus 120/121/123-127. CC 1, CC 11 and channel
aftertouch are the modulation matrix's three sources.

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

### Fitting a 4 MB Pico 2

The full image is 4.33 MB and needs a 16 MB board. `-DPICOFACEJV_4MB=ON` builds
a 3.76 MB image that fits the base Pico 2, leaving 234 KB clear of the veeprom
sectors at the top of flash:

```bash
cmake -S . -B build-4mb -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release -DPICOFACEJV_4MB=ON
cmake --build build-4mb --target PicoFaceJV
```

What it drops is the **user bank**: 64 patches, and with them the 22 samples
nothing else uses. Banks A and B — 128 patches — remain, and they are not
degraded in any way. Nothing is resampled, requantised or shortened; the
samples they reach are relocated into a smaller blob and the sample table is
rewritten to match. All 128 render bit-identically to the full build, which the
host harness checks patch by patch.

The saving comes from three places, in order of size: the user bank's own
samples, the 39 samples no patch of any bank references at all, and the ~130 KB
that plain sequential packing strands at the 1 MB page boundaries. The wave
address space is paged because the exponent nibbles for a byte live in the
first 32 KB of that byte's own 1 MB page, so a sample cannot cross a page and
cannot move by anything other than a multiple of 32. `compact()` in
[jv_make_blob.py](../../tools/jv_extract/jv_make_blob.py) carries the details.

Going further is not worth it: keeping the user bank as well lands at 3.99 MB,
which leaves 7 KB — not a margin, and any future code growth breaks the build.

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
