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
        └── (Module ui_panel und ui_menu)
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

## 3. Eine Hardwareplattform, sechs Instrumente
Die Platine ist fuer alle sechs Instrumente dieselbe. Pin-Belegung und Flash-Timing liegen deshalb im Kern: `core/include/project_config.h` und `core/include/pico_hw.h`. In den sechs Ursprungs-Repositories war jede einzelne Pin-Definition bereits identisch; die Dateien unterschieden sich nur in Kommentaren, in einer zusaetzlichen QMI-Timing-Konstante fuer PicoFaceRD und in einem inline-Helfer. Die Kern-Fassungen sind die Vereinigung aller Varianten.

### Warum der Kern trotzdem keine Bibliothek ist

Der Kern veroeffentlicht weiterhin Listen absoluter Quellpfade statt einer STATIC-Library, jetzt aber aus zwei anderen Gruenden. Erstens ist `core/src/usb_descriptors.c` ueber die Compile-Definitions PICOFACE_INSTRUMENT_NAME und PICOFACE_USB_PID parametriert, die pro Target verschieden sind; die Datei muss also je Target uebersetzt werden. Zweitens ersetzen einzelne Instrumente Kernquellen durch eigene Varianten, und aus einer fertig gebauten Bibliothek laesst sich kein Member pro Target austauschen.

Daraus folgt weiterhin die Include-Reihenfolge `instruments/<NAME>/include` vor `core/include`, damit eine instrumenteigene Variante eines Headers gewinnt. Nach der Zusammenfuehrung betrifft das nur noch fuenf Header (siehe Abschnitt 7).

Gegenbeispiel: lib/audio, lib/encoder und lib/u8g2 bleiben STATIC und werden einmal fuer alle sechs Targets gebaut.

**Anmerkung zur Audio-Bibliothek:** lib/audio/src/audio_subsystem.cpp inkludierte urspruenglich project_config.h und las daraus PIN_I2S_DOUT und PIN_I2S_BCK. In den Altprojekten fiel das nicht auf, weil ein globales include_directories() den Instrument-Include-Pfad in jedes Target leakte. Die Bibliothek nutzt jetzt die Standardmakros PICO_AUDIO_I2S_DATA_PIN und PICO_AUDIO_I2S_CLOCK_PIN_BASE und kennt die Instrument-Konfiguration nicht mehr.

## 4. Der Andock-Contract
picoface::Instrument in core/include/picoface/instrument.h ist die einzige Schnittstelle zwischen Kern und Instrument.

| Gruppe | Methoden | CPU-Kern |
|---|---|---|
| Identitaet | name() | core0 |
| Lebenszyklus | init() / sampleRate() | core0 |
| Audio | render(int32_t* out, frames) | Producer-Kontext (derzeit core0), harte Echtzeit |
| Audio-Hooks (optional) | consumeSampleRateChange, onAudioUnderrun, settingsSaveAllowed | Producer-Kontext bzw. core0 |
| MIDI | noteOn, noteOff, controlChange, programChange, pitchBend, sysEx | core0 |
| MIDI (optional) | realtime, midiActivity | core0 |
| GUI | uiInit(display), uiTick(display, input) | core0 |
| Persistenz | settingsVersion, settingsSize, settingsSave, settingsLoad | core0 |

Der Kern ruft init(), fragt danach sampleRate() ab und initialisiert damit den Audio-Pool. render() erhaelt ein int32-Wort pro Frame (gepacktes Stereo), wird blockweise aus der Producer-Schleife aufgerufen und darf nicht blockieren, nicht allokieren und kein printf verwenden; ein Instrument darf intern core1 als Worker nutzen, wie es PicoFaceRD tut. Ein Instrument registriert sich mit PICOFACE_REGISTER_INSTRUMENT(Typ).

## 4a. Zwei Laufzeitmodelle

Die sechs Instrumente laufen in zwei verschiedenen Modellen. Der Kern unterstuetzt beide; welches gilt, entscheidet das Instrument ueber `ownsUserInterface()`.

