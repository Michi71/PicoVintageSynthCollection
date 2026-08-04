# PicoFaceCP — MIDI Implementation

Equivalent to the MIDI implementation of the **Yamaha reface CP** (source:
*reface Data List*, pages 11-14; Yamaha's document, not shipped here).
Implemented in `include/midi_reface.h` / `src/midi_reface.cpp` (the protocol
layer) on top of the core's USB and DIN MIDI transports.

Version: 1.0 · as of 2026-07-02

---

## 1. Coverage

This specification describes how PicoFaceCP sends and receives MIDI data. In
the standalone repository that was **USB MIDI only** (TinyUSB device, cable 0);
in the collection the core additionally serves the DIN port, and both wires end
up in the same dispatch.

## 2. Compliance

* MIDI 1.0 (over the USB MIDI class protocol, 4-byte event packets, and over
  DIN at 31250 baud)

## 3. Transmit / Receive Data

### 3-1 Channel Voice Messages

| Message | Status | RX | TX | Remark |
|---|---|---|---|---|
| Note Off | `8nH` | ✓ | ✗ | velocity ignored |
| Note On / Off | `9nH` (v=0 -> Off) | ✓ | ✗ | v = 1-127; no local keyboard, hence no TX |
| Control Change | `BnH` | ✓ | ✓ | see the table below; TX only sends panel CCs while MIDI control is on |
| Pitch Bend | `EnH` | ✓ | ✗ | ±2 semitones, applied to all active voices |
| Program Change | `CnH` | ✓ | ✓ | **deviation:** PC 0-7 selects a factory preset (`PRESETS.md`); the original has no PC |
| Aftertouch (Key/Ch) | `AnH`/`DnH` | ✗ | ✗ | |

Receive channel: the SYSTEM parameter `MIDI receive channel` (1-16, All;
default **All**). Transmit channel: the SYSTEM parameter `MIDI transmit channel`
(default 1).

#### Controllers received at all times

| CC | Function | What PicoFaceCP does with it |
|---|---|---|
| 1 | Modulation | FX tremolo depth (audible when the trem/wah mode is not Off) |
| 7 | Volume | master volume of the FX chain |
| 11 | Expression | multiplied onto the master volume (`RefaceCpChain::setExpression`) |
| 64 | Sustain | engine sustain (`mdaEPiano`, CC 0x40) |
| 66 | Sostenuto | same as sustain (an approximation in the engine) |
| 67 | Soft Pedal | note-on velocity x 0.75 while held |

#### Controllers received and sent only while MIDI control is on

Identical to the reface CP table (Data List p. 13):

| CC | Name | Value ranges (RX) | TX values |
|---|---|---|---|
| 80 | TYPE | 0–21 Rd I · 22–42 Rd II · 43–64 Wr · 65–85 Clv · 86–106 Toy(=Pno) · 107–127 CP | 0 / 25 / 51 / 76 / 102 / 127 |
| 81 | DRIVE | 0–127 | 0–127 |
| 17 | TREMOLO/WAH SWITCH | 0–42 Off · 43–85 Tremolo · 86–127 Wah | 0 / 64 / 127 |
| 18 | TREMOLO/WAH DEPTH | 0–127 | 0–127 |
| 19 | TREMOLO/WAH RATE | 0–127 | 0–127 |
| 85 | CHORUS/PHASER SWITCH | 0–42 Off · 43–85 Chorus · 86–127 Phaser | 0 / 64 / 127 |
| 86 | CHORUS/PHASER DEPTH | 0–127 | 0–127 |
| 87 | CHORUS/PHASER SPEED | 0–127 | 0–127 |
| 88 | D.DELAY/A.DELAY SWITCH | 0–42 Off · 43–85 D.Delay · 86–127 A.Delay | 0 / 64 / 127 |
| 89 | DELAY DEPTH | 0–127 | 0–127 |
| 90 | DELAY TIME | 0–127 | 0–127 |
| 91 | REVERB DEPTH | 0–127 | 0–127 |

TX happens on a change at the front panel (`CP_Ui` ->
`RefaceMidi::txFxParam/txFxMode/txInstrument`) or when a preset is picked from
the menu (`RefaceMidi::txProgram` -> program change 0-7, see deviation 7).

### 3-2 Channel Mode Messages (RX)

| CC | Function | Behaviour |
|---|---|---|
| 120 | All Sound Off | all voices silenced immediately (`resetVoices`) |
| 121 | Reset All Controllers | pitch bend centred, expression max, sustain/sostenuto/soft off, engine controller reset |
| 123 | All Notes Off | release runs out; sustain is respected (`stopVoices`) |
| 124 | Omni Mode Off | like all notes off; receive channel := 1 |
| 125 | Omni Mode On | like all notes off; receive channel := All |
| 126 | Mono | like all sound off; polyphony = 1 |
| 127 | Poly | like all sound off; polyphony = max |

### 3-3 System Real Time Messages

**Active Sensing (`FEH`)**
* TX: every 200 ms (`RefaceMidi::tick()`, called from `uiTick()`).
* RX: after the first `FEH` the stream is supervised; if nothing arrives for
  more than about 350 ms, all notes and sounds are switched off and the
  supervision ends.

### 3-4 System Exclusive Messages

Yamaha header as on the reface CP: **Yamaha ID `43H`, group `7FH 1CH`, model
ID `04H`**. The device number `n` is ignored on receive; transmission uses n=1
(`10H`) resp. `00H` for bulk.

#### 3-4-1 Identity Request / Reply (Universal Non-Realtime)

* Request (RX): `F0 7E 0n 06 01 F7`
* Reply (TX): `F0 7E 7F 06 02 43 00 41 52 06 00 00 00 7F F7` (identical to the reface CP)

#### 3-4-2 Parameter Change (RX)

`F0 43 1n 7F 1C 04 <AddrHigh> <AddrMid> <AddrLow> <Data…> F7`
Parameters with a data size of 2 or more (master tune) carry correspondingly more data bytes.

#### 3-4-3 Bulk Dump (RX/TX)

`F0 43 0n 7F 1C <ByteCountHi> <ByteCountLo> 04 <Addr…> <Data…> <Checksum> F7`
* Byte count = model ID + address + data (without the checksum).
* Checksum: the sum of (model ID + address + data + checksum) ≡ 0 in the lower
  7 bits.
* An invalid checksum discards the block.

#### 3-4-4 Dump Request (RX)

`F0 43 2n 7F 1C 04 <Addr…> F7` - the reply:

| Request address | Reply |
|---|---|
| `00 00 00` | SYSTEM common (byte count 36) |
| `0E 0F 00` | bulk header (4) + TG common (`30 00 00`, 20) + bulk footer (4) |
| `30 00 00` | TG common on its own (an extension) |

#### 3-4-5 Parameter Request (RX)

`F0 43 3n 7F 1C 04 <Addr…> F7` - the reply is the corresponding parameter change
message carrying the current value, read live from the engine and FX chain.

---

## 4. MIDI parameter change table (SYSTEM), base address `00 00 00`

| Addr | Size | Range | Parameter | Implementation |
|---|---|---|---|---|
| 00 | 1 | 00-0F | MIDI transmit channel | stored, used for CC and SysEx TX |
| 01 | 1 | 00-0F, 10 | MIDI receive channel (1-16, All) | channel filter |
| 02 | 4 | one nibble each | master tune (-102.4…+102.3 cents, 0.1-cent steps, centre `0400H`) | mapped onto the engine fine tune, **clamped to ±50 cents** (the engine's range) |
| 06 | 1 | 00-01 | local control | stored (there is no local keyboard) |
| 07 | 1 | 34-4C | master transpose -12…+12 semitones | applied to note events, on top of the UI octave |
| 0B | 1 | 00-01 | sustain pedal select (FC3/FC4-5) | stored (half damper not supported) |
| 0C | 1 | 00-01 | auto power-off | stored |
| 0D | 1 | 00-01 | speaker output | stored |
| 0E | 1 | 00-01 | MIDI control | gates CC 17-19/80/81/85-91 (RX and TX) |
| others | 1 | - | reserved | stored |

Total block size: 32 bytes, as on the reface CP.

## 5. MIDI parameter change table (tone generator), base address `30 00 00`

| Addr | Range | Parameter | Target |
|---|---|---|---|
| 00 | 00-7F | Volume | FX chain master volume |
| 01 | — | reserved | — |
| 02 | 00-05 | wave type (Rd I, Rd II, Wr, Clv, Toy->Pno, CP) | `mdaEPiano::setInstrument` |
| 03 | 00–7F | Drive | FX Drive |
| 04 | 00-02 | effect 1 type (thru/tremolo/wah) | FX trem/wah mode |
| 05 | 00–7F | Effect 1 Depth | FX Trem/Wah Depth |
| 06 | 00–7F | Effect 1 Rate | FX Trem/Wah Rate |
| 07 | 00-02 | effect 2 type (thru/chorus/phaser) | FX cho/pha mode |
| 08 | 00–7F | Effect 2 Depth | FX Cho/Pha Depth |
| 09 | 00–7F | Effect 2 Speed | FX Cho/Pha Speed |
| 0A | 00-02 | effect 3 type (thru/D.delay/A.delay) | FX delay mode |
| 0B | 00–7F | Effect 3 Depth | FX Delay Depth |
| 0C | 00–7F | Effect 3 Time | FX Delay Time |
| 0D | 00–7F | Reverb Depth | FX Reverb |
| 0E | 2 | reserved | — |

Total block size: 16 bytes. Read accesses (parameter request, dump) return live
values from the `RefaceCpChain` getters and `mdaEPiano::getCurrentInstrument()`.

## 6. Bulk dump blocks

| Block | Description | Byte count (dec/hex) | Top address |
|---|---|---|---|
| SYSTEM | Common | 36 / 24H | `00 00 00` |
| TG | Bulk Header | 4 / 04H | `0E 0F 00` |
| TG | Common | 20 / 14H | `30 00 00` |
| TG | Bulk Footer | 4 / 04H | `0F 0F 00` |

## 7. MIDI Implementation Chart

```
PicoFaceCP                    MIDI Implementation Chart        Version: 1.0
Model: PicoFaceCP (reface CP compatible)                       Date: 2026-07-02

Function...        Transmitted        Recognized       Remarks
Basic   Default    1                  All (1-16)
Channel Changed    1-16 (SysEx)       1-16, All (SysEx)
Mode    Default    3                  1
        Messages   x                  1,3 (CC 124-127)
Note Number        x                  0-127            transpose +/-12 + octave +/-24
Velocity  NoteOn   x                  o v=1-127
          NoteOff  x                  x
After Touch        x                  x
Pitch Bend         x                  o                +/-2 semitones
Control    1,7,11  x                  o                Mod->Trem-Depth, Vol, Expr
Change     64,66,67 x                 o                Sustain, Sostenuto, Soft
           17-19   o *1               o *1
           80,81   o *1               o *1
           85-91   o *1               o *1
Program Change     o (0-7)            o (0-7)          factory presets (deviation)
System Exclusive   o                  o                Param Change/Request,
                                                       Bulk Dump/Request, Identity
System Common      x                  x
System Real Time   o (FE)             o (FE)           Active Sensing 200ms/350ms
Aux: All Sound Off x                  o (120,126,127)
     Reset All Ctrl x                 o (121)
     Local On/Off  x                  o (SysEx, stored)
     All Notes Off x                  o (123-125)
     Active Sense  o                  o
     Reset         x                  x

*1: Sent and recognized only while MIDI control is on (SYSTEM parameter 0E).
Mode 1: OMNI ON, POLY   Mode 3: OMNI OFF, POLY   o: Yes  x: No
```

## 8. Deliberate deviations from the original

1. **No note TX, no local keyboard** - PicoFaceCP is a tone generator; local
   control and note transmission do not apply (the parameter is accepted and
   stored).
2. **Master tune** is clamped to the fine-tune range of the mdaEPiano engine
   (±50 cents instead of ±102.4).
3. **CC 1 (modulation)** drives the FX tremolo depth. That is a
   parameter-deduplication decision; the original modulates pan or volume
   depending on the voice type - tonally equivalent, because the same tremolo
   block picks pan or volume mode per voice type anyway.
4. **Sostenuto (CC 66)** is treated like sustain by the engine, an
   approximation.
5. **Reset all controllers** does not reset the tremolo depth set from the panel,
   only the performance controllers: pitch bend, expression, pedals.
6. **Auto power-off / speaker output / sustain pedal select** are stored and
   carried in the bulk dump, but have no hardware function.
7. **Program change is implemented; the original has none.** PC 0-7 selects one
   of the 8 static factory presets (instrument + engine parameters + FX chain,
   see `PRESETS.md`); values >= 8 are ignored. TX happens when a preset is
   picked from the menu. It is not gated by the MIDI control setting - as an
   extension of our own it is filtered only by the receive channel, like notes.
   CC 80 (TYPE) keeps its original meaning, selecting the instrument in 6
   zones.
