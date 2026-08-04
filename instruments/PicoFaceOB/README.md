# PicoFaceOB

Oberheim OB-X emulation for the PicoFace platform. Die Klangerzeugung ist eine
Portierung von **OB-Xf** (https://github.com/surge-synthesizer/OB-Xf), dem
Nachfolger von OB-Xd.

## Lizenz — abweichend vom restlichen Repository

Das Monorepo steht unter MIT. **Dieses Instrument steht unter GPL-3.0-or-later**,
weil OB-Xf es tut. Konkret:

- `instruments/PicoFaceOB/` insgesamt, einschliesslich der Adapterdateien, die
  ich fuer diesen Port geschrieben habe: GPL-3.0-or-later, siehe `LICENSE`.
- Das gebaute `PicoFaceOB.uf2` ist damit ein GPL-3-Werk.
- Kern und die uebrigen sechs Instrumente bleiben MIT und sind nicht betroffen.
  MIT ist GPL-kompatibel, die Kombination ist zulaessig.
- Die Quellpflicht der GPL ist durch dieses oeffentliche Repository erfuellt.

Die Dateien unter `include/obxf/` sind Original-Quelltext von OB-Xf und tragen
unveraendert deren Copyright-Kopf. Jede portierte Datei hat zusaetzlich einen
Block `PORTED FOR PicoFaceOB`, der auflistet, was geaendert wurde.

## Was portiert ist

| Aus OB-Xf uebernommen | Zeilen |
|---|---|
| Voice, OscillatorBlock, Saw/Pulse/Triangle-Oszillatoren mit BLEP | ~1.100 |
| Filter (2- und 4-polig, Xpander-Modi) | 241 |
| ADSR-Huellkurven, LFO, Noise, Smoother, DelayLine | ~800 |
| BLEP-Tabellen | 1.086 |

Ersetzt durch eigenen Code: `Motherboard.h` und `SynthEngine.h` (776 + 579
Zeilen Desktop-Infrastruktur) durch `OB_Engine` — Stimmenvergabe, Parameter,
Renderschleife. Die Parameterbereiche sind aus `SynthEngine.h` uebernommen,
damit die Regelwege dieselben bleiben.

## Was nicht portiert ist

- **Modulationsmatrix** (`VoiceMatrix.h`, 785 Zeilen). Sie kostete pro Sample
  und Stimme das Sichern und Wiederherstellen von 13 Parametern.
- **Unisono, MPE, Panning, Patch-Bänke, Tempo-Sync, MIDI-Learn, OB-Xd-Import.**
- **Oversampling.** Upstream kann 2x, der Schalter steht dort ohnehin per
  Default auf aus.
- **Tuning-Tabellen.** Gleichstufige Stimmung, damit `tunedMidiNote()` nicht
  pro Sample eine Double-Rechnung macht.
- 32 Stimmen. Hier sind es sechs (`MAX_VOICES` in `include/obxf/ObxfPort.h`).

## Die drei Aenderungen, ohne die es nicht laeuft

Alle drei sind auf einem Desktop unsichtbar und auf einem Cortex-M33 fatal.

1. **19 unsuffixierte Fliesskomma-Literale** in `TriangleOsc.h`, `SawOsc.h` und
   `Lfo.h` (`0.5` statt `0.5f`). Jedes davon hebt seinen ganzen Ausdruck auf
   Double — mitten im Per-Sample-Pfad, in Software emuliert.
2. **`tan()` und `atan()`** im Filter, ebenfalls die Double-Varianten, je
   einmal pro Sample und Stimme. Ersetzt durch `ob_tan()` / `ob_atan()`.
3. **`getPitch()`** = `440 * exp(ln2/12 * i)`, dreimal pro Sample und Stimme.
   Ersetzt durch `ob_exp2()` (Exponentenfeld plus Polynom).

Nach 1–3 enthaelt kein Objekt dieses Instruments mehr einen Aufruf der
Double-Laufzeitbibliothek.

## Presets

Zwoelf Werkspatches aus `assets/installer/.../Patches` des Originals, umgesetzt
in `include/ob_presets.h`. Die .fxp-Dateien tragen ihre Parameter als benannte,
normierte Werte in einem eingebetteten XML-Block, die Umsetzung ist also eine
Namenszuordnung auf `ob_params.h`. Was dieser Port nicht hat — Unisono,
Panning, LFO 2, Modulationsmatrix, Velocity-Tracking — faellt weg, und die
LFO-Wellenform wird auf die naechste unserer fuenf festen Positionen gerundet.
Ein Patch, der stark daran haengt, klingt hier also nicht identisch.

Erreichbar unter Menu → Presets.

## Stand

**Erster Hardware-Lauf: es brummte.** Ursache war die neutrale Lage der
Oszillator-Grobstimmung. `Voice.h` fuettert die Oszillatoren mit
`midiNote - 93`; upstream gleicht das in `processOsc1Pitch()` aus, das den
normierten Parameter auf `val * 48` abbildet — die Mitte liegt also bei 24,
nicht bei 0. `OB_Engine` setzte `pitch1` gar nicht, es blieb auf seinem
deklarierten Default 0, und damit klang alles **zwei Oktaven zu tief**.
Behoben; `OB_OSC1_PITCH` ist jetzt ein eigener Parameter.

**Zweiter Lauf: es spielt, Peak 91 % bei 6 von 6 Stimmen.** Das sind rund
2.100 Zyklen je Stimme und Sample - viel mehr als die Rechnung erwarten liess.
Der Grund liess sich im Symbolverzeichnis nachsehen: `renderBlock()` lag zwar
im RAM, aber GCC hatte `Voice::ProcessSample` (3,6 KB) und
`OscillatorBlock::ProcessSample` (18 KB) nicht hineingezogen, sondern als
eigene Funktionen im Flash gelassen. Der gesamte DSP lief also weiter ueber
XIP - und 18 KB Code passen nicht in einen 16 KB grossen XIP-Cache, wenn sie
pro Sample sechsmal durchlaufen werden.

Beide sind jetzt mit `__not_in_flash_func()` markiert, ebenso die drei
Rauschgeneratoren. Dazu sind im 4-Pol-Filter zwei Divisionen je Sample und
Stimme entfallen, die rechnerisch schon dastanden: `1/(1+g)` ist `1 - lpc`,
und `g/(1+g)` ist `lpc`.

Kostet 22 KB mehr RAM (Code wandert aus dem Flash), Gesamtbild jetzt 68 KB
Flash-Text, 47,6 KB `.data`, 39,9 KB `.bss`.

**Dritter Lauf: Peak 53 % bei 6 von 6 Stimmen.** Der XIP-Cache war es also,
38 Prozentpunkte.

## Warum 44,1 kHz und nicht mehr Stimmen

Die freigewordene Reserve geht in die Samplerate, nicht in die Polyphonie.
Zwei Gruende, beide stehen im portierten Code:

- Die Resonanzkompensation des Filters ist um 44 kHz herum geschrieben
  (`sqrt(44000.f / sampleRate)` in `Filter.h`). Bei 44,1 kHz arbeitet der
  Filter an seinem eigenen Auslegungspunkt.
- Die Cutoff-Frequenz wird auf `sampleRate * 0.5 - 120` gedeckelt. Bei 32 kHz
  sind das 15,9 kHz — unterhalb der 19 kHz, die der Code sonst zulaesst. Der
  Filter kam also gar nicht an sein oberes Ende.

Erwartete Last: 53 % × 44100/32000 ≈ 73 %. Gemessen: **78 % Peak bei 6 von 6
Stimmen**, Klang stimmt. Die Alternative waere 8 Stimmen bei 32 kHz gewesen
(≈ 71 %) — beides zusammen geht nicht.

**Zur langen Nachhallzeit von Last und Stimmenzahl:** kein Fehler. Die
Huellkurvenzeiten der Werkspatches gehen durch `logsc(v, 8, 60000, 900)`,
also bis 60 Sekunden. "5 AM Pad" hat 45,7 s Release und 60 s Decay auf der
Amp-Huellkurve; solange haelt eine Stimme, und solange steht die Last. Bei
sechs Stimmen heisst das: wer mehr als sechs Noten innerhalb einer Release-
Phase spielt, klaut sich Stimmen. Die Vergabe nimmt dann die leiseste bereits
losgelassene.

Die Stellschrauben stehen an einer Zeile: `kSampleRate` in
`src/OB_Instrument.cpp`, `MAX_VOICES` in `include/obxf/ObxfPort.h`.
