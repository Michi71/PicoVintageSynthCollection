# Hardware

The module this firmware has been running on, turned into something
reproducible. Nothing here has been fabricated yet: these are the dimensions and
decisions the first board should be drawn to, worked out against the parts
actually in hand and written down before the layout rather than after.

The schematic is drawn: the KiCad 10 project in
[PicoVintageSynthCollection/](PicoVintageSynthCollection/) (root sheet plus
`main_board`, `front_board`, `panel`; ERC clean), with a PDF export next to it
([PicoFace-schematic.pdf](PicoVintageSynthCollection/PicoFace-schematic.pdf)).
The boards are not laid out yet.

The interface *between* several modules — power, MIDI, audio — is a separate
document: [The module bus](../docs/MODULE_BUS.md).

## Format

3U Eurorack, 10 HP. The panel is 50.8 x 128.5 mm and 1.6 mm thick, which is
mechanical rather than cosmetic: the EC11 encoders have a 5 mm threaded bushing,
and 1.6 mm of panel leaves 3.4 mm for the nut. A 2 mm panel leaves 3.0 mm, which
works but has no margin.

The layout is [Panel/panel_10hp.svg](Panel/panel_10hp.svg) (CorelDRAW source
next to it), measured against the real parts: the display on top, the SELECT
encoder between the RUN and BOOT buttons, PARAM A and B in a second row, the
four jacks along the bottom below the board. 14 HP was the plan as long as the
three encoders had to share one row; with SELECT in its own row and the jacks
off the board, 10 HP closes.

## The governing dimension

**The board sits 6.5 mm behind the panel.** That is the EC11's body height and
nothing else negotiates with it — every other panel-facing part has to reach the
same plane:

- the **display** module is 2.8 mm thick, so it rides on 3.5 mm standoffs
- the **jacks** could not reach it at all, which is why they are panel-mounted
  and wired (see below)

## Drill and cutout list

Origin top left, X right, Y down, panel 50.8 x 128.5 mm. Numbers read off
`panel_10hp.svg` (1 unit = 0.01 mm there); the SVG carries a 50 mm calibration
bar.

| Element | X | Y | Machining |
|---|---|---|---|
| Mounting M3 | 7.5 / 43.3 | 3.0 | 3.2 dia |
| Mounting M3 | 7.5 / 43.3 | 125.5 | 3.2 dia |
| Display window | 25.4 | 30.0 | 30 x 16 (X 10.4–40.4, Y 22–38) |
| Encoder SEL | 25.4 | 60.58 | 7.0 dia, 15 mm knob |
| RUN | 10.0 | 60.58 | 4.0 dia for the 6x6 tact switch actuator |
| BOOT | 40.8 | 60.58 | 4.0 dia for the 6x6 tact switch actuator |
| Encoder A | 12.9 | 85.61 | 7.0 dia, 15 mm knob |
| Encoder B | 37.9 | 85.61 | 7.0 dia, 15 mm knob |
| MIDI in | 8.15 | 109.6 | 8.2 dia (M8 thread), 10 mm body |
| MIDI out | 19.65 | 109.6 | 8.2 dia (M8 thread), 10 mm body |
| Out L | 31.15 | 109.6 | 8.2 dia (M8 thread), 10 mm body |
| Out R | 42.65 | 109.6 | 8.2 dia (M8 thread), 10 mm body |

No USB cutout: the 10 HP panel has no room for the USB-C lead, so USB stays at
the module's own socket.

The display module (35.5 x 33.5 mm) sits at X 7.65–43.15, Y 15.0–48.5, which
puts its four mounting holes at X 10.4 / 40.4 and Y 17.75 / 45.75 (30 x 28 hole
to hole, measured on the module in hand). The two boards, 45 x 80 mm each, sit
at X 2.9–47.9, Y 14.0–94.0 behind it; the jack row is below the boards.

**The window is cut to the pixels, not to the glass.** On the 1.3 inch module the
active area starts 7.35 mm below the module's top edge and is 14.7 mm tall, so
its centre lands 14.7 mm below that edge — the glass around it is 19 mm tall and
sits differently. Cutting to the glass would leave an uneven border.

Clearances, so the numbers can be checked rather than trusted: the display
glass ends at Y 48.5 and the SELECT knob begins at 53.1; the 15 mm knob leaves
4.9 mm to each 6 mm button; the PARAM knobs (78.1–93.1) end 0.9 mm above the
board edge at Y 94 and stand 10 mm apart; the jack bodies (10 mm) sit 1.5 mm
apart at 11.5 mm pitch — fine for round nuts, an 11 mm hex nut is marginal.

