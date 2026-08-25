# Reading the MKS-20 firmware

Notes from disassembling the 8 KB program ROM, kept because the question they
answer keeps coming back: **are the note descriptors in the ROM, so the packs
could be built without running an emulator?**

Short answer: the *inputs* are, the descriptors are not. The firmware
interpolates them. What follows is how far that has been read.

Tooling is [`rd_dasm.py`](rd_dasm.py). Its opcode table is lifted from the
reference emulator's own dispatch table, so the mnemonics are the ones that
emulator implements rather than a second-hand listing.

```bash
RDPIANO=~/rdpiano ./rd_make_rom.sh <romdir> /tmp/rd.blob     # also writes the program ROM
./rd_dasm.py ~/rdpiano/librdpiano/src/mcu.cpp /tmp/program.bin sweep
```

## The memory map, from the emulator

| Range | What |
|---|---|
| `$0000`–`$001f` | MCU registers (ports, timer) |
| `$0020`–`$0fff` | RAM |
| `$1000`–`$1fff` | **the sound chip** — writes go straight to it |
| `$4000`–`$bfff` | **the parameter ROM**, banked by a latch (2 bits) |
| `$c000`–`$ffff` | program ROM, 8 KB mirrored twice; the firmware runs in the `$e000` copy |

Vectors: `RESET=$e00f`, `ICF=$e121`, **`IRQ1=$ed1a`**.

## The sound chip interrupt is where the descriptors come from

`IRQ1` fires when a voice part finishes an envelope segment, and the handler
programs the next one. That is exactly the event the capture records, so this
routine *is* the descriptor generator.

```
ed1b  ldb $1000      ; the chip says which voice and part
ed20  andb #$f0      ;   high nibble = voice
ed27  andb #$0f      ;   low nibble  = part
ed2c  mul #$06       ; six bytes of state each
ed2d  addd #$0200    ;   at $0200 + (voice*16 + part) * 6
ed37  ldx $02,x      ; a POINTER, held in that state
ed39  beq  ...       ;   null: the part is finished
ed4f  addd #$0006    ; advance it by six
ed54  std $02,x      ; and store it back
```

So each part walks a **list of six-byte entries**, one entry per envelope
segment. That stride is why searching the parameter ROM for the captured
`(dest, speed)` byte pairs finds nothing — wrong shape, and wrong values.

## The six bytes are two interpolations, not one

```
ed61  lda $d1        ; weight from the key
ed65  ldb $03,x      ;   corner 3  *  d1
ed6d  ldb $01,x      ;   corner 1  * (256-d1)
ed70  addd $b7       ;   -> intermediate
ed73  lda $d2        ; weight from the velocity
ed77  ldb $02,x      ;   corner 2  *  d2
ed7f  ldb $00,x      ;   corner 0  * (256-d2)
ed85  ldx $d7        ; the chip register for this voice
ed87  std $04,x      ; write the pair
```

Read the stack traffic and it is **two independent linear interpolations, not
one bilinear one** — an earlier note here said bilinear and that was wrong.
`psha` at `ed72` keeps the high byte of the key-weighted sum; `pulb` at `ed84`
brings it back as B while A holds the velocity-weighted one, and `std` writes
the pair. So one byte of the chip register pair is interpolated over the key,
the other over the velocity, from four corners in the entry.

The captured `dest` and `speed` are those interpolated values; the ROM holds the
corners.

**Confirmed against measurement.** Patch 0, note 60, part 0, segment 2 reads
196 / 226 / 242 / 245 at velocities 40 / 80 / 110 / 127. Corners **187 and 251**
with the linear curve at `$f049` reproduce all four exactly, at weights 36, 156,
220, 232.

This accounts for every oddity the packs show:

- **Nothing matches a byte search.** The outputs are interpolates.
- **The velocity layers differ by a constant additive offset** (+28, +14, +3
  between 40/80/110/127 on patch 0). A linear blend does exactly that, and the
  chip's levels are logarithmic, so a blend is a gain.
- **Neighbouring notes are byte-identical**, timestamps included: they land on
  the same key weight. Only 24 distinct wave assignments across 88 keys, and
  27 % of all 5632 captured entries are distinct.

Neither weight comes in from outside: `$d1` is fetched at `ed43`–`ed4a` from a
RAM table at `$0040 + voice`, `$d2` from the part's own state byte 1. Both are
written by the note-on path.

## The note-on path

`$af` is the per-part parameter block, and it is built at `e78d`–`e7ab`:

```
e791  stb $40,x      ; the key weight, into $0040 + voice
e799  lda $e1
e79b  ldb #$15       ; zone table, 21 bytes an entry
e79e  addd $a7
e7a1  ldb $00,x      ; first byte of the zone entry
e7a6  lda #$46       ; part block, 70 bytes each
e7a9  addd $a9
e7ab  std $af
```

Two levels: a **21-byte zone entry** selects a **70-byte part block**. The
offsets the setup then reads out of that block — `$22, $29, $30, $37, $3e, $45`,
stride 7 — are its last 42 bytes: **six parts of seven bytes**. And:

