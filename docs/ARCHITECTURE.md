# Architecture - PicoVintageSynthCollection

## 1. Goal
A monorepo that unifies 7 previously separate RP2350 synthesizer firmwares (PicoFaceYC, PicoFaceCP, PicoFaceRD, PicoFaceJ6, PicoFaceMD, PicoFaceSM, PicoFaceDX); PicoFaceOB was born as the eighth inside the monorepo itself. One shared base - audio pipeline, hardware access, GUI, USB MIDI, persistence - that instruments merely dock onto. Every instrument comes out of a single build and is published as a binary under its own name.

## 2. Directory layout
```text
CMakeLists.txt
cmake/
├── PicoFaceInstrument.cmake
├── pico_sdk_import.cmake
└── pico_extras_import.cmake
core/
├── CMakeLists.txt
├── include/
│   ├── (shared headers)
│   └── picoface/
│       ├── instrument.h
│       ├── ui.h
│       ├── list_view.h
│       └── midi.h
└── src/
    ├── (base sources)
    └── ui/
        ├── display.cpp
        └── (module ui_menu)
lib/
├── audio
├── encoder
└── u8g2
instruments/
├── PicoFaceYC
├── PicoFaceCP
├── PicoFaceRD
├── PicoFaceJ6
├── PicoFaceMD
├── PicoFaceSM
├── PicoFaceDX
└── PicoFaceOB
(each with instrument.cmake, src/, include/, doc/, README.md)
tools/
├── migrate.sh
└── (host tools, see tools/README.md)
docs/
img/
.github/
└── workflows/
    └── build.yml
```

## 3. One hardware platform, eight instruments
The board is the same for all eight instruments. Pin map and flash timing therefore live in the core: `core/include/project_config.h` and `core/include/pico_hw.h`. In the seven original repositories every single pin definition was already identical; the files differed only in comments, in one extra QMI timing constant for PicoFaceRD, and in one inline helper. The core versions are the union of all variants.

### Why the core is still not a library

The core continues to publish lists of absolute source paths instead of a STATIC library, but for two different reasons now. First, `core/src/usb_descriptors.c` is parameterized through the compile definitions PICOFACE_INSTRUMENT_NAME and PICOFACE_USB_PID, which differ per target; the file therefore has to be translated once per target. Second, individual instruments replace core sources with their own variants, and a member of an already-built library cannot be swapped out per target.

From that follows the include order `instruments/<NAME>/include` before `core/include`, so that an instrument's own variant of a header wins. After the merge that affects only five headers (see section 7).

Counter-example: lib/audio, lib/encoder and lib/u8g2 stay STATIC and are built once for all eight targets.

**Note on the audio library:** lib/audio/src/audio_subsystem.cpp originally included project_config.h and read PIN_I2S_DOUT and PIN_I2S_BCK from it. In the old projects this went unnoticed because a global `include_directories()` leaked the instrument include path into every target. The library now uses the standard macros PICO_AUDIO_I2S_DATA_PIN and PICO_AUDIO_I2S_CLOCK_PIN_BASE and no longer knows anything about the instrument configuration.

## 4. The docking contract
picoface::Instrument in core/include/picoface/instrument.h is the only interface between core and instrument.

| Group | Methods | CPU core |
|---|---|---|
| Identity | name() | core0 |
| Lifecycle | init() / sampleRate() | core0 |
| Audio | render(int32_t* out, frames) | producer context (core0), hard realtime |
| Audio hooks (optional) | consumeSampleRateChange, onAudioUnderrun, settingsSaveAllowed | producer context resp. core0 |
| MIDI | noteOn, noteOff, controlChange, programChange, pitchBend, sysEx | core0 |
| MIDI (optional) | realtime, midiActivity | core0 |
| GUI | uiInit(display), uiTick(display, input) | core0 |
| Persistence | settingsVersion, settingsSize, settingsSave, settingsLoad | core0 |

The core calls init(), then queries sampleRate() and initializes the audio pool with it. render() receives one int32 word per frame (packed stereo), is called block-wise from the producer loop, and must not block, must not allocate and must not use printf; an instrument may use core1 internally as a worker, as PicoFaceRD does. An instrument registers itself with PICOFACE_REGISTER_INSTRUMENT(Type).

