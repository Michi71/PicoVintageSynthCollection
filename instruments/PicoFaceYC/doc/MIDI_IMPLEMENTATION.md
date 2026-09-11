# PicoFaceYC MIDI Implementation

Source of truth for everything below: the Yamaha *reface CS/DX/CP/YC Data
List* (PO-B0, published 04/2016), section "reface YC MIDI Data Format", pages
15-19, and its MIDI Implementation Chart. Where the firmware departs from it,
or fills in what the document leaves open, the text says so. Yamaha's own
Soundmondo editor (`reface-panel.js`) was read as a second witness for the
identity bytes and the dump it requests; it agrees with the data list.

The shared part of the reface dialect - SYSTEM block, active sensing, channel
filter, transpose, exclusive framing with byte count and checksum, the transmit
side - is `picoface::RefaceMidiBase` in the core
([core/include/picoface/reface_midi.h](../../../core/include/picoface/reface_midi.h)).
This document describes what the YC puts on top of it.

## 1. Overview

USB-MIDI and DIN-MIDI, both wires (the core parses both and the layer transmits
to both). The synth is permanently in POLY mode (the YC has no mono, legato or
portamento). Note numbers 0-127 are recognised with transpose. Program Change
is not part of the reface YC (implementation chart: x/x) and is not processed.

## 2. Note On/Off

Received notes are transposed by the SYSTEM master transpose (-12..+12) and the
panel octave (-2..+2, twelve semitones each), clamped to 0..127. A Note On
with velocity 0 is a Note Off. The RX channel filter (`setRxChannel`,
SYSTEM address 01) defaults to omni (0x10).

## 3. Pitch Bend

Recognised (implementation chart: receive only, o). The YC has no bend range
setting - that exists on the CS only - so the range is the MIDI default of
**±2 semitones**. The bend is a frequency factor 2^(semitones/12) applied to
the cached phase increments of every active voice when the message arrives,
and to the percussion oscillator; the per-sample path is untouched. Centre
(8192) restores the increments bit for bit.

## 4. Controllers recognised whatever the MIDI Control setting

| CC | Name | Effect here | Data list |
|---|---|---|---|
| 1 | Modulation | rotary speed: ≥ 64 FAST, < 64 SLOW | recognised; Yamaha's "Rotary Speed Control - reface YC" article: an external mod wheel switches the rotary between FAST and SLOW. The 64 threshold is ours, the article names none. |
| 7 | Volume | channel volume 0..127, folded into the master gain with the panel volume | recognised |
| 11 | Expression | 0..127, likewise | recognised |
| 64 | Sustain | ≥ 64 on, < 64 off; held notes release when the pedal comes up | recognised |
| 120 | All Sound Off | all voices off | recognised |
| 121 | Reset All Controllers | bend to centre, expression 127, sustain off. The data list also resets modulation to 0; here that would park the rotary at SLOW, so the rotary is left alone. Channel volume is not reset (MIDI convention). | recognised |
| 123 | All Notes Off | all voices off (the engine has no finer action) | recognised |
| 124 / 125 | Omni Off / On | receive channel to 1 / to all, notes off (shared layer) | recognised |
| 126 / 127 | Mono / Poly | "same function as All Sound Off" (data list 3-2-6, 3-2-7) | recognised |

The master gain is one float: panel volume × CC7 × CC11, each /127. At 127 a
controller is a factor of exactly 1.0, so an untouched controller leaves the
panel volume's gain bit-identical. The percussion goes through the same gain
(it used to bypass the volume).

## 5. Panel Control Changes (MIDI Control on)

Transmitted for panel edits and recognised only while MIDI Control (SYSTEM
address 0E) is on - the chart's *1. Bins and transmit values are the data
list's "Recognized" and "Transmitted" columns, verified line by line.

| CC | Name | Recognised bins | Transmitted |
|---|---|---|---|
| 18 | EFFECT DIST | 0-127 continuous | direct |
| 19 | ROTARY SPEED | 0-32 OFF, 33-64 STOP, 65-95 SLOW, 96-127 FAST | 0, 42, 85, 127 |
| 77 | VIBRATO/CHORUS DEPTH | 0-25, 26-51, 52-76, 77-102, 103-127 | 0, 32, 64, 95, 127 |
| 79 | VIBRATO/CHORUS SWITCH | 0-63 VIBRATO, 64-127 CHORUS | 0, 127 |
| 80 | WAVE | 0-25 H, 26-51 V, 52-76 F, 77-102 A, 103-127 Y | 0, 32, 64, 95, 127 |
| 91 | EFFECT REVERB | 0-127 continuous | direct |
| 102-110 | FOOTAGE 16' … 1' | 0-18, 19-36, 37-54, 55-73, 74-91, 92-109, 110-127 | 0, 21, 42, 64, 85, 106, 127 |
| 111 | PERCUSSION ON/OFF | 0-63 off, 64-127 on | 0, 127 |
| 112 | PERCUSSION TYPE | 0-63 A, 64-127 B | 0, 127 |
| 113 | PERCUSSION LENGTH | 0-25, 26-51, 52-76, 77-102, 103-127 | 0, 32, 64, 95, 127 |

