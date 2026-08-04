# PicoFaceYC MIDI Implementation

## 1. Overview

PicoFaceYC communicates via USB-MIDI using TinyUSB. There is no DIN-MIDI port. The synth operates permanently in POLY mode (no Mono, Legato, or Portamento). Note numbers 24-108 are supported with Transpose. Program Change messages are not supported by the reface YC according to the MIDI Implementation Chart and are therefore not processed.

## 2. Note On/Off

Octave transposition is derived from the panel Octave value (-2..+2, multiplied by 12 semitones). The final Note Number + Transpose value is clamped to a range of 0..127.

Incoming MIDI notes are subject to an RX channel filter (`RefaceMidi::setRxChannel`). The default setting is Omni (0x10).

## 3. Sustain (CC64)

Sustain messages (CC64) are evaluated as follows: values >=64 are treated as ON, values <64 are treated as OFF. Sustain is ALWAYS processed, independent of the MIDI Control Mode (unlike the panel CCs listed below). For organ tones, sustain strictly means Note-Off suppression, not envelope extension.

## 4. MIDI Control Mode Gating

An internal flag (`_midiControlEnabled`, default: true) determines whether the following panel Control Changes are processed and sent. This flag is persisted as `midi_ctrl_mode` in `yc_panel_state_t`.

## 5. Panel Control Changes

The following table lists all implemented Panel Control Changes:

| CC | Name | Bins (Received) | Transmit Values (Sent) |
|---|---|---|---|
| 18 | EFFECT DIST | 0-127 continuous | direct |
| 19 | ROTARY SPEED | 0-32=OFF, 33-64=STOP, 65-95=SLOW, 96-127=FAST | 0, 42, 85, 127 |
| 77 | VIBRATO/CHORUS DEPTH | 0-25, 26-51, 52-76, 77-102, 103-127 (5 steps) | 0, 32, 64, 95, 127 |
| 79 | VIBRATO/CHORUS SWITCH | 0-63=Vibrato, 64-127=Chorus | 0, 127 |
| 80 | WAVE | 0-25=H, 26-51=V, 52-76=F, 77-102=A, 103-127=Y | 0, 32, 64, 95, 127 |
| 91 | EFFECT REVERB | 0-127 continuous | direct |
| 102-110 | FOOTAGE 16'...1' | 0-18, 19-36, 37-54, 55-73, 74-91, 92-109, 110-127 (7 steps) | 0, 21, 42, 64, 85, 106, 127 |
| 111 | PERCUSSION ON/OFF | 0-63=off, 64-127=on | 0, 127 |
| 112 | PERCUSSION TYPE | 0-63=A, 64-127=B | 0, 127 |
| 113 | PERCUSSION LENGTH | 0-25, 26-51, 52-76, 77-102, 103-127 (5 steps) | 0, 32, 64, 95, 127 |

*Note: CC102 to CC110 represent 9 CCs, one per foot in the following order: 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'.*

*Note: CC1 (Mod), CC7 (Volume), and CC11 (Expression) are mentioned in the spec but are currently not mapped to a panel field in this implementation (no effect).*

## 6. Active Sensing

Active Sensing messages (0xFE) are transmitted every 200ms. The 350ms RX supervision timeout is only **armed** by actually receiving a real Active Sensing byte (0xFE) from the connected device. Once armed, it is kept alive by ANY incoming MIDI message (not just further 0xFE bytes; see `activity_callback` in `main.cpp`). If a controller never sends Active Sensing at all, the timeout is never armed and held notes sustain indefinitely, as expected. If the timeout does fire (armed, then no traffic for >350ms), an All-Notes-Off message is sent as a safety measure.

*Note: an earlier revision incorrectly armed the timeout from generic MIDI activity instead of only from a real 0xFE byte, which caused every held note to be killed after ~350ms when talking to controllers that do not implement Active Sensing (the common case). Fixed — see `doc/CHANGELOG_YC_ENGINE.md` §17.*

## 7. System Exclusive (SysEx)

### Identity Request
Universal Non-Realtime Identity Request (`F0 7E 7F 06 01 F7`) is recognized via a simplified pattern check. The response is an Identity Reply (`F0 7E 7F 06 02 43 ... F7`).

**IMPORTANT NOTE: The Model-ID byte is NOT verified from a real device. A placeholder constant `YC_MODEL_ID=0x00` is used in `midi_reface.cpp`. Before productive SysEx use with real editor software, this value must be confirmed from an actual reface YC device dump.**

### Parameter Change (Set) - Received
Format: `F0 43 1n 7F 1C <ModelID> 30 00 <AddrLow> <Value> F7` (11 bytes).
`n` represents the Device Number (0-15). The base address for the Tone Generator block is `30 00 00h`.

| Address (hex) | Parameter | Range |
|---|---|---|
| 00 | Volume | 0-127 (MIDI/SysEx only, no CC) |
| 02 | Wave | 0-4 |
| 03 | Footage 16' | 0-6 |
| 04 | Footage 5 1/3' | 0-6 |
| 05 | Footage 8' | 0-6 |
| 06 | Footage 4' | 0-6 |
| 07 | Footage 2 2/3' | 0-6 |
| 08 | Footage 2' | 0-6 |
| 09 | Footage 1 3/5' | 0-6 |
| 0A | Footage 1 1/3' | 0-6 |
| 0B | Footage 1' | 0-6 |
| 0C | Vibrato/Chorus Select | 0-1 |
| 0D | Vibrato/Chorus Depth | 0-4 |
| 0E | Percussion On/Off | 0-1 |
| 0F | Percussion Type | 0-1 |
| 10 | Percussion Length | 0-4 |
| 11 | Rotary Speed | 0-3 |
| 12 | Distortion | 0-127 |
| 13 | Reverb | 0-127 |

### Parameter Request - Received
Format: `F0 43 3n 7F 1C <ModelID> 30 00 <AddrLow> F7` (10 bytes).
Upon reception, the response is a Parameter-Change message containing the current value of the requested address (using the same address table as above).

## Known Limitations

* Model-ID byte unverified (see SysEx Identity Request note above).
* Full Bulk-Dump block (Byte-Count / Bulk-Header / Bulk-Footer / Checksum, TG Common block 26 bytes starting at address 0E 0F 00 header + 30 00 00 data + 0F 0F 00 footer according to the official Data List) is NOT implemented. This was intentionally deferred due to the higher risk of errors in block framing compared to the benefit (Parameter Change already covers every address individually).
* Dump Request (`F0 43 2n 7F 1C ... F7`) is not processed.
* No DIN-MIDI support (USB only).
* CC1, CC7, and CC11 are received but ignored (no panel field mapping).
