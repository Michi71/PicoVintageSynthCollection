# PicoFaceCM

Roland CM-32L: the LA synthesis module of the MT-32 family without a panel,
as it sat beside a PC around 1990. Eight parts and a rhythm part, 32 partials,
the MT-32's timbres plus the sound effects of the CM-32L.

The engine is the synthesis core of [munt](https://github.com/munt/munt)'s
mt32emu (LGPL 2.1 or later), in [mt32emu/](mt32emu/README.md) with a list of
everything that differs from upstream. It runs the integer renderer at the
LA32's own 32 kHz, digital output only, with the original control ROM and the
PCM ROM read in place from flash.

Both cores render. On one core, 32 partials took more than 150 % of the time
available, so core1 is the engine's second executor: the cores share the
generation of the partials, then core1 runs the reverb while core0 mixes the
rest. Everything the engine does while it plays - per sample, per run, per
MIDI event - is linked into RAM (27 KiB): flash is read through a 16 KiB
cache that the PCM samples go through as well.

## Building

Not buildable from the repository alone. Put the two ROM images into `roms/`
(gitignored) under any name; they are recognised by content:

| Image | Size | SHA-1 |
|---|---|---|
| CM-32L / LAPC-I control ROM v1.02 | 64 KiB | `a439fbb390da38cada95a7cbb1d6ca199cd66ef8` |
| CM-32L / CM-64 / LAPC-I PCM ROM | 1 MiB | `289cc298ad532b702461bfc738009d9ebe8025ea` |

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PicoFaceCM
```

Without the images the instrument is skipped with a note and every other
target builds as before. `tools/cm_extract/cm_make_blob.py` converts the set
at configure time into the build directory.

## MIDI

As the module expects it. After power-up the eight parts listen on channels
2 to 9 and the rhythm part on channel 10, which is where the music written
for it sends; the panel's MIDI page moves the parts to channels 1 to 8 for a
keyboard that sends on channel 1. Notes, program changes, controllers, pitch
bend and the module's SysEx all go to the engine unchanged. A SysEx message
can be up to 512 bytes long, which is the engine's own limit as well.

## Panel

The original has one knob. This panel adds nothing of its own to the module:
every value on it is a byte in the module's memory, read from there when it
is drawn and written there with a Roland data-set message, the way a
sequencer would. So what a MIDI file changes shows on the panel, and what is
set on the panel is what the file finds.

The select encoder walks eight pages; encoders A and B set the two values of
the page.

| Page | A | B |
|---|---|---|
| Part | the part: 1 to 8, R for rhythm | its program, 1 to 128; the timbre's name and the part's MIDI channel are shown |
| Mix *n* | level of the part, 0 to 100 | pan, L7 to R7 |
| Setup *n* | reverb for the part, on or off | key shift, -24 to +24 semitones |
| Master | master volume, 0 to 100 | reverb level, 0 to 7 |
| Reverb | mode: Room, Hall, Plate, Delay | time, 1 to 8 |
| MIDI | channels: 2-10 as on the original, or 1-8,10 | master tune in Hz, 427.5 to 452.6 |
| Display | what the module's display would show - the CM-32L has none, but its firmware keeps one, and games write to it | |
| Diag | holds up to 16 test notes on the selected part | |

Mix and Setup edit the part chosen on the Part page. On the rhythm part pan,
reverb and key have no meaning - they are set per key there - and show `-`.

**A long press on the select encoder** resets the module to its power-up
state and then applies the panel's settings again: the way out of whatever a
MIDI file left behind.

What is kept across a power cycle are the six settings of the module as a
whole - master volume, reverb mode, time and level, tune, channels - as they
were last set on the panel. The parts start with their factory timbres, as on
the original; what MIDI sets is never stored.

### The Diag page

As time against the time a block lasts:
`CPU` - the whole render on core0, MIDI events included: average, and the
worst block of the last three seconds;
`gen` - until both cores have generated the partials;
`mix` - until mix and reverb are done on both.
The third row takes the peak block apart: `g` and `m` are its `gen` and
`mix`, `e` the MIDI events it had to play, `r` the runs it was rendered in
(the engine starts a new run at every event). What the peak has beyond
`g` + `m` went into events and short runs.
The last row: CPU time to generate one sample of one partial in nanoseconds,
for synth partials (`s`) and PCM partials (`p`); sounding partials (`P`); I2S
underruns (`U`).

## Tests

`tools/host_tests/cm/build_cm_ui.sh` renders the panel to frames without a
ROM set: the real controller on a stand-in for the module. It is part of
`run_all.sh`. `tools/host_tests/cm/build_cm_panel.sh` runs the panel against
the real engine and needs the ROM set: it turns every value, reads the
module's memory back and, where a value can be heard - level, pan, the reverb
switch, the channel layout - measures the rendered audio.

## Figures

Image: 1.3 MB of flash, of which 1.06 MB are the two ROMs. Static RAM about
150 KB (the engine keeps a 64 KiB copy of the control ROM, and 27 KiB of code
and an 8 KiB table are in RAM); the engine takes roughly another 134 KB from the heap when it
opens, measured on the host.

Measured on the hardware (444 MHz system clock), in the order things were
found:

| Build | 16 test notes held (16 partials) | MIDI file, 32 partials |
|---|---|---|
| one core, engine in flash | above 150 % | - |
| two cores, per-sample code in RAM | 35 % | peaks above 100 % |
| MIDI through the engine's queue, a poly's abort in one run | - | peaks of 105 % |
| note and event path in RAM, short runs on both cores | - | 70 %, peak 89 % |
| per-sample path as one translation unit | 31 %, peak 37 % | 60 %, peak 79 % |
| wave generator: exponent table, cutoff and silent waves skipped | 28 %, peak 32 % | 50 %, peak 61 % |

The underrun count stood still through all the MIDI playing since the queue
went in.

The peaks above 100 % were not load. A note that takes partials from a
sounding one has to wait until that has faded out; the adapter bypassed the
engine's MIDI queue, which loses such a note and leaves the renderer going
sample by sample, and upstream renders the fade-out sample by sample in any
case. Both are changed, see the engine's README.

What a partial costs, as CPU time per sample: before the work on the wave
generator a synth partial took 1230 ns in a MIDI file (1000 ns held) and a
PCM partial 730 ns; after it 900 ns (800 ns held) and 560 ns. A PCM partial
costs about 60 % of a synth partial on a desktop machine as well, so reading
the PCM data from flash is not what costs: the time goes into the arithmetic
of the LA32 model, now some 300 instructions per sample for a synth partial
(see the engine's README for how that is counted).

## Not there yet

Editing timbres on the panel; patch and timbre memory across a power cycle; a
limit on the partials for when the load is too high (the worst block measured
so far took 61 % of its time).
