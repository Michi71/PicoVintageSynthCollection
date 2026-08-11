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
give it up easily, but it is not sealed either.

The file is an update container (`ESC91.000` magic) holding a plain ARM
Thumb loader with readable driver strings up to ~0x30000, followed by a
payload that measures as uniformly distributed: chi-square 158503 per 4 KB
against a uniform model, no archive signature, one duplicate 16-byte block
in 64 KB. That reads as encryption, and this file said so until the payload
turned out to contain factory patch names -- `Pizzagogo`, `Spacious Sweep`,
`Synthesizer D-05` -- which random data does not.

They sit in **LZSS-style compression**, not in cipher text. The bytes make
that plain: `FF` before `Pizzagog`, then `F3` before the rest of the word,
`FF` before `Spacio`. Those are the control bytes of an LZ77 variant with
eight symbols per flag byte, a set bit meaning "literal" -- which is why a
name appears in runs of eight interrupted by one byte, and why compressed
data measures as random overall. Whoever wants Roland's own patch and table
data can decompress this; the format is a small, well-trodden one.

It stopped being necessary here: the genuine factory bank exists as a SysEx
dump of the PN-D50-00 ROM card, the memory card the D-50 shipped with, and
that is what the instrument plays.

**The format is cracked; `d5_bq3_decompress.py` unpacks it.** The routine that
did the guessing unnecessary is in the plaintext ARM Thumb loader: at file
offset 0x58A it loads `MOVW r0, #0x0FEE`, and 0x0FEE = 4078 = N-F is the
canonical ring-buffer start of Okumura LZSS (window N=4096, longest match
F=18). Disassembling the routine settled every detail -- the ring buffer is
zero-filled (not space-filled), the flag byte is read LSB-first, a set bit is
a literal, and a match encodes position as `b1 | ((b2 & 0xF0) << 4)` and
length as `(b2 & 0x0F) + 3`.

The container holds three named components, each with a 0x40-byte header that
is its own descriptor (source offset at +0x2C, compressed size at +0x30,
destination at +0x38, decompressed size at +0x3C): `BQ3:Updater`,
`BQ3:SUB-CPU` and `BQ3:Appli`, the last being the D-50 re-implementation. It
decompresses to exactly its declared 1601636 bytes and consumes its
compressed input to exactly the region boundary -- a wrong format would not
land on either, so the decode is verified by construction.

**What it gives, and what it does not.** Not the samples: the PCM audio is not
in the update at all -- searched raw, bit-permuted, decoded to 16-bit and
8-bit, in both the compressed and the decompressed image -- because the D-05,
like the D-50, keeps its sample data in a separate mask ROM. What is in the
decompressed `BQ3:Appli` is ARM code and data tables, and Roland's own sample
table should be among them, root pitches included.

**The table is not in the update either, and the search is finished.** What
the decompressed application does contain is Roland's own PCM list: an array
of exactly 100 pointers at device address 0x60165CA8, in PCM order 1 to 100,
into a string pool holding every one of the D-50's wave names -- independent
confirmation that our name table and its numbering are right. What it does
not contain is any structure carrying start, length, loop or root pitch for
those 100 waves. Searched for as monotone address runs in every unit and
stride, as a length field restricted to the power-of-two sizes the layout law
demands, as a 100-entry parameter table anywhere near the name list, and by
following the one code reference to that list. The other two components rule
themselves out: `BQ3:Updater` is the flash writer, and `BQ3:SUB-CPU` is a
24 KB STM32 panel controller with no PCM data at all.

The conclusion is the same architecture we found in the D-50 itself, kept
across thirty years: the wave addressing lives with the waves, in the sound
subsystem's own ROM, not with the CPU firmware. A firmware update carries the
application and the panel, and neither needs to know where a sample starts.
So the reissue confirms the finding rather than dissolving it -- and the
reconstructed table stays what it is, verified against the genuine ROMs,
which are the authority the D-05 can only reproduce anyway.

## What the ROM marks itself

One boundary marker is in the sample data after all, and it took a listener's
doubt about the later samples to go looking for it. A one-shot is padded to
the end of its slot with digital silence, which in this log format is the
all-zero word -- magnitude zero is 2^-16, far below anything the format
otherwise encodes. So a run of zero words is the tail of a sample, and the
page boundary just past it is where the next one starts.

