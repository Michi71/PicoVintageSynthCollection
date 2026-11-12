# Adding a new instrument

The example is a fictional PicoFaceXX. Exactly three steps are needed; nothing in the build system has to change.

## Step 1: create the directory

```bash
mkdir -p instruments/PicoFaceXX/{src,include}
```

Note: the directory name is also the target and binary name.

The instrument does not need its own `project_config.h`. Pin map and flash timing apply to the whole platform and live in `core/include/project_config.h`; the same goes for `core/include/pico_hw.h`. Only if an instrument really requires different hardware does it place its own version in its include directory - that one then wins by include order.

## Step 2: implement the adapter

`instruments/PicoFaceXX/src/XX_Instrument.cpp`

```cpp
#include "picoface/instrument.h"

namespace {
class XXInstrument final : public picoface::Instrument {
public:
    const char* name() const override { return "PicoFaceXX"; }
    void init() override { /* engine setup */ }
    uint32_t sampleRate() const override { return 44100; } // the core initializes the audio pool with this
    void render(int32_t* out, uint32_t frames) override { /* 2*frames words: L,R per frame, each sample << 16 */ }
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override {}
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override {}
};
} // namespace

PICOFACE_REGISTER_INSTRUMENT(XXInstrument)
```

Note: `name()`, `init()`, `sampleRate()`, `render()`, `noteOn()` and `noteOff()` are mandatory; all other methods have sensible default implementations and can be overridden when needed.

`instruments/PicoFaceMD/src/MD_Instrument.cpp` serves as a complete example. It shows how an existing engine is attached together with controller, display and MIDI front end, how `render()` drains the IPC ring first, and how persistence runs through `settingsVersion()`, `settingsSize()`, `settingsSave()` and `settingsLoad()`.

## Step 3: create instrument.cmake

`instruments/PicoFaceXX/instrument.cmake`

```cmake
picoface_add_instrument(
    NAME PicoFaceXX
    PROGRAM_NAME "PicoFaceXX"
    VERSION "0.1"
    USB_PID 0x1058
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}
    SOURCES
        src/XX_Instrument.cpp
    INCLUDE_DIRS include
)
```

Note: the USB PID has to be unique; the ones already taken are listed in `docs/ARCHITECTURE.md`, section 6.

## Done

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target PicoFaceXX
```

The instrument is found automatically on the next configure run, included in the aggregate build `all_instruments`, and produces `PicoFaceXX.uf2`. To publish it as a release binary, the name additionally has to be entered in the matrix in `.github/workflows/build.yml`.

## Optional building blocks

| Need | How |
|---|---|
| menu list for `uiTick()` (`picoface::ui::ListView`) | add `CORE_MODULES ui_menu` |
| a Yamaha reface MIDI layer (SYSTEM block, active sensing, exclusive framing) | add `CORE_MODULES reface`, derive your `RefaceMidi` from `picoface::RefaceMidiBase` (`core/include/picoface/reface_midi.h`); PicoFaceDX is the template |
| snapshot autosave of the panel state, MIDI changes included | `picoface/settings_autosave.h`, see `instruments/PicoFaceYC/src/settings.cpp` |
| SysEx longer than 512 bytes (bank dumps) | return true from `wantsSysExStream()` and take the bytes in `sysExStream()`; pace dumps going out with `usbMidiOut().freeSpace()` / `midiSerial().txFree()`. PicoFaceM1's `M1_SysEx` is the example |
| user patches in flash beyond the veeprom's 240 bytes | a sector range of your own below the veeprom, written from RAM like `j6_patchstore.cpp` / `m1_memory.cpp` / `vs_store.cpp`; keep clear of the other instruments' sectors, since flash outside the image survives a switch of instrument |
| tables computed at configure time instead of committed (or built from the user's own files in `roms/`) | run the generator with `execute_process` in `instrument.cmake`, write into the build directory, add that to `INCLUDE_DIRS`, and skip the instrument with a STATUS note if the generator cannot run; `instruments/PicoFaceVS/instrument.cmake` is the example |
| MIDI going out besides SysEx (notes, controllers) | write the raw bytes with `usbMidiOut().write()` and `midiSerial().write()` after checking their free space, from `uiTick()`; `VS_Controller::sendChannelMessage` is the example |
| own hardware access | add your own `src/pico_hw.cpp` and set `CORE_EXCLUDE pico_hw.cpp` |
| own .pio files | list them under `PIO_SOURCES` |
| no double-tap RESET into BOOTSEL | add the flag `NO_DOUBLE_RESET` (see `docs/ARCHITECTURE.md`, section 7) |

## Documentation

An instrument brings its own `README.md` and, if there is more to say, a `doc/`
directory next to it - MIDI implementation chart, persistence format, preset
table, engineering log. The convention across the repository is English, and
manufacturer manuals are named rather than shipped (see the README of any of the
existing instruments). Host-side tools do not belong to the instrument; they go
into `tools/` and are listed in `tools/README.md`.
