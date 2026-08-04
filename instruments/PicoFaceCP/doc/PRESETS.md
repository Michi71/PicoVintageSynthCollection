# Factory presets

## Overview

The firmware provides **8 static factory presets**, which replaced the earlier
mdaEPiano programs (Default, Bright, Mellow, Autopan, Tremolo). Each preset
consists of:

- an **instrument** (Rd I, Rd II, Wr, Clv, Pno, CP),
- **12 engine parameters**, and
- the complete **FX chain setting** (drive -> trem/wah -> cho/pha -> delay ->
  reverb).

Tonal orientation: the _velvetkeys_ preset collection
(github.com/Michi71/velvetkeys). A deliberate deviation from the original Yamaha
reface CP, which has no presets at all.

---

## Selecting a preset

### (a) From the menu

1. **Long-press the selector** to open the menu.
2. Choose the **Presets** entry.
3. The encoder scrolls through P0…P7.
4. The preset is **applied immediately**.

### (b) MIDI program change

A **program change 0-7** selects the corresponding preset P0…P7; values >= 8 are
ignored. When a preset is picked from the menu, the firmware sends the matching
program change on the TX channel.

> **Note:** the original reface CP does not implement program change at all -
> this is the only preset-related MIDI deviation. **CC80 (TYPE) keeps its
> original meaning** and selects the instrument (6 zones) exactly as on the
> Yamaha device; picking an instrument on the VOICE screen still sends CC80.
> Program change is not gated by the MIDI control setting, only by the receive
> channel filter.

---

## Factory preset table

| #  | Name          | Instrument | Effects                                           | Character                              |
|----|---------------|------------|---------------------------------------------------|----------------------------------------|
| 0  | Rd I Classic  | Rd I       | reverb 15 %                                       | neutral Rhodes Mark I                  |
| 1  | Rd II Chorus  | Rd II      | chorus 45 %/30 %, reverb 20 %, treble +10 %        | classic suitcase with analog chorus    |
| 2  | Phaser Rd     | Rd II      | phaser 60 %/35 %, reverb 20 %                      | pronounced phaser sweep                |
| 3  | Wurli Trem    | Wr         | tremolo 55 %/50 %, drive 20 %, reverb 15 %         | Wurlitzer 200A with valve-style grit   |
| 4  | Funky Clv     | Clv        | touch wah 70 %/50 %, reverb 10 %                   | funky clavinet                         |
| 5  | Piano Hall    | Pno        | reverb 45 %, release +10 %                         | acoustic piano in a hall               |
| 6  | CP Delay      | CP         | analog delay 35 %/45 %, drive 15 %, reverb 20 %    | CP80 with tape-echo character          |
| 7  | Space Rd      | Rd I       | chorus 50 %/30 %, digital delay 30 %/50 %, reverb 40 %, release +10 % | ambient combined effect |

---

## Technical details

The preset table is defined statically in **`src/presets.cpp`** (struct
`CpPreset`, declared in `include/presets.h`).

A preset is applied **atomically by the audio producer** through
`IPC_CMD_PROGRAM -> preset_apply()`. That call sets:

- the instrument,
- the 12 engine parameters,
- all FX modes and values.

After loading, the **normal settings persistence** takes over: the resulting
panel state is stored, **not** the preset index. Powering up again therefore
gives back the last sound that was active, regardless of where it came from.

### Neutralized engine parameters

The engine parameters **4 (modulation)**, **5 (LFO rate)** and **11
(overdrive)** are neutralized by the presets, because the FX chain is
authoritative and those values would otherwise act on the signal twice. These
parameters are hidden on the **V.PARAMS screen**.
