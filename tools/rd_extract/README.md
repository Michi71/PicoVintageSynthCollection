# Way-2 Phase 1: Extraction and Reference Tools (Host)

*(Host-side extraction/verification toolchain for PicoFaceRD. For an overview
of the architecture and the data pipeline see
[../../instruments/PicoFaceRD/doc/ARCHITECTURE.md](../../instruments/PicoFaceRD/doc/ARCHITECTURE.md).)*

All tools link against the RD engine (native build, no Pico SDK). Paths below
are relative to the repository root; `I=instruments/PicoFaceRD` is used as a
shorthand for the instrument directory.

Build pattern:

    I=instruments/PicoFaceRD
    clang++ -O2 -std=c++17 -I $I/include/rd_engine -o <tool> tools/rd_extract/<tool>.cpp \
        $I/src/rd_engine/mcu.cpp $I/src/rd_engine/sound_chip.cpp \
        $I/src/rd_engine/mks20a_tables.cpp $I/src/rd_engine/mks20b_tables.cpp \
        $I/src/rd_engine/mk80_tables.cpp $I/src/rd_engine/program_tables.cpp \
        $I/src/rd_engine/rd_samples_ilv_a.cpp $I/src/rd_engine/rd_samples_ilv_b.cpp \
        $I/src/rd_engine/rd_samples_ilv_m.cpp

- `rd_extract.cpp`  — register capture per (patch, note, velocity) -> JSONL.
    ./rd_extract <patch> <out.jsonl> [notelist] [velocity]
- `rd_analyze.py`   — JSONL -> part descriptors (pitch/wave/env segment chains).
    python3 rd_analyze.py <in.jsonl> <out.json>
- `rd_wavedump.cpp` — sample-ROM regions (exp-decoded, full level) -> WAV.
    ./rd_wavedump <a|b|m> <wave_high|all> <outdir> [samplerate]
- `rd_host_test.cpp` / `rd_gliss_test.cpp` / `rd_burst_test.cpp` — reference
  regression (bit identity), glissando (+ "dual" thread mode), event bursts.

The capture hook lives in `$I/include/rd_engine/rd_capture.h` + `sound_chip.cpp`
(host-only, the firmware stays byte-identical).

## Way-2 Phase 2: New engine (host prototype)

- `$I/include/rd_engine/rd_new_engine.h` + `$I/src/rd_engine/rd_new_engine.cpp`:
  RdNewEngine — plays directly from .rdp packs (timeline replay of the
  firmware's chip programming, bit-exact part arithmetic, 32 voice slots).
- `rd_pack.py`   — descriptor JSON -> .rdp binary pack (~170 KB/patch).
- `rd_ab_test.cpp` — A/B new engine vs. reference emulator:
    ./rd_ab_test <pack.rdp> <patch> [note] [vel] [prefix]
  The reference side gets a primer note first (fresh-boot quirk of the
  firmware: the very first high note is killed after ~70 samples); metric =
  lag-scan cross-correlation ±300 samples (onset alignment is hypersensitive
  at high frequencies).

First validation (note-by-note, vel 110): r = 0.90–0.97, RMS ratio
0.93–1.02 across all three ROM banks (piano/harpsichord/MK-80).

## v2.2: 4-byte sample banks + block doorbell

- `gen_pk4.cpp` — losslessly repacks the 8-byte RdSampleEntry banks into u32
  (bits[13:0]=exp, [14]=exp_sign, [23:15]=delta, [24]=delta_sign; bit widths
  verified across all banks, the generator re-checks them). Halves the
  dominant XIP-miss stream (2 entries per 8-byte line) and 1.5 MB of flash.
- Since v2.2 the engine renders in 64-sample blocks with ONE core-1
  rendezvous per block (voice-outer/sample-inner: part state stays in
  registers, sample-ROM reads become sequential). renderSample() remains as
  a 1-sample wrapper for the host tools.

## How much of the render is waiting for flash

`build_xip_probe.sh` answers that with a number instead of a hunch. It builds
the engine for the host with `RD_XIP_TRACE` defined, which enables one hook at
the wave-ROM read in `rd_new_engine.cpp`, captures every address, and runs the
stream through a model of the RP2350's XIP cache (16 KB, two-way, 8-byte lines
-- the geometry the 4-byte packing above assumes). No firmware build defines
the macro; `nm` on `PicoFaceRD.elf` shows no such symbol.

```bash
tools/rd_extract/build_xip_probe.sh 0 12 200      # patch, voices, blocks
```

**Patch numbers here are engine indices, 0-15.** The instrument displays
`instrument() + 1`, so every number in this section is one below what the PATCH
page shows. The two patches with no flash cost at all are indices 5 and 15,
which are **`06 Vibraphone` and `16 Vibraphone`** on the device -- and that they
are both vibraphones is the mechanism showing through: short, simple, heavily
reused waveforms fit the cache, where a piano's spread across the ROM does not.

**What it found.** At the 32 kHz base limit of twelve voices the engine issues
119 wave-ROM loads per sample -- one per part, ten parts per note, as expected
-- and the flash cost is enormous but wildly patch-dependent:

| patch | rate | miss rate | flash-bound | | patch | rate | miss rate | flash-bound |
|---|---|---|---|---|---|---|---|---|
| 3 | 32k | 85.6 % | **65.7 %** | | 8 | 20k | 43.7 % | 19.6 % |
| 14 | 32k | 84.5 % | 64.9 % | | 11 | 32k | 37.1 % | 28.5 % |
| 0 | 20k | 77.1 % | 36.7 % | | 7 | 32k | 17.1 % | 12.4 % |
| 12 | 20k | 76.4 % | 35.8 % | | **5** | 20k | **0.1 %** | **0.1 %** |
| 13 | 20k | 82.9 % | 39.8 % | | **15** | 20k | **0.1 %** | **0.0 %** |

The rate column matters and used to be missing: eleven of the sixteen patches
render at 20 kHz and get half again as many cycles per sample as the five at
32 kHz. The probe assumed 32 kHz for all of them, which overstated every
20 kHz patch's flash-bound figure by a factor of 1.6. Fixed; the numbers above
are the corrected ones.

Miss rate is genuinely patch-dependent, and on patches 5 and 15 the cost is
nil -- their wave data fits the cache and every voice reuses it -- and stays
nil as voices are added:

| voices | patch 15 | patch 5 | patch 3 |
|---|---|---|---|
| 12 | 0.0 % | 0.1 % | 65.7 % |
| 16 | 0.0 % | 0.1 % | 82.8 % |
| 24 | 0.0 % | 0.1 % | **95.7 %** |
| 32 | 0.0 % | 0.1 % | 96.3 % |

## What the hardware said, and what it cost this section

The paragraph that used to sit here said the probe showed only that the flash
bottleneck disappears, not that a cache-clean patch can run twenty-four voices
-- and proposed the device settle it, since VOICES offers fixed polyphony and
the footer shows peak load. That test was run. It settled something else.

The decisive pair, both held until the footer read `A24`:

| device | index | rate | miss rate | peak load |
|---|---|---|---|---|
| **16 Vibraphone** | 15 | 20 kHz | **0.1 %** | **91 %** |
| **15 Clavi** | 14 | 32 kHz | **68.3 %** | **67 %** |

The patch that never touches flash is the expensive one. It issues 200.5
wave-ROM loads per sample against the clavinet's 217.0, at a *lower* sample
rate, with essentially every one of them a cache hit -- and it uses 91 % of its
budget where the clavinet uses 67 % of its. The device computes load against
each patch's own rate (`budget = length * 1e6 / s_sampleRates[instrument_]`),
so the two percentages are directly comparable.

That is not a caveat, it is a refutation. **Flash stalls are not what sets this
engine's ceiling.** Four more patches, measured the same way, say the same
thing from the other side:

| device | index | miss rate | peak load |
|---|---|---|---|
| 02 Piano 2 | 1 | 56.0 % | 100 % |
| 03 Piano 3 | 2 | 59.5 % | 57 % |
| 14 A. Piano 2 | 13 | 58.7 % | 90 % |
| 15 Clavi | 14 | 68.3 % | 67 % |

The lowest miss rate carries the highest load. Neither miss rate nor sample
rate orders this list.

**The 96-cycles-per-miss figure is refuted outright.** For device 15 the model
claims 94.8 % of the budget on stalls alone, while the whole patch measures
67 % -- and stalls cannot exceed the total. The true cost is under 68 cycles
per miss, and since the arithmetic is clearly substantial, well under.

What the probe measures -- the address stream, the hit and miss counts through
a stated cache geometry -- stands. What it never measured is the arithmetic
per part, which is now the obvious candidate for the difference: two patches
with near-identical load counts and opposite cache behaviour, and the cheap
one for the cache is the expensive one for the CPU. Envelope segment counts
and part types differ per patch and none of that is in this tool.

**So read the tables above as cache behaviour, not as a cycle budget.** They
correctly say which patches thrash and which do not, and the factor of several
hundred between the extremes is real. They do not predict load, and the
"flash-bound %" column should be read as an upper bound that hardware has
already shown to be loose.

A per-patch voice limit derived from these numbers -- floated in the paragraph
this replaces -- would therefore have been tuned to the wrong variable. The
lever is real; the measurement to derive it from is not this one.

## Regression runner (one command)

    tools/rd_extract/run_regression.sh

Builds `rd_dump_packs` (writes the 16 packs embedded in the firmware image
from `rd_packs_data.cpp` back to `build_host/packs/` at the repository root — so the test covers
exactly what ships, with no scratchpad dependency), `rd_ab_test`
and `rd_stress2`, then checks:

- A/B matrix against the reference emulator (6 cells: p0n60, p0n36, p3n60,
  p4n60, p8n60, p15n60; vel 110) — r must match the frozen reference values
  to ±0.002, rmsRatio within [0.85 … 1.15].
- Stuck-voice stress (`rd_stress2`, chord hammering + pedal): instr 4 and 0,
  single- and dual-thread — tailRMS < 0.001.

Output: one `[ok]/[FAIL]` line per check, `REGRESSION PASS/FAIL` at the end,
exit code 0/1. The working directory `build_host/` is gitignored.
Updating the reference values: only after a deliberate, auditioned sound
change — enter the new r values from the run into `ab_cases` in the script.
