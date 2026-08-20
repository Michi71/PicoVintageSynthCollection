# Hardware

The module this firmware has been running on, turned into something
reproducible. Nothing here has been fabricated yet: these are the dimensions and
decisions the first board should be drawn to, worked out against the parts
actually in hand and written down before the layout rather than after.

The schematic is drawn: the KiCad 10 project in
[PicoVintageSynthCollection/](PicoVintageSynthCollection/) (root sheet plus
`main_board`, `front_board`, `panel`; ERC clean), with a PDF export next to it
([PicoFace-schematic.pdf](PicoVintageSynthCollection/PicoFace-schematic.pdf)).

**Both boards are laid out**: [MainBoard/](MainBoard/) and
[FrontBoard/](FrontBoard/), routed, DRC clean; and the panel is a board in its
own right in [Panel/](Panel/). Nothing is fabricated.

The interface *between* several modules — power, MIDI, audio — is a separate
document: [The module bus](../docs/MODULE_BUS.md).

## Format

3U Eurorack, 10 HP. The panel is 50.8 x 128.5 mm and 1.6 mm thick, which is
mechanical rather than cosmetic: the EC11 encoders have a 5 mm threaded bushing,
and 1.6 mm of panel leaves 3.4 mm for the nut. A 2 mm panel leaves 3.0 mm, which
works but has no margin.

**Cut it to 50.4 mm, not 50.8.** 10 HP is 10 x 5.08 mm, and that nominal figure
is the space the module occupies in the rack, not the width of the metal. A panel
made to it has no room for the tolerances of its own edge, of its neighbours, or
of the rails, and a full row will bind rather than sit flat. Roughly 0.4 mm under
is the usual allowance. The height is not affected: 128.5 mm is already the
Eurorack panel height, 3U less the clearance the rails need.

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

No USB cutout: the 10 HP panel has no room for a USB lead, so USB stays at
the module's own socket.

The display module is **35.4 x 33.5 mm** with its four M2 holes **30.4 x 28.5**
apart, set 2.5 mm in from its top-left corner — all four numbers off the part
itself. Centred on the board it sits at X 7.7–43.1, Y 15.0–48.5, which puts the
holes at X 10.2 / 40.6 and Y 17.5 / 46.0. The two boards, 45 x 80 mm each, sit
at X 2.9–47.9, Y 14.0–94.0 behind it; the jack row is below the boards.

**The display's pin order is not the common one.** Measured on the part in hand:
**1 VDD, 2 GND, 3 SCK, 4 SDA**, numbered from the display side with pin 1 on the
left. The order most modules use — GND, VCC, SCL, SDA — would have put 3.3 V on
this module's ground pin and killed it on first power-up. The header is centred
on the module's top edge, which is where DS1 sits.

**The window is cut to the pixels, not to the glass.** On the 1.3 inch module the
active area starts 7.35 mm below the module's top edge and is 14.7 mm tall, so
its centre lands 14.7 mm below that edge — the glass around it is 19 mm tall and
sits differently. Cutting to the glass would leave an uneven border.

Clearances, so the numbers can be checked rather than trusted: the display
glass ends at Y 48.5 and the SELECT knob begins at 53.1; the 15 mm knob leaves
4.9 mm to each 6 mm button; the PARAM knobs (78.1–93.1) end 0.9 mm above the
board edge at Y 94 and stand 10 mm apart; the jack bodies (10 mm) sit 1.5 mm
apart at 11.5 mm pitch — fine for round nuts, an 11 mm hex nut is marginal.

## The panel as a board

The drawing is not a manufacturable file. `panel_10hp.svg` says where the holes
go and can be read by a person or a laser cutter; a board house needs the
outline on a copper-stackup layer and every opening as a closed contour. So the
panel is drawn a second time, as [Panel/Panel.kicad_pcb](Panel/Panel.kicad_pcb)
— a two-layer board 1.6 mm thick with **no copper on it at all**, which is the
usual way to have a fab cut a front panel: same process, same price bracket, and
the silkscreen comes with it instead of needing a separate print.

