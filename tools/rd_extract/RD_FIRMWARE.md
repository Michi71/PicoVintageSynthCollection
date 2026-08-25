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
ed24  mul            ; B was voice*16; times $a0, high byte, is voice*10
ed2c  mul #$06       ; six bytes of state each
ed2d  addd #$0200    ;   at $0200 + (voice*10 + part) * 6
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

## The parameter ROM's own header

The program-change handler (`$e19b`) reads it, and it is short:

```
e1a5  andb #$07      ; the patch number, three bits
e1aa  sta $e000      ; bank latch to 0
e1ad  ldx #$4000     ; the window
e1b0  abx            ;   + patch * 3
e1b1  abx
e1b2  abx
e1b3  lda $00,x      ; byte 0  = which bank
e1b5  ldx $01,x      ; bytes 1-2 = a 16-bit address in the window
e1b7  sta $e000      ; latch to that bank
e1bb  std $a5        ; $a5 = the address
e1bd  addd #$0100
e1c0  std $a7        ; $a7 = $a5 + $100
e1c2  addd #$081f
e1c5  std $a9        ; $a9 = $a7 + $81f
```

So the whole parameter ROM is laid out from one three-byte entry per patch at
the very start:

| From | Size | What |
|---|---|---|
| `$4000` | 3 bytes a patch | bank and base address, eight patches |
| `$a5` | `$100` | the velocity table, 256 bytes |
| `$a7` | `$81f` | the zone table — and `$81f / 21` is exactly **99 zones** |
| `$a9` | rest | the 70-byte part blocks |

**Confirmed against the MK-80 ROM.** Its parameter ROM begins

```
00 40 20  01 40 00  02 40 00  03 40 00  00 6c 00  01 71 f0  02 69 10  03 59 f0
```

and resolving each as `(address - $4000) | (bank << 15)` gives

```
0x000020 0x008000 0x010000 0x018000 0x002c00 0x00b1f0 0x012910 0x0199f0
```

which is, byte for byte, the MK-80 half of the `patchToOffset` table the
reference emulator carries hard-coded. That table was derived from this
structure; it can be read from the ROM instead.

## There are three sound-CPU firmwares, and this was the RD-200's

`RD200_B.bin`, `mks20_cpub_1.0.bin` and `MKS20_B.BIN` are three different 8 KB
ROMs. Everything above was read out of the first, which is also the one the
reference emulator loads -- its own `read_byte` says so: *"HACK: only works with
the RD200 ROM"*. It plays MKS-20 sample and parameter ROMs with RD-200 firmware.

All three boot at `$e00f` and all three dispatch the same way, only from
different addresses:

| ROM | ICF | IRQ1 | dispatch table |
|---|---|---|---|
| `RD200_B.bin` | `$e121` | `$ed1a` | `$e16e` |
| `MKS20_B.BIN` | `$e120` | `$e78b` | `$e164` |
| `mks20_cpub_1.0.bin` | `$e0dd` | `$e748` | `$e121` |

**The MKS-20's command set is smaller.** Its table has no `$30` (program
change), no `$b0` (note off) and no `$50` (sustain) -- those entries are zero.
Its main CPU does more of the work.

## Which is why its patch table is somewhere else entirely

The MKS-20 selects patches with `$40`, not `$30`, and its table is in **its own
program ROM** at `$e82b` rather than at the head of the parameter ROM:

```
00 40 00  01 40 00  02 40 00  07 40 00  04 7c 20  05 6b 50  06 82 60  07 7e f0
```

Same three-byte shape, one twist -- the handler splits the first byte:

```
e19e  lda $00,x      ; the bank byte
e1a2  anda #$03      ; only the low two bits reach the latch
e1a4  ldb $3b
e1a6  andb #$04      ; and bit 2 comes from elsewhere
e1a9  sta $e000
e1ac  ldd $01,x      ; the address, as before
```