## Two boards

Both boards are **45 x 80 mm, two layers**.

**Front board**, 6.5 mm behind the panel, carries everything the format dictates:
three EC11 encoders, the RUN and BOOT tact switches (6x6, four pins; the
actuator has to reach through 6.5 mm of gap plus the 1.6 mm panel, so the 9.5
mm or taller types), the display on standoffs, the encoder pull-ups, and the
header J8 for the panel-wired jacks.

**Main board** behind it carries what does not care about the format: the
RP2350-Plus on sockets, the PCM5102, the MIDI circuitry, and the bus connectors.

They join with **two 1x12 headers** (J1/J2 male on the main board, J3/J4
sockets on the front board) — two rather than one because they also hold the
boards parallel. Nineteen signals: I2C 2, encoders 9, MIDI 4, audio 2, RUN,
BOOT; plus 3V3, VSYS and three GND = 24. MIDI is four lines rather than two
because the current loop crosses the headers — the opto-coupler and its
resistors stay on the main board, where the bus MIDI RX joins the same node —
and the two extra grounds are what makes a single 24-way link acceptable for
audio. The plan said "roughly nineteen" and two 10-pin strips; the schematic is
where the count became exact.

The split is the point. A different enclosure means a new front board; the main
board does not change.

## Why the jacks are wired rather than soldered to the board

The available jack has its collar 2.9 mm above the board and its thread ends at
8.9 mm. With a 1.6 mm panel and a 2 mm nut, the panel can sit no further out
than 5.3 mm — and the encoders put it at 6.5 mm. Short by 1.2 mm, with no slack
anywhere to take it out of.

So the jacks mount to the panel and wire back to a 10-pin header (J8). That
removes the constraint instead of working around it: the board stops having an
opinion about which jack fits, which is what makes the three-conductor MIDI
sockets possible at all — those need TRS. The jacks are the M8-thread panel
type (10 mm body, nut, solder lugs), stereo for MIDI, mono or stereo for audio;
with a stereo jack on an audio output the ring is simply left open. On a metal
panel the bushing ties the sleeve to the panel — fine for the three grounded
jacks, and for MIDI IN either an insulating washer or accepting it (the opto
still isolates the loop).

## Parts

Already in hand: RP2350-Plus (16 MB), three EC11 with 5 mm bushings, 15 mm
knobs, PCM5102 module.

Still needed:

| Part | Qty | Note |
|---|---|---|
| OLED 1.3" SH1106, I2C, 4-pin | 1 | 35.4 x 33.5 mm |
| 3.5 mm jack, panel mount, M8 thread, 10 mm, solder lugs | 4 | 2× stereo (MIDI), 2× mono or stereo (audio) |
| Tact switch 6x6, 4-pin, actuator ≥ 9.5 mm | 2 | RUN, BOOT — on the front board |
| Header strips 12-pin, male + female | 2 each | board to board (J1/J2, J3/J4) |
| Header strip 10-pin | 1 | panel jacks (J8) |
| Socket strips for the RP2350-Plus | 2 | keep the module replaceable |
| Socket strips 1x6 and 1x9 | 1 each | the PCM5102 module, likewise replaceable |
| H11L1 optocoupler (+ DIP-6 socket) | 1 | MIDI in, Schmitt trigger type |
| 220R, 4k7, 1N4148, 100 nF | 1 each | MIDI in (4k7 pull-up on the H11L1, 100 nF bypass) |
| 33R, 0.5 W | 2 | MIDI out at 3.3 V (the source resistor sees a pin-4 short) |
| 1N5817 Schottky | 1 | bus 5 V into VSYS |
| BAT85 Schottky | 1 | bus MIDI RX into the RX node (small signal: low Vf, low leakage) |
| 1k 0805 | 2 | in series with the audio outputs (mispatch and load protection) |
| 100k 0805 | 4 | PCM5102 FLT/DEMP/XSMT/FMT to the module's own rails |
| 100 nF 0805 | 1 | on RUN, at the module pin |
| Shrouded header 2x3 | 1 | bus 1: power and MIDI |
| Header 3-pin | 2 | bus 2: audio; UART0 debug (footprint only) |
| 10k 0805 | 9 | encoder and switch pull-ups |
| 10 nF 0805 | 6 | **buy, leave unpopulated** |
| 100 nF 0805 | 1 | MIDI IN sleeve RF ground, **leave unpopulated** |
| M2 standoff 3.5 mm + screws | 4 | display |
| USB-C panel socket with lead | 0–1 | optional: the 10 HP panel has no cutout for it |

