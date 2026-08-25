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

## The six bytes are the corners of a bilinear interpolation

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

Four corner values per entry, two weights, one 16-bit write. The captured
`dest` and `speed` are the *interpolated* values; the ROM holds the corners.

This accounts for every oddity the packs show:

- **Nothing matches a byte search.** The outputs are interpolates.
- **The velocity layers differ by a constant additive offset** (+28, +14, +3
  between 40/80/110/127 on patch 0). A linear blend does exactly that, and the
  chip's levels are logarithmic, so a blend is a gain.
- **Neighbouring notes are byte-identical**, timestamps included: they land on
  the same key weight. Only 24 distinct wave assignments across 88 keys, and
  27 % of all 5632 captured entries are distinct.

The `$d1` key weight is built at `ed43`–`ed4a` from a table indexed by the
voice's high nibble; `$d2` comes in from the note-on path, which is not read
yet.

## What is still missing

- **The note-on path**: what sets `$02,x` (the list pointer), `$d1` and `$d2`.
  That is where the parameter ROM is actually indexed, and it is the piece that
  turns this into a parser.
- **Bytes 4 and 5** of each entry. The pointer advances by six and only four
  are consumed here.
- **Where the timestamps come from.** They are not in the ROM at all: a
  segment's duration is however long the chip takes to ramp from the previous
  destination to this one, which is why two notes with the same chain have the
  same timing. `RdNewEngine` already computes that arithmetic, so the packs
  could drop the timestamps entirely -- 3.17 MB would become roughly 0.5 MB
  with the duplicates removed as well.

Only three parameter-ROM reads in the whole firmware use a fixed address
(`$babf`, `$bbc1`, `$babd`); everything else is indexed, so the note-on path
has to be read to find the tables rather than grepped for.

## Why this matters

If the note-on path yields to the same treatment, the packs become a pure
function of the ROM set: no emulator, no second checkout, no risk of a stale
reference quietly producing a different-sounding instrument. That is the whole
point of chasing it.