| Modell | Instrumente | core0 | core1 | Bedienung |
| --- | --- | --- | --- | --- |
| Standard | PicoFaceMD, PicoFaceSM, PicoFaceJ6, PicoFaceRD, PicoFaceYC | Audio, USB, MIDI, GUI | ungenutzt (RD: Voice-Worker) | Kern pollt die Encoder in einen InputState und ruft uiTick() |
| Instrument besitzt die UI | PicoFaceCP | nur Audio | USB, MIDI, Encoder, Frontpanel-Menue | Instrument fuehrt seine eigene Schleife in runUserInterface() |

Im zweiten Modell initialisiert der Kern weder Display noch Encoder, verdrahtet kein USB-MIDI und ruft weder uiInit() noch uiTick(). Er startet core1 mit runUserInterface() - zwingend vor init_audio(), weil der SDK-Handshake die SIO-FIFO benutzt - und ruft in jedem Durchlauf der Producer-Schleife pumpCrossCore(), womit das Instrument seinen Cross-Core-Kanal leert. Findet der Producer keinen freien Puffer, geht core0 per `__wfe()` schlafen statt zu drehen.

PicoFaceCP schreibt auch selbst in den veeprom und meldet daher `settingsSize() == 0`. Weil sein Menue auf core1 laeuft, muss vor einem Flash-Zugriff core0 in RAM-residentem Code geparkt werden; das Paar dafuer liefert es ueber `flashLockHook()` und `flashUnlockHook()`.

### Die Umstellung von PicoFaceYC

PicoFaceYC lief bis zur Migration im zweiten Modell und nutzt jetzt das Standardmodell. Zwei Dinge daran waren nicht offensichtlich:

- `pico_UserInterfaceFrontPanel()` enthielt `for(;;)` und kehrte nie zurueck - die Funktion *war* die core1-Schleife, nicht ein Menue, das gelegentlich blockiert. An ihre Stelle tritt `YC_Ui` (instruments/PicoFaceYC/src/YC_Ui.cpp): ein Zustandsautomat ueber Panel, Menu, System, About und CPU Load, der pro `uiTick()` einen Durchlauf macht. Das Listen-Widget dahinter liegt als `picoface::ui::ListView` im Kernmodul ui_menu, weil CP es bei seiner Umstellung mitnutzen wird.
- `ipc.h` schob seine Pakete mit `multicore_fifo_push_blocking` ueber die SIO-FIFO. Laeuft alles auf core0, blockiert dieser Push ohne Konsument fuer immer. Ersatz ist ein Same-Core-Ring nach dem Vorbild von `instruments/PicoFaceMD/include/md_ipc.h`, den `render()` zu Blockbeginn leert. Ein Zwischenstand war hier nicht moeglich: ein Ring ohne Speicherbarrieren waere ueber Kerngrenzen hinweg unsicher gewesen, solange die UI noch auf core1 sass. IPC, UI und Adapter mussten zusammen umgestellt werden.

Damit entfallen fuer YC `ownsUserInterface`, `runUserInterface`, `pumpCrossCore`, beide Flash-Hooks und der Flash-Park-Handshake; die MIDI-Methoden des Adapters leiten jetzt wirklich weiter, statt leer zu sein. Der Watchdog bleibt und wird aus `render()` statt aus `pumpCrossCore()` gefuettert.

Die einheitliche GUI-Schnittstelle ist damit fuer fuenf der sechs Instrumente erreicht; die Umstellung des blockierenden Frontpanel-Menues von CP auf den InputState bleibt offen.

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
| CORE_MODULES | optionale Kernmodule: ui_panel, ui_menu, midi_reface |

ui_panel und ui_menu sind Alternativen, keine Schichten: ui_panel bringt die blockierenden Widgets mit, die ihre Encoder selbst pollen (nur noch CP), ui_menu die nicht blockierenden fuer Instrumente, die aus `uiTick()` heraus zeichnen.

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

## 6a. MIDI-Transporte

MIDI kommt ueber zwei Wege herein und geht ueber zwei Wege hinaus: USB und DIN. Die Callback-Signaturen von MIDISerial sind absichtlich identisch mit denen von MIDIInputUSB, sodass beide Transporte in dieselben Dispatch-Funktionen muenden - ein Instrument sieht nicht, auf welchem Draht ein Ereignis ankam.

### Hardware

