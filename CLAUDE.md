# PicoVintageSynthCollection — Arbeitsregeln

Elf RP2350-Synthesizer-Firmwares auf einem gemeinsamen Kern (`core/`), je
Instrument ein Ordner unter `instruments/` und ein eigenes `.uf2`. Alles
Weitere zur Struktur steht in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
offene Punkte in dessen §8.

**Stand:** Release **v1.21.1** (13.09.2026, Tag auf `18e65fd`): #107 beantwortet —
Billigboards ignorieren das VREG-Register, der Kern bleibt bei ~300 MHz, Flash
und Stepping unschuldig (#172, Boot-Diagnose `PICOFACE_BOOT_DIAG` und
`PICOFACE_EXTRA_DEFINES`); Bootrom-Revision `b4` im Splash (#170);
Hardware-Anforderung im README (#173). Seither auf `main`, noch nicht
releast: **PicoFaceM1** (Korg M1, #176, auf der Hardware getestet).

## Sprache

Michael schreibt Deutsch, und die Antworten an ihn sind Deutsch. Code,
Kommentare, Commit-Nachrichten, PR-Texte, Release-Notes und alle Doku im Repo
sind Englisch — bis auf das D5-Protokoll, das deutsch gewachsen ist.

## Arbeitsweise

- **Branch → PR → Hardware-Test durch Michael → Merge auf getrennten Zuruf.**
  Nie direkt auf `main`. Pushen und PR öffnen, wenn die Arbeit steht; mergen
  erst, wenn er es sagt — auch bei grüner CI. Gemergte Branches darf ich lokal
  und auf `origin` löschen, ungemergte nie.
- Den Hardware-Test macht **er**. Im PR steht deshalb, was zu testen ist und
  was ungetestet bleibt. Sein Prüfstand ist ein Mac; was nur unter Windows/
  Linux auftritt (z. B. ein Host, der USB-MIDI nicht liest, #161), sieht er
  nicht — im Zweifel gedanklich prüfen und dazuschreiben.
- **Antworten auf GitHub-Issues postet er selbst.** Ich schreibe Entwürfe.
- Diagnose vor Fix: erst die Ursache belegen, dann den Umfang mit ihm
  entscheiden. Messungen gehen vor Annahmen; eine Messung, die der Firmware
  oder einem Referenzgerät widerspricht, ist zuerst selbst verdächtig.
- Vor jedem PR: `tools/host_tests/run_all.sh` (die CI führt dasselbe aus und
  ein Release braucht es grün). GUI-Änderungen mit `tools/host_tests/ui`
  rendern statt flashen; die Frames hängen als CI-Artefakt am PR.

## Releases

Nur auf Zuruf. Ablauf: Tag `vX.Y.Z` auf `main` pushen → die CI baut die sieben
Instrumente ohne ROMs und legt das Release an → Release-Text von Hand setzen
(`gh release edit`, Stil wie die vorigen). Danach lokal `cmake -S . -B build`
und alle elf bauen, damit D5/JV/RD/M1-Abbilder die Version im Splash tragen
(`PICOFACE_VERSION` kommt aus `git describe` beim Konfigurieren). Zum Schluss
den Stand oben nachziehen und `decomphub.progress.json` prüfen (statische
Zahlen mit `measuredAt`, von DecompHub gelesen; ändert sich eine, Datum mit).

## Was nie ins Repo darf

Roland- und Korg-ROMs, die CP-Quellaufnahmen und alles, was aus Hersteller-
Plugins/VSTs gelesen wurde (nur als Orakel bei der Analyse) — auch nicht in
einen privaten Fork. `roms/` ist ignoriert; D5, JV, RD und M1 bauen nur
lokal. Die Kette für die CP-Sample-Sätze ist
[tools/cp_sampleprep](tools/cp_sampleprep/README.md), die
Aufnahmen liegen nur bei Michael (Herkunft nicht rekonstruierbar, nicht neu
diskutieren).

## Entscheidungen, die stehen

- **Encoder-Pins** in `core/include/project_config.h` sind so belegt, wie die
  Platine unter `hardware/` sie verdrahtet (A/B gegenüber älteren Aufbauten
  getauscht). Diese Platine ist die Referenz; **keine Bauvariante, kein
  `REVERSED_DIR`**, ältere Aufbauten drehen ihre Drähte (#161).
- **Ein Sample-Satz für 16 und 4 MB** beim CP; keine Variante.
- **D5: Firmware > Plugin/VST > Ohr.** Das Protokoll dazu ist
  [instruments/PicoFaceD5/doc/PROJEKTSTAND.md](instruments/PicoFaceD5/doc/PROJEKTSTAND.md)
  — vor jeder Änderung am D5-Klang §1, §5 und §6 lesen; dort stehen auch die
  bewusst liegen gelassenen Reste.
- **M1: Firmware > Aufnahmen eines echten M1 > Plugin.** Was gemessen, an
  Aufnahmen gefittet (VDF-Rate) oder angenommen ist (vier Effektwerte), steht in
  [instruments/PicoFaceM1/doc/ENGINE.md](instruments/PicoFaceM1/doc/ENGINE.md).
  Die Effekt-Annahmen bleiben, Plugin-Werte kommen nicht hinein.
- Der Boot-Benchmark `B` ist über Builds hinweg nicht vergleichbar
  (XIP-Cache-Zeilen wandern mit dem Code-Layout); nur direkt hintereinander.
- Toter Code kostet kein Flash (`gc-sections`); Aufräumen nie damit begründen.

## Wo was steht

- Kern und Instrumentschnittstelle: `docs/ARCHITECTURE.md`; neues Instrument:
  `docs/ADDING_AN_INSTRUMENT.md`; Modul-Bus: `docs/MODULE_BUS.md`.
- Je Instrument `instruments/<name>/README.md` und `doc/` (MIDI-Charts,
  Persistenz, Presets, Engineering-Logs).
- Host-Tests und Renderer: `tools/host_tests/README.md`.
- Platine: `hardware/README.md`, inklusive der Befunde der ersten Inbetriebnahme.
