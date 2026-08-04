# 11-Page Encoder Menu (YC_Controller/YC_GUI)

## Encoder Controls
- **Selector**: Page navigation (bidirectional modulo).
- **ParamA**: Edits the first value of the current page.
- **ParamB**: Edits the second value of the current page.
- **Selector Long Press** (>=500ms): Opens System menu (About/CPU Load). Implemented via `pico_frontpanel.cpp`, reused from PicoFaceDX.
- **ParamA Short Press**: Toggles Percussion On/Off. This is only effective on the PERCUSSION page.

## Pages (YcPage enum, order matters)

| Page ID | Page Name       | ParamA                                | ParamB                            |
|---------|-----------------|---------------------------------------|-----------------------------------|
| 0       | VOLUME          | Volume (0-127)                        | Unused (reserved)                |
| 1       | WAVE_OCTAVE     | Wave (0-4: H/V/F/A/Y)                 | Octave (-2..2)                    |
| 2       | FOOT_16_513     | Footage 16' (0-6)                     | Footage 5 1/3' (0-6)              |
| 3       | FOOT_8_4        | Footage 8' (0-6)                      | Footage 4' (0-6)                  |
| 4       | FOOT_223_2      | Footage 2 2/3' (0-6)                  | Footage 2' (0-6)                  |
| 5       | FOOT_135_113    | Footage 1 3/5' (0-6)                  | Footage 1 1/3' (0-6)              |
| 6       | FOOT_1          | Footage 1' (0-6)                      | Unused (reserved)                |
| 7       | PERCUSSION      | Type (A/B)                            | Length (0-4), Push=On/Off-Toggle  |
| 8       | VIBCHO          | Select (Vibrato/Chorus)               | Depth (0-4)                       |
| 9       | ROTARY          | Speed (OFF/STOP/SLOW/FAST)            | Unused (reserved)                 |
| 10      | EFFECT          | Distortion (0-127)                    | Reverb (0-127)                    |

## Parameter Update Behavior
Every parameter change sends an IPC command to Core 0 (Engine Mutation). If MIDI Control is active, it also sends a MIDI-CC-Panel-Mirror message (see `doc/MIDI_IMPLEMENTATION.md`).
**Exception:** Octave and Volume do not send a MIDI CC message (no CC equivalent according to spec).
