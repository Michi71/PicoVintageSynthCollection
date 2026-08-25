# PicoFaceD5 - Roland D-50

A native LA engine over the D-50's own PCM data: sampled attacks dovetailed
with synthesized sustains, the seven structures with their ring modulator, the
three tone-global LFOs and the pitch envelope, and the common block's
equalizer, chorus and reverb. Sixteen voices on one tone, eight and eight when
both play -- the machine's own polyphony, out of its own allocator.

Like PicoFaceJV, this instrument **needs a local ROM set** and is therefore not
in the release binaries. Without one the configure step skips it with a note
and everybody else's build stays green.

## What it needs

Put the D-50's ROM images in `roms/` (gitignored). Files are identified by
content, so their names do not matter:

| Image | Size | CRC32 | Purpose |
|---|---|---|---|
| PCM ROM A (IC30) | 256 KB | `1461C0FB` | lower half of the sample data |
| PCM ROM B (IC29) | 256 KB | `E50599BF` | upper half |
| PCM combined (late boards) | 512 KB | `E2AED2D9` | A and B in one chip, accepted in place of the pair |
| program EPROM (IC22) | 64 KB | any known version | the PCM names |

The EPROM versions the converter recognises are v1.04, v1.10, v2.10, v2.20,
v2.21, v2.22 and v1.06; an unknown 64 KB image carrying the name table is
accepted with a warning. `tools/d5_extract/d5_rom.py` holds the checksums.

512 KB dumps that contain a 256 KB chip twice are folded automatically, and a
combined 512 KB image of both chips is accepted in place of the pair. The
build converts them once at configure time into a 512 KB blob of 16-bit
samples, so the firmware needs no decoding table at runtime.

