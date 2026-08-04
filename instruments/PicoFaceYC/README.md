# PicoFaceYC

A Yamaha reface YC combo/home organ clone, one of the eight instruments in
[PicoVintageSynthCollection](../../README.md). The organ engine is a
wavetable-based additive footage synthesis architecture ported from setBfree
concepts.

## Features

| Category | Details |
| :--- | :--- |
| **Engine** | Organ Flutes tone generator (wavetable-based additive footage synthesis). 5 wave types: H (tonewheel warm), V (Vox square), F (Farfisa sawtooth), A (Japanese transistor bright), Y (Yamaha transistor). Derived/ported from setBfree (BeatrixCPP). |
| **Polyphony** | 16 voices (`YC_MAX_VOICES`). The Yamaha spec states 128; 32 still overloaded the RP2350 at 9 footages per voice plus full FX (§36), so the cap was lowered to 16 for guaranteed stability. Tunable `constexpr` in `yc_core.h`; a CPU-load watchdog (§38) forces all-notes-off on sustained overload. Raise only after an on-hardware CPU-load measurement. |
| **Sample rate** | 44100 Hz |
| **DMA buffer** | 64 samples (`kChunkLen` in `YC_Synth_Bridge`) |
| **DSP style** | Header-only (`include/yc_engine/`), no dynamic allocation, fixed buffers, single-precision float throughout. |
| **MIDI** | USB MIDI and DIN MIDI (both provided by the core). Note on/off with octave transpose, sustain (CC64) always active, RX channel filter (omni default), gated panel CCs, outgoing panel mirroring, SysEx parameter change/request, identity reply. NO program change. |
| **Effects** | Fixed chain: percussion -> vibrato/chorus -> overdrive -> rotary speaker -> reverb -> soft-clip limiter. |
| **Persistence** | Virtual EEPROM (wear-levelled flash append log). Autosave 2 s after the last change. |
| **Presets** | None. The panel state is fully SysEx-addressable. |
| **Display** | SH1106 128x64 OLED |
| **Controls** | 3 rotary encoders (selector/param A/param B) across 11 pages. |

## Signal flow / architecture

```text
USB MIDI / DIN MIDI (core dispatch)
    |
    v
RefaceMidi --ring--> YC_Synth_Bridge --> yc_engine (tonegen/percussion/vibrato/rotary/FX/reverb) --> I2S DAC
    ^
    |
YC_Controller --ring--> [same bridge]
    |
    v
YC_Ui --> YC_GUI --> SH1106 OLED
```

Everything runs on core0: the audio producer drains the ring at the start of
each block, MIDI dispatch and panel edits push into it. Until the move to the
collection's standard runtime model, the whole user interface owned core1 and
the ring was a cross-core FIFO - see
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md), section 4a, and
[doc/IPC_PROTOCOL.md](doc/IPC_PROTOCOL.md).

## Hardware

The board is the same for every instrument in the collection; the pin map lives
in [core/include/project_config.h](../../core/include/project_config.h). The
DIN MIDI header on GPIO 4/5, unused in the standalone version of this project,
is wired up by the core now.

## Layout

```text
instruments/PicoFaceYC/
├── effects/ram_hot.h
├── include/
│   ├── yc_engine/          header-only DSP: core, tonegen, wavetable,
│   │                       percussion, vibrato, rotary, fx, reverb, LUT data
│   ├── YC_Controller.h     panel pages, encoder handling
│   ├── YC_GUI.h            panel drawing
│   ├── YC_Synth_Bridge.h   engine wrapper for the audio path
│   ├── YC_Ui.h             front panel and menu as a state machine
│   ├── ipc.h               same-core ring between control side and producer
│   ├── midi_reface.h       reface MIDI layer (channel filter, CC, SysEx)
│   └── settings.h          persisted panel snapshot
├── src/                    the matching .cpp files plus YC_Instrument.cpp,
│                           the adapter implementing picoface::Instrument
├── doc/                    MIDI implementation, persistence, UI page map,
│                           IPC protocol, engine changelog
└── instrument.cmake
```

## Building

