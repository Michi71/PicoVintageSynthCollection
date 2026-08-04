# Architektur - PicoVintageSynthCollection

## 1. Ziel
Monorepo, das 6 bisher getrennte RP2350-Synthesizer-Firmwares vereint (PicoFaceYC, PicoFaceCP, PicoFaceRD, PicoFaceJ6, PicoFaceMD, PicoFaceSM). Eine gemeinsame Basis - Audio-Pipeline, Hardwareanbindung, GUI, USB-MIDI, Persistenz - an die sich Instrumente nur andocken. Alle Instrumente entstehen aus einem Build und werden unter ihrem eigenen Namen als Binary veroeffentlicht.

## 2. Verzeichnisstruktur
```text
CMakeLists.txt
cmake/
├── PicoFaceInstrument.cmake
├── pico_sdk_import.cmake
└── pico_extras_import.cmake
core/
├── CMakeLists.txt
├── include/
│   ├── (gemeinsame Header)
│   └── picoface/
│       ├── instrument.h
│       ├── ui.h
│       └── midi.h
└── src/
    ├── (Basisquellen)
    ├── midi/
    │   └── (Modul midi_reface)
    └── ui/
        └── (Modul ui_panel)
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
└── PicoFaceSM
(je mit instrument.cmake, src/, include/)
tools/
└── migrate.sh
docs/
.github/
└── workflows/
    └── build.yml
```

## 3. Zentrale Entwurfsentscheidung: der Kern ist keine Bibliothek
project_config.h (Pin-Map, QMI-Flash-Timing) ist pro Instrument verschieden und wird von Kernquellen (pico_hw.cpp, veeprom.cpp, midi_input_usb.h) inkludiert. Eine vorgebaute STATIC-Library liesse sich nicht pro Instrument gegen ein anderes project_config.h uebersetzen. Deshalb veroeffentlicht core/CMakeLists.txt nur Listen absoluter Quellpfade; jedes Instrument-Target uebersetzt sie selbst.
Konsequenz: Include-Reihenfolge ist instruments/<NAME>/include VOR core/include - so gewinnt immer die instrumenteigene Variante eines Headers.
Gegenbeispiel: lib/audio, lib/encoder und lib/u8g2 bleiben STATIC, werden also nur einmal gebaut und von allen sechs Targets geteilt.

**Anmerkung zur Audio-Bibliothek:** lib/audio/src/audio_subsystem.cpp inkludierte urspruenglich project_config.h und las daraus PIN_I2S_DOUT und PIN_I2S_BCK. In den Altprojekten fiel das nicht auf, weil ein globales include_directories() den Instrument-Include-Pfad in jedes Target leakte; im Monorepo mit target-lokalen Include-Pfaden brach der Build daran. Da beide Pins in allen sechs Instrumenten identisch sind (26 und 27), nutzt die Bibliothek jetzt die Standardmakros PICO_AUDIO_I2S_DATA_PIN und PICO_AUDIO_I2S_CLOCK_PIN_BASE mit ebendiesen Defaults und kennt die Instrument-Konfiguration nicht mehr.

## 4. Der Andock-Contract
picoface::Instrument in core/include/picoface/instrument.h ist die einzige Schnittstelle zwischen Kern und Instrument.

| Gruppe | Methoden | CPU-Kern |
|---|---|---|
| Identitaet | name() | core0 |
| Lebenszyklus | init() / sampleRate() | core0 |
| Audio | render(int32_t* out, frames) | Producer-Kontext (derzeit core0), harte Echtzeit |
| Audio-Hooks (optional) | consumeSampleRateChange, onAudioUnderrun, settingsSaveAllowed | Producer-Kontext bzw. core0 |
| MIDI | noteOn, noteOff, controlChange, programChange, pitchBend, sysEx | core0 |
| GUI | uiInit(display), uiTick(display, input) | core0 |
| Persistenz | settingsVersion, settingsSize, settingsSave, settingsLoad | core0 |

