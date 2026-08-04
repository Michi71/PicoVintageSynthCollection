# macOS host test for the PicoFaceCP engine

Runs the **unmodified Pico mdaEPiano engine**
(`instruments/PicoFaceCP/src/mdaEPiano.cpp`) on the Mac through **CoreAudio**,
played over **MIDI** from a virtual PortMidi input. It lets the engine and new
sample sets be auditioned before anything is flashed.

There are two builds here:

| Script | Binary | What it runs |
|---|---|---|
| `build.sh` | `mdaepiano_test` | the bare engine |
| `build_cp.sh` | `cp_test` | the engine **plus** the reface CP insert-effect chain from `instruments/PicoFaceCP/effects/` |

## Build and run

```bash
brew install portmidi          # CoreAudio comes with the system
./build_cp.sh && ./cp_test
```

Point a DAW or MIDI tool at the virtual port named `mdaepiano`. Note on/off,
control change (sustain, volume, mod wheel, ...) and program change are
received.

## Keyboard

| Key | Action |
|-----|--------|
| `+` / `-` | program (preset) up / down |
| `1`…`5` | select a program directly |
| `i` | next instrument (mda default, Clavinet, Piano, rhodes_suitcase, wurlitzer_200a, yamaha_cp80) |
| `t` | tremolo/wah: off -> tremolo -> wah (`cp_test` only) |
| `c` | chorus/phaser: off -> chorus -> phaser (`cp_test` only) |
| `d` | delay: off -> digital -> analog (`cp_test` only) |
| `r` | reverb on/off (`cp_test` only) |
| `s` | sustain pedal (CC64) |
| `m` | mod wheel (CC1) to 127 |
| `q` | quit |

## How it works

- The test creates the engine, calls `setSampleRate(44100)` and `setProgram(0)`,
  and turns incoming MIDI bytes into `noteOn` / `noteOff` /
  `processMidiController` / `setProgram`.
- Per `process()` call the engine delivers `I2S_BUFFER_WORDS` (16) frames as
  separate `int16` left/right blocks. The CoreAudio render callback keeps a ring
  buffer so that arbitrarily large audio blocks can be filled without losing
  samples.
- All synth instances are created at startup and stay alive; switching
  instruments only moves a `std::atomic` pointer (audio thread) and an index
  (main thread), which keeps the switch click-free and free of use-after-free.

## Engine changes for the host build (only active with `-DMDA_HOST_BUILD`)

Two guarded minimal changes make the Pico engine compile and run on the Mac; the
firmware build is untouched because it does not define the macro.

1. `include/mdaEPiano.h`: the Pico `#include "audio_subsystem.h"` is skipped in
   the host build and the needed macros (`SAMPLES_PER_BUFFER` = 16,
   `PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL` = 3) are defined locally.
2. `include/mdaEPianoData.h`: `epianoData` becomes writable on the host
   (`int16_t` instead of `const int16_t`), because the constructor writes the
   crossfaded loop into the waveforms. On the Pico that data lives in RAM;
   `const` data on the Mac would be read-only and the write would bus-error.

The engine was generalized slightly for multiple sample sets - these changes are
active in the firmware build too but behave identically for mda:

- `kgrp[34]` -> `kgrp[64]`, plus the members `nKeygroups` / `noteonStep`
  (mda: 33 / 3).
- new method `loadInstrument(data, kgrpTable, nKeygroups, noteonStep)`: swaps
  waveforms and keygroup table and stops the running voices.
- `noteOn`: `k += 3` became `k += noteonStep`, bounded by `nKeygroups`. For mda
  (33/3) the keygroup search stays exactly as it was.
- The crossfade in the constructor still runs, on the mda data only; the other
  instruments do not need it, their loop points are click-free already.