## 4a. The runtime model

**All eight instruments run in the same model:** core0 does audio, USB, MIDI and GUI, the core polls the encoders into an InputState and calls `uiTick()`. core1 belongs to the instrument - PicoFaceRD uses it as a voice worker, the other seven leave it idle.

Until the conversion of PicoFaceYC and PicoFaceCP there was a second model in which an instrument took over the entire user interface on core1 via `ownsUserInterface()`. The core then started core1 itself, initialized neither display nor encoders, and called `pumpCrossCore()` instead of `uiTick()`; for flash access the instrument supplied a pair of park hooks. With the last user gone, the five methods disappeared from `picoface::Instrument` and so did the corresponding branch in `picoface_main.cpp`. An instrument that needs core1 starts it from its own adapter, the way PicoFaceRD does.

### The conversion of PicoFaceYC and PicoFaceCP

Both ran in the second model. Two things about it were not obvious:

- `pico_UserInterfaceFrontPanel()` contained `for(;;)` and never returned - the function *was* the core1 loop, not a menu that occasionally blocks. It has been replaced by `YC_Ui` and `CP_Ui`: state machines that do one pass per `uiTick()`. The list widget behind them lives in the core module ui_menu as `picoface::ui::ListView` and is used by both.
- `ipc.h` pushed its packets across the SIO FIFO with `multicore_fifo_push_blocking`. With everything running on core0, that push blocks forever with no consumer on the other side. The replacement is one same-core ring each, modelled on `instruments/PicoFaceMD/include/md_ipc.h`, which `render()` drains at the start of a block. There was no intermediate state to be had: a ring without memory barriers would have been unsafe across core boundaries as long as the UI still sat on core1. IPC, UI and adapter had to be converted together.

This removes `ownsUserInterface`, `runUserInterface`, `pumpCrossCore`, the flash hooks and the flash park handshake for both; the adapters' MIDI methods now really forward instead of being empty. YC's watchdog stays and is fed from `render()` instead of from `pumpCrossCore()`; CP never had one.

Both still write their own veeprom record and therefore report `settingsSize() == 0` - the debounce logic in `settings_task()` is finer-grained than the core's and knows about values that are deliberately not persisted. The write now happens on core0 between two audio blocks, like everywhere else.

CP was the harder case: eight panel screens and 22 local variables in the loop. The variables have become members of `CP_Ui`. They are still needed because an edit has to appear on screen immediately, while the ring only reaches the engine at the next block; `refresh()` conversely picks up whatever happened behind the panel's back - MIDI, a preset, a SysEx parameter.

### PicoFaceDX arrived later

PicoFaceDX only came in from its own repository after that conversion, and it still sat entirely in the second model: UI, USB and MIDI on core1, IPC across the SIO FIFO, plus a flash park handshake (`IPC_CMD_FLASH_LOCK` and a RAM-resident spin loop on core0) so that core1 could write the settings record while XIP was off. It was converted on the way in, not afterwards - the core did not have to bring the second model back. It was the same work as for YC and CP, only already familiar:

- `pico_UserInterfaceFrontPanel()` and the three blocking menu screens (Presets, System, Master Volume) have become `DX_Ui`, a state machine over `picoface::ui::ListView`. The preset list now gets an array of 32 name pointers instead of a 512-byte string joined with `\n`.
- `ipc.h` is a same-core ring modelled on YC's. `IPC_CMD_FLASH_LOCK`, `flash_park_core0()` and the two veeprom hooks are gone for good: the write now sits on core0 between two audio blocks like everyone else's.
- `settings_boot_restore_core0()` and `settings_boot_restore_core1()` collapsed into a single `settings_boot_restore()`. The split existed only because patch and master volume had to be set before the core1 launch and octave and SYSTEM block after it.
- The two pieces of state that were core1 globals in `main.cpp` - the UI octave and the master volume mirror - moved into the adapter together with their four accessors and kept their C linkage, because `midi_reface.cpp`, `settings.cpp` and `DX_Ui.cpp` all need them and none of them knows the adapter type.

Like YC and CP, DX writes its own veeprom record and reports `settingsSize() == 0`; that record contains a complete patch. In addition, `RefaceMidi::txBytes()` now writes to the DIN output as well (section 6a); the original repository did not have one.

