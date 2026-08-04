# PicoFaceDX — MIDI Implementation

Version: 2.0

Implemented in `include/midi_reface.h` / `src/midi_reface.cpp` on top of the core's USB and DIN MIDI transports. The old reface CP MIDI layer was removed entirely; only this specification applies.

## 1 Coverage

PicoFaceDX is a MIDI tone generator, a Yamaha reface DX FM synth clone. It receives on USB and, since the move into the collection, on the DIN port as well - the core feeds both into the same dispatch. It transmits identity replies, bulk dump replies, parameter request replies, active sensing and the program change side effect of picking a preset (`txProgram`). There is no local keyboard and no generic note on/off transmit path.

## 2 Compliance

Voice messages are received according to the channel filter (SYSTEM RX channel, default All). Note events are transposed (+SYSTEM master transpose, +UI octave) and handed to `DX_Synth_Bridge::noteOn`/`noteOff` through the ring. Pitch bend is passed on as a raw 14-bit value; the producer converts it into a centred signed value before calling `RDX_Synth::updatePB`, and the range is governed by the patch parameter `pbRange` (per algorithm and patch, not a fixed ±2 semitones). Aftertouch is not implemented (RX and TX both x). Program change covers the full address range of the real reface DX (0-31, 32 factory presets, see section 9).

## 3 Transmit / Receive

### 3-1 Channel Voice Messages

| Status | Funktion | TX | RX | Bemerkung |
|---|---|---|---|---|
| 8n / 9n v=0 | Note Off | x | o | Kanalgefiltert (SYSTEM RX), transponiert, IPC an `DX_Synth_Bridge::noteOff` |
| 9n v>0 | Note On | x | o | Kanalgefiltert (SYSTEM RX), transponiert, IPC an `DX_Synth_Bridge::noteOn` |
| Bn | Control Change | o* | o | *TX only as a program change side effect (`txProgram`); no generic CC TX, since there is no front-panel FX chain. For RX see 3-2 and 3-3 |
| En | Pitch Bend | x | o | Roh 14-Bit per IPC; Core 0 wandelt in Signed; Bereich via Patch `pbRange` |
| Cn | Program Change | o | o | 0-31, all 32 real factory presets (`DX_NPRESETS=32`, see `PRESETS.md`). Selects a preset via `preset_stage()`. Not restricted by the MIDI control gate, only by the channel filter |
| An / Dn | Aftertouch (Poly/Ch) | x | x | Nicht implementiert |

### 3-2 Control Change - tier 1 (handled locally, not forwarded to the engine)

| CC | Funktion | TX | RX | Bemerkung |
|---|---|---|---|---|
| 121 | Reset All Controllers | x | o | Sets, through the ring: pitch bend -> centre, modulation (CC1) -> 0 (minimum), expression (CC11) -> 127 (maximum), sustain (CC64) -> off - exactly as specified in the official Data List |
| 124 | Omni Off | x | o | All Notes Off + RX-Kanal := 0 |
| 125 | Omni On | x | o | All Notes Off + RX-Kanal := All (0x10) |

**CC66 (sostenuto) and CC67 (soft pedal) do not exist on the real reface DX** (only the reface CP has them) and are therefore deliberately not handled - they fall through to tier 2 and are ignored, since `RDX_Synth::processCC` has no case for them. An earlier version of this firmware inherited a CC67 soft pedal handler from the (since removed) reface CP layer; that was a deviation from the spec and has been removed.

### 3-3 Control Change — Tier 2 (per IPC unverändert an Core 0 `DX_Synth_Bridge::processCC` → `RDX_Synth::processCC` weitergeleitet)

