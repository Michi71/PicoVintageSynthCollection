# PicoVintageSynthCollection

Sechs Vintage-Synthesizer-Emulationen fuer den RP2350, aus einer gemeinsamen Codebasis gebaut.

## Instrumente

| Ordner | Instrument | Binary |
|---|---|---|
| instruments/PicoFaceYC | Yamaha reface YC (Drawbar-Orgel) | PicoFaceYC.uf2 |
| instruments/PicoFaceCP | Yamaha reface CP (E-Piano, mdaEPiano) | PicoFaceCP.uf2 |
| instruments/PicoFaceRD | Roland RD / MKS-20 (Sample-Piano) | PicoFaceRD.uf2 |
| instruments/PicoFaceJ6 | Roland Juno-6 | PicoFaceJ6.uf2 |
| instruments/PicoFaceMD | Minimoog Model D | PicoFaceMD.uf2 |
| instruments/PicoFaceSM | ARP/Eminent Solina String Ensemble | PicoFaceSM.uf2 |

## Hardware

- RP2350
- Board sparkfun_promicro_rp2350
- I2S-Audio
- OLED 128x64 via I2C
- Drei Drehgeber mit Taster
- USB-MIDI

## Bauen

```bash
git clone --recurse-submodules https://github.com/Michi71/PicoVintageSynthCollection.git
cd PicoVintageSynthCollection
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Die Artefakte liegen danach als `build/<Instrument>.uf2` vor.

Ein einzelnes Instrument bauen:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceMD
cmake --build build
```

## Dokumentation

- [Architektur](docs/ARCHITECTURE.md)
- [Ein neues Instrument hinzufuegen](docs/ADDING_AN_INSTRUMENT.md)

## Status

**Alle sechs Instrumente bauen aus einem gemeinsamen Configure-Lauf, je mit eigener USB-PID, und laufen auf der Hardware.** Was noch offen ist, steht in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), Abschnitt 8.

## Lizenz

MIT