So **bits 0-1 are the parameter bank and bit 2 is the sample set**, and both of
the emulator's hard-coded tables fall out of this one:

| Patch | byte | offset from bits 0-1 | set from bit 2 | emulator |
|---|---|---|---|---|
| 0 | `$00` | `0x000000` | 0 | `0x000000` / 0 |
| 1 | `$01` | `0x008000` | 0 | `0x008000` / 0 |
| 2 | `$02` | `0x010000` | 0 | `0x010000` / 0 |
| 3 | `$07` | `0x018000` | 1 | `0x018000` / 1 |
| 4 | `$04` | `0x003c20` | 1 | `0x003c20` / 1 |
| 5 | `$05` | `0x00ab50` | 1 | `0x00ab50` / 1 |
| 6 | `$06` | `0x014260` | 1 | `0x014260` / 1 |
| 7 | `$07` | `0x01bef0` | 1 | `0x01bef0` / 1 |

All eight, both columns. `patchToOffset` and `patchToRomSet` are the same table
read twice, and it is in the ROM.

## Bytes four and five: a coarse velocity split

The interrupt reads `$00,x` to `$03,x` and advances by six, so two bytes of each
entry looked unused. They are not — the *pointer* is what moves.

Note-on, having fetched the list pointer out of the parameter block:

```
e940  ldd $02,x      ; the list pointer
e942  bne $e94b      ; zero means this part is unused
e94b  ldx $b5
e94d  addd $c4       ; <-- offset the whole list
e94f  std $02,x      ; and that is what the interrupt walks
e952  jsr $eb16      ; program the first segment
```

and `$c4` was set from **bit 7 of the velocity weight**:

```
e7be  ldx #$0000
e7c1  ldb $c0        ; $a5[velocity]
e7c3  bpl $e7c7      ; bit 7 clear -> $c4 = 0
e7c5  inx
e7c6  inx            ; bit 7 set   -> $c4 = 2
e7c7  stx $c4
```

So **each entry carries six corner values, and bit 7 picks which four apply**:
bytes 0-3 or bytes 2-5. A coarse two-layer velocity split sitting on top of the
fine interpolation, and it persists for the whole chain because the offset is
applied once, to the starting pointer, while the stride stays six.

The two roles do not collide. `$c1` is `(($c0 << 1) & $ff) >> 2`, and the `aslb`
discards bit 7 — so the curve index is built from bits 0-6 and bit 7 alone
chooses the layer. One byte out of `$a5`, two jobs, no overlap.

That also means `$a5` is not just a velocity curve: it is a velocity **map**,
one byte a step, carrying a layer bit and a within-layer position.

## The "key weight" is not a key weight

`$0040 + voice` is what the interrupt reads as `$d1`:

```
ed3f  lsrb           ; four times: the IRQ id's high nibble is the voice
ed47  ldb $40,x
ed49  aslb
ed4a  stb $d1
```

and note-on writes it, right after allocating the voice:

```
e5c9  jsr $e51b      ; allocate, voice comes back in B
e5cc  stb $91
e5d4  ldx $90
e5d6  ldb $c0        ; the same byte again
e5d8  stb $40,x      ; -> $0040 + voice
```

**`$c0`.** The same byte that becomes `$c1` and through it `$d2`. So both
interpolation weights come from one lookup:

| | |
|---|---|
| `$d1` | `$c0 << 1` — straight |
| `$d2` | `curve[(($c0 << 1) & $ff) >> 2]` — through one of the eight |

Two different laws over the same input, which is exactly what the captured data
shows: across the four velocity layers of patch 0, note 60, part 0, segment 2,
`dest` moves 196 → 226 → 242 → 245 while `speed` barely moves at all, 120 → 122
→ 123 → 123. One byte of the register pair is interpolated straight, the other
through a curve.

**So the note does not enter the interpolation at all.** It enters through the
zone: 88 keys over 99 zone entries, each with its own part blocks and its own
segment lists. Which is why neighbouring notes come out byte-identical — they
land in the same zone — and why there are only 24 distinct wave assignments
across the keyboard.

