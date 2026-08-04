# Architektur - PicoVintageSynthCollection

## 1. Ziel
Monorepo, das 6 bisher getrennte RP2350-Synthesizer-Firmwares vereint (PicoFaceYC, PicoFaceCP, PicoFaceRD, PicoFaceJ6, PicoFaceMD, PicoFaceSM); PicoFaceOB ist als siebtes im Monorepo selbst entstanden. Eine gemeinsame Basis - Audio-Pipeline, Hardwareanbindung, GUI, USB-MIDI, Persistenz - an die sich Instrumente nur andocken. Alle Instrumente entstehen aus einem Build und werden unter ihrem eigenen Namen als Binary veroeffentlicht.

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
│       ├── list_view.h
│       └── midi.h
└── src/
    ├── (Basisquellen)
    └── ui/
        ├── display.cpp
        └── (Modul ui_menu)
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
└── PicoFaceOB
(je mit instrument.cmake, src/, include/)
tools/
└── migrate.sh
docs/
.github/
└── workflows/
    └── build.yml
```

## 3. Eine Hardwareplattform, sieben Instrumente
Die Platine ist fuer alle sieben Instrumente dieselbe. Pin-Belegung und Flash-Timing liegen deshalb im Kern: `core/include/project_config.h` und `core/include/pico_hw.h`. In den sechs Ursprungs-Repositories war jede einzelne Pin-Definition bereits identisch; die Dateien unterschieden sich nur in Kommentaren, in einer zusaetzlichen QMI-Timing-Konstante fuer PicoFaceRD und in einem inline-Helfer. Die Kern-Fassungen sind die Vereinigung aller Varianten.

### Warum der Kern trotzdem keine Bibliothek ist

Der Kern veroeffentlicht weiterhin Listen absoluter Quellpfade statt einer STATIC-Library, jetzt aber aus zwei anderen Gruenden. Erstens ist `core/src/usb_descriptors.c` ueber die Compile-Definitions PICOFACE_INSTRUMENT_NAME und PICOFACE_USB_PID parametriert, die pro Target verschieden sind; die Datei muss also je Target uebersetzt werden. Zweitens ersetzen einzelne Instrumente Kernquellen durch eigene Varianten, und aus einer fertig gebauten Bibliothek laesst sich kein Member pro Target austauschen.

Daraus folgt weiterhin die Include-Reihenfolge `instruments/<NAME>/include` vor `core/include`, damit eine instrumenteigene Variante eines Headers gewinnt. Nach der Zusammenfuehrung betrifft das nur noch fuenf Header (siehe Abschnitt 7).

Gegenbeispiel: lib/audio, lib/encoder und lib/u8g2 bleiben STATIC und werden einmal fuer alle sieben Targets gebaut.

**Anmerkung zur Audio-Bibliothek:** lib/audio/src/audio_subsystem.cpp inkludierte urspruenglich project_config.h und las daraus PIN_I2S_DOUT und PIN_I2S_BCK. In den Altprojekten fiel das nicht auf, weil ein globales include_directories() den Instrument-Include-Pfad in jedes Target leakte. Die Bibliothek nutzt jetzt die Standardmakros PICO_AUDIO_I2S_DATA_PIN und PICO_AUDIO_I2S_CLOCK_PIN_BASE und kennt die Instrument-Konfiguration nicht mehr.

## 4. Der Andock-Contract
picoface::Instrument in core/include/picoface/instrument.h ist die einzige Schnittstelle zwischen Kern und Instrument.

| Gruppe | Methoden | CPU-Kern |
|---|---|---|
| Identitaet | name() | core0 |
| Lebenszyklus | init() / sampleRate() | core0 |
| Audio | render(int32_t* out, frames) | Producer-Kontext (core0), harte Echtzeit |
| Audio-Hooks (optional) | consumeSampleRateChange, onAudioUnderrun, settingsSaveAllowed | Producer-Kontext bzw. core0 |
| MIDI | noteOn, noteOff, controlChange, programChange, pitchBend, sysEx | core0 |
| MIDI (optional) | realtime, midiActivity | core0 |
| GUI | uiInit(display), uiTick(display, input) | core0 |
| Persistenz | settingsVersion, settingsSize, settingsSave, settingsLoad | core0 |

Der Kern ruft init(), fragt danach sampleRate() ab und initialisiert damit den Audio-Pool. render() erhaelt ein int32-Wort pro Frame (gepacktes Stereo), wird blockweise aus der Producer-Schleife aufgerufen und darf nicht blockieren, nicht allokieren und kein printf verwenden; ein Instrument darf intern core1 als Worker nutzen, wie es PicoFaceRD tut. Ein Instrument registriert sich mit PICOFACE_REGISTER_INSTRUMENT(Typ).

## 4a. Das Laufzeitmodell

**Alle sieben Instrumente laufen im selben Modell:** core0 macht Audio, USB, MIDI und GUI, der Kern pollt die Encoder in einen InputState und ruft `uiTick()`. core1 gehoert dem Instrument - PicoFaceRD nutzt ihn als Voice-Worker, die uebrigen sechs lassen ihn liegen.

Es gab bis zur Umstellung von PicoFaceYC und PicoFaceCP ein zweites Modell, in dem ein Instrument ueber `ownsUserInterface()` die gesamte Bedienoberflaeche auf core1 uebernahm. Der Kern startete core1 dann selbst, initialisierte weder Display noch Encoder und rief `pumpCrossCore()` statt `uiTick()`; fuer den Flash-Zugriff lieferte das Instrument ein Paar Park-Hooks. Mit dem letzten Nutzer sind auch die fuenf Methoden aus `picoface::Instrument` und der zugehoerige Zweig in `picoface_main.cpp` verschwunden. Ein Instrument, das core1 braucht, startet ihn wie PicoFaceRD selbst aus seinem Adapter.

### Die Umstellung von PicoFaceYC und PicoFaceCP

Beide liefen im zweiten Modell. Zwei Dinge daran waren nicht offensichtlich:

- `pico_UserInterfaceFrontPanel()` enthielt `for(;;)` und kehrte nie zurueck - die Funktion *war* die core1-Schleife, nicht ein Menue, das gelegentlich blockiert. An ihre Stelle treten `YC_Ui` und `CP_Ui`: Zustandsautomaten, die pro `uiTick()` einen Durchlauf machen. Das Listen-Widget dahinter liegt als `picoface::ui::ListView` im Kernmodul ui_menu und wird von beiden genutzt.
- `ipc.h` schob seine Pakete mit `multicore_fifo_push_blocking` ueber die SIO-FIFO. Laeuft alles auf core0, blockiert dieser Push ohne Konsument fuer immer. Ersatz ist je ein Same-Core-Ring nach dem Vorbild von `instruments/PicoFaceMD/include/md_ipc.h`, den `render()` zu Blockbeginn leert. Ein Zwischenstand war nicht moeglich: ein Ring ohne Speicherbarrieren waere ueber Kerngrenzen hinweg unsicher gewesen, solange die UI noch auf core1 sass. IPC, UI und Adapter mussten zusammen umgestellt werden.

Damit entfallen fuer beide `ownsUserInterface`, `runUserInterface`, `pumpCrossCore`, die Flash-Hooks und der Flash-Park-Handshake; die MIDI-Methoden der Adapter leiten jetzt wirklich weiter, statt leer zu sein. YCs Watchdog bleibt und wird aus `render()` statt aus `pumpCrossCore()` gefuettert; CP hatte nie einen.

Beide schreiben ihren veeprom-Satz weiterhin selbst und melden daher `settingsSize() == 0` - die Debounce-Logik in `settings_task()` ist feiner als die des Kerns und kennt Werte, die bewusst nicht persistiert werden. Der Schreibvorgang laeuft jetzt wie bei den anderen vier auf core0 zwischen zwei Audiobloecken.

CP war der haertere Fall: acht Panel-Screens und 22 lokale Variablen in der Schleife. Die Variablen sind Member von `CP_Ui` geworden. Sie bleiben noetig, weil eine Bedienung sofort auf dem Schirm stehen muss, waehrend der Ring die Engine erst zum naechsten Block erreicht; `refresh()` holt umgekehrt ein, was hinter dem Panel vorbei passiert ist - MIDI, ein Preset, ein SysEx-Parameter.

## 5. Build-System
`picoface_add_instrument()` in `cmake/PicoFaceInstrument.cmake` erzeugt pro Instrument ein vollstaendiges Firmware-Target. Alle Einstellungen sind target-lokal (`target_compile_definitions` / `target_compile_options` statt globaler `add_compile_options`), weil sieben Targets mit widersprechenden Defines koexistieren muessen.

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
| CORE_MODULES | optionale Kernmodule; derzeit nur ui_menu |

ui_menu enthaelt das nicht blockierende Listen-Widget `picoface::ui::ListView` fuer Instrumente, die aus `uiTick()` heraus zeichnen. Sein Vorgaenger ui_panel hielt die blockierenden Widgets, die ihre Encoder selbst pollten, dazu die reface-CP-MIDI-Schicht und deren Persistenz; mit der Umstellung von PicoFaceCP hatte er keinen Nutzer mehr. Die CP-spezifischen Teile liegen jetzt unter `instruments/PicoFaceCP`, der Rest ist geloescht. Seither enthaelt der Kern keinen instrumentspezifischen Code mehr.

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
| PicoFaceOB | 0x1056 |

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

Der Empfang laeuft interruptgesteuert in einen lock-freien Ring von 256 Byte. Bei 31250 Baud trifft alle 320 Mikrosekunden ein Byte ein; die 32 Byte tiefe Hardware-FIFO gaebe nur rund 10 ms Reserve. Das reichte schon fuer die blockierenden Menues nicht, die es inzwischen nicht mehr gibt, und deckt heute den Flash-Schreibvorgang der Persistenz ab. Der Interrupt ist RAM-resident und bewegt nur Bytes, geparst wird in process(). Die Interruptprioritaet liegt unter der Audio-DMA und ueber USB.

Der Parser beherrscht Running Status, SysEx bis 256 Byte und laesst Realtime-Bytes mitten in einer Nachricht unbeschadet durch. Note-On mit Velocity 0 wird als Note-Off behandelt.

### Einbau

`MIDISerial::process()` laeuft fuer alle sieben Instrumente in `picoface_main.cpp`, direkt neben `MIDIInputUSB::process()`.

Realtime-Bytes und die reine Empfangsaktivitaet reicht der Kern ueber die optionalen Methoden `realtime()` und `midiActivity()` durch. Beide sind fuer die Active-Sensing-Ueberwachung der reface-Schicht von YC und CP noetig - deren 350-ms-Timeout schaltet bei Ausbleiben von 0xFE alle Stimmen ab. Die Defaults sind leer; die vier uebrigen Instrumente ignorieren beides.

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


## 6b. PicoFaceOB: portierte Fremd-Engine und abweichende Lizenz

PicoFaceOB ist das erste Instrument, dessen Klangerzeugung nicht aus diesem
Projekt stammt: sie ist aus [OB-Xf](https://github.com/surge-synthesizer/OB-Xf)
portiert. Zwei Dinge unterscheiden es deshalb von den uebrigen sechs.

**Lizenz.** OB-Xf steht unter GPL-3.0-or-later. `instruments/PicoFaceOB/` steht
daher ebenfalls unter GPL-3, und das gebaute `PicoFaceOB.uf2` ist ein
GPL-3-Werk. Kern und die anderen sechs Instrumente bleiben MIT; MIT ist
GPL-kompatibel, die Kombination ist zulaessig, und die Trennung laeuft genau
entlang der Instrumentgrenze, die das Monorepo ohnehin zieht. Einzelheiten in
`instruments/PicoFaceOB/README.md`.

**Was die Portierung ausmacht.** Die Engine ist header-only und war
erfreulich JUCE-arm; die eigentliche Arbeit lag woanders. Drei Fundstellen
waren auf dem Desktop unsichtbar und haetten auf dem M33 alles erledigt:

1. 19 unsuffixierte Fliesskomma-Literale (`0.5` statt `0.5f`) in den
   Oszillatoren. Jedes hebt seinen Ausdruck auf Double - in Software emuliert,
   im Per-Sample-Pfad.
2. `tan()` und `atan()` im Filter, ebenfalls die Double-Varianten, je einmal
   pro Sample und Stimme.
3. `getPitch()` = `440 * exp(ln2/12 * i)`, dreimal pro Sample und Stimme.

Ersetzt durch Float-Approximationen in `include/obxf/ObxfPort.h`, nach dem
Vorbild von `instruments/PicoFaceCP/effects/dsp_fastmath.h`. Danach ruft kein
Objekt dieses Instruments mehr die Double-Laufzeitbibliothek. Nicht portiert
sind Modulationsmatrix, Unisono, MPE, Patch-Bänke und Oversampling; 32 Stimmen
sind sechs geworden.

**Der teuerste Posten war aber keiner davon, sondern der XIP-Cache.**
`OscillatorBlock::ProcessSample` sind 18 KB Code, die pro Sample sechsmal
durchlaufen werden - das passt nicht in einen 16 KB grossen Cache. Erst
`__not_in_flash_func()` auf dieser und `Voice::ProcessSample` brachte den Peak
von 91 % auf 53 % bei 32 kHz. Die freigewordene Reserve ging in die Samplerate:
44,1 kHz ist der Auslegungspunkt des Filters (`sqrt(44000 / sampleRate)`) und
hebt den Cutoff-Deckel von 15,9 auf 22 kHz. Endstand **78 % Peak bei 6 von 6
Stimmen**, auf der Hardware bestaetigt.

## 7. Verbliebene Divergenzen

Nach der Zusammenfuehrung von project_config.h, pico_hw.h und pico_hw.cpp in den Kern bleibt Folgendes instrumentspezifisch.

**Ersetzte Kernquellen**

| Datei | Instrumente |
|---|---|
| midi_input_usb.cpp | PicoFaceYC |
| veeprom.cpp | PicoFaceRD |

YCs `midi_input_usb.cpp` unterscheidet sich in genau einem Punkt: es macht aus einem Note-On mit Velocity 0 ein Note-Off. Der DIN-Parser des Kerns tut das ohnehin, YCs `RefaceMidi::onNoteOn()` nicht - dort gehoert die Behandlung hin, dann kann die Datei entfallen.

`settings.cpp`, `midi_reface.cpp` und `pico_frontpanel.cpp` standen hier bis zur Umstellung von YC. Die ersten beiden liegen weiterhin unter `instruments/PicoFaceYC/src/`, ersetzen aber nichts mehr, weil es im Kern keine Quellen dieser Namen mehr gibt. `pico_frontpanel.cpp` ist ersatzlos entfallen.

**Verdeckte Header**

Keine mehr. `midi_reface.h` und `settings.h` waren CP-Fassungen im Kern, die von den gleichnamigen YC-Dateien verdeckt wurden - beide sind mit der CP-Umstellung nach `instruments/PicoFaceCP/include/` gewandert. `pico_frontpanel.h` und `pico_userinterface.h` sind mit den blockierenden Menues verschwunden. CPs `veeprom.h` unterschied sich von der Kernfassung in genau einer Kommentarzeile und ist geloescht.

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
- Alle sieben Adapter, alle im Standardmodell. PicoFaceMD ist die Vorlage.
- Alle sieben bauen aus einem gemeinsamen Configure-Lauf und tragen je eine eigene USB-PID.
- Sechs davon auf der Hardware getestet und lauffaehig, PicoFaceRD einschliesslich der 480-MHz-Taktung. PicoFaceOB ist neu und dort noch ungetestet (Abschnitt 6b).
- PicoFaceYC und PicoFaceCP auf das Standardmodell umgestellt (Abschnitt 4a) und in dieser Fassung auf der Hardware bestaetigt. Damit ist das zweite Laufzeitmodell ersatzlos aus dem Kern entfernt.

| Instrument | Flash | RAM | PID | Original (Flash/RAM) |
|---|---|---|---|---|
| PicoFaceYC | 135.224 | 47.824 | 0x1050 | 130.408 / 44.780 |
| PicoFaceCP | 4.431.496 | 178.104 | 0x1051 | 4.431.112 / 175.612 |
| PicoFaceRD | 5.318.096 | 35.420 | 0x1052 | 5.312.968 / 33.928 |
| PicoFaceJ6 | 104.056 | 19.188 | 0x1053 | 101.644 / 17.688 |
| PicoFaceMD | 99.168 | 268.624 | 0x1054 | 96.828 / 267.124 |
| PicoFaceSM | 96.232 | 21.784 | 0x1055 | 91.868 / 20.288 |
| PicoFaceOB | 131.724 | 42.248 | 0x1056 | - (neu) |

Gemessen mit `arm-none-eabi-size` (text / bss). PicoFaceOBs RAM sind zu 32 KB
die sechs Stimmen des OB-Xf-Voice-Objekts, gut 5,3 KB je Stimme; dazu kommen
47,6 KB `.data`, weil dort der RAM-residente Renderpfad und die BLEP-Tabellen
liegen (Abschnitt 6b).

Der Aufschlag gegenueber den Einzelprojekten liegt bei 2 bis 4 KB Flash und rund 940 Byte RAM je Instrument - im Wesentlichen die vtable der Instrument-Schnittstelle und die zusaetzliche Indirektion.

Die Umstellung von YC und CP samt dem Wegfall des zweiten Laufzeitmodells kostet YC 1.300 und CP 5.012 Byte Flash weniger als vorher, bei 552 bzw. 512 Byte mehr RAM: der Same-Core-Ring belegt je 1032 Byte, dafuer entfallen die instrumenteigenen Encoder-, Button-, USB-MIDI- und u8g2-Objekte. J6, MD und SM verlieren rund 500 Byte Flash - der `owns_ui`-Zweig und die fuenf entfallenen virtuellen Methoden wiegen mehr als die zwei neuen MIDI-Methoden; nur RD liegt 528 Byte hoeher, weil hinter seinem grossen Sampleblock das Ausrichtungs-Padding anders faellt.

**Besonderheiten von PicoFaceRD:** RD nutzt core1 als RAM-residenten Voice-Worker und wechselt die Samplerate zur Laufzeit zwischen 20 und 32 kHz. Beides laeuft ueber die optionalen Hooks: `consumeSampleRateChange` laesst den Kern die Hardware-Rate erst umschalten, wenn die bereits in der DMA-Pipeline liegenden Puffer abgelaufen sind; `onAudioUnderrun` loest den Voice-Governor aus; `settingsSaveAllowed` verhindert einen Flash-Schreibvorgang, solange Stimmen klingen. Fuer die uebrigen Instrumente sind die Defaults dieser Hooks wirkungslos.

**Offen:**

1. Hardware-Test des DIN-MIDI (Abschnitt 6a).
2. YCs `midi_input_usb.cpp`, die letzte ersetzte Kernquelle neben RDs `veeprom.cpp` (Abschnitt 7).