CC102-110 are one CC per drawbar in the order 16', 5 1/3', 8', 4', 2 2/3', 2',
1 3/5', 1 1/3', 1'. Octave and volume have no CC and are not mirrored. The
real YC also transmits CC11 from its expression pedal jack; this build has no
pedal and transmits none.

## 6. Active Sensing

0xFE every 200 ms out. The 350 ms receive supervision is armed only by a
received 0xFE and then kept alive by any incoming byte; when it fires, all
sound off. A controller that never sends active sensing never arms it and held
notes sustain indefinitely (see `CHANGELOG_YC_ENGINE.md` §17 for the bug that
once armed it from ordinary traffic).

## 7. System Exclusive

Every reface exclusive starts `F0 43 <cmd|dev> 7F 1C <model>`; the device
number is ignored on receive (the data list: "this instrument receives under
omni"). The model byte is **06H** for the reface YC (03 CS, 04 CP, 05 DX) and
is checked: a message carrying another reface's byte is ignored.

### Identity Request / Reply

`F0 7E 0n 06 01 F7` is answered with the data list's reply for the YC:

```
F0 7E 7F 06 02 43 00 41 54 06 03 00 00 7F F7
```

Bytes 8-9 (`54 06`) are what Soundmondo matches its "reface YC" entry against
(the CP is `52 06`, the DX `53 06`); it also insists on the 15-byte length.
Byte 10 is the firmware version as 1.0 + n/10: the data list prints 00 for the
1.0 it was written against, the current reface YC firmware is 1.30 and what
that update added on the MIDI side (master tune, transmit and receive channel,
local control) is all served by the SYSTEM block here, so 03 is reported, as
the DX port does.

### Parameter Base Addresses

| Block | Address | Size |
|---|---|---|
| SYSTEM | 00 00 00 | 32 (shared layer) |
| TG (tone generator) | 30 00 00 | 22 |

### Parameter Change (1n) - received

`F0 43 1n 7F 1C 06 <ah> <am> <al> <data…> F7`. SYSTEM addresses go to the
shared layer, TG addresses to the table below. Values above a parameter's
range are clamped to its maximum (an editor once sent a footage of 127 and the
engine indexed a 7-entry table with it, §39).

| Address (30 00 xx) | Parameter | Range |
|---|---|---|
| 00 | Volume | 0-127 ("can be set only via MIDI" on the original) |
| 01 | reserved | |
| 02 | Wave | 0-4 H V F A Y |
| 03 - 0B | Footage 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' | 0-6 |
| 0C | Vibrato/Chorus Select | 0-1 |
| 0D | Vibrato/Chorus Depth | 0-4 |
| 0E | Percussion On/Off | 0-1 |
| 0F | Percussion Type | 0-1 |
| 10 | Percussion Length | 0-4 |
| 11 | Rotary Speaker Speed | 0-3 OFF STOP SLOW FAST |
| 12 | Distortion Drive | 0-127 |
| 13 | Reverb Depth | 0-127 |
| 14 - 15 | reserved | |

### Parameter Request (3n) - received

`F0 43 3n 7F 1C 06 <ah> <am> <al> F7` is answered with one Parameter Change
carrying the current value at that address. Addresses past the block are not
answered.

### Dump Request (2n) - received, Bulk Dump (0n) - transmitted and received

`F0 43 2n 7F 1C 06 <ah> <am> <al> F7`. The data list: "to carry out TG bulk
dump request, designate its corresponding Bulk Header address". So:

| Requested | Sent |
|---|---|
| 0E 0F 00 (bulk header) | header, TG block, footer - three messages |
| 30 00 00 | the TG block alone |
| 00 00 00 | the SYSTEM block (shared layer) |

Each block is `F0 43 00 7F 1C <bc hi> <bc lo> 06 <ah> <am> <al> <data…> <sum> F7`
with the byte count over model + address + data and a checksum that brings the
sum of those bytes to 0 mod 128:

| Block | Address | Byte count |
|---|---|---|
| SYSTEM common | 00 00 00 | 36 (4 + 32) |
| Bulk header | 0E 0F 00 | 4 |
| TG common | 30 00 00 | 26 (4 + 22) |
| Bulk footer | 0F 0F 00 | 4 |

A received TG block is applied byte by byte through the same path as a
Parameter Change, header and footer are accepted and carry nothing. This is
what Soundmondo sends on connect (a dump request on 0E 0F 00) and what it
sends back when a sound is loaded.

## 8. Host test

[tools/host_tests/yc_sysex](../../../tools/host_tests/yc_sysex/) runs the
real `midi_reface.cpp` on the shared layer, drains the ring through the
firmware's own `yc_ipc_apply()` and pins every item above: identity bytes,
the three-block dump with byte counts and checksums, an editor's dump reaching
every parameter, range clamping, the model byte check, Parameter Change and
Request, and the controllers of section 4. `run_all.sh` (and the CI) runs it.

## Known limitations

* No real reface YC was on the bench. Model byte, identity, block layout and
  CC bins come from the data list and Soundmondo's source; the CC1 threshold
  (64) and the identity's version byte are the firmware's own choices.
* CC1 switches only between SLOW and FAST, as Yamaha describes it; whether the
  real device also leaves OFF/STOP on a wheel move is not documented.
* No expression pedal input, so CC11 is received but never transmitted.
