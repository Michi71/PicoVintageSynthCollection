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

## Stand

Baut, auf Hardware **noch nicht getestet und klanglich nicht verifiziert**.
Der Schirm unter Menu → System → CPU Load zeigt Auslastung, klingende Stimmen
und verworfene IPC-Pakete — das ist der Messpunkt fuer die Frage, ob sechs
Stimmen tragen.
