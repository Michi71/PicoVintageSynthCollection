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

**What it found.** At the 32 kHz base limit of twelve voices the engine issues
119 wave-ROM loads per sample -- one per part, ten parts per note, as expected
-- and the flash cost is enormous but wildly patch-dependent:

| patch | miss rate | flash-bound | | patch | miss rate | flash-bound |
|---|---|---|---|---|---|---|
| 3 | 85.7 % | **65.8 %** | | 8 | 43.0 % | 32.1 % |
| 14 | 84.6 % | 65.0 % | | 11 | 37.3 % | 28.6 % |
| 0 | 77.1 % | 58.7 % | | 7 | 16.2 % | 12.5 % |
| 12 | 75.8 % | 58.1 % | | **5** | **0.2 %** | **0.2 %** |
| 13 | 83.1 % | 63.8 % | | **15** | **0.1 %** | **0.1 %** |

Two thirds of the cycle budget goes to stalls on the worst patches. On patches
5 and 15 the cost is nil -- their wave data fits the cache and every voice
reuses it -- and it stays nil as voices are added:

| voices | patch 15 | patch 5 | patch 3 |
|---|---|---|---|
| 12 | 0.1 % | 0.2 % | 65.8 % |
| 16 | 0.1 % | 0.2 % | 82.8 % |
| 24 | 0.1 % | 0.2 % | **95.7 %** |
| 32 | 0.1 % | 0.2 % | 96.3 % |

So the base limit of twelve is set by patches like 3, which is already at 96 %
of budget on stalls alone at twenty-four voices -- while patches 5 and 15 never
touch flash at all. A per-patch base limit, derived offline from this
measurement rather than guessed at runtime, is the lever the voice governor
does not currently have.

**What this does not show.** Only that the flash bottleneck disappears, not
that those patches can run twenty-four voices: the arithmetic scales with voice
count too, and this probe does not measure it. If it fails there, it will not
be flash. The device can answer that today without any code change -- VOICES
offers fixed polyphony, the footer shows peak load, so patch 15 at 24 voices
against patch 3 at 24 voices settles it.

**On the numbers.** The miss rate is measured: a real address stream from the
real engine through a stated cache geometry. The conversion to percent of
budget is an estimate with its assumptions in the source -- 96 CPU cycles per
miss, from a 120 MHz QSPI fetch at the 4:1 clock ratio. Halve or double that
and the absolute figures move; the ordering, and the factor of several hundred
between patch 3 and patch 15, do not.

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
