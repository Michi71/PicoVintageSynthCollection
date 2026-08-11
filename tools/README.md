# Host tools

Nothing in here is part of a firmware image. These are the host-side tools that
produced data now baked into the instruments, plus the regression harness for
PicoFaceRD. They are kept because the generated data is only auditable if the
generator is around.

Unless stated otherwise, run them from the repository root.

| Tool | Instrument | What it does |
|---|---|---|
| [`migrate.sh`](migrate.sh) | - | one-time population of this monorepo from the original single-instrument repositories. Read the header before running it: PicoFaceDX is deliberately not in its list. |
| [`yc_gen_luts/gen_luts.cpp`](yc_gen_luts/gen_luts.cpp) | YC | generates the drawbar wavetables and the sine LUT for `yc_engine`. Prints C++ to stdout, which is pasted into `yc_lut_data.h` / `yc_sine_lut.h`. |
| [`dx_syx_to_patches/syx_to_patches.cpp`](dx_syx_to_patches/syx_to_patches.cpp) | DX | converts the 32 reface DX factory-bank `.syx` bulk dumps into the static table in `instruments/PicoFaceDX/src/presets.cpp`. Needs the `.syx` files, which are not part of this repository. |
| [`rd_extract/`](rd_extract/README.md) | RD | the extraction and verification toolchain of the RD port: register capture from the reference emulator, sample-ROM dumps, `.rdp` pack building, A/B comparison and the stuck-voice stress test. `run_regression.sh` runs the whole matrix and prints one PASS/FAIL. |
| [`rd_midi/`](rd_midi/) | RD | test MIDI files plus `midi_keyboard_only.py`, which strips a Standard MIDI File down to pure keyboard performance (notes, pedal, bend) so a capture is not polluted by controller traffic. |
| [`jv_extract/`](jv_extract/README.md) | JV (planned) | JV-880 ROM extraction and parameter analysis: wave-ROM descrambling, DPCM decoding, the multisample/sample/patch tables, and a differential probe harness that identifies patch parameters by measuring the reference emulator. Needs ROMs and an emulator checkout, neither of which is in this repository. |
| [`d5_extract/`](d5_extract/README.md) | D5 | D-50 ROM identification and PCM decoding, the reconstructed sample table, the firmware blob generator, the SysEx patch-bank converter, and a host harness that renders the engine to WAV. Needs a ROM set, which is not in this repository. |
| [`host_tests/jv_engine_test/`](host_tests/jv_engine_test/) | JV (planned) | host harness for the PicoFaceJV engine: loads a ROM set, renders a patch to WAV, and can be driven with the same tone-byte modifications as `jv_extract/jv_probe` for A/B against the reference emulator. |
| [`host_tests/`](host_tests/README.md) | YC, CP, J6, MD, SM, core | host builds of the engines - CoreAudio plus PortMidi for the four that make sound, plus the veeprom unit test and the J6 panel harness, which need nothing at all. |

## Build

The C++ tools are single-file, C++17, no dependencies beyond the instrument's
own headers:

```bash
c++ -O2 -std=c++17 -o gen_luts tools/yc_gen_luts/gen_luts.cpp
c++ -O2 -std=c++17 -Iinstruments/PicoFaceDX/include -o syx_to_patches tools/dx_syx_to_patches/syx_to_patches.cpp
```

The RD tools link against several engine translation units; the command lines
are in [`rd_extract/README.md`](rd_extract/README.md), and
`rd_extract/run_regression.sh` builds what it needs itself.

## What is not here

The original repositories also carried complete third-party projects under
`tools/`: OpenB3/Beatrix (AGPL-3.0, JUCE), whose tone generation concepts the YC
drawbar engine follows, and the reface DX reference implementation the FM engine
was ported from. They are not vendored here. For OpenB3 that also keeps a
stricter licence out of a GPL-3 repository - see the licensing section of the
root README, which sets out why the AGPL does not reach PicoFaceYC. Each
instrument's README names where its engine came from and links the upstream
project.
