# ob_fxp_to_presets

Converts the OB-Xf factory patches (`.fxp`) into PicoFaceOB's
`include/ob_presets.h`. This is the tool that generated the shipped preset
table; rerun it after changing `ob_params.h` (the `PARAM_ORDER` list in the
script mirrors the enum and must be kept in step) or to pick up new upstream
patches.

```bash
git clone --depth 1 https://github.com/surge-synthesizer/OB-Xf /tmp/obxf
tools/ob_fxp_to_presets/ob_fxp_to_presets.py \
    "/tmp/obxf/assets/installer/Surge Synth Team/OB-Xf/Patches" -v
```

The default output path is the instrument's `ob_presets.h`; `-o` overrides it,
`-v` lists every skipped patch with its reason.

## What it does

- Parses the VST2 FPCh chunk, reads the XML attribute blob, and maps every
  parameter with the exact SynthEngine control laws (the depth curves are
  inverted where the port keeps an effective value instead of a route/amount
  pair).
- Folds what has no parameter of its own: Transpose into both oscillator
  semitone offsets, the three invert switches into the bipolar signs of
  Env Amt / Env Pitch / Env PW, tempo-synced LFO rates into Hz at 120 BPM.
- Merges a patch built on LFO 2 onto the port's single global LFO when LFO 1
  is inaudible.
- **Auto filter**: skips patches that lean on what the port cannot express -
  two audibly active LFOs (effective depth thresholds, see the script header),
  off-centre master tune, a Transpose that leaves the 0..48 semitone range.

Known approximations, deliberately tolerated: LFO waveform snapped to the
five panel positions, negative LFO route signs dropped, per-voice LFO2 phase
becomes global, envelope attack curve and LFO pulse width ignored, unison
thickness lost (the port has no unison), per-voice pan lost (mono port).
