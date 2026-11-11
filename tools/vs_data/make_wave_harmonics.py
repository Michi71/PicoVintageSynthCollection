#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Erzeugt data/vs_wave_harmonics.txt: die Wellen des Prophet VS als Obertontabelle.

Einmaliges Werkzeug fuer den Pfleger des Projekts; wer die Engine nur baut,
braucht es nicht. Es liest die Wellen 32..125 aus einem ROM-Abbild (OS r1.2)
und beschreibt jede als Summe von Obertoenen:

  * Betrag je Oberton auf ein 0,5-dB-Raster gerundet, bezogen auf den
    staerksten Oberton der Welle; alles unter -60 dB entfaellt
  * Phase je Oberton auf Vielfache von 90 Grad gerastet, nachdem die Welle
    um eine gemeinsame Zeitlage (Raster 1/8 Sample) verschoben wurde
  * Gleichanteil entfaellt

Die Tabelle ist damit eine geglaettete Beschreibung der Klangfarben, keine
Kopie der Samplewerte; sie ist aber aus den Wellen des ROMs abgeleitet.
tools/gen_data.py rechnet daraus beim Bauen die Wellentabellen.

Aufruf: make_wave_harmonics.py [--rom DATEI | --msb DATEI --lsb DATEI]
"""
import argparse
import os

import numpy as np

import gen_data as g

OUT = g.HARMONICS
STEP_DB, FLOOR_DB, SHIFT_STEP = 0.5, -60.0, 0.125


def analyse(w, max_shift=4):
    """12-Bit-Welle -> (Zeitlage, Bezugspegel in dBFS, [(Oberton, dB, Quadrant)])"""
    X = np.fft.rfft(w / 2048.0)
    amp, ph = 2.0 * np.abs(X[1:64]) / 128.0, np.angle(X[1:64])
    if amp.max() <= 0:
        return 0.0, 0.0, []
    ref_db = round(20 * np.log10(amp.max()), 2)
    db = np.round(20 * np.log10(np.maximum(amp, 1e-12) / amp.max()) / STEP_DB) * STEP_DB
    keep = db >= FLOOR_DB
    h = np.arange(1, 64)
    weight = np.where(keep, 10 ** (db / 10), 0.0)
    best = None
    for s in np.arange(-max_shift, max_shift, SHIFT_STEP):
        p = ph + 2 * np.pi * h * s / 128
        quad = np.round(p / (np.pi / 2))
        err = float(np.sum(weight * np.angle(np.exp(1j * (p - quad * np.pi / 2))) ** 2))
        if best is None or err < best[0] - 1e-12:
            best = (err, s, quad.astype(int) % 4)
    _, shift, quad = best
    return float(shift), ref_db, [(int(k), float(db[k - 1]), int(quad[k - 1])) for k in h if keep[k - 1]]


def write_table(path, title, waves, max_shift=4):
    lines = [f"# {title} (erzeugt von tools/make_wave_harmonics.py).",
             "# Zeile: Welle  Zeitlage in Samples  Bezugspegel in dBFS  dann je Oberton Nummer:dB:Quadrant",
             "#   dB: Betrag relativ zum Bezugspegel (0,5-dB-Raster, unter -60 dB weggelassen)",
             "#   Quadrant q: Phase q*90 Grad",
             "# Welle[n] = Summe ueber die Obertoene h von",
             "#   10^((Bezug+dB)/20) * cos(2*pi*h*(n - Zeitlage)/128 + q*90 Grad),  n = 0..127"]
    total = 0
    for n, w in waves:
        shift, ref_db, parts = analyse(w, max_shift)
        total += len(parts)
        lines.append(f'{n} {shift:g} {ref_db:.2f} ' + ' '.join(f'{k}:{d + 0.0:g}:{q}' for k, d, q in parts))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w').write('\n'.join(lines) + '\n')
    print(f'{path}: {len(waves)} Wellen, {total} Obertoene')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom')
    ap.add_argument('--msb')
    ap.add_argument('--lsb')
    rom, src = g.load_rom(ap.parse_args())
    write_table(OUT, 'Wellen 32..125 des Prophet VS als Obertontabelle',
                [(n, g.rom_wave(rom, n)) for n in range(32, 126)])
