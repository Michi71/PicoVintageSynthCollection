# PicoFaceM1 engine notes

How the engine maps the M1, where each number comes from, and what is assumed.
The source analysis was done outside this repository (OS disassembly of the
Plus+1 and Rev 19 OS, the PCM and coefficient ROMs, the owner's manual and the
service manual); this file keeps what the code depends on. Addresses
`EF83:xxxx` are the voice driver and `E066:xxxx` the main program, in the
segment numbering of the Plus+1 OS that the analysis used; the Rev 19 code is
equivalent with shifted offsets. `DS:xxxx` addresses are Rev 19 unless noted.

Tags: **firmware** = read from the OS code or tables; **assumed** = an
inference without a measurement.

## ROM data

| What | Where (Rev 19) | Format |
|---|---|---|
| PCM | IC36–43 | 2 M words, 16-bit linear; IC36–39 low bytes, IC40–43 high bytes |
| multisound table | DS:6978, 100 × 14 B | split-list pointer, kind (2 = DWGS), split count, name |
| split record | 10 B | sample record, top key, pan, cents, root (bit 7 NT), level trim, cutoff trim, decay trim, pad |
| sample record | 14 B | tune (1/128 st), start, loop start, flags (bit 2 loop), end; absolute 20-bit addresses, bank in bits 20–23 of the end |
| DWGS | multisounds 77–99 | one split list; wave offset `(ms − 77) × 0x3FC` words added to every address |
| drum sounds | DS:5B9E, 44 × 14 B | sample records as above |
| interpolation kernel | IC30, first 32 KB | 4096 phases × 8 taps (lanes 3/4 unsigned, weights 1,1,2,8,8,2,1,1 /1226) |
| VDF coefficients | IC30 0x8000 (VDF 1) / 0xC000 (VDF 2), second table | 4096 × Q13 one-pole `k` |
| pitch | DS:0616 (153 × u16), DS:0748 (128 × u16) | semitone steps and fraction multipliers of the 4.12 step |
| EG rates | CS:1133 / CS:1333 (Plus+1 numbering), 256 × u16 | rise: phase increment; fall: exponential coefficient |
| velocity curve | CS:1BC3, 256 × u16 | VDA gain; index 252 = unity |
| MG | DS:738E freq, CS:106B delay, CS:0EDB / CS:0FA3 depth | 100 × u16 each |
| OSC 2 delay start | DS:7230 | 100 × u16, 1.3 ms ticks |
| pan register | DS:0854 | 14 words: A in bits 0–5, B in bits 6–11, C bit 12, D bit 13 |
| balance curve | DS:8272 | 101 × Q9, DRY:EFF balance and Out-3/4 panpots |
| shelf corners / gains | DS:8966 / DS:8970 | Q13 `k` for 250/500 Hz and 1/2/4 kHz, Q15 gains 0…−12 dB |
| effect coefficients | see "Effects" | read by `tools/m1_extract/m1_fx_tables.py` |
| programs, combinations, kits | the ALL DATA dump | 143 / 124 / 210 bytes, Korg 7-bit packing |

The converter checks each located table against a known property (the kernel's
constant tap sum, the first shelf corners, the unity entry of the velocity
curve, ...) and refuses an image where one does not hold.

## Voice

Rate 31 250 Hz. Every voice is updated every 650 samples (20.8 ms: the OS
visits one of 16 channels per 1.3 ms tick); between updates the cutoff and
amplitude ramp linearly to the new target (**assumed**: the chips ramp in
hardware at a rate the CPU sets; the exact shape is not known). Pitch changes
in steps, as on the M1.

**Oscillator** (firmware): phase 20.12 from the sample start, step 4.12,
clamped to 0x2FFF. Output = Σ kernel(phase fraction) × 8 samples around the
read point. A loop is `[loop, end)`: the ROM data repeats with period
`end − loop` right up to the end, and the word *at* `end` does not continue
the loop (measured over all looped splits: 0.04 against 0.3 of the loop's
RMS). So taps from `end` on read from the loop start. Taps before the loop
start read the ROM linearly, where Korg's data mostly continues the loop
anyway (**assumed** for the hardware). The analysis' Python reference wrapped
one word too late; on the shortest loops (PanWave: 15 words) that put a
wrong sample into every period, about 15 dB below the signal. Reference mode
keeps the old behaviour for the comparison. A one-shot ends when the phase passes
the end; the filters then ring out until the next update.

**Pitch** (firmware, EF83:178B):
`d = (n_e << 7) + OSC2 detune + MG + bend/aftertouch/detune/tune + pitch EG + 3 − root`
in 1/128 semitone, then `step = S[hi] + S[hi]·F[lo] >> 16`. The `+3` is in the
code (+2.3 cents). `n_e` = key + 12·octave + combi transpose (+ interval on
OSC 2), folded into 0–127; it selects the zone and sets the pitch. Root =
`rootKey·128 − cents·1.285 + sample tune` (NT: root key = played key). Scale
types other than equal temperament are not implemented.

