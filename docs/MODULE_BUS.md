# The module bus

A hardware interface, not firmware. It exists so that several PicoFace modules
in one case can share power and MIDI, and send their audio to a mixer, without
twenty patch cables and ten mains supplies.

Nothing here is required. A single module powered over USB uses none of it, and
should keep working exactly as it does today. The bus is a set of footprints
that cost nothing when left unpopulated and cannot be added later — which is the
only reason to write this down before the first board is fabricated rather than
after.

## What connects to what

Three kinds of board are planned. Only the first exists as firmware today:

- **sound module** — one of the ten instruments. Consumes power and MIDI, emits
  a stereo line-level signal.
- **MIDI hub** — takes MIDI from the outside world and fans it out to the sound
  modules. Also the obvious place for the shared supply.
- **mixer** — sums the modules' outputs.

## Two connectors, deliberately

Power and audio do not share a cable. A module draws around 150 mA, so ten of
them put 1.5 A of return current on the ground path; running a line-level signal
alongside that is how a mixer ends up humming. Two connectors and a few
millimetres of board is the cheaper end of that trade.

### Bus 1 — power and MIDI, 2x3 shrouded header, 2.54 mm

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | GND | 2 | GND |
| 3 | +5V | 4 | +5V |
| 5 | GND | 6 | MIDI RX |

Two pins each for +5V and GND is not padding: 28 AWG ribbon carries about 1 A
per conductor, and the far end of a ten-module chain would otherwise see a
visible voltage drop.

MIDI RX is 3.3 V logic at 31250 baud, driven by the hub and received by every
module in parallel. Inside one case the modules share a ground, so no isolation
is needed between them — the opto-isolator belongs once, in the hub, where the
outside world connects. That is the saving: one H11L1 instead of ten.

The hub sends the MIDI stream through unchanged. Instruments already filter by
channel themselves (`D5_Midi::accepts` and its equivalents), so a plain fan-out
needs no firmware change at all. Routing, splits and layering can be added in
the hub later without touching the ten instruments.

### Bus 2 — audio to the mixer, 3-pin header, 2.54 mm

| Pin | Signal |
|---|---|
| 1 | Left |
| 2 | GND |
| 3 | Right |

Line level, roughly 2 V peak to peak from the PCM5102. Its own short cable, run
away from the power ribbon.

## The rule that makes USB and bus power safe together

Changing the firmware is a normal thing to do here — ten images share one board
— so somebody will plug in USB while the module is powered from the bus. Two
5 V sources on one node.

**Feed the bus 5V into VSYS through a Schottky diode, never into VBUS.** The
RP2350 module already has a diode between VBUS and VSYS, so with one more on the
bus side the two sources coexist, the higher one wins, and nothing back-feeds
into the USB host. This is what VSYS is for.

## Audio without patch cables

Bus 2 is wired in **parallel** with the front jacks, not switched by them. The
output is present at both at once: patch the front and the mixer still receives
the module.

Switched jacks would be the tidier answer -- with no plug inserted the switch
contact routes the signal to the bus, and a patch cable at the front breaks that
connection, which is standard Eurorack normalling. It was the original plan and
it was dropped for availability rather than principle: the switched types are
hard to get here, and the two-pole jacks that are available cannot do it.

Nothing is lost electrically. The mixer input has to be high impedance anyway --
the PCM5102 module carries 470 ohm in series with each output, so a low
impedance load would cost real signal -- and a high impedance input does not
care that the front jack is also connected. What is lost is the ability to take
a module out of the mix by patching it, which is a convenience rather than a
function.

The jacks are mounted to the panel and wired back to a header on the front
board, not soldered to it. That was forced by a 1.2 mm mismatch -- the available
jack's collar sits 2.9 mm above the board and its thread runs out before the nut
can reach a panel at 6.5 mm, where the encoders put it -- but it turned out to be
the better arrangement anyway. The board no longer constrains which jack fits,
which is how the MIDI sockets ended up possible at all: those need three
conductors, and the two-pole jacks that are available locally have two.

## Current

| | |
|---|---|
| one module at 444 MHz | ~150 mA at 5 V (RP2350 ~80, PCM5102 ~10, OLED ~20) |
| ten modules | ~1.5 A |
| from a USB port | one module, comfortably; a full chain, no |

A USB 2.0 port gives 500 mA. Feeding a chain from the hub therefore needs the
hub to have its own supply of at least 2 A, or a Eurorack +12 V rail and a buck
converter — 1.5 A at 5 V is 7.5 W out, so about 0.7 A off +12 V, which is a real
share of a small case's budget.

## What a sound module needs on it

Two connectors, one diode, and the right jacks:

- 2x3 shrouded header (bus 1)
- 3-pin header (bus 2)
- 1N5817 or similar Schottky, bus +5V to VSYS
- a small-signal Schottky (BAT85) from the RP2350's UART RX node to bus pin 6.
  The module's own MIDI IN arrives there from the H11L1, an open-collector
  output with a 4k7 pull-up; the bus RX is push-pull from the hub. Joined
  through the diode the two are a wired AND: both idle high, either can pull
  the line low, and neither can fight the other. Both inputs are live at once,
  no jumper. The 4k7 is what makes the fan-out work: each module sinks about
  0.6 mA into the hub's driver when the line is low, so ten modules cost the
  hub ~6 mA — a GPIO at 12 mA drive strength or any buffer can do that, and
  the low level at the far module stays around 0.5 V (RP2350 VIL is 0.8 V).
  With the 470R a MIDI IN would normally use, ten modules would demand 60 mA
  and the level would sit on the threshold.
- a 10-pin header for the panel-mounted jacks (MIDI OUT needs its sleeve
  grounded, so three conductors; MIDI IN gets a landing for its open sleeve)

Line level means no output stage, which in turn means the sound module needs no
±12 V at all — no Eurorack power header, no regulator, no op-amp. It lives on
5 V from either source. The Eurorack connector, if there is one, belongs on the
hub where it is needed once.

## Open

- **The hub's supply.** Own mains adapter or the case's +12 V rail. Not decided.
- **The mixer sums in the analog domain.** Ten independent RP2350s each run their
  own 32 kHz clock and drift against each other, so a digital mixer would have to
  resample or drop samples continuously. Summing voltages does not care what
  clock produced them. One PCM1808 then digitises the finished stereo sum if
  effects are wanted — one converter and one clock instead of ten.
- **Nothing here has been built yet.** These are the numbers the first three
  boards should agree on, not measurements from a working system.