The `$80`/`$a0` handler at `$e777`/`$e77d` writes the same location by the same
route. It is the second way in, not the only one, and note-on does not depend
on it.

## The three bytes, settled

The doubt in the previous pass was mine and unfounded: the fetch at `$e157`
reads **two** data bytes, not one.

```
e157  tim #$01,$03   ; wait for the strobe
e15a  beq $e157
e15c  lda $02        ; first data byte
e15e  aim #$ef,$03   ; acknowledge
e161  tim #$01,$03   ; wait again
e164  bne $e161
e166  ldb $02        ; second data byte
e168  oim #$10,$03
e16b  std $e1        ; -> $e1 and $e2
e16d  rts
```

Three reads in all, counting the command byte at `$e129`. Which the reference
emulator corroborates without meaning to: its `read_byte` pops the command queue
at exactly three program counters — `$e12b`, `$e15e`, `$e168` — the reads at
`e129`, `e15c` and `e166`. Three bytes queued by `sendMidiCmd` as `$c0`, note,
velocity; three reads to take them.

So, finally and without hedging:

| | |
|---|---|
| `$dc` | the command byte |
| `$e1` | the **note** |
| `$e2` | the **velocity** |

and `e5be`'s `ldd $e1` puts the note in A — kept at `$9f` — and the velocity in
B, which indexes `$a5`. **`$c0` is `$a5[velocity]`**, as the measurements had
already forced it to be.

## The path, end to end

Everything above, in the order it happens:

1. Three bytes arrive: `$c0`, note, velocity.
2. **Velocity** indexes `$a5`, a 256-byte map at the head of the patch's
   parameter block. One byte out, two jobs: bit 7 picks a coarse layer, bits 0-6
   a fine position.
3. **Note** picks a zone, one of 99 entries of 21 bytes, which names a 70-byte
   part block — six parts of seven bytes, a segment-list pointer at `+2`, a
   curve selector at `+4`.
4. The list pointer is offset by 0 or 2 from the layer bit and stored per part.
5. Each interrupt from the chip walks that list six bytes at a time, reads four
   corners, and interpolates twice -- `$c0 << 1` straight for one byte of the
   register pair, one of eight 64-byte curves for the other.
6. A segment interpolating to zero ends the chain.

Nothing in that is timing. The durations the captured packs record are the
chip's own ramps, not the firmware's.

## The note becomes a zone by subtracting fifteen and folding octaves

```
e60f  lda $9f        ; the note
e611  suba #$0f      ; less fifteen
e613  bcc $e619
e615  adda #$0c      ;   under: add an octave until it is not
e617  bcc $e615
e619  cmpa #$62      ; over 98?
e61b  bls $e621
e61d  suba #$0c      ;   yes: take an octave off
e61f  bra $e619      ;        and ask again
e621  jsr $e79b      ; -> build $af from it
```

**Zone = note − 15, folded by octaves into 0…98.** Ninety-nine values, which is
`$81f / 21` exactly — the zone table's own size, arrived at from the other
direction.

For the keyboard the machine actually has, 21 to 108, that is zones 6 to 93 and
the folding never runs; it is there to catch anything outside 15…113. Eighty-
eight keys, eighty-eight zones, one each — and the 74 distinct segment chains
the packs show mean some of those zones point at the same list.

`jsr $e79b` enters the `$af` construction one instruction in, with the zone
already in A, which is why that routine reads `lda $e1` at `$e799` when reached
the other way.

## The zone entry is one index and ten pitches

```
e80b  ldx $ad        ; the zone entry, one byte in
e80d  ldd $00,x      ; a 16-bit value
e80f  ldx $b5
e811  std $04,x      ;   into the part state
e813  addd $e5       ;   plus the global tuning
e816  std $00,x      ;   into the frame that reaches the chip
e818  ldx $ad
e81a  ldd $02,x      ; the next one
e81e  std $0a,x      ;   state, stride six
e823  std $10,x      ;   frame, stride sixteen
e825  ldx $ad
e827  ldd $04,x      ; and the next
```