Der Kern ruft init(), fragt danach sampleRate() ab und initialisiert damit den Audio-Pool. render() erhaelt ein int32-Wort pro Frame (gepacktes Stereo), wird blockweise aus der Producer-Schleife aufgerufen und darf nicht blockieren, nicht allokieren und kein printf verwenden; ein Instrument darf intern core1 als Worker nutzen, wie es PicoFaceRD tut. Ein Instrument registriert sich mit PICOFACE_REGISTER_INSTRUMENT(Typ).

## 4a. Zwei Laufzeitmodelle

Die sechs Instrumente laufen in zwei verschiedenen Modellen. Der Kern unterstuetzt beide; welches gilt, entscheidet das Instrument ueber `ownsUserInterface()`.

| Modell | Instrumente | core0 | core1 | Bedienung |
| --- | --- | --- | --- | --- |
| Standard | PicoFaceMD, PicoFaceSM, PicoFaceJ6, PicoFaceRD | Audio, USB, MIDI, GUI | ungenutzt (RD: Voice-Worker) | Kern pollt die Encoder in einen InputState und ruft uiTick() |
| Instrument besitzt die UI | PicoFaceYC, PicoFaceCP | nur Audio | USB, MIDI, Encoder, Frontpanel-Menue | Instrument fuehrt seine eigene Schleife in runUserInterface() |

Im zweiten Modell initialisiert der Kern weder Display noch Encoder, verdrahtet kein USB-MIDI und ruft weder uiInit() noch uiTick(). Er startet core1 mit runUserInterface() - zwingend vor init_audio(), weil der SDK-Handshake die SIO-FIFO benutzt - und ruft in jedem Durchlauf der Producer-Schleife pumpCrossCore(), womit das Instrument seinen Cross-Core-Kanal leert. Findet der Producer keinen freien Puffer, geht core0 per `__wfe()` schlafen statt zu drehen.

Diese beiden Instrumente schreiben auch selbst in den veeprom und melden daher `settingsSize() == 0`. Weil ihr Menue auf core1 laeuft, muss vor einem Flash-Zugriff core0 in RAM-residentem Code geparkt werden; das Paar dafuer liefern sie ueber `flashLockHook()` und `flashUnlockHook()`.

Die einheitliche GUI-Schnittstelle ist damit fuer vier der sechs Instrumente erreicht; die Umstellung der blockierenden Frontpanel-Menues von YC und CP auf den InputState bleibt offen.

## 5. Build-System
`picoface_add_instrument()` in `cmake/PicoFaceInstrument.cmake` erzeugt pro Instrument ein vollstaendiges Firmware-Target. Alle Einstellungen sind target-lokal (`target_compile_definitions` / `target_compile_options` statt globaler `add_compile_options`), weil sechs Targets mit widersprechenden Defines koexistieren muessen.

| Schluesselwort | Bedeutung |
|---|---|
| NAME | Target- und Dateiname, z.B. PicoFaceMD |
| PROGRAM_NAME | Programmname im UF2-Header und USB-Produktstring |
| VERSION | Versionsstring |
| USB_PID | USB Product ID, pro Instrument eindeutig |
| DIR | absolutes Instrumentverzeichnis |
| SOURCES | instrumenteigene Quellen, relativ zu DIR |
| INCLUDE_DIRS | zusaetzliche Include-Pfade, relativ zu DIR |
| DEFINES | zusaetzliche Compile-Definitions |
| LINK_LIBRARIES | zusaetzliche Bibliotheken |
| PIO_SOURCES | optionale .pio-Dateien |
| CORE_EXCLUDE | Basisnamen von Kernquellen, die das Instrument durch eine eigene Variante ersetzt |
| CORE_MODULES | optionale Kernmodule: ui_panel, midi_reface |

Instrumente werden per Auto-Discovery ueber `instruments/*/instrument.cmake` gefunden. Die CMake-Option `PICOFACE_INSTRUMENTS_FILTER` baut nur die genannten Instrumente; leer bedeutet alle. Das Sammel-Target `all_instruments` baut alles. `pico_add_extra_outputs` erzeugt die Dateien bereits unter dem Instrumentnamen, ein Umbenennen entfaellt.

```bash
# Alle bauen
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Nur eines bauen
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceMD
cmake --build build
```

