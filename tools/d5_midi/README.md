# tools/d5_midi

A five-minute MIDI file that plays itself: it selects patches over CC 0 and
program change and gives each one a phrase that suits it. Not a random walk
through 384 presets -- 28 of them, picked so that between them they show what
is peculiar about this machine.

| File | What it is |
|---|---|
| `PicoFaceD5-Demo.mid` | the file. Format 0, one track, channel 1, 120 BPM |
| `d5_demo_midi.py` | the generator -- edit the setlist at the bottom and re-run |
| `bank.py` | reads the generated patch table, so the generator knows what it is selecting |

```bash
python3 d5_demo_midi.py PicoFaceD5-Demo.mid
```

`bank.py` finds `d5_patch_data.h` in the newest `build*/` directory, so it
needs a build that had the bank dumps present; `D5_PATCH_DATA=<path>` overrides
that.

## What is in it, and why

Each section is a marker in the file, so a DAW shows the patch name and the
reason it is there. In order:

- **the machine's own voice** -- Fantasia (PCM attack over a synthesized
  sustain), String Ensemble (the stereo width), Legato Strings (a 248 ms tape
  delay where the panel says "reverb", and an LFO that keys on every attack),
  Cathedral Organ (chapel reverb, pressure where an organ has no lever)
- **solo voices** -- Pipe Solo (pulse width 0 is an honest square, so the flute
  rank stands in front), Shakuhachi, Living Calliope (reverb 23 is a cross
  delay, not a room)
- **the sawtooth and the wheel** -- Rich Brass on the octave the chip gives a
  saw, Staccato Heaven with the wheel over a full octave, Soundtrack with
  pulse width 82 on all four partials, Spacious Sweep in whole mode with all
  sixteen slots on one tone
- **aftertouch** -- PressureMe Strings (on the filter), Pressure Me Lead (as a
  second lever)
- **the PCM side** -- Pizzagogo, Digital Native Dance on the combination waves,
  Gamelan Bell and Aqua Bells on ring modulation, Xarmonica, and the hold pedal
  on an electric piano out of bank 4
- **split keyboards and the low end** -- Basin Strat Blues and Slap Bass n
  Brass (their split points are read out of the patch, not typed in),
  Synthectric Bass in solo mode, and Mono Octabass with **portamento switched
  on from outside**, because no factory patch switches it on
- **the rest of the library** -- Hammer Feel, a bank 3 lead, an effects patch
  with aftertouch bend, and Reso Release, whose filter jumps open when the key
  comes up

## Verification

The file is checked by playing it through the engine on the host and reporting
peak, RMS and hanging notes per section -- a section that renders silent, clips,
or leaves a note hanging is a fault in the file, and the ear is a slow way to
find it. As it stands: peak 0.479, no clipped samples, no note left on, no
section silent, and every program change lands on the patch its marker names.

No rendered audio is committed here. A preview is a `ffmpeg -f f32le -ar 32000
-ac 2` away from the harness output if one is wanted.

The loudness spread across the 28 sections is about 20 dB, and that is the
bank's own, not the file's: the 384 patches span -55 to -10 dB on the same
four-note chord, median -28.4. Two of the machine's own quietest --
Glockenspiel (-54.9, the quietest of all 384) and Intruder FX (-53.2) -- were
dropped from the setlist for that reason, because a section nobody can hear
demonstrates nothing; the traits they carry are shown by louder patches
instead.