**Cutoff** (firmware, EF83:1846/1F58): register = program cutoff × 82.75 +
`max(0, n_e·0x47 − 0x900)` (a built-in tracking every program has) +
keyboard tracking × (key − centre) + split trim × 0x52 + MG + joystick/
aftertouch + EG level × intensity, clamped to 0…0x1FFF; VDF 1 additionally
to 0x1FD2. Both VDF chips get the same value.

**VDF topology** (**assumed**): two one-pole low-passes in series, stage 1
with the IC26 coefficient (0.707 k), stage 2 with the IC20 coefficient (k).
The chips also hold a second table `2k − 1` whose use is unknown; no
resonance.

**Amplitude** (firmware, EF83:1EE9): osc level × split level trim, plus
keyboard tracking and aftertouch, times the velocity curve, the combi timbre
level and the VDA EG, all multiplied; full scale 0x1FFF (**assumed**; the
M1 sits about 0.5 dB below it).

**Envelopes** (firmware, EF83:1642 ff.): levels are parameter × 330. Rising
segments move linearly (`phase += RISE[idx]` until 0xFFDC). Falling VDF and
VDA segments are **exponential** (`phase −= RISE·phase >> 16`), to −48 dB of
the segment, or −60 dB when the VDA falls to zero. The pitch EG is linear in
both directions with the rise table. `idx = t·330·F >> 22`, where `F` is the
time factor from the EG time KBD/VEL parameters and their switch/polarity
bytes 36–39 (bits 0–3 switch attack/decay/slope/release, bits 4–7 flip the
sign). A drum's Decay scales the VDA decay segment. This corrects the
analysis' first model, which had all segments linear; the Python reference
still has that, and the engine reproduces it in its reference mode only.

**MGs** (firmware, EF83:1533): 16-bit phase, step `DS:738E[freq]` per update
(≈ 1.64·v² → 11.8 Hz at 99); triangle, saw up, saw down, and a *unipolar*
square. Delay = an accumulator that holds the depth at 0 to half way and then
fades it in. Pitch depth 99 = ±12 semitones; VDF depth 99 ≈ ±97 cutoff steps.
Without key sync a new voice takes over the timbre's running phase.

**Controllers** (firmware, E066:8998): joystick X = bend, range = the
program's JS pitch bend in semitones; JS VDF sweep also follows joystick X.
+Y (CC 1) adds pitch-MG depth and speed, −Y (CC 2) VDF-MG depth and speed;
aftertouch adds pitch, pitch-MG, cutoff, VDF-MG and amplitude.

**Allocation** (firmware, EF83:098E ff.): one queue of 16 channels kept as
`[free | releasing, oldest first | held, oldest first]`; a note takes the
head; a released or damper-held channel moves in front of the held ones; a
finished one to the head. Double programs take two channels. Mono is a
retrigger with last-note priority. Hold skips the release. A delayed OSC 2
whose key is released before its delay never starts.

**Drum kits** (firmware, E066:2E48): program byte 12 picks the global kit;
an instrument plays on its key and on the free keys below it, transposed.
Tune is in 10-cent steps; Level and Decay are trims of 330 per step on 1.0.
Drum programs always use the instruments' own pans.

## Effects

The MDE (MB87405) is a programmable DSP: the OS uploads 128 instruction words
per program, compiled from 22 templates by an effect compiler (segment FX =
F2C3 in Rev 19). The instruction semantics are not decoded far enough to
emulate the chip, so each of the 33 types is an algorithm with the structure
the analysis worked out, and the numbers come out of the OS:

- **From the OS, exact** (`tools/m1_extract/m1_fx_tables.py`): comb feedback
  and damping of all reverbs (computed per comb by FX:09D5/0988 from small DS
  tables, not stored as tables), delay damping (FX:05EE), pre-delay bases and
  ER scales (DS:8576, DS:85A2), LFO increments (FX:0671), rotary speeds and
  ramp intervals, tap and microphone positions, shelf corners and gains
  (DS:8966/8970), balance curve (DS:8272), tremolo and exciter constants,
  ER tap times.
- **From the OS templates, to the coefficient's 9-bit resolution**: allpass
  and comb gains and delay lengths of all reverbs, ER tap gains, rotary
  filter and microphone gains, Delay/Hall and Delay/Room.
