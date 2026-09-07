# J6 against Roland's JUNO-60 VST, automated

Renders the same front panel through Roland's JUNO-60 VST3 (via
[pedalboard](https://github.com/spotify/pedalboard), no DAW) and through the
J6 engine on the host, and compares them. Both sides run at 44.1 kHz, the
engine's own rate, so neither is resampled.

Local only: needs the Roland Cloud JUNO-60 installed and a Python venv.

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/j6_vst/requirements.txt
tools/j6_vst/build_render.sh                       # engine-side renderer
.venv/bin/python tools/j6_vst/compare.py 0 3 24     # a few patches
.venv/bin/python tools/j6_vst/compare.py --all --dry --csv all_dry.csv
```

Per patch: level under the note (V = VST, O = ours), the tail after the key,
spectral centroid, h2 and h5 re h1, stereo correlation, and how many
parameters had to be clamped to the plugin's range. `--dry` switches the
chorus off on both sides, `--keep DIR` keeps the stereo f32 renders.

Unlike the D-50, whose plugin takes the factory patch bytes directly, the
Juno's two sides share a *panel* rather than a patch: `render_note dumpbank`
writes the 56 factory patches as 29 normalised parameters each and
`j6vst.set_patch` puts the same numbers on the plugin.

## What the plugin's parameters mean

Nothing about them is documented, and four of them do the opposite of what
their names suggest. All of the following was measured against the running
plugin; the map in `j6vst.py` carries each one at its line.

* **The modulation depths are bipolar with 128 as the centre** — DCO LFO,
  VCF LFO, VCF contour and VCF key follow all do nothing at 128 and reach
  full depth at 0 and at 255. Writing 0 for "no modulation" detunes the
  oscillator by nearly four semitones, which is how this was found.
* **`env1` is the filter contour and `env2` the amplifier contour.** A
  Juno-60 has one contour generator for both, so both are written from the
  same four parameters.
* **`vca_mode` has three positions and none of them is the obvious one**: 0
  hangs the amplifier on env1, 1 on env2, and only 2 is the plain gate.
  Reading 0 as the gate makes every patch whose contour sustains at zero —
  the three organs among them — die away instead of holding.
* **`dco_range` is six octaves**, not the instrument's three, with 3 = 8'.
* `effect_type` 2, 3 and 4 modulate at roughly 0.9, 1.7 and 9.2 Hz and the
  third is all but mono: Roland's Chorus I, II and I+II. Off is depth 0.
* `dco_pwm_source` 1 takes the width from the LFO, 2 from the contour, 0
  holds it still. 3..5 repeat 2 and 0.
* The plugin renders **silence at 32 kHz** once a patch is set, exactly like
  the D-50 one. 44.1 kHz is the engine's own rate anyway.

## Roland's own patches

`/Library/Application Support/Roland Cloud/JUNO-60/? Preset.bin` holds three
banks of 64. The records are 20223 bytes apart, each begins with its name as
printable ASCII, and the parameter block starts 14 bytes after the name with
**one nibble per half-byte** in the plugin's own parameter order — so
`(hi << 4) | lo` per parameter reproduces the plugin's defaults exactly.

That settles a question the comparison rests on: **Roland's presets use the
whole 0..255 of `vcf_cutoff_freq`** (median 105, 98 distinct values), and the
same for resonance, the oscillator levels and the high-pass. The plugin's
byte range really is the panel's travel, so mapping a 0..1 slider straight
onto 0..255 is right rather than a guess.
