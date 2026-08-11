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

### The "600-byte table" the D-50 really has

A claim that circulates is that the D-50's firmware holds a 600-byte table
of 100 six-byte entries carrying start, loop and flag data. Half of that is
right: there is exactly such a table, 100 entries of 6 bytes at file offset
0xFC00 -- but every one of its 600 bytes is printable ASCII. It is the list
of PCM *names* (`Marmba`, `Vibes `, ... `Loop24`), and it is what this
directory uses to label samples.

No second 100x6 table carries addresses. Tested exhaustively over both the
EPROM and the internal ROM: every base offset, all six field positions,
byte / u16 (both endiannesses) / u24 fields, and every plausible address
unit, required to reproduce the six independently established positions
(the three ear-confirmed pianos plus the measured Steam, Lips1 and Pizz).
Zero tables satisfy all six; the best partial agreement is two of six,
which is what chance produces across that many candidates.

The other half of the claim -- that the ROM data is bit-scrambled and needs
a specific algorithm to decode -- is correct, and that decoder is
`d5_rom.py`. And MAME does not document these sounds: its `roland_d50.cpp`
is a skeleton flagged `MACHINE_NOT_WORKING | MACHINE_NO_SOUND`, with the
synth chip line commented out.

### The D-05 Boutique firmware

`BQ3_UPD.BIN` (4 MB) is the obvious place to look for Roland's own copy of
the table, since the 2017 reissue reproduces the D-50 exactly. It does not
yield it: the file is an update container (`ESC91.000` magic) holding a
plain ARM Thumb loader with readable USB driver strings up to ~0x30000,
followed by a payload that is uniformly distributed (chi-square 158503 per
4 KB against a uniform model, no compression signature, one duplicate
16-byte block in 64 KB -- so neither compressed nor ECB-encrypted, but
encrypted with a stream or chained cipher). Neither the PCM data in any
tested encoding nor the sample names appear in it. Anyone revisiting this
needs the loader's key handling, i.e. an analysis of the ARM code in the
first section.

## Reconstructing the table anyway

Two independent paths, both in this directory.

**Without hardware** (`d5_table_derive.py`): the layout model "PCM 1..100
in numeric order" plus audio segmentation, constrained by everything that
could be established independently:

- every region is `2048 << n` words on the page grid, the table format the
  MT-32 sibling uses (munt, `ControlROMPCMStruct`: `addr = pos * 0x800`,
  `len = 0x800 << exp`);
- three positions are measured, not derived: labeled sample rips found on
  the web cross-correlate against the decoded ROM at 0.95 to 1.00 (Steam
  at word 135168, Lips1 at 155648, Pizz at 184320). A fourth, Loop19,
  matches the very start of the ROM -- which is how the combination loops
  77..100 turned out to be address ranges over the primary material rather
  than stored data;
- 18 weighted checks over named families (pitch order of the three pianos,
  the organ octave, pair and block similarity, formant ratios for the
  vocals, spectral flatness for the Spect series and Noise) plus a purity
  check that rejects any attack region containing a timbre change;
- and the ear review: ten rounds of listening produced start positions,
  inequalities ("the clarinet begins later than this") and two refuted
  expectations (Xylo1/Xylo2 and Eguit1/Eguit2 are genuinely different
  instruments, not pairs).

The frozen result is [`d5_sample_table.json`](d5_sample_table.json), with
`basis` per entry: 3 measured, 29 ear-confirmed, 30 derived, 14 forced by
arithmetic (the last 14 samples fill the last 14 pages, one page each, so
nothing is left to choose), 24 unresolved combination ranges. Regions
1..76 tile the ROM without a gap.

What the table does **not** carry is a root pitch per sample -- that lives
in the chip with the addresses. `d5_make_blob.py` measures one from the
material instead (lowest prominent partial, unpitched material reports 0).
Those values are estimates; the three pianos come out an octave apart as
they audibly are, but individual entries can be off by an octave and want
a pass against reference recordings once the engine plays.

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
| `d5_make_blob.py` | decodes the ROM set into `d5_pcm.bin` (512 KiB, 16-bit) plus `d5_blob.S` and `d5_pcm_table.h` for the firmware. |
| `d5_sample_table.json` | the frozen table: start, length, loop flag and provenance per sample. |

```bash
python3 tools/d5_extract/d5_rom.py ~/develop/Roland_D50
python3 tools/d5_extract/d5_wavedump.py ~/develop/Roland_D50
python3 tools/d5_extract/d5_table_derive.py ~/develop/Roland_D50
python3 tools/d5_extract/d5_make_blob.py ~/develop/Roland_D50 build/d5
```

The engine can be heard on the host without hardware:

```bash
c++ -O2 -std=c++17 -Ibuild/d5 -Iinstruments/PicoFaceD5/include \
    -o d5_render tools/host_tests/d5_engine_test/render.cpp
./d5_render build/d5/d5_pcm.bin survey.wav    # every resolved sample
./d5_render --synth synth.wav                 # cutoff, resonance, pulse width
./d5_render --la build/d5/d5_pcm.bin la.wav   # sampled attack + synth sustain
./d5_render --structures build/d5/d5_pcm.bin s.wav   # all seven structures
./d5_render --mod build/d5/d5_pcm.bin mod.wav        # LFOs and pitch envelope
./d5_render --fx build/d5/d5_pcm.bin fx.wav          # equalizer, chorus, reverb
```

Outputs land in `tools/d5_extract/out/`, which is not committed.