Ten 16-bit pitch values at stride two, read from `zone + 1` — `$ad` is the zone
entry incremented past its first byte, which is the part-block index. **One
index byte plus ten pitches of two bytes each is twenty-one**, which is the zone
entry's size arrived at independently for the second time.

Each is stored twice: into the part state, and — with the global tuning from the
`$e0` command added — into the frame that is handed to the chip. So the ROM
carries an absolute pitch per part per zone; nothing is transposed from the note
at play time, because the zone *is* the note.

## Programming a note is ten parts, unrolled

Note-on writes every one of the ten parts explicitly rather than looping.
`ldx $b1` — the voice's chip base, `$1000 + voice * $100` — appears ten times in
a row at `$e955` through `$eb05`, storing to `+$04`, `+$14`, `+$24` … `+$94`:
chip fields 4 and 5, the envelope pair, one part every sixteen bytes. Flags and
the envelope offset go the same way at `+$06`, `+$16`, and so on from `$e7df`.

The interpolation itself is shared. `$eb16` is the interrupt's arithmetic as a
subroutine — four corners at X, weights in `$c2` and `$c3` rather than `$d1` and
`$d2`, the pair returned in D — and note-on calls it once per part to program
the first segment. The interrupt then re-implements the same maths inline for
every segment after that.

## The part block is ten records of seven bytes

The wave address was the last thing unaccounted for, and it is the first two
bytes of each record:

```
e88d  ldx $af
e88f  ldd $00,x      ; record 0
e891  addb $b9       ;   plus an offset
e894  std $02,x      ;   -> frame +$02  = chip fields 2 and 3, part 0
e898  ldd $07,x      ; record 1, stride seven
e89b  std $12,x      ;   -> part 1, stride sixteen
e89f  ldd $0e,x      ; record 2
e8a2  std $22,x
e8a6  ldd $15,x      ; record 3
e8a9  std $32,x
e8ad  ldd $1c,x      ; record 4 ...
```

Ten of them, stride seven, and **10 x 7 = 70 = `#$46`** — the very multiplier the
block is addressed with. The size confirms itself from both ends, as the zone
entry's twenty-one did.

| In the record | What |
|---|---|
| `+0..1` | the **wave address**, to chip fields 2 and 3 |
| `+2..3` | the **segment list pointer** |
| `+4`, `+5` | the two **curve selectors**, soft and hard |
| `+6` | one byte a part, read on its own |

`+4` and `+5` being a pair is the same coarse velocity layer again: `$ab` is
`$af` or `$af + 1` depending on bit 7 of the velocity weight, and everything
read `$04,x` off it lands on one or the other. The same bit that offsets the
segment list by two picks the curve. One decision, applied in both places.

## The whole structure

```
parameter ROM
  +0        3 bytes a patch: bank, and a 16-bit base    (MK-80; the MKS-20
                                                         keeps this in its
                                                         own program ROM)
  base                -> $a5   256 bytes   velocity map
  base + $100         -> $a7   99 x 21     zone table
                                  +0       part-block index
                                  +1..     ten 16-bit pitches
  base + $91f         -> $a9   n x 70      part blocks
                                  ten records of seven:
                                  +0..1    wave address
                                  +2..3    segment list  -> six bytes an entry,
                                                            four corners of two
                                                            interpolations
                                  +4,+5    curve selectors
                                  +6       ?
```

## The part state, and a correction

Byte 6 of each record goes to the part state, and the trick is `txs`:

