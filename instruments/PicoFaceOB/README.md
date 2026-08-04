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

Die CPU-Last ist noch nicht eingeordnet. Zwei Dinge sind seither passiert:
der Renderpfad und die BLEP-Tabellen liegen im RAM statt im XIP-Flash (ein
Oszillator liest pro Sample zwei Zeilen zu 32 Floats — das schlaegt den
XIP-Cache), und der Peak-Wert setzt sich beim Betreten des CPU-Load-Schirms
zurueck. Vorher war er ein Maximum seit dem Einschalten, und der allererste
Block nach dem Start laeuft mit kaltem Cache — ein einzelner Ausreisser stand
danach fuer immer auf dem Schirm.

Bleibt die Last zu hoch, ist die Stellschraube `MAX_VOICES` in
`include/obxf/ObxfPort.h`.