## 5. Build system
`picoface_add_instrument()` in `cmake/PicoFaceInstrument.cmake` creates a complete firmware target per instrument. All settings are target-local (`target_compile_definitions` / `target_compile_options` instead of a global `add_compile_options`), because eight targets with conflicting defines have to coexist.

| Keyword | Meaning |
|---|---|
| NAME | target and file name, e.g. PicoFaceMD |
| PROGRAM_NAME | program name in the UF2 header and USB product string |
| VERSION | version string |
| USB_PID | USB product ID, unique per instrument |
| DIR | absolute instrument directory |
| SOURCES | instrument sources, relative to DIR |
| INCLUDE_DIRS | additional include paths, relative to DIR |
| DEFINES | additional compile definitions |
| LINK_LIBRARIES | additional libraries |
| PIO_SOURCES | optional .pio files |
| CORE_EXCLUDE | base names of core sources that the instrument replaces with its own variant |
| CORE_MODULES | optional core modules; currently only ui_menu |
| NO_DOUBLE_RESET | flag: do not link `pico_bootsel_via_double_reset`; set by PicoFaceMD and PicoFaceSM |

ui_menu contains the non-blocking list widget `picoface::ui::ListView` for instruments that draw from `uiTick()`. Its predecessor ui_panel held the blocking widgets that polled their encoders themselves, plus the reface CP MIDI layer and its persistence; with PicoFaceCP converted it had no users left. The CP-specific parts now live under `instruments/PicoFaceCP`, the rest has been deleted. Since then the core contains no instrument-specific code at all.

Instruments are found by auto-discovery over `instruments/*/instrument.cmake`. The CMake option `PICOFACE_INSTRUMENTS_FILTER` builds only the named instruments; empty means all. The aggregate target `all_instruments` builds everything. `pico_add_extra_outputs` already creates the files under the instrument name, so no renaming is needed.

```bash
# build everything
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# build a single one
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceMD
cmake --build build
```

## 6. USB identity
| Instrument | USB PID |
|---|---|
| PicoFaceYC | 0x1050 |
| PicoFaceCP | 0x1051 |
| PicoFaceRD | 0x1052 |
| PicoFaceJ6 | 0x1053 |
| PicoFaceMD | 0x1054 |
| PicoFaceSM | 0x1055 |
| PicoFaceOB | 0x1056 |
| PicoFaceDX | 0x1057 |
| PicoFaceJV | 0x1058 |

The VID stays 0x2E8A. Before the merge all seven original firmwares carried the same PID 0x104C, so hosts could not tell them apart; PicoFaceYC additionally identified itself as "PicoFaceDX" - which, with the real PicoFaceDX on the same host, would have been a double misunderstanding. `core/src/usb_descriptors.c` is now parameterized through the compile definitions `PICOFACE_INSTRUMENT_NAME` and `PICOFACE_USB_PID`, which removes both faults.

## 6a. MIDI transports

MIDI comes in over two paths and goes out over two paths: USB and DIN. The callback signatures of MIDISerial are deliberately identical to those of MIDIInputUSB, so that both transports end up in the same dispatch functions - an instrument does not see which wire an event arrived on.

### Hardware

| Signal | GPIO | Peripheral |
|---|---|---|
| MIDI RX | 5 | uart1 |
| MIDI TX | 4 | uart1 |
| stdio | 0 / 1 | uart0 |

31250 baud, 8N1, opto-coupler on the board. stdio deliberately sits on uart0 so the two never get in each other's way.

### Receive

Reception is interrupt-driven into a lock-free ring of 256 bytes. At 31250 baud a byte arrives every 320 microseconds; the 32-byte deep hardware FIFO would only give about 10 ms of headroom. That was already not enough for the blocking menus, which no longer exist, and today it covers the flash write of the persistence layer. The interrupt is RAM-resident and only moves bytes; parsing happens in process(). The interrupt priority sits below the audio DMA and above USB.

The parser handles running status, SysEx up to 256 bytes, and lets realtime bytes pass through unharmed in the middle of a message. Note-on with velocity 0 is treated as note-off.

### Integration

`MIDISerial::process()` runs for all eight instruments in `picoface_main.cpp`, right next to `MIDIInputUSB::process()`.

