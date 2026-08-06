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