- **Assumed** (the OS does not hold them; the converter fills them in and
  says so, `fill_assumed` in `m1_make_blob.py`):

  | Value | Assumption | Affects | Against the analysis' reference |
  |---|---|---|---|
  | LFO sine | raised cosine `sin²(π(i+½)/128)` over 128 steps, Q9; the triangle is the firmware's exact shape | chorus, flanger, phaser, tremolo, rotary, combinations | 12 of 128 points off by 1/512; inaudible (r 1.000) |
  | second LFO wave | raised cosine at twice the rate | flanger shape, tremolo shape, symphonic | up to 0.09 off, peaks 2–3 steps early; flanger r 0.93, tremolo r 1.00, symphonic up to 4.6 dB in single bands |
  | rotary drum low-pass pole | unity DC gain of the two firmware coefficients | Rotary SP | 0.910 against 0.908, +0.15 dB |
  | rotary depth | word 18 (v/100) as AM depth, word 27 (v·0.002) × 256 samples as Doppler swing | Rotary SP | Doppler about 6 % (horn) and 24 % (drum) wider; ≤ 1.1 dB in single bands |
  | symphonic tap offset, second-LFO mix, voice phases | 300 samples + the template's spreads, 0.1, phase words A000/C000/E000h as table steps | Symphonic Ens | offset 12 samples short, mix 0.1 against 0.084, phases spaced 45° instead of 90°; up to 3 dB in single bands |
  | Delay/Hall, Delay/Room pre-filter | the template's unpatched words decoded with the reverb-EQ layout: two flat shelves | Delay/Hall, Delay/Room | the reference has a gentle bass cut here; +0.65 dB, ≤ 0.9 dB in single bands |

The last column comes from running the analysis' Python effects once with
its own values and once with these (outside this repository). The Symphonic
Ensemble is the one audible gap; nothing in the OS fixes it, so it needs
either the MDE's LFO semantics or a recording of an M1.

The factory preload uses Symphonic Ens 7 times, Rotary SP once and
Delay/Hall 4 times; the LFO shape matters to every Chorus (115 uses).

Where the OS and the analysis' first reconstruction disagree, the engine
follows the OS: Delay/Room is a mono Large Room in the firmware (combs
1625/884/781/1401); Delay/E.Ref chains three allpasses; the ER allpass slopes
are 4.297/1.641; the rotary microphone gains are ±0.648/±0.799; Ensemble Hall
and Concert Hall each have one allpass gain of a different sign or size; the
chorus speed is `v + 1` only up to 70.

Section (firmware F298:003A, F298:017F; owner's manual pp. 36–38): effect 1
reads buses A/B. Parallel: effect 2 reads C/D, its output is added to outputs
1/2 through the Out-3/4 panpots. Serial: C/D are added to effect 2's input
through the panpots, effect 2 follows effect 1. Each effect half has its
DRY:EFF balance from the S-curve; a switched-off effect is balance 0. For
delay, chorus, flanger, tremolo and exciter the EQ stays in the dry path, as
the manual says; No Effect switches everything off.

Delay RAM: 2 × 32 K int16 words, effect 1 in one half, effect 2 in the other,
one ring per half with a pointer running down once per sample. Every line is
an offset range, as in the MDE; the host test `fxcheck` checks that every
type fits at its longest settings.

Out 3/4: the PicoFace has one stereo output, which is M1 outputs 1/2. In
parallel placement, effect 2 is heard only through the Out-3/4 panpots, as on
an M1 with nothing plugged into outputs 3/4.

## Checks (host)

- `compare_reference.py`: the oscillator, kernel, VDF, VDA and EG core in
  reference mode against the analysis' Python engine, all 26 single factory
  programs: worst peak difference 2.6·10⁻³ of the peak, correlation 1.000000.
  The reference has linear EG segments; the engine's firmware mode does not,
  so this checks the port of the signal path, not the envelope shape.
- `compare_effects.py`: all distinct effect blocks of the factory programs and
  combinations over a noise burst, engine against the analysis' Python
  effects. Level within ±0.3 dB except Rotary SP (+2.1 dB: the firmware's
  microphone gains) and Flanger into a delay (+1 dB). Correlation 1.000 where
  both use the same numbers (delays, Over Drive, Distortion, Exciter);
  0.8–0.99 for the reverbs, whose gains and delays differ by a LSB or a
  sample; low for Symphonic Ens, whose values are assumed.
- `m1_render fxcheck`: every type in both slots at its extreme settings,
  serial and parallel, driven by noise: the delay lines fit their 32 K words
  and the output stays bounded.
- All 100 programs and 100 combinations render without silence (except
  "Wait......", whose attack time is 99) or overflow.

## Known gaps

- Hardware ramp shape of the VDF/VDA chips, VDF topology, interpolator taps
  at the loop edge: assumed, see above.
- The 1.3 ms tick rests on a 2 MHz timer clock inferred from the MIDI baud
  divisor.
- No comparison against a real M1 has been made. The Python reference and
  this engine share the same analysis, so agreement between them checks the
  port, not the analysis.
