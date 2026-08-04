# Ein neues Instrument hinzufuegen

Beispiel ist ein fiktives PicoFaceXX. Es sind genau vier Schritte noetig; am Build-System muss nichts geaendert werden.

## Schritt 1: Verzeichnis anlegen

```bash
mkdir -p instruments/PicoFaceXX/{src,include}
```

Hinweis: Der Verzeichnisname ist zugleich der Target- und Binaryname.

## Schritt 2: project_config.h anlegen

`instruments/PicoFaceXX/include/project_config.h` enthaelt die Pin-Map und das QMI-Flash-Timing dieses Instruments. Am einfachsten von einem bestehenden Instrument kopieren und anpassen. Dieser Header wird von den Kernquellen inkludiert; weil das Instrument-Include-Verzeichnis vor `core/include` steht, gewinnt diese Datei.

## Schritt 3: Adapter implementieren

`instruments/PicoFaceXX/src/XX_Instrument.cpp`

```cpp
#include "picoface/instrument.h"

namespace {
class XXInstrument final : public picoface::Instrument {
public:
    const char* name() const override { return "PicoFaceXX"; }
    void init() override { /* engine setup */ }
    uint32_t sampleRate() const override { return 44100; } // der Kern initialisiert damit den Audio-Pool
    void render(int32_t* out, uint32_t frames) override { /* ein int32-Wort pro Frame, gepacktes Stereo */ }
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override {}
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override {}
};
} // namespace

PICOFACE_REGISTER_INSTRUMENT(XXInstrument)
```

Hinweis: `name()`, `init()`, `sampleRate()`, `render()`, `noteOn()` und `noteOff()` sind Pflicht, alle uebrigen Methoden haben sinnvolle Standardimplementierungen und koennen bei Bedarf ueberschrieben werden.

Als vollstaendiges Beispiel dient `instruments/PicoFaceMD/src/MD_Instrument.cpp`. Dort ist zu sehen, wie eine bestehende Engine samt Controller, Display und MIDI-Frontend angebunden wird, wie `render()` zuerst den IPC-Ring leert und wie die Persistenz ueber `settingsVersion()`, `settingsSize()`, `settingsSave()` und `settingsLoad()` laeuft.

## Schritt 4: instrument.cmake anlegen

`instruments/PicoFaceXX/instrument.cmake`

```cmake
picoface_add_instrument(
    NAME PicoFaceXX
    PROGRAM_NAME "PicoFaceXX"
    VERSION "0.1"
    USB_PID 0x1056
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}
    SOURCES
        src/XX_Instrument.cpp
    INCLUDE_DIRS include
)
```

Hinweis: Die USB-PID muss eindeutig sein; die bereits vergebenen stehen in `docs/ARCHITECTURE.md`, Abschnitt 6.

## Fertig

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target PicoFaceXX
```

Das Instrument wird beim naechsten Configure automatisch gefunden, in den Sammel-Build `all_instruments` aufgenommen und erzeugt `PicoFaceXX.uf2`. Fuer die Veroeffentlichung als Release-Binary muss der Name zusaetzlich in die Matrix in `.github/workflows/build.yml` eingetragen werden.

## Optionale Bausteine

| Bedarf | Vorgehen |
|---|---|
| Frontpanel-UI des Kerns nutzen | `CORE_MODULES ui_panel` ergaenzen |
| Reface-MIDI-Mapping nutzen | `CORE_MODULES midi_reface` ergaenzen |
| Eigene Hardware-Anbindung | eigene `src/pico_hw.cpp` anlegen und `CORE_EXCLUDE pico_hw.cpp` setzen |
| Eigene .pio-Dateien | ueber `PIO_SOURCES` eintragen |
