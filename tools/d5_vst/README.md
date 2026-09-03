# D5 against Roland's D-50 VST, automated

Renders the same factory patch through Roland's D-50 VST3 (via
[pedalboard](https://github.com/spotify/pedalboard), no DAW) and through the
D5 engine on the host, and compares them. The VST exposes the whole
448-byte D-50 patch as parameters in SysEx order, so both sides play
exactly the bytes the firmware plays.

Local only: needs the Roland Cloud D-50 installed, a configured D5 build
directory (the ROM set, `build-d5` by default) for the patch table and the
PCM blob, and a Python venv.

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/d5_vst/requirements.txt
tools/d5_vst/build_render.sh              # engine-side renderer
.venv/bin/python tools/d5_vst/compare.py 3 21 36     # a few patches
.venv/bin/python tools/d5_vst/compare.py --bank 1 --csv bank1.csv
.venv/bin/python tools/d5_vst/compare.py --all --dry --csv all_dry.csv
```

Per patch: level under the note (V = VST, O = ours), tail one second
after the key, spectral centroid, h2 and h5 re h1, L/R correlation, and
how many bytes had to be clamped to the VST's parameter range (0 for the
factory bank). `--dry` zeroes reverb and chorus balance on both sides,
`--keep DIR` keeps the stereo f32 renders (32 kHz) for closer analysis.

Traps found on the way: the VST renders silence at 32 kHz once a patch is
set (it renders at 44.1 kHz and the tool resamples); changing the sample
rate on one plugin instance hangs it; program change and SysEx do
nothing, the parameters are the only way in. `d5vst.py` is the mapper
(`set_patch(bytes448)`, `render(note, vel, hold, tail, sr)`).