The core passes realtime bytes and bare receive activity through via the optional methods `realtime()` and `midiActivity()`. Both are needed for the active sensing supervision of the reface layer in YC, CP and DX - their 350 ms timeout silences all voices when 0xFE stops arriving. The defaults are empty; the other instruments ignore both.

### Transmit

| Instruments | What is sent |
|---|---|
| YC, CP | panel changes as CC and SysEx replies; goes through RefaceMidi::txBytes(), which now writes to the UART as well |
| DX | the same layer, different content: active sensing, program change on a preset switch, SysEx replies (parameter, bulk dump, identity reply). DX does not send panel changes - the three encoders only reach a handful of the patch parameters, so one CC per edited value would be an incomplete picture |
| MD, J6 | panel changes as CC out of the parameter table; every entry carries its CC number, 0xFF meaning none |
| SM, RD | panel changes as CC out of a table defined for this project, see below |

Sending only happens from the encoder path. A value that arrived over MIDI lands in onMidiParam() and does not take this route, so no feedback loop can form. The transmit channel is the receive channel; if that is set to omni, the transmit channel falls back to channel 1.

### CC assignment for PicoFaceSM and PicoFaceRD

Unlike MD and J6, these two did not bring a parameter-to-CC mapping with them: the Solina is purely electromechanical in the original, and the RD only answers sustain and a few mode messages. The following tables are therefore a decision of this project. They live in `instruments/PicoFaceSM/include/sm_cc_map.h` and `instruments/PicoFaceRD/include/rd_cc_map.h` and apply to **both** directions - send and receive use the same table and therefore cannot drift apart.

Guidelines: standard controllers where the function genuinely fits (72 release, 73 attack, 74 brightness); the General MIDI effect block for the modulation sections (92 tremolo, 93 chorus, 94 detune, 95 phaser); everything else from the undefined range. CC 7, 64 and 120/121/123 are deliberately left free - the engines already handle those on their own path, and a second route to the same value would be ambiguous.

**PicoFaceRD**

| CC | Parameter | | CC | Parameter |
|---|---|---|---|---|
| 92 | Tremolo on | | 105 | Tremolo depth |
| 93 | Chorus on | | 106 | Bass |
| 94 | Master tune | | 107 | Treble |
| 95 | Phaser on | | 108 | Volume |
| 102 | Chorus rate | | 109 | DAC filter on |
| 103 | Chorus depth | | 110 | Phaser rate |
| 104 | Tremolo rate | | 111 | Phaser depth |

Voice mode has no CC: the value is an enumeration, not a 0..127 quantity.

**PicoFaceSM**

| CC | Parameter | | CC | Parameter |
|---|---|---|---|---|
| 3 | Shaper | | 108 | Bass volume |
| 72 | Sustain (release) | | 109 | Volume |
| 73 | Crescendo (attack) | | 110 | Tremolo rate |
| 74 | Tone lowpass | | 111 | Chorus rate |
| 92 | Tremolo depth | | 112 | Chorus depth |
| 93 | Ensemble | | 113 | Ensemble tone |
| 94 | Tune | | 114 | Ensemble width |
| 95 | Phaser | | 115 | Phaser rate |
| 102..107 | Registers contrabass, cello, viola, violin, trumpet, horn | | 116 | Phaser color |
| | | | 117 | Tone highpass |
| | | | 118 | Tone shelf |
| | | | 119 | Formant |


## 6b. PicoFaceOB: a ported foreign engine and a different license