| CC | Funktion | TX | RX | Bemerkung |
|---|---|---|---|---|
| 0 / 32 | Bank Select MSB / LSB | x | o | Accepted, currently unused (all 32 presets are addressable through program change 0-31 alone, so no bank switching is needed) |
| 1 | Mod Wheel | x | o | |
| 5 | Portamento Time | x | o | |
| 7 | Main Volume | x | o | Berechnet Ausgangsverstärkung neu |
| 11 | Expression | x | o | Multiplied with the main volume; recomputes the output gain, as CC7 does |
| 64 | Sustain | x | o | Correct note-off-if-not-held logic on release |
| 65 | Portamento On/Off | x | o | |
| 80 *1 | Algorithm Quick-Select | x | o | 0-127 scaled onto 0-11 |
| 85 *1 | Op1 Output Level | x | o | |
| 86 *1 | Op1 Feedback | x | o | |
| 87 *1 | Op1 Feedback Type | x | o | |
| 88 *1 | Op1 Freq Mode | x | o | |
| 89 *1 | Op1 Freq Coarse | x | o | |
| 90 *1 | Op1 Freq Fine | x | o | |
| 102-107 *1 | Op2: the same 6 parameters | x | o | As CC85-90, for operator 2 |
| 108-113 *1 | Op3: the same 6 parameters | x | o | As CC85-90, for operator 3 |
| 114-119 *1 | Op4: the same 6 parameters | x | o | As CC85-90, for operator 4 |
| 120 | All Sound Off | x | o | |
| 123 | All Notes Off | x | o | |

*1: Received (and transmitted) only while SYSTEM "MIDI control" (address `0E`) is on - exactly as specified for the real reface DX. All other tier 2 CCs are always active, independent of that setting.


### 3-4 Channel Mode Messages

See tier 1 (CC124 omni off, CC125 omni on) plus CC120 all sound off, CC123 all notes off and CC121 reset all controllers. Omni off sets the RX channel to 0, omni on sets it to All (0x10); both additionally trigger all notes off.

## 4 SYSTEM-Parametertabelle

Basisadresse `00 00 00`, 32 Bytes gesamt (spiegelt `RDX_System` aus `dx_engine` bytefürbyte).

| Adresse | Größe | Feld | Wertebereich / Bemerkung |
|---|---|---|---|
| 00 | 1 | TX MIDI Channel | 0–0F |
| 01 | 1 | RX MIDI Channel | 0–0F; 10 = All |
| 02-05 | 4 | Master Tune | 4 nibbles (`SYS_TUNE_0..3`), recombined into a 16-bit raw value (`(t0<<12)\|(t1<<8)\|(t2<<4)\|t3`), decoded to cents (`(raw-1024)*0.1`, clamped to ±102.4/±102.3 cents) and applied through its own ring command (`IPC_CMD_DX_MASTER_TUNE`) additively to the pitch bend of all operators (phase D, see `CHANGELOG_DX_ENGINE.md` §16) |
| 06 | 1 | Local Control | 0/1; no local keyboard, stored only |
| 07 | 1 | Master Transpose | 0x34-0x4C = -12…+12 semitones; applied to incoming notes |
| 08-09 | 2 | Tempo | Stored, not consumed |
| 0A | 1 | LCD Contrast | Stored, not consumed (there is no LCD) |
| 0B | 1 | Pedal Model | Stored, not consumed |
| 0C | 1 | Auto Power Off | 0/1, gespeichert |
| 0D | 1 | Speaker On | 0/1, gespeichert |
| 0E | 1 | MIDI Control | 0/1, gespeichert; gated CC80/85-90/102-119 (siehe 3-3, *1) |
| 0F–1F | 17 | reserviert | Gespeichert |

## 5 Common-Parametertabelle

Basisadresse `30 00 00`, 38 Bytes gesamt (spiegelt `RDX_Common`).

| Offset | Größe | Feld |
|---|---|---|
| 00–09 | 10 | voiceName |
| 0A–0B | 2 | reserved1 |
| 0C | 1 | transpose |
| 0D | 1 | monoPoly |
| 0E | 1 | portaTime |
| 0F | 1 | pbRange |
| 10 | 1 | algorithm (0–11) |
| 11 | 1 | lfoWave |
| 12 | 1 | lfoSpeed |
| 13 | 1 | lfoDelay |
| 14 | 1 | lfoPMD |
| 15–18 | 4 | pegRate |
| 19–1C | 4 | pegLevel |
| 1D-22 | 6 | effects (2x3): per slot [type, param1, param2] - type 0-7 = thru/distortion/touch wah/chorus/flanger/phaser/delay/reverb, processed post-mix by `DX_FXHost` (`include/dx_engine/DX_FXHost.h`); patch- and SysEx-addressable only, no dedicated CC control (confirmed against the official Data List) |
| 23–25 | 3 | reserved2 |