**The patches are a separate file, and they are optional.** Those three images
are the sound; the D-50's sixty-four factory *patches* live in a SysEx bulk
dump, which goes in the same `roms/` folder as a `.syx`. Without one the
instrument still builds and plays -- with eight hand-built presets instead of
the factory bank, and the configure step says so. See
[The patches](#the-patches) below.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceD5
cmake --build build
```

The result is 884 KB of flash - 512 KB of it sample data, 168 KB the six patch
banks - and 264 KB of RAM, which fits any RP2350 board including the 4 MB ones.
Of the collection's four sample-based instruments this is the only one that
does.

## Playing it

The select encoder walks nine pages; A and B are the two value knobs. Every
parameter below the first two rows is a D-50 parameter in the D-50's own
units, and the ranges are the firmware's own -- they are read out of the
maximum-value table in its EPROM rather than guessed:

| Page | Encoder A | Encoder B | D-50 parameter |
|---|---|---|---|
| Patch | patch | polyphony cap | - |
| Mix | master volume | tone balance | pb33 |
| Reverb | balance | type 1-32 | pb31 / pb30 |
| Chorus | balance | type 1-8 | c45 / c42 |
| Cho Mod | rate | depth | c43 / c44 |
| EQ Low | frequency (shown in Hz) | gain +/-12 dB | c37 / c38 |
| EQ High | frequency (shown in Hz) | gain +/-12 dB | c39 / c41 |
| EQ Q | Q | - | c40 |
| Tune | master tune | MIDI channel | - |

The patch parameters follow the patch when it changes and are not written to
the settings: they belong to the patch, not to the panel. Chorus and equalizer
are per tone on the real machine; these pages edit both tones together and
read the upper one back.

The footer reads `P B U A/limit N`: peak load, the boot benchmark, the I2S
underrun counter, sounding voices against what the governor allows, and the
note counter -- which turns into `S<n>` while the CPU governor is trimming
tails, n being how many it retired in the last second. Zero, or an `N`, means
the render has room.

### MIDI

The D-50's own control-change list is short, and it is known exactly: its
receive dispatcher indexes a table in the EPROM, and that table reads 1, 5, 6,
7, 38, 64, 65, 98, 99, 100, 101, plus the mode messages 122-127. Implemented
here: **CC1** (modulation lever), **CC5** and **CC65** (portamento time and
switch), **CC6** with **CC100/101** (RPN 0, bender range), **CC7** (volume),
**CC64** (hold pedal), all-notes-off, program change, pitch bend and channel
pressure. CC38 and the NRPN pair are received by the original but have nothing
here to act on.

Two controls are ours and **not** on the original, worth knowing when
comparing against real hardware: **CC91/CC93** move reverb and chorus balance
(General MIDI sends, four years younger than this machine), and **CC0** picks
the bank ahead of a program change, without which the six banks cannot be
reached at all.

### SysEx

Roland's one-way exclusive, `F0 41 <device> 14 <command> <address> <data>
<checksum> F7`, is spoken in both directions:

- **DT1 into the temporary area** (address `00 00 00`, 448 bytes in the same
  seven 64-byte blocks a bulk dump carries) -- a single parameter or a whole
  patch. This is how an editor programs the instrument, and it takes effect
  while notes are sounding: no delay line is cleared and no LFO phase
  restarted, so a stream of edits does not click.
- **DT1 into internal memory** (`02 00 00`, 448 bytes a slot) -- a librarian
  can send a whole bank of 64. They are held in RAM, shadowing the flash bank,
  and each announces itself with the name in its own bytes. They survive patch
  changes but not a power cycle: the D-50 keeps its sixty-four in
  battery-backed memory, ours would be flash, and a flash write per message
  would stall the render and wear the part.
- **RQ1** is answered with DT1 messages of 256 data bytes each, the same
  chunking the machine itself uses, from the temporary area or from internal
  memory -- so a bank can be pulled back out for backup.

Not implemented: the handshake protocol (WSD/RQD/DAT/ACK/EOD) that some older
librarians use instead of RQ1/DT1.

## The patches

Drop D-50 SysEx bulk dumps (`*.syx`) into `roms/` beside the ROM images and
the build converts them: **several dumps make several banks of 64** - the
factory card leads, the rest follow in filename order, up to 384 patches.
The converter verifies every checksum and eight documented parameter ranges,
so it will not silently accept a file that is not a D-50 bank.

With more than one bank aboard, the display shows the patch as `bank-number`
("2-37 Nightfall"), and MIDI **CC 0 (bank select)** ahead of a program change
reaches past the factory bank - CC0 0..5 picks the bank, the program byte the
slot within it. The front-panel encoder walks the whole range linearly.

Without a bank the instrument falls back to eight patches built by hand from
the engine's parameters, chosen to cover the ground: every structure appears,
both waveforms, ring modulation, the pitch envelope and each effect.

The bank to lead with is the dump of **PN-D50-00**, the ROM card the D-50
shipped with: Fantasia, Digital Native Dance, Soundtrack, Pizzagogo, Glass
Voices, Staccato Heaven, Shakuhachi, Nightmare -- the sixty-four sounds that
made the machine. Two of those names turn up inside Roland's own D-05
firmware, which is as close to a signature as this gets.

Five more banks come from that same D-05 firmware: its update image holds the
whole 384-slot table (the factory card byte-identically, Roland's 64 new D-05
presets, and the four D-50 card-library banks), and
`tools/d5_extract/d5_bq3_extract_banks.py` turns banks two to six into card
dumps for `roms/`, round-trip-verified against the image.

A bank is somebody's work, and not every file called "factory" is one: a
second dump tried here announced itself in its tone names ("by SG", "By Sven
GODIJN") as a user bank. Worth checking before publishing anything made with
one.

## How the engine works, and what is not the original

**The firmware is the master template.** The D-50's program EPROM and the
internal ROM of its uPD78312 were disassembled for this port, and where the
machine's own code answers a question, that answer wins over any measurement
taken by ear. Read out of it and implemented byte-exactly: the envelope
arithmetic (a rate index per segment, a time law that compensates the level
distance so inner segments are time-constant, a release that is rate-constant
because its distance lookup is computed and then overwritten -- dead code in
the ROM), the pitch constant and its neutral coarse value, the keyfollow and
depth tables, the LFO rate law and its two-phase delay, portamento, aftertouch
and the bender modes, the TVA level basis, and the voice allocation: one pool
of sixteen slots, all sixteen to the upper tone in whole mode, and a free list
that makes the machine **drop** a new note when it is empty rather than steal a
held one.

The sample table -- which PCM sound starts where, how long it is and whether it
loops -- is not in the program ROM either; the D-50 resolves it inside the
MB87136. It was reconstructed from the decoded audio, and later **confirmed
from the firmware**: a bank window in the EPROM holds a start-page and a
length-class byte per wave, which reproduce all 76 known geometries exactly and
resolve the 24 combination waves as loops over larger regions of the same
material. The wave-name table next to them matches this instrument's own
numbering 100 out of 100.
[`tools/d5_extract/README.md`](../../tools/d5_extract/README.md) documents the
derivation.

Where this still differs from the machine:

- **The reverb** is the biggest one. The D-50's reverb chip holds 32 types of
  188 coefficients each, in silicon, and the firmware says nothing about them.
  What stands in for it is the reverb of the MT-32 -- the same era, the same
  Roland department, a Boss RRV-10 whose data lines the munt project read out
  and modelled. Its topology is here (entrance delay, three series allpasses,
  three parallel combs, left and right taken from different taps), with the 32
  panel types mapped onto its room, hall and plate cores plus a tapped delay
  line for the delay family, and their decay times calibrated against
  recordings of the real machine. Reverb of the right era and character, not
  the original's impulse response.
- **Chorus and equalizer** sit in the same effect chip and are modelled the
  same way: the panel's parameters do what they say, the algorithm is not the
  chip's.
- **The absolute envelope clock.** Every ratio in the envelope arithmetic comes
  from the ROM; the one constant that scales all of them was calibrated against
  four measurements in reference recordings, and is good to a few percent
  rather than exact.
- **Root pitch of the attack samples.** The sustained loops are exact (each is
  one cycle, so the root is the sample rate divided by its length); the attacks
  carry a measured estimate.
- **Sixteen voices are allocated, not always rendered.** The allocator is the
  machine's, but the RP2350 is not the LA32: on the heaviest patches -- two
  resonant synth partials and a three-second release, Spacious Sweep being the
  worst of the factory bank -- eleven or twelve of them fit inside the audio
  block. Rather than let that turn into dropouts, a governor watches the block
  load and retires the longest-ringing *released* tail with a 20 ms fade; held
  keys are never touched. The original trims too when a phrase outruns its
  sixteen slots, only later. The footer's `S` shows when it is happening.

Everything else follows the machine's own documentation: the structure table
and the parameter ranges come from the Advanced Course manual and the service
notes, which are named here rather than shipped.

## Host tools

[`tools/d5_extract/`](../../tools/d5_extract/README.md) holds the ROM
identification, the decoder, the sample table and its derivation, the blob
generator, the SysEx converter that turns bulk dumps into patch banks, the
extractor that lifts five more banks out of the D-05 update image, and a host
harness that renders the engine to WAV - `--synth`, `--la`, `--structures`,
`--mod` and `--fx` - so the sound can be judged without flashing anything.
