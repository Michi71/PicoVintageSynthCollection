# Hardware

The module this firmware has been running on, turned into something
reproducible. Nothing here has been fabricated yet: these are the dimensions and
decisions the first board should be drawn to, worked out against the parts
actually in hand and written down before the layout rather than after.

The interface *between* several modules — power, MIDI, audio — is a separate
document: [The module bus](../docs/MODULE_BUS.md).

## Format

3U Eurorack, 14 HP. The panel is 70.8 x 128.5 mm and 1.6 mm thick, which is
mechanical rather than cosmetic: the EC11 encoders have a 5 mm threaded bushing,
and 1.6 mm of panel leaves 3.4 mm for the nut. A 2 mm panel leaves 3.0 mm, which
works but has no margin.

12 HP was the original plan and was given up when the display grew. A 1.3 inch
SH1106 module is 35.4 mm wide, so at 12 HP nothing fits beside it and the
encoders need two rows, which in turn leaves about 3 mm between rows. At 14 HP
the display gets its own row, the three encoders sit in one line, and 18 mm of
height is left over.

## The governing dimension

**The board sits 6.5 mm behind the panel.** That is the EC11's body height and
nothing else negotiates with it — every other panel-facing part has to reach the
same plane:

- the **display** module is 2.8 mm thick, so it rides on 3.5 mm standoffs
- the **jacks** could not reach it at all, which is why they are panel-mounted
  and wired (see below)

## Drill and cutout list

Origin top left, X right, Y down, panel 70.8 x 128.5 mm.

| Element | X | Y | Machining |
|---|---|---|---|
| Mounting M3 | 7.62 / 63.18 | 3.0 | slot 4.0 x 3.2 |
| Mounting M3 | 7.62 / 63.18 | 125.5 | slot 4.0 x 3.2 |
| Display window | 35.4 | 23.7 | 30.5 x 15.8, R1 |
| Encoder SEL | 15.4 | 58.0 | 7.2 dia |
| Encoder A | 35.4 | 58.0 | 7.2 dia |
| Encoder B | 55.4 | 58.0 | 7.2 dia |
| MIDI in | 14.4 | 88.0 | 6.0 dia |
| MIDI out | 28.4 | 88.0 | 6.0 dia |
| Out L | 42.4 | 88.0 | 6.0 dia |
| Out R | 56.4 | 88.0 | 6.0 dia |
| USB-C | 24.0 | 110.0 | 15 x 8, R1 |
| RUN | 50.0 | 110.0 | to suit the button thread |

The display module's top edge sits at Y 9.0, which puts its four mounting holes
at X 20.2 / 50.6 and Y 11.5 / 40.0 (30.4 x 28.5 hole to hole).

**The window is cut to the pixels, not to the glass.** On the 1.3 inch module the
active area starts 7.35 mm below the module's top edge and is 14.7 mm tall, so
its centre lands 14.7 mm below that edge — the glass around it is 19 mm tall and
sits differently. Cutting to the glass would leave an uneven border.

Clearances, so the numbers can be checked rather than trusted: the display ends
at Y 42.5 and the knobs begin at 50.5; the knobs end at 65.5 and the jack
flanges begin at 83.5. Knob gap is 5 mm at 20 mm pitch, and 7.9 mm remains to
each side edge.

## Two boards

**Front board**, 6.5 mm behind the panel, carries everything the format dictates:
three EC11 encoders, the display on standoffs, the RUN button, the encoder
pull-ups, and a header for the panel-mounted jacks.

**Main board** behind it carries what does not care about the format: the
RP2350-Plus on sockets, the PCM5102, the MIDI circuitry, and the bus connectors.

They join with **two 10-pin headers** — two rather than one because they also
hold the boards parallel. Roughly nineteen signals: I2C 2, encoders 9, MIDI 2,
audio 2, RUN 1, and 3V3 / 5V / GND.

The split is the point. A different enclosure means a new front board; the main
board does not change.

## Why the jacks are wired rather than soldered to the board

The available jack has its collar 2.9 mm above the board and its thread ends at
8.9 mm. With a 1.6 mm panel and a 2 mm nut, the panel can sit no further out
than 5.3 mm — and the encoders put it at 6.5 mm. Short by 1.2 mm, with no slack
anywhere to take it out of.

So the jacks mount to the panel and wire back to an 8-pin header. That removes
the constraint instead of working around it: the board stops having an opinion
about which jack fits, which is what makes the three-conductor MIDI sockets
possible at all — those need TRS, and the two-pole jacks available here have two
conductors.

## Parts

Already in hand: RP2350-Plus (16 MB), three EC11 with 5 mm bushings, 15 mm
knobs, PCM5102 module.

Still needed:

| Part | Qty | Note |
|---|---|---|
| OLED 1.3" SH1106, I2C, 4-pin | 1 | 35.4 x 33.5 mm |
| 3.5 mm jack, mono, panel mount | 2 | audio out |
| 3.5 mm jack, **stereo**, panel mount | 2 | MIDI needs three conductors |
| Header strips 10-pin, male + female | 2 each | board to board |
| Socket strips for the RP2350-Plus | 2 | keep the module replaceable |
| H11L1 optocoupler | 1 | MIDI in, Schmitt trigger type |
| 220R, 470R, 1N4148 | 1 each | MIDI in |
| 33R | 2 | MIDI out at 3.3 V |
| 1N5817 Schottky | 1 | bus 5 V into VSYS |
| Shrouded header 2x3 | 1 | bus 1: power and MIDI |
| Header 3-pin | 1 | bus 2: audio |
| 10k 0805 | 9 | encoder and switch pull-ups |
| 10 nF 0805 | 6 | **buy, leave unpopulated** |
| M2 standoff 3.5 mm + screws | 4 | display |
| Panel button, threaded | 1–2 | RUN, and optionally BOOT |
| USB-C panel socket with lead | 1 | |

## Checked against the datasheets

- **RUN is on pin 30** of the RP2350-Plus, so the panel button works.
- **VSYS is pin 39**, which is where the bus Schottky goes.
- **BOOT is a pad** at the top edge beside USB_N and USB_P. A second button there
  makes the module unbrickable: a firmware too broken to reach its own interface
  is still recoverable without taking it apart.
- **GP29 is not brought out**, which independently confirms that `PIN_POT_1` in
  `core/include/project_config.h` is a leftover rather than a connection.
- The pin map fits this board: MIDI on GP4/GP5 lands on **UART1**, which is what
  `midi_serial.cpp` uses, and the display on GP2/GP3 lands on **I2C1**, which is
  what `pico_hw.cpp` initialises.
- The **PCM5102 module takes 3.3–5.5 V** and brings its analog outputs out on a
  3-pin header, so the audio bus taps there rather than at its own jack. Output
  is 2.1 Vrms — about 4.5 dB below the 10 Vpp a Eurorack module would normally
  put out, which is why no output stage is planned.

## Open

- Not drawn, not fabricated, not built.
- Powered over USB. The bus 5 V path exists as footprints for later; where the
  hub's own supply comes from is not decided.
- The panel is the expensive part to have made — 128.5 mm exceeds the 100 x 100
  mm that sponsored and cheap-tier PCB runs are usually capped at. Both boards
  stay well under it.
