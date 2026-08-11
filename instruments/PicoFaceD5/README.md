# PicoFaceD5 - Roland D-50

A native LA engine over the D-50's own PCM data: sampled attacks dovetailed
with synthesized sustains, the seven structures with their ring modulator, the
three LFOs and the pitch envelope, and the common block's equalizer, chorus
and reverb.

Like PicoFaceJV, this instrument **needs a local ROM set** and is therefore not
in the release binaries. Without one the configure step skips it with a note
and everybody else's build stays green.

## What it needs

Put the D-50's ROM images in `roms/` (gitignored). Files are identified by
content, so their names do not matter:

| Image | Size | Purpose |
|---|---|---|
| PCM ROM A (IC30) | 256 KB | lower half of the sample data |
| PCM ROM B (IC29) | 256 KB | upper half |
| program EPROM (IC22) | 64 KB | the PCM names |

512 KB dumps that contain a 256 KB chip twice are folded automatically, and a
combined 512 KB image of both chips is accepted in place of the pair. The
build converts them once at configure time into a 512 KB blob of 16-bit
samples, so the firmware needs no decoding table at runtime.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceD5
cmake --build build
```

The result is 626 KB of flash - 512 KB of it sample data - and 79 KB of RAM,
which fits any RP2350 board including the 4 MB ones. Of the collection's four
sample-based instruments this is the only one that does.

## Playing it

| Encoder | Patch page | Mix page | Tune page |
|---|---|---|---|
| Select | switches pages | | |
| A | patch | volume | master tune |
| B | polyphony cap | reverb | MIDI channel |

The footer shows peak load, the I2S underrun counter and active/allowed
voices, as everywhere in the collection. MIDI arrives over USB and DIN alike;
channel volume, reverb send, chorus send, all-notes-off and pitch bend are
recognised.

## The patches

The eight patches it boots with are **not** the D-50's factory sixty-four.
They are built by hand from the engine's parameters and chosen to cover the
ground: every structure appears, both waveforms, ring modulation, the pitch
envelope and each effect. The factory patches exist as SysEx bulk dumps and
the format for reading them is fully documented in the machine's own MIDI
implementation, so importing them is a host-side tool waiting to be written,
in the same shape as `tools/dx_syx_to_patches`.

## How the engine works, and what is not the original

The sample table - which PCM sound starts where, how long it is and whether it
loops - is not in any ROM. The D-50 resolves it inside the MB87136, and that
mask ROM cannot be read out. The table this instrument uses was reconstructed
from the decoded audio and verified by ear over ten review rounds;
[`tools/d5_extract/README.md`](../../tools/d5_extract/README.md) documents both
the evidence and the two open points.

Two more places where this deliberately differs from the machine:

- **Root pitch per sample.** The real table carries one; ours is measured from
  the material, so individual samples can be an octave off until someone
  calibrates them against a recording.
- **The reverb.** The D-50 has a dedicated reverb chip whose 32 types are 188
  coefficients each. This is an ordinary Schroeder reverb with the 32 slots
  mapped onto room, plate, gate and long settings - reverb of the right
  character and length, not the original's impulse response.

Everything else follows the machine's own documentation: the structure table
and the parameter ranges come from the Advanced Course manual and the service
notes, which are named here rather than shipped.

## Host tools

[`tools/d5_extract/`](../../tools/d5_extract/README.md) holds the ROM
identification, the decoder, the sample table and its derivation, the blob
generator, and a host harness that renders the engine to WAV -
`--synth`, `--la`, `--structures`, `--mod` and `--fx` - so the sound can be
judged without flashing anything.