That gives 46 boundaries stated by the ROM itself, and **every one of them
agrees with the reconstructed table**, including the three positions measured
independently against labeled rips. The attack half of the table (PCM 1..47)
is confirmed by the data, not only by ear. `zero_run_boundaries()` in
`d5_table_derive.py` now enforces them: a candidate table that contradicts one
is discarded.

The marks stop at page 92, where the sustained loops begin -- a loop fills its
slot, so there is no padding to mark. And that is exactly the region where the
by-ear review kept finding trouble. Two findings there, from the one law those
samples do obey (a loop repeats at exactly one period):

- **PCM 60 (CELLlp) and PCM 62 (Reedlp) are wrong.** Their regions contain
  internal period changes -- 256 to 127 to 128 across CELLlp, and 128 to 256
  to 27 across Reedlp -- which means each spans more than one sample. They are
  also the two over-long regions in the table, at 4096 and 8192 words.
- **Fifteen of the static loops are internally consistent** (one period from
  start to end) and are very likely right.

Putting `cp_sampleprep`'s loop finder on them sharpened that considerably --
it is the better judge, and it found the harpsichord's real period of 156
words where the estimator here had bottomed out at its 16-word floor. Offered
several period counts (its default of ten rejects any sample too short to
hold that many), it rates **20 of the 29 static loops excellent or good**,
which confirms those boundaries from a direction nothing else in this
directory tests. Nine do not loop: EG_lp, SAXlp1, SAXlp2, Ooh_lp, Manlp1,
Spect3, Spect4, Spect6 and Spect7. For several of those a half or a quarter
of the region loops perfectly while the rest does not, which is what a region
merging two samples looks like.

The period changes in that zone land on 512-word positions, not on the 2048
grid the table uses, so the static boundaries were quantised too coarsely.
Deriving the correct ones automatically did not work: the Spect series and
Noise are aperiodic by construction, so a segmentation that scores periodicity
puts its boundaries wherever it likes there. The stretch was settled the way
every other boundary in this table was -- by ear, from candidate splits: the
one following the measured period zones won, giving CELLlp 1536 words,
VIOLlp 5632 and Reedlp 7168. The two strings come out at 257 and 263 Hz,
which is why no amount of period analysis could have separated them.

**And there is no header.** The obvious way to build such a ROM is to put a
small record in front of each sample carrying its length and pitch, so that
addressing the sample delivers its parameters with it. Tested against the 47
confirmed starts: the first word is 0x0000 in every single one, and the second
through sixth are 47 distinct values apiece that continue smoothly into the
waveform. So the ROM marks where a sample begins and says nothing else about
it. That is consistent with everything else here -- the parameters live in the
synth chip, and the sample ROM holds nothing but samples.

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
| `d5_syx_to_patches.py` | converts a D-50 SysEx bulk dump into `d5_patch_data.h`: 64 patches as raw parameter bytes, checksums and parameter ranges verified. |
| `d5_bq3_decompress.py` | unpacks a Roland Boutique BQ3 firmware update (D-05) into its components -- Okumura LZSS, verified against the loader's own routine. |
| `d5_review_render.py` | renders the frozen table for review by ear: one-shots padded with silence, loops tiled and pitch-normalised, plus one file with all of them in order, and the unprocessed cuts under `raw/`. |
| `d5_loop_audit.py` | judges every static loop with `cp_sampleprep/FindLoopPoints`: a sustain loop that will not loop is not one. |
| `d5_repartition.py` | re-splits the stretches that fail that audit, scoring candidate partitions with the same loop finder. |
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
./d5_render --bank build/d5/d5_pcm.bin b.wav 1 6 8   # patches from a converted bank
```

## Patch banks

`d5_syx_to_patches.py` reads any D-50 bulk dump: DT1 messages from address
02-00-00, 448 bytes per patch in seven 64-byte blocks (upper partial 1 and 2,
upper common, lower partial 1 and 2, lower common, patch). Roland's own manual
lists those addresses with a gap in them; the layout above is what the data
shows, and the converter proves it -- it checks eight parameters whose ranges
are documented, and a wrong block assignment puts them out of range at once.

The bytes stay raw in the generated header. `d5_patch_map.h` in the instrument
converts them into engine specs, so one piece of code knows what parameter 22
of a partial means -- and the same conversion can serve a patch arriving over
MIDI later, since that is the identical format.

Outputs land in `tools/d5_extract/out/`, which is not committed.