## Checked against the datasheets

- **RUN is on pin 30** of the RP2350-Plus, so the RUN switch works.
- **VSYS is pin 39**, which is where the bus Schottky goes.
- **BOOT is a test pad on the underside**, under the BOOT button (the USB_N /
  USB_P pads sit under the USB connector, the three SWD pads at the far end).
  In the module's schematic that pad sits on the button side of the 1k (R20)
  that separates it from QSPI_SS_N, so the BOOT switch goes straight from the
  pad to ground — no extra resistor. A second button there makes the module
  unbrickable: a firmware too broken to reach its own interface is still
  recoverable without taking it apart.
- **RUN has only the RP2350's internal ~50k pull-up** (no external one on the
  module), and here it travels over two headers to the tact switch on the front
  board. 100 nF at the module pin (C9) so a touch or a spike does not reset a
  running instrument.
- **GP29 is not brought out**, which independently confirms that `PIN_POT_1` in
  `core/include/project_config.h` is a leftover rather than a connection.
- The pin map fits this board: MIDI on GP4/GP5 lands on **UART1**, which is what
  `midi_serial.cpp` uses, and the display on GP2/GP3 lands on **I2C1**, which is
  what `pico_hw.cpp` initialises.
- The **PCM5102 module takes 3.3–5 V** (two on-board LDOs) and brings its analog
  outputs out on a header row, so the audio bus taps there rather than at its
  own jack. The small purple GY-PCM5102 has a 6-pin digital row (SCK BCK DIN
  LCK GND VIN) and a 9-pin row on the long edge (FLT DEMP XSMT FMT A3V3 AGND
  ROUT AGND LROUT); the schematic sockets both, ties SCK to ground and leaves
  the four format pins to the module's own solder bridges (1=L 2=L 3=H 4=L for
  I2S) — plus 100k pulls to the module's own rails, so a replacement module
  that arrives unbridged still plays. Output is 2.1 Vrms behind 470R — about
  4.5 dB below the 10 Vpp a Eurorack module would normally put out, which is
  why no output stage is planned. The board adds 1k in series (R15/R16): a
  ±12 V Eurorack output patched into ours by mistake is then limited to
  ~8 mA into the DAC's clamps, and a half-inserted plug does not drop the load
  below the PCM5102's 1 kΩ minimum. Into a 100 kΩ input it costs nothing.
- **MIDI at 3.3 V.** In: 220R, reverse 1N4148, H11L1 from 3V3 with 4k7
  pull-up, sleeve left open (CA-033: no DC path to ground at the receiver; a
  100 nF footprint to ground is the RF option the spec allows). The pull-up is
  4k7 rather than the usual few hundred ohms because the bus MIDI RX joins the
  same node through a diode: every module then loads the hub's driver with
  only ~0.6 mA, ten of them with ~6 mA, and the low level at the module stays
  around 0.5 V. Firmware should call `gpio_disable_pulls(PIN_MIDI_RX)` — the
  pad's default pull-down would otherwise divide that pull-up. Out: 33R from
  3V3 to pin 4, 33R from TX to pin 5, sleeve grounded. CA-033's pair is
  33R/10R; 33/33 gives about 4.1 mA worst case into a standard receiver, the
  same class as the 47/47 the Teensy design ships with. The firmware should
  raise GP4's drive strength to 12 mA — the default 4 mA is below the loop
  current.

## Open

- Schematic drawn; boards not laid out, not fabricated, not built. Layout note:
  KiCad wants one board per project, so the two boards will be two projects
  whose root sheets are `main_board.kicad_sch` and `front_board.kicad_sch`
  (the hierarchical labels then just dangle at the connectors).
- Panel-mounted parts (the four jacks, the optional USB lead) are in the
  schematic with "exclude from board" set, so they appear in the BOM but on
  neither layout.
- Not on the schematic: mounting holes for board-to-board standoffs (the two
  header strips are the only mechanical link so far), and the module's SWD
  pads (underside, unreachable once the module sits in its sockets).
- Powered over USB at the module's own socket (the 10 HP panel has no USB
  cutout; a panel lead is optional). The bus 5 V path exists as footprints for
  later; where the hub's own supply comes from is not decided.
- The panel is the expensive part to have made — 128.5 mm exceeds the 100 x 100
  mm that sponsored and cheap-tier PCB runs are usually capped at. Both boards
  stay well under it.