**The coordinates are the table above shifted 0.2 mm left.** The table is drawn
on the 50.8 mm nominal; cutting to 50.4 takes 0.2 mm off each edge, and
everything has to follow or the pattern sits off-centre. It stays symmetric
after the shift, which is the check: SELECT lands on 25.2, exactly half of 50.4,
and each pair still mirrors (mounting 7.3 / 43.1, buttons 9.8 / 40.6, PARAM 12.7
/ 37.7, jacks 7.95 / 42.45). The mounting holes end up ±17.9 mm from the panel
centre, which is where the rail thread expects them.

**What is drilled and what is routed** is decided by one number: 6.3 mm, the
largest drill most fabs keep.

| Opening | Layer | Why |
|---|---|---|
| outline 50.4 x 128.5 | `Edge.Cuts` | |
| display window 30 x 16 | `Edge.Cuts` | rectangular |
| 3 x encoder 7.0 dia | `Edge.Cuts` | over 6.3 |
| 4 x jack 8.2 dia | `Edge.Cuts` | over 6.3 |
| 4 x mounting 3.2 dia | NPTH | |
| 2 x button 4.0 dia | NPTH | |

Routed circles are cut with an end mill and come out a little rougher than a
drilled hole, which is of no consequence behind a nut. The display window's
inside corners will carry the mill's radius, about 1 mm — that is normal and the
module's bezel covers it.

**The F side is the face.** Silkscreen sits on `F.SilkS` and is not mirrored, so
whichever side the fab prints as top is the side that faces the player. Worth
saying to them explicitly when ordering, because a panel has no components to
make the intent obvious. White on black is the usual choice.

DRC reports six warnings and nothing else: the six hole footprints are generated
in the board file rather than pulled from a library, so the library-parity check
has nothing to compare them against. Same class of warning the other two boards
carry, and it does not reach the Gerbers.

## Two boards

Both boards are **45 x 80 mm, two layers**.

![The two boards pulled apart, front board above the main board](render/Assembly-exploded.jpg)