| Signal | GPIO | Peripherie |
|---|---|---|
| MIDI RX | 5 | uart1 |
| MIDI TX | 4 | uart1 |
| stdio | 0 / 1 | uart0 |

31250 Baud, 8N1, Optokoppler auf der Platine. stdio liegt bewusst auf uart0, damit sich beide nie in die Quere kommen.

### Empfang

Der Empfang laeuft interruptgesteuert in einen lock-freien Ring von 256 Byte. Bei 31250 Baud trifft alle 320 Mikrosekunden ein Byte ein; die 32 Byte tiefe Hardware-FIFO gaebe nur rund 10 ms Reserve, was das blockierende Menue von PicoFaceCP ueberschreiten kann. Der Interrupt ist RAM-resident und bewegt nur Bytes, geparst wird in process(). Die Interruptprioritaet liegt unter der Audio-DMA und ueber USB.

Der Parser beherrscht Running Status, SysEx bis 256 Byte und laesst Realtime-Bytes mitten in einer Nachricht unbeschadet durch. Note-On mit Velocity 0 wird als Note-Off behandelt.

### Einbau je Laufzeitmodell

| Modell | Instrumente | Wo process() laeuft |
|---|---|---|
| Standard | MD, SM, J6, RD, YC | picoface_main.cpp, neben MIDIInputUSB::process() |
| Instrument besitzt die UI | CP | ui_poll_usb() im Adapter, weil MIDI dort auf core1 liegt |

Realtime-Bytes und die reine Empfangsaktivitaet reicht der Kern ueber die optionalen Methoden `realtime()` und `midiActivity()` durch. Beide sind fuer die Active-Sensing-Ueberwachung der reface-Schicht noetig - deren 350-ms-Timeout schaltet bei Ausbleiben von 0xFE alle Stimmen ab. Die Defaults sind leer; die vier uebrigen Instrumente ignorieren beides.

### Senden

| Instrumente | Was gesendet wird |
|---|---|
| YC, CP | Panel-Aenderungen als CC und SysEx-Antworten; laeuft ueber RefaceMidi::txBytes(), das jetzt zusaetzlich auf den UART schreibt |
| MD, J6 | Panel-Aenderungen als CC aus der Parametertabelle; jeder Eintrag traegt seine CC-Nummer, 0xFF bedeutet keine |
| SM, RD | Panel-Aenderungen als CC aus einer eigens festgelegten Tabelle, siehe unten |

Gesendet wird nur aus dem Encoder-Pfad. Ein Wert, der ueber MIDI hereinkam, landet in onMidiParam() und nimmt diesen Weg nicht, sodass keine Rueckkopplung entstehen kann. Als Sendekanal dient der Empfangskanal; steht dieser auf Omni, faellt der Sendekanal auf Kanal 1 zurueck.

### CC-Belegung fuer PicoFaceSM und PicoFaceRD

Anders als MD und J6 brachten diese beiden keine Parameter-zu-CC-Zuordnung mit: die Solina ist im Original rein elektromechanisch, und der RD beantwortet nur Sustain und ein paar Mode-Messages. Die folgenden Tabellen sind deshalb eine Festlegung dieses Projekts. Sie liegen in `instruments/PicoFaceSM/include/sm_cc_map.h` und `instruments/PicoFaceRD/include/rd_cc_map.h` und gelten fuer **beide** Richtungen - Senden und Empfangen greifen auf dieselbe Tabelle zu und koennen daher nicht auseinanderlaufen.

Leitlinien: Standard-Controller dort, wo die Funktion wirklich passt (72 Release, 73 Attack, 74 Brightness); der General-MIDI-Effektblock fuer die Modulationssektionen (92 Tremolo, 93 Chorus, 94 Detune, 95 Phaser); alles Uebrige aus dem undefinierten Bereich. CC 7, 64 und 120/121/123 bleiben bewusst frei - die verarbeiten die Engines bereits auf eigenem Weg, ein zweiter Pfad zum selben Wert waere mehrdeutig.

**PicoFaceRD**

| CC | Parameter | | CC | Parameter |
|---|---|---|---|---|
| 92 | Tremolo an | | 105 | Tremolo Depth |
| 93 | Chorus an | | 106 | Bass |
| 94 | Master Tune | | 107 | Treble |
| 95 | Phaser an | | 108 | Volume |
| 102 | Chorus Rate | | 109 | DAC-Filter an |
| 103 | Chorus Depth | | 110 | Phaser Rate |
| 104 | Tremolo Rate | | 111 | Phaser Depth |