## 6 Operator-Parametertabelle

Basisadresse `31 <opNum 0–3> 00`, 28 Bytes pro Operator (spiegelt `RDX_OpParams`).

| Offset | Größe | Feld |
|---|---|---|
| 00 | 1 | enable |
| 01–04 | 4 | egRate |
| 05–08 | 4 | egLevel |
| 09 | 1 | rateScaling |
| 0A–0D | 4 | scaleLD / scaleRD / scaleLC / scaleRC |
| 0E | 1 | lfoAMD |
| 0F | 1 | lfoPMDEnable |
| 10 | 1 | pegEnable |
| 11 | 1 | velSens |
| 12 | 1 | outLevel |
| 13 | 1 | feedback |
| 14 | 1 | fbType |
| 15 | 1 | freqMode |
| 16 | 1 | freqCoarse |
| 17 | 1 | freqFine |
| 18 | 1 | freqDetune |
| 19–1B | 3 | reserved |

## 7 Bulk Dump Blöcke

Yamaha SysEx header: ID `43H`, group `7F 1C`, model ID `05H` (reface DX; the old reface CP layer used `04H`). The command nibble is bits 4-6 of the third byte (`d[2]&0x70`): `0x10` parameter change (RX), `0x00` bulk dump (RX), `0x20` dump request (RX, answered by TX), `0x30` parameter request (RX, answered by TX).

### Identity Request / Reply

Identity Request (RX): `F0 7E 0n 06 01 F7`
Identity Reply (TX): `F0 7E 7F 06 02 43 00 41 53 06 00 00 00 7F F7`

Note the model ID bytes `41 53 06` (the reface CP used `41 52 06`; byte 0x53 vs 0x52 is the only difference, confirmed against the real reface DX and the ESP32 reference implementation).

### Parameter Change (RX)

`F0 43 1n 7F 1C 05 <AddrH> <AddrM> <AddrL> <Data> F7` — ein Byte pro Parameter; adressiert System/Common/Operator-Blöcke (siehe Tabellen in den Abschnitten 4–6).

### Bulk Dump (RX/TX)

`F0 43 0n 7F 1C <ByteCountHi> <ByteCountLo> 05 <AddrH> <AddrM> <AddrL> <Data...> <Checksum> F7`

Byte Count = 1 (Model ID) + 3 (Adresse) + Datenlänge.
Checksum: the Yamaha/Roland standard, `(128 - (sum & 0x7F)) & 0x7F` over the model ID, address and data bytes; an invalid checksum discards the block.

RX writes into a staging patch (`include/dx_patch_stage.h`) instead of into the live engine - the control side must never mutate the live patch directly. The bulk footer block (address `0F 0F xx`) triggers `IPC_CMD_DX_PATCH_APPLY`, which copies the staging patch into the live engine at the next block boundary. The bulk header block (address `0E 0F xx`) first initializes the staging area with the CURRENT live patch, so that a partial dump - only the common block, say - does not overwrite unrelated operator data with stale staging leftovers.

### Dump Request (RX) → TX-Antwort

| Adresse | Antwort |
|---|---|
| `00 00 00` | Bulk: SYSTEM Common (32 Bytes) |
| `0E 0F 00` | Bulk Header (0 Bytes) + Common-Block (`30 00 00`, 38 Bytes) + 4× Operator-Blöcke (`31 00 00`..`31 03 00`, je 28 Bytes) + Bulk Footer (0 Bytes) — vollständiger Patch-Dump |
| `30 00 00` | the common block only (38 bytes) |
| `31 <op> 00` | a single operator block only (28 bytes) |

### Parameter Request (RX) → TX-Antwort

Same addressing as the dump request; the reply is the single current byte at that address, read live from the engine (a read of `DX_Synth_Bridge::patch()` from the control side is an accepted convention in this codebase).