```
e8d5  ldx $b5        ; the voice's state base
e8d7  txs            ;   make it the frame, so tsx addresses it
e8da  ldb $06,x      ; record 0, byte 6
e8dd  stb $00,x      ;   -> state +0
e8e1  ldb $0d,x      ; record 1, byte 6
e8e4  stb $06,x      ;   -> state +6
e8e8  ldb $14,x      ; record 2
e8eb  stb $0c,x
```

Six bytes a part, and every one of them is now accounted for:

| In the part state | From |
|---|---|
| `+0` | record byte 6 |
| `+1` | the velocity weight — the interrupt's `$d2` |
| `+2..3` | the segment list pointer, offset by the layer bit |
| `+4..5` | the pitch, out of the zone entry |

**A correction to the first pass.** It said the interrupt addresses state at
`$0200 + (voice*16 + part) * 6`. It is **voice*10**, and the way the firmware
gets there is worth seeing:

```
ed20  andb #$f0      ; B = voice * 16
ed22  lda #$a0
ed24  mul            ; D = voice * $a00 -- and the high byte is voice * 10
ed29  aba            ; + part
ed2a  ldb #$06
ed2c  mul            ; * 6
ed2d  addd #$0200
```

Multiply by 16, then by `$a0`, then keep the high byte: `16 * 160 / 256 = 10`.
Ten parts a voice, sixty bytes a voice — which is exactly what note-on computes
the other way round at `$e7b6` with a plain `ldb #$3c`. The two agree for every
voice and part.

## State `+0` is a release speed

The reader is at `$e6d7`, and it runs over all ten parts:

```
e6d7  ldb #$3c
e6d9  mul            ; voice * 60
e6da  addd #$0200
e6dd  std $b5        ; the voice's state
e6e4  lda $91        ; the voice
e6e6  adda #$10
e6e8  xgdx           ; X = $1000 + voice * $100, the chip
e6eb  txs            ;   as the frame
e6ec  clra           ; destination zero
e6ed  ldx $b5
e6ef  ldb $00,x      ; part 0's state +0
e6f1  andb #$7f
e6f3  lsrb
e6f6  orb #$80       ; -> a speed
e6f9  std $04,x      ; chip fields 4 and 5: ramp to zero at that speed
e6fd  ldb $06,x      ; part 1 ...
e707  std $14,x
```

A destination of **zero** and a speed out of the state: that is a release, and
byte 6 of each record is what sets its rate. Ten parts, unrolled, same striding
as everywhere else — six in the state, sixteen at the chip.

So the record's seventh byte is the part's **release rate**, and the part state
holds it from note-on until the key comes up.

## Note-off: the release rate comes from the velocity

`$e556` is the note-off handler, but `$e4fe` and `$e535` are only bookkeeping --
a voice-table search and a ring-pointer bump. The release is at the end of it:
note-off falls through to `jmp $ebbf` when the voice's ten-part counter at
`$70,x` is still set.

```
ebbf  lda $40,x      ; the voice's velocity weight -- $c0, kept since note-on
ebc1  rola           ; three times
ebc2  rola
ebc3  rola
ebc4  coma           ; complemented
ebc5  anda $bc       ; and masked
ebc8  std $c6        ; the pair, kept four times over
ebca  std $c8
ebcc  std $ca
ebce  std $cc
ebd0  lda #$0a
ebd2  sta $70,x      ; the ten-part counter, reset
ebd6  ldb #$3c
ebd8  mul            ; voice * 60
ebdc  std $b5        ; the state
ebdf  clra
ebe0  clrb
ebe1  std $02,x      ; zero the segment list pointer
```

**The segment list pointer is zeroed**, which is what takes the interrupt out of
the chain: `ed39` tests exactly that and gives up when it is null. And the
ten-part counter is reset. Those two are certain.

**The rest of it is not a release rate, and an earlier note here said it was.**
`$bc` turns out to be a two-bit mask -- see below -- so `anda $bc` leaves a value
of 0 to 3, which cannot be a speed byte.