Voice Mode bleibt ohne CC: der Wert ist eine Aufzaehlung, keine 0..127-Groesse.

**PicoFaceSM**

| CC | Parameter | | CC | Parameter |
|---|---|---|---|---|
| 3 | Shaper | | 108 | Bass Volume |
| 72 | Sustain (Release) | | 109 | Volume |
| 73 | Crescendo (Attack) | | 110 | Tremolo Rate |
| 74 | Tone Lowpass | | 111 | Chorus Rate |
| 92 | Tremolo Depth | | 112 | Chorus Depth |
| 93 | Ensemble | | 113 | Ensemble Tone |
| 94 | Tune | | 114 | Ensemble Width |
| 95 | Phaser | | 115 | Phaser Rate |
| 102..107 | Register Contrabass, Cello, Viola, Violin, Trumpet, Horn | | 116 | Phaser Color |
| | | | 117 | Tone Highpass |
| | | | 118 | Tone Shelf |
| | | | 119 | Formant |


## 7. Verbliebene Divergenzen

Nach der Zusammenfuehrung von project_config.h, pico_hw.h und pico_hw.cpp in den Kern bleibt Folgendes instrumentspezifisch.

**Ersetzte Kernquellen**

| Datei | Instrumente |
|---|---|
| midi_input_usb.cpp | PicoFaceYC |
| veeprom.cpp | PicoFaceRD |

YCs `midi_input_usb.cpp` unterscheidet sich in genau einem Punkt: es macht aus einem Note-On mit Velocity 0 ein Note-Off. Der DIN-Parser des Kerns tut das ohnehin, YCs `RefaceMidi::onNoteOn()` nicht - dort gehoert die Behandlung hin, dann kann die Datei entfallen.

`settings.cpp`, `midi_reface.cpp` und `pico_frontpanel.cpp` standen hier bis zur Umstellung von YC. Die ersten beiden liegen weiterhin unter `instruments/PicoFaceYC/src/`, ersetzen aber nichts mehr: YC fordert weder ui_panel noch midi_reface an, also gibt es keine Kernquelle gleichen Namens im Target. `pico_frontpanel.cpp` ist ersatzlos entfallen.

**Verdeckte Header**

| Datei | Instrument |
|---|---|
| midi_reface.h, settings.h | PicoFaceYC |
| veeprom.h | PicoFaceCP |

`pico_frontpanel.h` und `pico_userinterface.h` sind mit dem blockierenden Frontpanel aus YC verschwunden. Die verbliebenen zwei sind gleichnamige, aber inhaltlich voellig andere Dateien: YCs `RefaceMidi` ist eine eigene Klasse fuer die YC-Parameter, nicht die CP-Fassung des Kerns.

### Per-Instrument-Defines

Was frueher als eigene pico_hw.cpp vorlag, ist jetzt eine Handvoll Compile-Definitions in der jeweiligen instrument.cmake.

| Define | Instrumente | Wirkung |
|---|---|---|
| PICO_USE_SW_SPIN_LOCKS=1 | YC, J6, MD, SM | Software-Spinlocks statt Hardware |
| PICO_STACK_SIZE, PICO_CORE1_STACK_SIZE = 0x1000 | YC, CP, RD | Stacks in die Scratch-Baenke |
| TARGET_RP2350=1 | RD, J6, MD, SM | wirkungslos, da das SDK ohnehin PICO_BUILD definiert; nur der Vollstaendigkeit halber uebernommen |
| RD_CLOCK_504=1 | RD | schaltet den 480-MHz-Zweig frei |
| PICOFACE_SYS_CLOCK_HZ, PICOFACE_QMI_M0_TIMING_TARGET | RD | Taktziel und passendes Flash-Timing; muessen zusammen geaendert werden |

Diese Defines sind bewusst nicht im Helper vereinheitlicht, sondern je Instrument gesetzt wie in den Ursprungsprojekten. Spinlock-Implementierung und Stack-Layout eines Multicore-Audiobuilds ohne Geraet umzustellen waere leichtfertig.