### Bulk-Dump-Blockübersicht

| Block | Adresse | Bytes |
|---|---|---|
| SYSTEM Common | `00 00 00` | 32 |
| Bulk Header | `0E 0F 00` | 0 |
| Common | `30 00 00` | 38 |
| Operator 0 | `31 00 00` | 28 |
| Operator 1 | `31 01 00` | 28 |
| Operator 2 | `31 02 00` | 28 |
| Operator 3 | `31 03 00` | 28 |
| Bulk Footer | `0F 0F xx` | 0 |

## 8 MIDI Implementation Chart

```
PicoFaceDX — MIDI Implementation Chart
Model: PicoFaceDX (reface DX clone)        Version: 2.0

+-----------------------------+----+----+-------------------------------------------+
| Function                    | TX | RX | Remarks                                   |
+-----------------------------+----+----+-------------------------------------------+
| Basic Channel               |  o |  o | 1–16 (SYSTEM TX/RX channel)              |
| Mode                        |  3 |  3 | Mode 3: OMNI OFF / POLY                   |
| Note Number                 |  x |  o | RX only; transposed (master+UI); IPC      |
| Velocity                    |  x |  o | RX note on v>0                            |
| After Touch                 |  x |  x | Not implemented                           |
| Pitch Bend                  |  x |  o | 14-bit raw via IPC; range = patch pbRange |
| Control Change              |  o*|  o | *TX only PC side-effect; Tier1: CC121/    |
|                             |    |    | 124/125 local; Tier2: CC0/32/1/5/7/11/64/ |
|                             |    |    | 65/120/123 always on, CC80/85-90/102-119  |
|                             |    |    | gated by MIDI Control (real spec)         |
| Program Change               |  o |  o | 0-31, all 32 real factory voices          |
| System Exclusive             |  o |  o | Yamaha ID 43H, Group 7F 1C, Model 05H;    |
|                             |    |    | Param Change/Bulk/Dump Req/Param Req;     |
|                             |    |    | Identity Reply 41 53 06                    |
| System Common                |  x |  x | Not implemented                           |
| System Real Time             |  o |  o | Active Sensing FE: TX 200ms; RX 350ms      |
|                             |    |    | timeout -> All Sound Off + All Notes Off   |
| Aux: All Sound Off           |  x |  o | CC120                                     |
| Aux: Reset All Controllers   |  x |  o | CC121 (local)                             |
| Aux: Local On/Off             |  x |  o | SYSTEM byte stored; no local keyboard     |
| Aux: All Notes Off            |  x |  o | CC123; CC124/125 also trigger             |
| Aux: Active Sense              |  o |  o | See System Real Time                      |
| Aux: Reset                    |  x |  x | Not implemented                           |
| Notes                        |    |    | Mode 1: OMNI ON/POLY; Mode 3: OMNI OFF/    |
|                             |    |    | POLY. 'o'=Yes, 'x'=No. No DIN port; USB-   |
|                             |    |    | MIDI Cable 0 only. Front-panel FX1/FX2    |
|                             |    |    | pages. MIDI Control gate active.          |
+-----------------------------+----+----+-------------------------------------------+
```

## 9 Deviations

1. **No local keyboard, no generic note TX** - PicoFaceDX is a tone generator; local control is accepted and stored but has no effect.
2. **Master tune fully wired** (since phase D) - `tuningSemitones` acts additively to the pitch bend on all operators; see the SYSTEM table above (address 02-05) and `CHANGELOG_DX_ENGINE.md` §16.
3. **Program change fully implemented** - 0-31, all 32 real factory presets (4 banks of 8), parsed byte-exact from the official `.syx` factory dumps. See `PRESETS.md` for the complete preset table and the provenance of the conversion.
4. **CC66 (sostenuto) and CC67 (soft pedal) deliberately not implemented** - neither exists on the real reface DX, only on the reface CP. An earlier version of this firmware inherited a CC67 soft pedal handler from the (since removed) reface CP layer; that was a deviation from the spec and has been removed.
