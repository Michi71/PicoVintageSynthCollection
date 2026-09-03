# Host tests

Every engine here compiles for the host as well as for the RP2350, which is how
most of them were developed: audition a change on the Mac, then flash. The
scripts build the **unmodified** instrument sources - the only difference is a
`-D..._HOST_BUILD` guard that swaps the Pico audio subsystem for CoreAudio.

Run them from anywhere; each script locates the repository itself. The binaries
land next to their script and are gitignored.

| Directory | Needs | What it does |
|---|---|---|
| [`veeprom/`](veeprom/) | nothing | unit test of the core's virtual EEPROM: wear levelling, CRC, oversize rejection, 1000 saves. Prints PASS/FAIL per case. This one covers `core/`, not an instrument. |
| [`yc/`](yc/) | nothing | renders the YC organ engine to a WAV file. No audio device involved. |
| [`d5/`](d5/) | nothing | pins two D-50 laws read from the firmware: the PCM pitch (every sample advances at f/250 words per output sample) with synthetic cycles, and the TVF base cutoff (2 × panel + 54 chip units, capped by the pitch) through the harmonic profile of a sawtooth. Prints pass/FAIL per case. |
| [`ob/`](ob/) | nothing | regression checks on the OB-X engine - pitch bend ranges, LFO->cutoff without LFO->pitch, mod lever vibrato, all presets finite - plus a WAV render. Prints pass/FAIL per case. |
| [`dx_sysex/`](dx_sysex/) | nothing | round trip through PicoFaceDX's SysEx layer, both directions: a voice dump requested by an editor is parsed back and compared byte for byte, and a voice sent by an editor is compared against what reaches the engine. Prints pass/FAIL per direction. |
| [`j6/build_ui.sh`](j6/) | nothing | drives `J6_Controller` and the patch store through a scripted panel session - menu navigation, patch save/load, the free-slot rule. |
| [`cp/`](cp/README.md) | portmidi | plays the mdaEPiano engine, with or without the reface CP effect chain, over CoreAudio. |
| [`j6/build_juno.sh`](j6/) | portmidi | plays the Juno engine over CoreAudio. |
| [`md/`](md/) | portmidi | plays the Minimoog engine over CoreAudio. |
| [`sm/`](sm/) | portmidi | plays the Solina engine over CoreAudio. |

```bash
brew install portmidi     # only for the four CoreAudio ones
tools/host_tests/veeprom/build_veeprom.sh
tools/host_tests/md/build_moog.sh && tools/host_tests/md/moog_test
```

## shim/

`shim/` holds the two things a host build needs that the Pico SDK would
otherwise provide: a stand-in `pico/stdlib.h` and no-op implementations of the
DIN MIDI transport. They exist because instrument controllers mirror panel edits
to both MIDI wires now, so `core/include/midi_serial.h` follows a controller
into the host build even though nothing is transmitted there. Link
`shim/host_midi_serial.cpp` into any host test that pulls in a controller;
engine-only tests do not need it.

## Not migrated

The compiled binaries and the rendered reference WAV from the original
repositories are not here - they are build output. The Solina project also
carried six WAV recordings demonstrating ensemble width and phaser settings;
at 10 MB of uncompressed audio that GitHub will not play inline, they stayed
behind.