Built together with the rest of the collection, or on its own:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceYC
cmake --build build
```

Footprint in the collection build: 135,224 bytes of flash, 47,824 bytes of RAM
(`arm-none-eabi-size`, text/bss). The LUT set lives in XIP flash (`.rodata`);
the active wave table is mirrored into an 8 KB RAM buffer (§33) so the
voice-render hot path never touches flash.

## Controls

3 rotary encoders (selector, param A, param B) across 11 pages. A long press of
the selector opens the system menu (About / CPU Load).

| Page | Param A | Param B |
| :--- | :--- | :--- |
| 1. VOLUME | Volume (0-127) | - (reserved) |
| 2. WAVE/OCTAVE | Wave type | Octave |
| 3. FOOTAGE 16'/5 1/3' | 16' drawbar | 5 1/3' drawbar |
| 4. FOOTAGE 8'/4' | 8' drawbar | 4' drawbar |
| 5. FOOTAGE 2 2/3'/2' | 2 2/3' drawbar | 2' drawbar |
| 6. FOOTAGE 1 3/5'/1 1/3' | 1 3/5' drawbar | 1 1/3' drawbar |
| 7. FOOTAGE 1' | 1' drawbar | - (reserved) |
| 8. PERCUSSION | Type | Length |
| 9. VIBRATO/CHORUS | Select | Depth |
| 10. ROTARY | Speed | - |
| 11. EFFECT | Distortion | Reverb |

*Note: on the PERCUSSION page a short press of param A toggles percussion on/off.*

## MIDI

- **Note handling**: note on/off with octave transpose. Sustain (CC64) is always active. The RX channel filter defaults to omni.
- **Panel CCs**: the gated panel CCs (wave / footage x9 / percussion x3 / vibrato-chorus x2 / rotary / distortion / reverb) are only processed while MIDI control is active.
- **Panel mirroring**: encoder edits on the panel are transmitted as outgoing MIDI CC, on USB and DIN alike.
- **SysEx**: parameter change (set) and parameter request (reply) for all tone generator addresses, plus identity reply.
- **Program change**: NOT supported (the YC does not support it per the official MIDI implementation chart).
- **Limitations**: the model ID byte for SysEx is an unverified placeholder (see [doc/MIDI_IMPLEMENTATION.md](doc/MIDI_IMPLEMENTATION.md)). The full bulk dump block (with checksum) is not implemented yet.

Full spec: [doc/MIDI_IMPLEMENTATION.md](doc/MIDI_IMPLEMENTATION.md).

## Persistence

A virtual EEPROM (wear-levelled flash append log) stores `yc_panel_state_t`:
wave, octave, 9 footages, percussion, vibrato/chorus, rotary speed, distortion,
reverb, volume and the MIDI control mode. Autosave triggers 2 s after the last
change, with a 250 ms polling interval. This instrument writes its own record
and reports `settingsSize() == 0` to the core.

Full spec: [doc/PERSISTENCE.md](doc/PERSISTENCE.md).

## Presets

**None.** The reface YC has neither program change nor a preset bank per the
official MIDI implementation chart. The complete panel state is directly
SysEx-addressable.

## Effects

A fixed chain (no slot system like PicoFaceDX):

1. **Percussion**: monophonic, single-trigger, 2nd/3rd harmonic. Attack/decay controlled by Length.
2. **Vibrato/chorus**: modulated delay line.
3. **Overdrive**: 1024-point LUT with a tanh curve.
4. **Rotary speaker**: algorithmic horn/drum model with crossover filter and physical speed ramps between OFF/STOP/SLOW/FAST.
5. **Reverb**: Schroeder type (4 comb + 2 allpass).
6. **Soft-clip limiter**: final safety stage.

## Design notes

- **DSP style**: header-only under `include/yc_engine/`, no dynamic allocation.
- **RAM_HOT()**: the macro in `effects/ram_hot.h` places audio hot-path data in RAM.
- **Soft-clip limiter**: a single `yc_soft_clip()` stage as the last DSP step inside `yc_engine_render_block` (§29); the I2S path only does a `*32767` cast plus hard clamp.
- **Lookup tables in flash, active wave mirrored to RAM**: the wavetable/sine/overdrive/footage-gain LUTs are `inline const float[]` in `include/yc_engine/yc_lut_data.h` (`.rodata`/XIP flash). The active wave table is copied into the 8 KB RAM buffer `yc_wavetable_ram` (`yc_wavetable_select`, §33) so the inner voice-render loop reads RAM, not flash - the RP2350 XIP cache is only 16 KB. Regenerate the flash tables with [`tools/yc_gen_luts/gen_luts.cpp`](../../tools/yc_gen_luts/gen_luts.cpp) (host compiler).
- **Hardware watchdog**: a 4 s hardware watchdog, fed from `render()`, reboots the chip if core0 stalls. A watchdog-reboot counter `WDR: N` on the System -> CPU Load screen tells the two failure modes apart: a rising WDR means core0 stalled and recovered itself.

## Acknowledgements

The tone generation concepts (tonewheel/drawbar model, percussion,
vibrato/chorus scanner, rotary speaker principle) were derived from the DSP
concepts of [setBfree](https://github.com/pantherb/setBfree) / BeatrixCPP and
reimplemented for the RP2350 header-only architecture. The setBfree tree itself
is not vendored here; see [tools/README.md](../../tools/README.md).

Code for the YC port was developed with an LLM-assisted workflow: architecture
and review by the maintainer, code generation via glm-5.2, matching the existing
project conventions.

## License

GPL-3.0-or-later, as for the repository as a whole - but note the open question
about this instrument in particular: the tree its tone generation goes back to
(OpenB3 / BeatrixCPP) is AGPL-3.0, which would be stricter. Whether that reaches
here depends on whether code was copied or the behaviour reimplemented from
reading. See [the licensing section of the root README](../../README.md#license).