## 6. USB-Identitaet
| Instrument | USB-PID |
|---|---|
| PicoFaceYC | 0x1050 |
| PicoFaceCP | 0x1051 |
| PicoFaceRD | 0x1052 |
| PicoFaceJ6 | 0x1053 |
| PicoFaceMD | 0x1054 |
| PicoFaceSM | 0x1055 |

VID bleibt 0x2E8A. Vor der Zusammenfuehrung trugen alle sechs Firmwares dieselbe PID 0x104C, sodass Hosts sie nicht unterscheiden konnten; PicoFaceYC meldete sich zudem faelschlich als "PicoFaceDX". `core/src/usb_descriptors.c` ist jetzt ueber die Compile-Definitions `PICOFACE_INSTRUMENT_NAME` und `PICOFACE_USB_PID` parametriert, wodurch beide Fehler entfallen.

## 7. Verbliebene Divergenzen
Stand der Migration:

| Datei | betroffene Instrumente |
|---|---|
| pico_hw.cpp | PicoFaceYC, PicoFaceCP, PicoFaceRD |
| midi_input_usb.cpp | PicoFaceYC |
| veeprom.cpp | PicoFaceRD |
| veeprom.h | PicoFaceCP |
| pico_hw.h | alle (jeweils eigene Variante) |

Jede dieser Divergenzen ist ein Kandidat fuer eine spaetere Rueckfuehrung in den Kern. `tools/migrate.sh` meldet solche Dateien beim Lauf mit `DIVERGENT, kept locally`; byteidentische Dateien entfernt es automatisch.

## 8. Offene Arbeit
**Erledigt:**

- `core/src/picoface_main.cpp`: gemeinsames main() fuer beide Laufzeitmodelle.
- `core/src/ui/display.cpp`: u8g2-Fassade; flush() stoesst nur die zeilenweise Ausgabe an.
- Alle sechs Adapter. PicoFaceMD ist die Vorlage fuer das Standardmodell, PicoFaceYC fuer das Modell mit instrumenteigener UI.
- Alle sechs bauen aus einem gemeinsamen Configure-Lauf und tragen je eine eigene USB-PID.

| Instrument | Flash | RAM | PID | Original (Flash/RAM) |
| --- | --- | --- | --- | --- |
| PicoFaceYC | 135.332 | 46.708 | 0x1050 | 130.408 / 44.780 |
| PicoFaceCP | 4.435.156 | 177.028 | 0x1051 | 4.431.112 / 175.612 |
| PicoFaceRD | 5.308.352 | 34.852 | 0x1052 | 5.312.968 / 33.928 |
| PicoFaceJ6 | 105.424 | 20.676 | 0x1053 | 101.644 / 17.688 |
| PicoFaceMD | 100.544 | 270.112 | 0x1054 | 96.828 / 267.124 |
| PicoFaceSM | 97.720 | 23.272 | 0x1055 | 91.868 / 20.288 |

Der Aufschlag liegt bei 3 bis 6 KB Flash und rund 3 KB RAM je Instrument; PicoFaceRD ist rund 5 KB kleiner, weil sein frueherer rd_main.cpp entfaellt.

**Besonderheiten von PicoFaceRD:** RD nutzt core1 als RAM-residenten Voice-Worker und wechselt die Samplerate zur Laufzeit zwischen 20 und 32 kHz. Beides laeuft ueber die optionalen Hooks: `consumeSampleRateChange` laesst den Kern die Hardware-Rate erst umschalten, wenn die bereits in der DMA-Pipeline liegenden Puffer abgelaufen sind; `onAudioUnderrun` loest den Voice-Governor aus; `settingsSaveAllowed` verhindert einen Flash-Schreibvorgang, solange Stimmen klingen. Fuer die uebrigen Instrumente sind die Defaults dieser Hooks wirkungslos.

**Offen:**

1. Test auf echter Hardware. Bisher ist nur verifiziert, dass alle sechs Images bauen und die richtige USB-Identitaet tragen.
2. Umstellung der Frontpanel-Menues von PicoFaceYC und PicoFaceCP auf den InputState, damit auch sie das Standardmodell nutzen.
3. Rueckfuehrung der Divergenzen aus Abschnitt 7 in den Kern.