`tools/migrate.sh` meldet abweichende Dateien mit `DIVERGENT, kept locally`; project_config.h, pico_hw.h und usb_descriptors.c entfernt es unbedingt, weil die Kern-Fassung massgeblich ist.

## 8. Offene Arbeit
**Erledigt:**

- `core/src/picoface_main.cpp`: gemeinsames main() fuer beide Laufzeitmodelle.
- `core/src/ui/display.cpp`: u8g2-Fassade; flush() stoesst nur die zeilenweise Ausgabe an.
- Alle sechs Adapter. PicoFaceMD ist die Vorlage fuer das Standardmodell, PicoFaceCP das letzte mit instrumenteigener UI.
- Alle sechs bauen aus einem gemeinsamen Configure-Lauf und tragen je eine eigene USB-PID.
- Alle sechs auf der Hardware getestet und lauffaehig, PicoFaceRD einschliesslich der 480-MHz-Taktung.
- PicoFaceYC auf das Standardmodell umgestellt (Abschnitt 4a). Auf der Hardware noch nicht nachgemessen.

| Instrument | Flash | RAM | PID | Original (Flash/RAM) |
|---|---|---|---|---|
| PicoFaceYC | 135.840 | 47.828 | 0x1050 | 130.408 / 44.780 |
| PicoFaceCP | 4.436.620 | 177.592 | 0x1051 | 4.431.112 / 175.612 |
| PicoFaceRD | 5.318.312 | 35.420 | 0x1052 | 5.312.968 / 33.928 |
| PicoFaceJ6 | 104.680 | 19.192 | 0x1053 | 101.644 / 17.688 |
| PicoFaceMD | 99.792 | 268.628 | 0x1054 | 96.828 / 267.124 |
| PicoFaceSM | 96.856 | 21.788 | 0x1055 | 91.868 / 20.288 |

Gemessen mit `arm-none-eabi-size` (text / bss).

Der Aufschlag gegenueber den Einzelprojekten liegt bei 2 bis 4 KB Flash und rund 940 Byte RAM je Instrument - im Wesentlichen die vtable der Instrument-Schnittstelle und die zusaetzliche Indirektion.

Die Umstellung von YC kostet 684 Byte Flash weniger und 556 Byte RAM mehr als die vorige Fassung: der Same-Core-Ring belegt 1032 Byte, dafuer entfallen die instrumenteigenen Encoder-, Button-, USB-MIDI- und u8g2-Objekte. Die uebrigen fuenf wachsen um 112 bis 744 Byte Flash - die beiden neuen MIDI-Methoden der Schnittstelle samt Verdrahtung im Kern; bei RD und SM kommt Ausrichtungs-Padding hinter den grossen Datenbloecken dazu.

**Besonderheiten von PicoFaceRD:** RD nutzt core1 als RAM-residenten Voice-Worker und wechselt die Samplerate zur Laufzeit zwischen 20 und 32 kHz. Beides laeuft ueber die optionalen Hooks: `consumeSampleRateChange` laesst den Kern die Hardware-Rate erst umschalten, wenn die bereits in der DMA-Pipeline liegenden Puffer abgelaufen sind; `onAudioUnderrun` loest den Voice-Governor aus; `settingsSaveAllowed` verhindert einen Flash-Schreibvorgang, solange Stimmen klingen. Fuer die uebrigen Instrumente sind die Defaults dieser Hooks wirkungslos.

**Offen:**

1. Umstellung des Frontpanel-Menues von PicoFaceCP auf den InputState, damit auch es das Standardmodell nutzt. Der deutlich haertere Fall: 8 Screens und 22 lokale Zustandsvariablen, waehrend YC nur 3 Screens hatte und ohnehin schon an `YC_Controller` delegierte. Das Listen-Widget aus ui_menu steht bereit.
2. Rueckfuehrung der Divergenzen aus Abschnitt 7 in den Kern.
3. Hardware-Test des DIN-MIDI (Abschnitt 6a) und des umgestellten PicoFaceYC. Fuer YC ist der Blick auf System -> CPU Load der entscheidende: USB, MIDI und UI liegen jetzt auf dem Audio-Kern. Unter etwa 60 % Peak ist das tragfaehig.