*Renders under [render/](render/); how they are made and what they settled is in
[3D view](#3d-view) below. The encoders and the display are stand-in models, and
the module is drawn as a Pico.*

**Front board**, 6.5 mm behind the panel, carries everything the format dictates
— and that 6.5 mm is a hard ceiling for anything on its panel side, which is why
the jack wires are soldered into J8 rather than plugged into a header: a 2.54 mm
header stands 8.5 mm tall and would hit the panel. It carries:
three EC11 encoders, the RUN and BOOT tact switches (6x6, four pins; the
actuator has to reach through 6.5 mm of gap plus the 1.6 mm panel, so the 9.5
mm or taller types), the display on standoffs, the encoder pull-ups, and the
header J8 for the panel-wired jacks.

**Main board** behind it carries what does not care about the format: the
module on sockets, the PCM5102, the MIDI circuitry, and the bus connectors.

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

Already in hand: Waveshare RP2350-Plus (16 MB), three EC11 with 5 mm bushings, 15 mm
knobs, PCM5102 module.

Still needed:

| Part | Qty | Note |
|---|---|---|
| OLED 1.3" SH1106, I2C, 4-pin | 1 | 35.4 x 33.5 mm |
| 3.5 mm jack, panel mount, M8 thread, 10 mm, solder lugs | 4 | 2× stereo (MIDI), 2× mono or stereo (audio) |
| Tact switch 6x6, 4-pin, actuator ≥ 9.5 mm | 2 | RUN, BOOT — on the front board |
| Header strips 12-pin, male + female | 2 each | board to board (J1/J2, J3/J4) |
| Header strip 10-pin | 1 | panel jacks (J8) |
| Socket strips for the module | 2 | keep the module replaceable |
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
| USB panel socket with lead | 0–1 | optional, and the 10 HP panel has no cutout for it; type follows the module — micro-USB on a Pico 2, USB-C on the Waveshare |

## Checked against the datasheets

- **The boards are drawn for a Raspberry Pi Pico 2**, the module most of the
  instruments in this collection run on. A **Waveshare RP2350-Plus** drops into
  the same sockets when 16 MB of flash is wanted — same 21 x 51 mm outline, same
  40 pins. Which one goes in is a firmware build setting, not a board question;
  see the flash-size note in the [top-level README](../README.md). The one place
  the difference does reach the board is BOOT, below.
- **Everything the board assumes about the module is checked against
  [the Pico 2 datasheet](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf)**,
  not against a footprint that happened to be to hand:

  | | board | datasheet |
  |---|---|---|
  | outline | 21.00 x 51.00 mm | 51 x 21 mm, 1 mm thick |
  | pin pitch | 2.54 mm | 2.54 mm |
  | pin 1 to pin 20 | 48.26 mm | 48.26 mm |
  | row spacing | 17.78 mm | 17.78 mm |
  | pin 1 from the top edge | 1.37 mm | (51 − 48.26)/2 |
  | hole per pin | 1.00 mm | 1 mm |

  Also from the drawing and not used here: four Ø2.1 mm mounting holes, 2 mm in
  from each end and 11.4 mm apart — the module hangs in its sockets, so they stay
  empty.
- **The pin map matches the firmware**, checked pad by pad against
  `core/include/project_config.h`: MIDI on GP4/GP5 → pins 6/7, display on GP2/GP3
  → pins 4/5, I2S on GP26/27/28 → pins 31/32/34, the three encoders on
  GP6-GP8/GP10-GP15, stdio on GP0/GP1 → pins 1/2. Ground lands on 3, 8, 13, 18,
  23, 28 and 38, and AGND on 33 — which the datasheet allows to be tied to
  digital ground when the ADC is not used, and it is not.
- **RUN is on pin 30** of the Pico format, so the RUN switch works.
- **VSYS is pin 39**, which is where the bus Schottky goes.
- **BOOT is a test pad on the underside**, under the BOOT button (the USB_N /
  USB_P pads sit under the USB connector, the three SWD pads at the far end).
  That pad sits on the button side of the 1 k that separates it from QSPI_SS_N,
  so the BOOT switch goes straight from the pad to ground — no extra resistor. A second button there makes the module
  unbrickable: a firmware too broken to reach its own interface is still
  recoverable without taking it apart.

  BOOT is the only signal on either board that does not end on a connector pin,
  which is why it gets two terminals rather than one:

  **TP2** is a spring-pin landing pad at **25.00 / 15.13**, directly under the
  module's BOOT pad, so the contact is made by seating the module and the module
  stays a plug-in part. That matters: the sockets are in the BOM specifically to
  keep it replaceable, and a soldered wire would tether it. The pin has to span
  **11.04 mm** — socket body 8.5 plus header plastic 2.54, both read out of their
  own STEP models — so roughly 12 mm free length compressing to 11. A crown or
  serrated tip, not a spear: the module's pad is flat and unperforated. It is
  **1.5 x 1.5 mm** though, which is a kinder target than it first looked, and
  leaves room for the tilt a 11 mm free-standing pin will have.

  **Where the position comes from.** Figure 5 of the Pico 2 datasheet, the SMT
  footprint, which gives every test-point centre in a frame whose origin is the
  board's bottom-left corner. The board is 21 x 51 mm there, and the USB shell
  pads at x 7.775 and 13.225 fix the centreline at 10.5. There are **seven** test
  points, not six, and the drawing labels none of them — it only gives
  coordinates, so which one is TP6 has to be reasoned out:

  | coordinate | mm from the USB end | what it must be |
  |---|---|---|
  | (9.5, 49.8), (11.5, 49.8), (10.5, 46.5) | 1.2 / 1.2 / 4.5 | TP1–TP3, the USB group, clustered at the connector |
  | (8, 43) | 8.0 | TP5, GPIO25/LED — the LED sits 7.4 mm from that end |
  | (8, 40.5) | 10.5 | TP4, GPIO23/SMPS PS |
  | **(8, 38)** | **13.0** | **TP6, BOOTSEL — the button sits 13.2 mm from that end** |
  | (11.5, 30.5) | 20.5 | TP7, 1V1, over by the chip |

  The LED and the BOOTSEL button are visible on any photo of the board, and
  measuring both against the three candidates leaves only one reading. The frame
  itself is confirmed independently: it puts pin 1's centre 1.37 mm from the top
  edge, which is exactly what KiCad's `RaspberryPi_Pico_Common_THT` footprint
  uses.

  Which side of the centreline a drawing means is the one thing it cannot say,
  since it does not state the viewing face — **checked on the part, the pad is
  nearer the GP0 row**, and that is the side the board carries. Against the
  module's own features TP6 sits **6.39 mm inwards from the GP0 pin row** and
  **11.63 mm from pin 1 along it**, which is the form to check with callipers.

  **The Waveshare RP2350-Plus is a different PCB**, so its BOOT pad is somewhere
  else and the spring pin will land on bare laminate. That build uses TP1 and a
  wire — which is exactly why TP1 stayed. Nothing needs changing on the board to
  switch between them; you fit either the pin or the wire.

  **TP1 stays** as the fallback, out at the board edge where a soldering iron
  reaches, for the case where the pad turns out to be somewhere else entirely.
  Then it is the short-wire build after all, and nothing has been lost.
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

## The main board layout

| front | back |
|---|---|
| ![Main board, panel side: J1 / J2 and the eight 0805 parts](render/MainBoard-front.jpg) | ![Main board, back: module on sockets, MIDI section, PCM5102 sockets](render/MainBoard-back.jpg) |

[MainBoard/](MainBoard/) is a second KiCad project, because KiCad wants one
board per project. Its root sheet is a wrapper that pulls in
`../PicoVintageSynthCollection/main_board.kicad_sch` — there is one copy of the
schematic, not two. Every symbol carries a second instance path, which is what
lets one sheet belong to two projects without re-annotating anything. The 19
board-to-board signals end on global labels there, because they leave the board
through J1 / J2.

45 x 80 mm, two layers, 0.2 mm signal tracks (0.3 mm for 3V3 / VSYS / bus 5V),
0.2 mm clearance, 0.6/0.3 mm vias. 630 track segments, 1414 mm of copper, 93
vias — of which about a third are ground stitching. Ground is a pour on both
sides rather than routed.

**Everything sits on the back except J1 / J2 and the eight 0805 parts.** That is
the decision the layout turned on: with the module on the front it would
stand 13.5 mm into the gap between the boards and J1 / J2 would need long-pin
headers, which are a part you have to go looking for. On the back, nothing
between the boards is taller than a 0805, and ordinary headers reach. It also
puts USB, the bus connectors and the module's own BOOT/RESET buttons on the side
you can reach with the module in a case.

- **A1** sits at the top with its USB socket at the board's top edge, so a plug
  can come in from above without dismantling anything. A Pico 2 has **micro-USB**
  (the Waveshare board has USB-C), overhanging its own top edge by 1.3 mm typ.
  The module's edge is 2.13 mm inside the carrier board's, so the socket face
  ends up 0.83 mm *behind* the board edge — which sounds like it would foul a
  plug and does not, because the module stands 11 mm off the back on its sockets
  and the plug arrives in free air well above the board plane. Worth writing down
  so nobody 'fixes' it later.
- **TP2, the BOOT spring-pin pad, cost two reroutes.** It lands where UART0_RX
  used to run, so that net was laid again; the first attempt then took the
  corridor past C9 and pinched the ground pour into an 8.4 mm² island — with
  C9's own ground pad inside it, which would have left the RUN capacitor with no
  return path. The island was too narrow for a stitching via (widest point
  0.468 mm, a 0.6 mm via needs 0.50), so the fix is upstream: that corridor is a
  router keep-out now and UART0_RX goes round. Worth knowing because the same
  trap is waiting for the next track laid through there. BOOT ends up 72.2 mm
  long over two vias, which is long for a node that sits 1k from the flash chip
  select — harmless, because it is only sampled at reset, but it is the reason
  to keep the wire short if the fallback build happens instead.
- **The PCM5102 module** is at the bottom, on the socket offsets taken from the
  STEP model and cross-checked against a KiCad footprint — see
  [PCM5102_module_geometry.md](PCM5102_module_geometry.md). Its own 3.5 mm jack
  (unused) hangs about 11 mm past the bottom edge, which is drawn on
  User.Comments as a reminder.
- **The MIDI section** is down the right-hand strip beside the module, next to
  GP4/GP5 where its signals land.
- **A1's courtyard was clipped** to its own outline plus 0.25 mm. The library
  footprint reserves room for a connector overhang that here points off the
  board. Running "Update Footprints from Library" would put it back and J5 would
  then report a courtyard overlap.

DRC is clean: no unconnected items, no clearance, short, hole or courtyard
errors, and the board matches the schematic. What remains are warnings —
footprints differing from their library copies (they are generated, and A1's
courtyard is deliberately clipped) and a handful of silkscreen overlaps on a
board this dense.

## The front board layout

| panel side | back |
|---|---|
| ![Front board, panel side: display, three encoders, RUN and BOOT](render/FrontBoard-front.jpg) | ![Front board, back: J3 / J4, J8 and the debounce parts](render/FrontBoard-back.jpg) |

[FrontBoard/](FrontBoard/), same arrangement as the main board: a wrapper root
sheet around the shared `front_board.kicad_sch`, one copy of the schematic.
Power arrives through J3 rather than from anything on the board, so ERC rule
`power_pin_not_driven` is set to ignore in that project only.

45 x 80 mm, two layers, same rules as the main board. 229 track segments, 691 mm
of copper, 20 vias, ground poured both sides.

Almost nothing here was a free choice — the panel fixes it. Board origin is
panel (2.9, 14), so the encoder shafts land at (22.5, 46.58), (10.0, 71.61) and
(35.0, 71.61), the two buttons at (7.1, 46.58) and (37.9, 46.58), and the
display occupies (4.75, 1.0) to (40.25, 34.5). What was left to decide:

- **J3 / J4 sit on the back at exactly the coordinates J1 / J2 occupy on the
  main board** — y 55.4 and 60.4, pin 1 at x 8.89. Both boards are drawn in the
  same frame (both have F.Cu facing the panel), so a point (x, y) on one is
  directly behind the same point on the other, and pin k meets pin k.
- **J8 sits on the back**, in the free band between the display and the encoder
  row. Its wires run in the gap between the boards and leave at the bottom edge:
  on the panel side the encoder bodies fill the whole 6.5 mm, so there is no
  lane for ten wires there.
- **The pull-ups and debounce footprints sit beside the encoder they belong to**,
  on the back, where the encoders' own courtyards do not reach because those are
  on the other side.

## Fabrication output

Gerbers and drill files are not in the repository -- they are regenerated from
the board files, and nothing has been ordered yet. Two commands per board, from
`hardware/`:

```
kicad-cli pcb export gerbers --output MainBoard/fab/ \
  --layers "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,F.Paste,B.Paste,Edge.Cuts" \
  --subtract-soldermask --check-zones MainBoard/MainBoard.kicad_pcb

kicad-cli pcb export drill --output MainBoard/fab/ --format excellon \
  --excellon-units mm --drill-origin absolute --excellon-separate-th \
  --excellon-oval-format route \
  --generate-map --map-format gerberx2 MainBoard/MainBoard.kicad_pcb
```

That gives nine Gerbers, a plated and a non-plated Excellon file, drill maps and
a `.gbrjob` that declares two layers at 1.6 mm. Zip everything except the drill
maps -- a fab that auto-detects layers can mistake a `-drl_map.gbr` for copper.

Checked against the board files rather than assumed: main board 183 plated holes
(111 pads + 72 vias) and 4 unplated, front board 86 (67 + 19) and 8, both
outlines exactly 45 x 80 mm.

`--excellon-oval-format route` is deliberate. The encoders' six mounting lugs are
oval, and KiCad's default writes them as `X..Y..G85X..Y..` -- one line that means
"slot from here to there". It is normal Excellon and fabs read it, but strict
parsers do not all accept it: gerbonara refuses the file outright, so a check in
a viewer never happens. The routed form (`G00` to the start, `M15` pen down,
`G01` to the end, `M16` up) says the same thing in statements every tool knows.
The six slots come out as a 1.5 mm tool travelling 1.3 mm, so 2.8 x 1.5 mm, on
the three encoder axes at x 10.0 / 22.5 / 35.0. They are plated, which is what
holds the encoder bodies down.

The panel takes the same two commands with fewer layers — it has no paste and
no copper worth subtracting a mask from:

```
kicad-cli pcb export gerbers --output Panel/fab/ \
  --layers "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts" \
  Panel/Panel.kicad_pcb

kicad-cli pcb export drill --output Panel/fab/ --format excellon \
  --excellon-units mm --drill-origin absolute --excellon-separate-th \
  Panel/Panel.kicad_pcb
```

Read back: `Edge.Cuts` spans exactly 0–50.4 by 0–128.5 with eight closed
contours inside it, the unplated Excellon carries four 3.2 mm and two 4.0 mm
holes at the coordinates in the table, and the plated file and both copper
layers are empty — which is what a panel should look like.

Both packages were read back with gerbonara and rendered per side. What that
found and fixed: the Pico's `A1` designator sat entirely above the top edge and
would have been trimmed away by the fab, `TP1` and `A2` sat on pads and would
have been cut open by the soldermask subtraction, and the display outline ran
0.12 mm alongside the `DS1` box. Reference designators for the axial parts now
sit centred on their own bodies, which is also the only unambiguous place in a
4 mm-pitch stack. Both boards are at zero DRC violations, zero unconnected and
zero schematic-parity issues; nothing on any layer reaches past the outline.

The Excellon files carry a `G90` after the header that KiCad has always written
and that a strict reader warns about. It is not a defect and needs no action.

The main board carries **one DRC exclusion**, written into `MainBoard.kicad_pro`
with its reason: TP2's hole sits inside A1's courtyard, which is the entire point
of it. It is an exclusion for that one pair rather than the rule turned off, so
`pth_inside_courtyard` still guards the rest of the board — it caught a real
problem once already, when J1's pads sat under the module. TP2 has no courtyard
of its own, because a landing pad is not a component body.

## 3D view

The board files carried **no `(model ...)` references at all** — the footprints
were placed by script and the model lines never came along, so both boards
opened to an empty 3D view. Every footprint now points at its library model
again, with two exceptions and one correction:

- **The encoders had to be modelled.** KiCad ships 3D models for 105 of its 155
  footprint libraries and `Rotary_Encoder` is not one of them. These three parts
  are what fills the 6.5 mm between the front board and the panel, so a 3D view
  without them answers nothing.
  [3dmodels/RotaryEncoder_Alps_EC11E_H20mm.wrl](3dmodels/RotaryEncoder_Alps_EC11E_H20mm.wrl)
  takes its outline and shaft axis from the KiCad footprint and its heights from
  the EC11E nominal dimensions. Measured back out of a render: body 6.2 mm,
  bushing 4.8, 19.4 mm total above the board.
- **The display is an envelope**, not a model —
  [3dmodels/Display_I2C_35x33_Envelope.wrl](3dmodels/Display_I2C_35x33_Envelope.wrl),
  35.4 x 33.5 x 2.8 mm on its 3.5 mm standoffs, hung off H1. It exists to occupy
  the right space, nothing more.
- **The spring pin has a stand-in too** —
  [3dmodels/SpringPin_11mm_Stand-in.wrl](3dmodels/SpringPin_11mm_Stand-in.wrl).
  No part number behind it; it draws the envelope the contact has to fill, and
  the render below is what it is for: the pin has to out-reach the socket strips
  beside it, and you can see that it does.

  ![The BOOT spring pin at TP2, module hidden](render/MainBoard-pogo.jpg)

- **A1 now shows the module on its sockets**, not lying on the board. The
  footprint offers five variants and the one that was visible had headers
  soldered flat — which hides the very dimension the whole layout turns on. The
  socketed pair is visible and the module sits 11 mm up. The library has no
  Pico 2 model, but the outline is the same one.

Per board:

```
kicad-cli pcb render --output MainBoard-back.png --side bottom \
  --quality high --floor --perspective --rotate '-32,0,24' --zoom 0.85 \
  MainBoard/MainBoard.kicad_pcb
```

The two boards stacked is not something kicad-cli does, but it renders 3D models
and a board can be exported as one. Export the front board to VRML in tenths of
an inch (the unit KiCad's model loader expects), then add a footprint to a
throwaway copy of the main board that carries it as a model:

```
kicad-cli pcb export vrml --output FrontBoard.wrl --units tenths \
  FrontBoard/FrontBoard.kicad_pcb
```

```
(footprint "Assembly:FrontBoardStack"
  (layer "F.Cu") (at 22.5 40)
  (model "FrontBoard.wrl" (offset (xyz 0 0 11.04)))
)
```

No rotation and no mirroring: both boards are drawn in the same frame with F.Cu
towards the panel, so the front board only has to move up. **11.04 mm** is the
mated height of the two connectors, read out of their own STEP models — header
plastic 2.54 plus socket body 8.5. Raise the offset to 40 mm for an exploded
view. The VRML export's origin is the board centre with z=0 at the bottom face,
which is why the footprint goes at (22.5, 40) and the offset is the plain gap.

![The stack in profile: panel side at the top, module hanging off the back](render/Assembly-profile.jpg)

The profile is the one worth reading twice. Top to bottom: encoder shafts and the
display on their standoffs, the front board, the two socket strips filling the
gap with J1 / J2 inside them, the main board, and the module hanging off its
sockets at the back. Also visible on the right is DS1's pin going straight
through the display.

![The assembled pair from the panel side](render/Assembly-iso.jpg)

The seven renders in [render/](render/) come out of exactly these commands,
scaled to 1200 px and saved as JPEG at quality 90 — they are photographic images
with smooth shading, where JPEG costs a third of the space and, checked at 200 %
zoom, leaves the silkscreen lettering clean. Regenerate them whenever a board
changes; they are documentation, not an artefact anything depends on.

What the view settled:

- **J1/J2 against J3/J4: all 24 pins coincide to 0.000 mm.** Worth checking
  rather than assuming — the two footprints are stored with opposite rotations
  (90 and -90) and only the back-side coordinate flip makes them run the same
  way.
- **DS1 and the display collide.** The display's carrier sits 3.5 mm up, a fitted
  2.54 mm header reaches 8.54 mm, and the render shows its pins straight through
  the module. That is not a board error — it says the display's own pins solder
  into DS1 from the front and no header gets fitted there. Worth knowing before
  ordering parts.
- **The tact switches in the render are the standard 5 mm type**, about 6.9 mm
  over the board, and the panel face is at 8.1 mm. That is the 9.5 mm-or-taller
  requirement noted above, now visible rather than only written down.


## Open

- Nothing on either board is assumed any more: every footprint position comes
  from a datasheet, a STEP model, the panel drawing or a measurement on the part.
- The board-to-board standoffs moved from the corners to y 63 on both boards:
  at y 77.5 the hole falls inside the PARAM encoders' courtyards. The front
  board is anchored to the panel by three encoder nuts anyway.
- Not fabricated, not built.
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
