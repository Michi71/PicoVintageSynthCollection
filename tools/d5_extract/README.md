# d5_extract - Roland D-50 ROM extraction

Host-side toolchain for a planned PicoFaceD5. Nothing here is part of a
firmware image, and no ROM data is in this repository.

As with the JV, the goal is **not** a cycle emulation of the D-50's hardware.
It is a native LA engine that plays the original PCM data. That requires the
sample data (decoded below) and the sample table -- start, length and loop
metadata for the 100 PCM sounds -- which is the current blocker (see "Where
the sample table is").

## ROM images

You need your own D-50 ROM set. Point the tools at a directory; files are
identified by CRC32 after folding doubled dumps, names do not matter:

| Image | Size | CRC32 | Purpose |
|---|---|---|---|
| PCM A (TC532000P-7469, IC30) | 256 KB | `1461c0fb` | lower 128K words of sample data |
| PCM B (TC532000P-7470, IC29) | 256 KB | `e50599bf` | upper 128K words of sample data |
| program EPROM (27C512, IC22) | 64 KB | `e92c69f9` (v2.22) et al. | firmware; PCM names, UI, LUTs |
| uPD78312 internal ROM (IC25) | 8 KB | `9564903f` | CPU-internal kernel; **required for the sample table**, currently missing from the local set |

A 512 KB image with CRC `e2aed2d9` (TC534000P-7477, late boards) is accepted
as a combined A+B substitute. 512 KB dumps of the 256 KB chips (doubled
read-outs) are folded automatically; the CRCs above refer to the folded size.

## PCM data format

16-bit big-endian words per chip, read each chip on its own (the two are
separate address ranges, not interleaved). Each word is bit-permuted by the
board routing and log-encoded; both details are documented in the VOGONS
thread on LA-synth sample ROMs (topic 77094), which this directory
reimplements independently in `d5_rom.py`:

```
ordered = permute(raw)            # 15,6,14..8,5..0,7 (see _reorder_bits)
sample  = sign(ordered) * 2^(((ordered & 0x7FFF) - 32767) / 2048)
```

The decoded space is 262144 words at 32 kHz mono (8.2 s). Verified against
this set: both chips decode to clean audio (mean |diff|/RMS 0.22/0.28, white
noise would be ~1.7), and every detected attack transient starts exactly on a
2048-word page boundary.

## What is known about the 100 samples

The structure matches the owner's manual: PCM 1..47 attack transients,
48..76 static loops, 77..100 combinations of earlier samples played as loops.
The names live in the program EPROM at file offset 0xFC00, 100 entries of 6
ASCII chars, identical in v1.06 and v2.22.

Acoustic ground truth derived from the decoded audio (used to verify any
table hypothesis, see `d5_table_scan.py`): 45 pages qualify as attack
starts, exactly one page (112) is spectrally flat noise ("Noise", PCM 76),
and running `d5_rom.py` on the set prints the per-page classification.

## Where the sample table is: inside the synth chip

Not in the 64 KB EPROM: that was searched exhaustively -- monotone address
runs, set-coverage windows, page-aligned u16/u24/byte tables in every unit
and stride, anchored on the acoustic ground truth, over the regions that
are identical across the EPROM versions (the table cannot differ between
firmware versions that drive identical PCM ROMs). Everything that lights up
is UI data or envelope/pitch LUTs.

Not in the uPD78312's 8 KB internal ROM either (`d78312g-022_15179266.ic25`,
CRC `9564903f`; the part number matches the service notes' IC25 entry).
Disassembling it (MAME debugger, `upd78k3` core; memory map in MAME's
`roland_d50.cpp`: internal ROM 0x0000-0x1FFF, EPROM 0x2000-0x7FFF plus
banked 16 KB pages at 0x8000-0xBFFF, synth chip at 0xE000-0xE7FF) shows a
kernel that services envelopes and modulation from RAM blocks the
application stages -- and never writes anything address-shaped to the chip.
The application never touches the chip at all.

Conclusion: the MB87136 resolves PCM numbers to ROM addresses in its own
mask ROM. No CPU-side dump contains the table, which is why it has never
been documented.

## Reconstructing the table anyway

Two independent paths, both in this directory:

**Without hardware** (`d5_table_derive.py`): the layout model "PCM 1..100
in numeric order" plus audio segmentation. Attacks start page-aligned at
onsets; the "Noise" block (PCM 76) is exactly detectable and pins the
static zone's end; the factory-rendered combination loops 77..100 follow
it. The attack/static frontier and the sub-page loop boundaries are chosen
by maximizing named-family checks (Lpiano<Mpiano<Hpiano in pitch, Horgan
above Lorgan, EP/SAX pairs spectrally similar, the seven Spect loops a
similarity block). Current result: **all 6 checks pass**, frontier at page
94, Noise at pages 112..113. Output is
`d50_sample_table_hypothesis.json` plus one WAV per sample under
`samples_hypothesis/` -- reviewing those against the known names by ear is
the acceptance test.

**With a real D-50/D-550** (`d5_probe_midi.py` + `d5_match.py`): the
precision path, open to anyone with the hardware. `d5_probe_midi.py` emits
a MIDI file that configures a bare PCM partial via SysEx (temporary area
only) and plays PCM 1..100 one note at a time, preceded by a calibration
block that identifies a clean structure/mute combination. `d5_match.py`
locates every recorded note inside the decoded ROM audio by envelope plus
sample-exact cross-correlation (with global playback-rate calibration) and
emits the measured table. A dry recording of the probe run is enough to
confirm or correct the hypothesis table sample by sample.

## Tools

| File | What it does |
|---|---|
| `d5_rom.py` | CRC identification, dump folding, PCM decode, name table, per-page acoustic classification. Importable; run directly for a ROM-set summary. |
| `d5_wavedump.py` | renders the decoded PCM space to WAV (full + per chip) for listening. |
| `d5_table_scan.py` | anchored sample-table search over a binary image (how the EPROM and internal ROM were ruled out). |
| `d5_table_derive.py` | hardware-free table reconstruction with family validation (needs numpy). |
| `d5_probe_midi.py` | probe MIDI generator for measuring a real D-50. |
| `d5_match.py` | matches a probe recording against the ROM audio, emits the measured table (needs numpy). |

```bash
python3 tools/d5_extract/d5_rom.py ~/develop/Roland_D50
python3 tools/d5_extract/d5_wavedump.py ~/develop/Roland_D50
python3 tools/d5_extract/d5_table_derive.py ~/develop/Roland_D50
```

Outputs land in `tools/d5_extract/out/`, which is not committed.