| Offset in the 70-byte block | What |
|---|---|
| `+2..3` | pointer to the segment list, six bytes an entry (`e940`, `e946`) |
| `+4` | which velocity curve (`e92b`) |
| `+0x22 …` stride 7 | six per-part records |

The velocity weight is a table lookup, not arithmetic:

```
e92b  ldb $04,x      ; curve selector out of the part block
e92d  ldx #$ed9d     ; eight pointers, here in the program ROM
e930  abx
e931  ldx $00,x      ; -> $f049, $f089, $f0c9, $f109, $f149, $f189, $f1c9, $f209
e933  ldb $c1        ; the velocity index
e935  abx
e936  ldb $00,x      ; the weight
e93a  stb $01,x      ; -> part state byte 1, which the IRQ reads as $d2
```

**Eight velocity curves of 64 bytes each, in the program ROM.** `$f049` is the
straight ramp (0, 4, 8 … 252); `$f089` is bent (0, 4, 7, 10, 13 … 252).

## The command protocol, and where notes actually arrive

This MCU never sees MIDI. It is the sound CPU of a pair, and the main CPU hands
it byte commands over port 1. `ICF` (`$e121`) is that interface:

```
e129  lda $02        ; the command byte
e130  bpl $e134      ; bit 7 set -> a second byte follows ($e157)
e136  andb #$f0      ; high nibble selects
e13b  ldx #$e16e     ; the dispatch table
e141  jsr $00,x
```

That table is why a recursive descent from the vectors reaches only 9 % of the
ROM: every handler sits behind it.

| Cmd | Handler | | Cmd | Handler |
|---|---|---|---|---|
| `$00` | `$ebb7` | | `$80` | `$e77d` |
| `$10` | `$e372` | | `$90` | `$e372` |
| `$20` | — | | `$a0` | `$e777` |
| `$30` | `$e19b` — program change | | `$b0` | `$e556` — note off |
| `$40` | `$e34c` | | `$c0` | `$e5a1` — **note on** |
| `$50` | `$e44f` — sustain | | `$d0` | `$e59d` |
| `$60` | `$e3fd` | | `$e0` | `$e18e` |
| `$70` | `$e3f2` | | `$f0` | `$edbd` |

The four labelled ones are certain: they are what the reference emulator
synthesises in `Mcu::sendMidiCmd`. **Note on is `$c0`, then the note, then the
velocity.**

## Where the velocity weight comes from

Note-on at `$e5a1`, once the data bytes are in `$e1` (note) and `$e2`
(velocity):

```
e5ac  lda $a1
e5ae  beq $e5be      ; one of two mappings
e5b0  ldb $e2        ;   scaled: (velocity * 247) >> 8, then + $80
e5b2  lda #$f7
e5b4  mul
e5b5  tab
e5ba  addb #$80
e5be  ldd $e1        ;   plain: index by the velocity itself
e5c0  ldx $a5
e5c2  abx
e5c3  ldb $00,x      ; -> the weight
e5c5  stb $c0
e5c7  sta $9f        ; the note, kept
```

`$a5` is a velocity table in the parameter ROM, addressed either directly or
through a `247/256` scaling into a second half at `+$80`.

**This closes the question the previous pass left open.** `$c1` is not
`velocity >> 1`; it is

```
e7c9  aslb           ; $c1 = (($a5[velocity] << 1) & $ff) >> 2
e7cc  lsrb
e7cd  lsrb
```

a *two-stage* mapping, table first and shift second. Which is exactly why
fitting the four measured layers put them at curve indices 9, 39, 55, 58 where
`velocity >> 1` would have wanted 20, 40, 55, 63.

## What is still missing

- **`$a5`, `$a7` and `$a9`**, the three table bases. All three are set together
  at `$e06c`–`$e076` and again at `$e1bb`–`$e1c5`, which is the program-change
  handler — so reading that gives the parameter ROM's own header.
- **Bytes 4 and 5** of each segment entry. The pointer advances by six and the
  interrupt consumes four.
- **The key weight** at `$0040 + voice`. It is written by the `$80`/`$a0`
  handler, not by note-on, and the reference emulator never sends those
  commands — so how they are driven on real hardware is open.
- **Where the timestamps come from.** Not the ROM: a segment's duration is
  however long the chip takes to ramp from the previous destination to this
  one, which is why two notes with the same chain have identical timing.
  `RdNewEngine` already computes that arithmetic, so the packs could drop the
  timestamps entirely — 3.17 MB would become roughly 0.5 MB with the duplicates
  removed as well, independently of whether the parser ever gets written.

Only three parameter-ROM reads in the whole firmware use a fixed address
(`$babf`, `$bbc1`, `$babd`); everything else is indexed, which is why this had
to be read rather than grepped for.

## Why this matters

If the rest yields to the same treatment, the packs become a pure function of
the ROM set: no emulator, no second checkout, no risk of a stale reference
quietly producing a different-sounding instrument. That is the whole point of
chasing it.