Tracing the rotate settles what it is instead. `rola` is a rotate *through the
carry*: `C' = bit 7`, `A = (A << 1) | C`. Three of them turn `a7 a6 a5 a4 a3 a2
a1 a0` into `a4 a3 a2 a1 a0 c a7 a6`, so after `coma` and `& $03` what is left
is the complement of **the velocity weight's top two bits**. A number from 0 to
3, stored four times over into `$c6` through `$cd`.

Four levels, chosen by how hard the note was struck, and only for a patch whose
mask says so. On a piano that is the shape of a damper or release layer; what it
actually drives has not been followed.

## The second per-patch table

`$bc` is written in exactly one place, and it is the program-change handler:

```
e1ef  ldx #$e254     ; a second table, in the program ROM
e1f2  ldb $bb        ; the patch number
e1f4  abx            ;   + patch * 3
e1f5  abx
e1f6  abx
e1f7  lda $00,x      ; byte 0 -> the mask
e1f9  sta $bc
e1fb  ldd $01,x      ; bytes 1-2 -> a word, used further on
```

| Patch | | mask | word |
|---|---|---|---|
| 0 | Piano 1 | `$03` | `$5a06` |
| 1 | Piano 2 | `$03` | `$5a08` |
| 2 | Piano 3 | `$03` | `$5a06` |
| 3 | Harpsichord | `$00` | `$0000` |
| 4 | Clavi | `$00` | `$0000` |
| 5 | Vibraphone | `$00` | `$0000` |
| 6 | E-Piano 1 | `$00` | `$0000` |
| 7 | E-Piano 2 | `$00` | `$0000` |

**Only the three pianos.** Everything else gets zero and the whole mechanism
switches itself off — which is what a damper would do on instruments that have
none.

## The captured release is not a release routine at all

Two passes went looking for where the packs' release speeds of 200 and 201 come
from, and neither `$e6d7` nor `$ebbf` could produce them. They do not: **it is
the last entry of the segment list, interpolated like every other one.**

The data settles it. Patch 0, part 0, the release segment:

| | | | | |
|---|---|---|---|---|
| note 60, by velocity | 40 → 202 | 80 → 201 | 110 → 200 | 127 → 200 |
| velocity 110, by note | 36 → 194 | 48 → 196 | 60 → 200 | 72 → 201 |

It moves with the velocity *and* with the note. A constant from a release
routine could do neither; an interpolation does both — the velocity supplies the
weight, the note supplies the zone and so the corners.

Which was already written down in the first pass and then lost sight of: **a
segment interpolating to zero ends the chain.** `ed89`'s `tsta` tests exactly
that, and `ed8d` zeroes the pointer when it happens. The release is a normal
segment whose destination corner is nothing, and `rd_analyze` files it under
`release_segments` because it lands after note-off, not because the firmware
treats it differently.

So the two routines found on the way are other things:

| | |
|---|---|
| `$e6d7` | a forced release, per-part rate out of state `+0`, range `$80…$bf` |
| `$ebbf` | note-off proper: zero the list pointer, reset the counter, set the pianos' four-level damper |

## What is still missing

The envelope path is read end to end. Two things beside it are not:


And one thing that is not missing but worth restating, because it changes what a
pack has to hold:

- **The timestamps are not in the ROM and need not be in the packs.** A
  segment's duration is however long the chip takes to ramp from the previous
  destination to this one, which is why two notes on the same chain time
  identically. `RdNewEngine` already computes that arithmetic. Dropping the
  timestamps and the duplicates would take the packs from 3.17 MB to roughly
  0.5 MB, whether or not the parser ever gets written.

Only three parameter-ROM reads in the whole firmware use a fixed address
(`$babf`, `$bbc1`, `$babd`); everything else is indexed, which is why all of
this had to be read rather than grepped for.

## Why this matters

If the rest yields to the same treatment, the packs become a pure function of
the ROM set: no emulator, no second checkout, no risk of a stale reference
quietly producing a different-sounding instrument. That is the whole point of
chasing it.
