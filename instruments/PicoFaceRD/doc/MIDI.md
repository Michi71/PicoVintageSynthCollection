# PicoFaceRD — MIDI Implementation

USB-MIDI (receive only). Modeled on the Roland **MK-80 MIDI
Implementation Chart** (MK-80 Owner's Manual pp. 46–48, "Recognized"
column — the manual is Roland's copyrighted document and is not
redistributed here).
Receive channel 1–16 or Omni, selectable on the SYS page (persisted).

## Recognized

| Message | Data | Behavior |
|---|---|---|
| Note Off | `8n kk vv` / `9n kk 00` | Release velocity ignored |
| Note On | `9n kk vv`, v = 1–127 | Sounding range 21–108 (the captured 88-key sweep); notes outside are octave-shifted into range (identically for on and off). The MK-80 chart folds into 15–113, but the descriptor packs carry no entries for 15–20/109–113 — folding to the pack range keeps those notes audible instead of silent. Velocity 0 = Note Off |
| Control Change 64 | Damper (Hold 1) | ≥ 64 = ON. Half-damping (1–63) is not modeled (treated as OFF) |
| Control Change 92 | Tremolo switch | 0–63 = OFF, 64–127 = ON |
| Control Change 93 | Chorus switch | 0–63 = OFF, 64–127 = ON |
| Control Change 95 | Phaser switch | 0–63 = OFF, 64–127 = ON |
| Control Change 121 | Reset All Controllers | Damper → off, pitch bend → center |
| Control Change 123 | All Notes Off | Releases everything (also the panic rescue) |
| Program Change | `Cn pp` | Chart range 0–63; mapped modulo 16 to the instrument table (see below) |
| Pitch Bend | `En ll mm` | ±2 semitones (MK-80 default bender depth) |

All other controllers and messages are ignored.

### Program numbers

| PC (mod 16) | Instrument | PC (mod 16) | Instrument |
|---|---|---|---|
| 0 | MKS-20 Piano 1 | 8 | MK-80 Classic |
| 1 | MKS-20 Piano 2 | 9 | MK-80 Special |
| 2 | MKS-20 Piano 3 | 10 | MK-80 Blend |
| 3 | MKS-20 Harpsichord | 11 | MK-80 Contemporary |
| 4 | MKS-20 Clavi | 12 | MK-80 A. Piano 1 |
| 5 | MKS-20 Vibraphone | 13 | MK-80 A. Piano 2 |
| 6 | MKS-20 E-Piano 1 | 14 | MK-80 Clavi |
| 7 | MKS-20 E-Piano 2 | 15 | MK-80 Vibraphone |

## Deliberately not implemented

| Message | Reason |
|---|---|
| CC 1 Modulation | The engine has no vibrato path |
| Active Sensing (FE) | Meaningless over USB transport |
| System Exclusive (Roland model 2FH) | The MK-80 SysEx edits patch RAM; PicoFaceRD plays fixed ROM-derived descriptors |
| Aftertouch, mode messages, system common/realtime | Not recognized by the MK-80 either |

## Notes

- FX switches received via MIDI (CC 92/93/95) change the engine state
  directly; the panel shadow is not updated, and the next encoder edit
  on that page re-sends the panel value. Panel state, not MIDI state, is
  what gets persisted.
- Master tune is a panel-only function (the MK-80 does not recognize
  MIDI tune messages either).