PicoFaceOB is the first instrument whose sound generation does not come from
this project: it is ported from
[OB-Xf](https://github.com/surge-synthesizer/OB-Xf). Two things therefore set it
apart from the other seven.

**License.** OB-Xf is GPL-3.0-or-later, and so is this repository as a whole
(root `LICENSE`). What sets PicoFaceOB apart is not the licence itself but its
origin: the files under `include/obxf/` are upstream source and keep the
upstream copyright headers, so the instrument carries its own copy of the
licence text next to them. Per-instrument licensing is possible at all because
the boundary runs exactly along the instrument border the monorepo draws anyway
- every instrument is its own binary. The per-instrument upstreams and one open
question about PicoFaceYC are collected in the licensing section of the root
README.

**What the port consisted of.** The engine is header-only and was pleasantly
free of JUCE; the actual work was elsewhere. Three findings were invisible on
the desktop and would have finished off the M33:

1. 19 unsuffixed floating point literals (`0.5` instead of `0.5f`) in the
   oscillators. Each one promotes its expression to double - emulated in
   software, in the per-sample path.
2. `tan()` and `atan()` in the filter, also the double variants, once per sample
   and voice each.
3. `getPitch()` = `440 * exp(ln2/12 * i)`, three times per sample and voice.

Replaced by float approximations in `include/obxf/ObxfPort.h`, modelled on
`instruments/PicoFaceCP/effects/dsp_fastmath.h`. After that, no object of this
instrument calls into the double runtime library any more. Not ported are the
modulation matrix, unison, MPE, patch banks and oversampling; 32 voices have
become six.

**The most expensive item, however, was none of those - it was the XIP cache.**
`OscillatorBlock::ProcessSample` is 18 KB of code that runs six times per
sample - that does not fit into a 16 KB cache. Only `__not_in_flash_func()` on
that function and on `Voice::ProcessSample` brought the peak down from 91 % to
53 % at 32 kHz. The headroom this freed went into the sample rate: 44.1 kHz is
the filter's design point (`sqrt(44000 / sampleRate)`) and lifts the cutoff
ceiling from 15.9 to 22 kHz. Final state: **78 % peak with 6 of 6 voices**,
confirmed on the hardware.

## 7. Remaining divergences

After merging project_config.h, pico_hw.h and pico_hw.cpp into the core, the following stays instrument-specific.

**Replaced core sources**

| File | Instruments |
|---|---|
| midi_input_usb.cpp | PicoFaceYC |
| veeprom.cpp | PicoFaceRD |

YC's `midi_input_usb.cpp` differs in exactly one point: it turns a note-on with velocity 0 into a note-off. The core's DIN parser does that anyway, YC's `RefaceMidi::onNoteOn()` does not - that is where the handling belongs, and then the file can go.

`settings.cpp`, `midi_reface.cpp` and `pico_frontpanel.cpp` were listed here until YC was converted. The first two still live under `instruments/PicoFaceYC/src/`, but no longer replace anything, because the core has no sources by those names any more. `pico_frontpanel.cpp` is gone without replacement.

**Shadowed headers**

None left. `midi_reface.h` and `settings.h` were CP versions in the core, shadowed by YC's files of the same name - both moved to `instruments/PicoFaceCP/include/` with the CP conversion. `pico_frontpanel.h` and `pico_userinterface.h` disappeared along with the blocking menus. CP's `veeprom.h` differed from the core version in exactly one comment line and has been deleted.

### Per-instrument defines

What used to be a separate pico_hw.cpp is now a handful of compile definitions in the respective instrument.cmake.

| Define | Instruments | Effect |
|---|---|---|
| PICO_USE_SW_SPIN_LOCKS=1 | YC, J6, MD, SM | software spin locks instead of hardware ones |
| PICO_STACK_SIZE, PICO_CORE1_STACK_SIZE = 0x1000 | YC, CP, RD, DX | stacks into the scratch banks |
| TARGET_RP2350=1 | RD, J6, MD, SM | ineffective, since the SDK defines PICO_BUILD anyway; carried over only for completeness |
| RD_CLOCK_504=1 | RD | enables the 480 MHz branch |
| PICOFACE_SYS_CLOCK_HZ, PICOFACE_QMI_M0_TIMING_TARGET | RD | clock target and matching flash timing; must be changed together |

`NO_DOUBLE_RESET` belongs to the same family of per-instrument decisions. The
library that turns a double tap on RESET into BOOTSEL is linked by default,
because it is what keeps a board without an accessible BOOTSEL button
reflashable. PicoFaceMD and PicoFaceSM opt out: with a Waveshare Pico Audio
board driving 3 W speakers, the inrush current on plug-in dips the supply, the
chip browns out, and the library reads that reset as a double tap - the device
then sits in BOOTSEL instead of running. The flag lives in the POWMAN register
and survives the dip, so shortening the detection window does not help. Both
original repositories therefore did not link it; the keyword restores that.

These defines are deliberately not unified in the helper but set per instrument the way they were in the original projects. Changing the spin lock implementation and the stack layout of a multicore audio build without a device would be careless.

`tools/migrate.sh` reports differing files as `DIVERGENT, kept locally`; project_config.h, pico_hw.h and usb_descriptors.c it removes unconditionally, because the core version is authoritative.

## 8. Open work
**Done:**

- `core/src/picoface_main.cpp`: shared main() for both runtime models.
- `core/src/ui/display.cpp`: u8g2 facade; flush() only arms the row-by-row output.
- All eight adapters, all in the standard model. PicoFaceMD is the template.
- All eight build from a single configure run and carry their own USB PID.
- All eight tested on the hardware and working, PicoFaceRD including the 480 MHz clocking.
- PicoFaceOB (section 6b) confirmed on the hardware on 2026-08-04, PicoFaceDX (section 4a) on 2026-08-05 - the last two. For DX that covers audio out, the factory presets sounding as they should, the whole of the user interface that used to run on core1 (preset switching, master volume, the settings write), and USB MIDI far enough that Soundmondo connects and loads voices into it.
- PicoFaceYC and PicoFaceCP converted to the standard model (section 4a) and confirmed on the hardware in that form. With that, the second runtime model has been removed from the core without replacement.

| Instrument | Flash | RAM | PID | Original (flash/RAM) |
|---|---|---|---|---|
| PicoFaceYC | 135,224 | 47,824 | 0x1050 | 130,408 / 44,780 |
| PicoFaceCP | 4,431,496 | 178,104 | 0x1051 | 4,431,112 / 175,612 |
| PicoFaceRD | 5,318,096 | 35,420 | 0x1052 | 5,312,968 / 33,928 |
| PicoFaceJ6 | 104,056 | 19,188 | 0x1053 | 101,644 / 17,688 |
| PicoFaceMD | 99,168 | 268,624 | 0x1054 | 96,828 / 267,124 |
| PicoFaceSM | 96,232 | 21,784 | 0x1055 | 91,868 / 20,288 |
| PicoFaceOB | 131,724 | 42,248 | 0x1056 | - (new) |
| PicoFaceDX | 170,052 | 218,508 | 0x1057 | 164,964 / 216,012 |

Measured with `arm-none-eabi-size` (text / bss). 32 KB of PicoFaceOB's RAM are
the six voices of the OB-Xf voice object, a good 5.3 KB each; on top of that come
47.6 KB of `.data`, because that is where the RAM-resident render path and the
BLEP tables live (section 6b).

The surcharge compared to the individual projects is 2 to 4 KB of flash and around 940 bytes of RAM per instrument - essentially the vtable of the instrument interface and the extra indirection.

Converting YC and CP, together with dropping the second runtime model, costs YC 1,300 and CP 5,012 bytes *less* flash than before, at 552 resp. 512 bytes more RAM: the same-core ring takes 1,032 bytes each, but the instrument's own encoder, button, USB MIDI and u8g2 objects are gone. J6, MD and SM lose around 500 bytes of flash - the `owns_ui` branch and the five removed virtual methods weigh more than the two new MIDI methods; only RD ends up 528 bytes higher, because the alignment padding behind its large sample block falls differently.

DX falls outside that range with 5,088 bytes of flash and 2,496 bytes of RAM, but in a different direction than YC and CP: it came in already converted and therefore is not compared against a freshly removed core1 loop, but against a repository without DIN MIDI and without `ListView`. The same-core ring again takes 1,032 bytes, the DIN receive ring another 256; the rest is carried by the parser and core objects that did not exist in the original repository in that form.

**Special case PicoFaceRD:** RD uses core1 as a RAM-resident voice worker and switches the sample rate between 20 and 32 kHz at runtime. Both run through the optional hooks: `consumeSampleRateChange` lets the core switch the hardware rate only once the buffers already in the DMA pipeline have drained; `onAudioUnderrun` triggers the voice governor; `settingsSaveAllowed` prevents a flash write while voices are still sounding. For the other instruments the defaults of these hooks are ineffective.

**Open:**

1. Hardware test of DIN MIDI (section 6a).
2. YC's `midi_input_usb.cpp`, the last replaced core source next to RD's `veeprom.cpp` (section 7).
