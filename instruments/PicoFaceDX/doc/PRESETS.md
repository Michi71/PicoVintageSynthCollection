# Factory presets

## Overview

The firmware contains the complete factory preset bank of the Yamaha reface DX:
all 32 official factory voices, parsed byte-exact from the official `.syx`
dumps. The presets live in `src/presets.cpp` as a constant table and are
addressed by index or by MIDI program change.

Definitions in `include/presets.h`:

```c
#define DX_NPRESETS 32
struct DxPreset { const char* name; RDX_Patch patch; };
extern const DxPreset dxPresets[DX_NPRESETS];
```

The array order matches the real reface DX program change assignment exactly
(per the official Yamaha Data List):

- PC 0-7 = bank 1, slots 1-8
- PC 8-15 = bank 2, slots 1-8
- PC 16-23 = bank 3, slots 1-8
- PC 24-31 = bank 4, slots 1-8

## Selecting a preset

### a) From the menu

A long press of the selector opens the menu: **MENU -> Presets**. A selection
list of all 32 factory preset names appears, opening on the one currently
loaded. The encoder scrolls the list; picking an entry applies the preset via
`preset_stage` / `preset_set_current` and sends the corresponding MIDI program
change through `refaceMidi.txProgram`.

### b) MIDI program change

Program change covers the **entire real address range**:

- PC 0-31 are valid and select the corresponding preset through
  `onProgramChange -> preset_stage`.
- Values >= 32 are ignored, matching the real spec: exactly 32 addressable
  slots, no open gap.

## Factory preset table

| PC# | Name |
|----:|------|
| 0 | DigiChord |
| 1 | WobbleBass |
| 2 | MotionPad |
| 3 | LegendEP |
| 4 | DynaLead |
| 5 | DarkBass |
| 6 | TublarBell |
| 7 | D_n_Beats |
| 8 | BeginSweep |
| 9 | MoDemLead |
| 10 | BeepBass |
| 11 | BitTune |
| 12 | TinPerc |
| 13 | BleepClv |
| 14 | FeelIt |
| 15 | BuzzSiren |
| 16 | WoodEP |
| 17 | UniLead |
| 18 | AttackBass |
| 19 | CloudPad |
| 20 | AmbiPluck |
| 21 | Marimba |
| 22 | CheezOrgan |
| 23 | FM_Brass |
| 24 | SolPhase |
| 25 | FlyingKode |
| 26 | AlTiPad |
| 27 | StarPad |
| 28 | WarmPad |
| 29 | FutureBell |
| 30 | GlassHarp |
| 31 | Chopper |

## Technical details

### Provenance of the `.syx` conversion

The 32 presets were extracted from the official `.syx` factory voice bulk dumps
that ship with the ESP32 reference project this codebase's FM engine was ported
from (`RDX/data/patches/*.syx` in that project). Neither the reference project
nor the `.syx` files are part of this repository.

A one-time host-only tool,
[`tools/dx_syx_to_patches/syx_to_patches.cpp`](../../../tools/dx_syx_to_patches/syx_to_patches.cpp),
parses those dumps with the already verified `syxToPatch()` function
(`include/dx_engine/RDX_Types.h`). The tool is **not part of the firmware
build** and is **not compiled by CMake**. Its stdout produces 32 byte-exact
`patchFromBytes({...})` initializers, which were pasted straight into the
`dxPresets[]` table in `src/presets.cpp`.

### Verification

Byte-exact verification: the `voiceName` field of each patch, decoded as ASCII,
matches the real factory voice name exactly (spot-checked on "DigiChord",
"WobbleBass", "GlassHarp" and "Chopper").

### preset_apply / preset_stage

- `preset_stage(uint8_t idx)` (control side): stages `dxPresets[idx].patch` in
  the staging slot (`include/dx_patch_stage.h`) and sends
  `IPC_CMD_DX_PATCH_APPLY`.
- `preset_apply(DX_Synth_Bridge* dx)` (**producer only**): copies the staged
  patch into `dx->patch()`.

## Persistence

Unchanged: after a preset is loaded, the resulting **patch bytes** - not the
preset index - are stored by the normal settings autosave.

## Status

What used to be a separate planned task ("factory voice library": one hardcoded
preset versus the 32-voice factory bank of the real reface DX) is **done**. The
preset table now has full parity with the real reface DX factory bank: 32
voices, PC 0-31.

Note: the patch directory of the ESP32 reference project contains a 33rd file,
`00-Init_Voice.syx`. It is not a real factory bank slot and falls outside the
program change address range 0-31; it was deliberately left out of the preset
table to keep the addressing exactly on the real 32-slot spec.
