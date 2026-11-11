#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Optional: Daten des Behringer Pro VS Mini fuer die provs-Engine erzeugen.

Die Engine baut und laeuft ohne diese Daten. Wer die Dateien des Pro VS Mini
auf dem Rechner hat, bekommt damit zusaetzlich dessen Patches und den
Chord-Modus. Geschrieben wird engine/include/provs/gen/pvm_data.h; liegt die
Datei vor, schaltet die Engine die Behringer-Teile beim Bauen von selbst ein.

Das Skript nimmt, was es findet:

  QSPI-Abbild      -> ADSR-Zeiten (Voraussetzung, um Patches des Mini zu
                      spielen) und die Wellen 0..31, die beim Original im
                      User-RAM liegen
  Programm-Abbild  -> Akkordsaetze des Chord-Modus (ab Firmware 2.0)

Patches sind nicht dabei: Die Engine laedt Patchbaenke des Mini zur Laufzeit
aus SysEx-Dateien (Engine::loadPvmSysex).

Gesucht wird in dieser Reihenfolge:
  1. die mit --qspi / --intflash angegebenen Dateien
  2. die Ordner aus --search (mehrfach moeglich)
  3. der Ordner roms/ des Projekts (nicht im Repository)
  4. dort, wo die Synthtribe-App die Firmware nach einem Update ablegt

Gelesen werden unverschluesselte Abbilder. Synthtribe legt die Firmware
verschluesselt ab; solche Dateien liest das Skript nur, wenn im Ordner local/
(nicht Teil des Repositorys) ein Modul pvm_decrypt.py mit einer Funktion
decrypt(bytes) -> bytes liegt.
Die Tabellen werden am Inhalt erkannt, nicht an festen Adressen.

Findet das Skript nichts Brauchbares, schaltet es die Behringer-Teile ab und
endet ohne Fehler.
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np

HERE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
OUT = os.path.join(HERE, 'engine', 'include', 'provs', 'gen')
ROMS = os.path.join(HERE, 'roms')
CHORD_NAMES = ['Octave', '4th', '5th', 'Major', 'Minor', 'EDM 1', 'EDM 2', 'Jazz']


def search_dirs(extra):
    home = os.path.expanduser('~')
    dirs = list(extra) + [
        ROMS,                                # eigener Ordner des Projekts (nicht im Repository)
        # Synthtribe (macOS, Windows): Ablage der Firmware nach einem Update
        '/Applications/Synthtribe.app/Contents/Resources',
        os.path.join(home, 'Library', 'Caches', 'com.behringer.synthtribe'),
        os.path.join(home, 'Library', 'Application Support', 'Synthtribe'),
        os.path.join(home, 'Library', 'Application Support', 'com.behringer.synthtribe'),
        os.path.join(os.environ.get('APPDATA', ''), 'Synthtribe'),
        os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Synthtribe'),
        os.path.join(os.environ.get('PROGRAMFILES', ''), 'Synthtribe'),
    ]
    return [d for d in dirs if d and os.path.isdir(d)]


def candidates(dirs, words, exts):
    """Dateien, deren Name alle Woerter enthaelt (ohne Gross/Klein), neueste zuerst."""
    found = []
    for d in dirs:
        for root, _, files in os.walk(d):
            if root.count(os.sep) - d.count(os.sep) > 6:
                continue
            for f in files:
                low = f.lower()
                if low.endswith(exts) and all(w in low for w in words):
                    found.append(os.path.join(root, f))
    return sorted(set(found), key=lambda p: -os.path.getmtime(p))


def readable(path):
    """Abbild so liefern, dass die Tabellen darin stehen: erst roh, dann entschluesselt."""
    raw = open(path, 'rb').read()
    yield raw, ''
    try:
        sys.path.insert(0, os.path.join(HERE, 'local'))
        sys.path.insert(0, ROMS)             # oder neben den Abbildern (roms/ ist nie im Repository)
        import pvm_decrypt
        yield pvm_decrypt.decrypt(raw), ' (entschluesselt)'
    except ImportError:
        return


def find_adsr(paths):
    """Phasenschritt-Tabelle des ADSR: 257 fallende uint32, erster Wert 2^32/24."""
    first = struct.pack('<I', 178956970)
    for p in paths:
        for data, note in readable(p):
            i = data.find(first)
            while i >= 0:
                if i % 4 == 0 and i + 257 * 4 + 257 * 2 <= len(data):
                    t = np.frombuffer(data[i:i + 257 * 4], dtype='<u4').astype(float)
                    if np.all(np.diff(t) < 0):
                        curve = np.frombuffer(data[i + 257 * 4:i + 257 * 4 + 257 * 2], dtype='<u2') / 65535.0
                        return t, curve, f'{p}{note}, Tabelle @0x{i:X}', data
                i = data.find(first, i + 1)
    return None


def user_waves(qspi):
    """Wellen 0..31 (beim Original User-RAM): Bank 0 am Anfang des QSPI-Abbilds,
    126 Wellen x 4 Baenke x 128 float32, zeitgespiegelt. Geprueft wird, dass
    Welle 32 dort der Sinus des Originals ist. Die Mip-Stufen werden wie bei
    den ROM-Wellen selbst gerechnet."""
    n = 126 * 4 * 128
    if len(qspi) < n * 4:
        return None
    q = np.frombuffer(qspi[:n * 4], dtype='<f4').reshape(126, 4, 128)[:, :, ::-1].astype(float)
    if not np.all(np.isfinite(q)) or np.abs(q[32, 0] - np.cos(2 * np.pi * np.arange(128) / 128)).max() > 0.01:
        return None
    harm = (64, 31, 15, 7)
    tab = np.zeros((32, 4, 128))
    for w in range(32):
        tab[w, 0] = q[w, 0]
        for m in range(1, 4):
            X = np.fft.rfft(q[w, 0])
            X[harm[m] + 1:] = 0
            tab[w, m] = np.fft.irfft(X, 128)
    return tab


def find_chords(paths):
    """Akkordtabelle: beginnt mit zwoelfmal (0, 12, 24), 8 Saetze x 12 x 3 Bytes."""
    start = bytes([0, 12, 24]) * 12
    for p in paths:
        for data, note in readable(p):
            i = data.find(start)
            if i >= 0 and data[i + 36:i + 72] == bytes([0, 5, 10]) * 12:
                tab = [[tuple(data[i + s * 36 + 3 * k:i + s * 36 + 3 * k + 3]) for k in range(12)]
                       for s in range(8)]
                return tab, f'{p}{note}, Tabelle @0x{i:X}'
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--qspi', help='QSPI-Abbild des Pro VS Mini')
    ap.add_argument('--intflash', help='Programm-Abbild des Pro VS Mini (ab Firmware 2.0)')
    ap.add_argument('--search', action='append', default=[], help='zusaetzlicher Suchordner')
    ap.add_argument('--out', help='Zielordner fuer die Header (sonst engine/include/provs/gen)')
    ap.add_argument('--roms', help='Ordner mit den eigenen Dateien (sonst roms/)')
    ap.add_argument('--no-auto', action='store_true',
                    help='nur die angegebenen Dateien und Ordner benutzen, nicht selbst suchen')
    args = ap.parse_args()
    global OUT, ROMS
    if args.out:
        OUT = args.out
    if args.roms:
        ROMS = args.roms
    for p in (args.qspi, args.intflash):
        if p and not os.path.exists(p):
            sys.exit(f'{p}: Datei nicht gefunden')
    dirs = [d for d in args.search if os.path.isdir(d)] if args.no_auto else search_dirs(args.search)

    qspi = ([args.qspi] if args.qspi else []) + candidates(dirs, ('qspi',), ('.bin',))
    prog = ([args.intflash] if args.intflash else []) + candidates(dirs, ('intflash',), ('.bin',))

    adsr = find_adsr(qspi)
    chords = find_chords(prog)

    waves = user_waves(adsr[3]) if adsr else None
    print('ADSR-Zeiten:  ' + (adsr[2] if adsr else 'kein QSPI-Abbild gefunden'))
    print('Wellen 0-31:  ' + ('aus demselben QSPI-Abbild' if waves is not None else 'nicht verfuegbar'))
    print('Akkordsaetze: ' + (chords[1] if chords else 'kein Programm-Abbild ab Firmware 2.0 gefunden'))
    def set_state(on):
        os.makedirs(OUT, exist_ok=True)
        text = ("// Auto-generiert - nicht von Hand aendern, nicht einchecken.\n"
                f"#pragma once\n#define PROVS_HAS_PVM {1 if on else 0}\n")
        path = os.path.join(OUT, 'pvm_state.h')
        if not os.path.exists(path) or open(path).read() != text:
            open(path, 'w').write(text)

    if not (chords or adsr):
        data = os.path.join(OUT, 'pvm_data.h')
        if os.path.exists(data):
            os.remove(data)
        set_state(False)
        print('Nichts gefunden - die Engine baut ohne Behringer-Teile.')
        return 0

    out = ["// Auto-generiert von tools/gen_pvm.py - nicht von Hand aendern, nicht einchecken.",
           "// Daten des Behringer Pro VS Mini (optional).",
           "#pragma once", "#include <cstdint>", ""]

    def fl(v):
        s_ = '%.4g' % v
        return (s_ if ('.' in s_ or 'e' in s_) else s_ + '.0') + 'f'
    if adsr:
        t, c = adsr[0], adsr[1]
        sec = [2 ** 32 / t[min(v * 660 >> 8, 256)] / 48000.0 for v in range(100)]
        x = np.arange(257) / 256.0
        k = min(((np.sum((1 - np.exp(-x * kk) - c) ** 2), kk) for kk in np.arange(1, 10, 0.05)))[1]
    else:
        sec, k = [0.0] * 100, 4.2
    out += ["namespace provs {",
            f"constexpr bool kPvmHasEnvTable = {'true' if adsr else 'false'};",
            "// Dauer eines ADSR-Abschnitts in Sekunden fuer die Patchwerte 0..99.",
            "// Firmware (FUN_240130d4): Index = Wert*660 >> 8 in die Phasenschritt-Tabelle",
            "// (257 x uint32 im QSPI-Abbild); der Abschnitt endet nach 2^32/Schritt Schritten.",
            "// [ANN] Umgerechnet mit 48000 Schritten je Sekunde (0,5 ms .. 11,6 s). Mit",
            "// einem Schritt je 20-Sample-Callback kaemen 10 ms .. 231 s heraus; die",
            "// Factory-Patches (Orgel-, Bass-, Zupfklaenge mit Release-Werten um 60)",
            "// passen nur zur kurzen Skala.",
            "inline constexpr float kPvmEnvSeconds[100] = {"]
    out += ['    ' + ', '.join(fl(v) for v in sec[i:i + 8]) + ',' for i in range(0, 100, 8)]
    out += ["};",
            "// Der Verlauf folgt der Kurventabelle direkt hinter der Phasenschritt-Tabelle;",
            "// sie entspricht gut 1 - exp(-k * t/T) mit diesem k.",
            f"constexpr float kPvmEnvCurveK = {k:.2f}f;",
            "// Aufrufe des Audio-Callbacks je Sekunde (20-Sample-Block bei 48 kHz);",
            "// gilt fuer die Mix-Envelope-Zeiten",
            "constexpr float kPvmCallsPerSecond = 2400.0f;", ""]
    # Wellen 0..31
    if waves is not None:
        peak = float(np.abs(waves).max())
        gain = 1.0 if peak <= 1.0 else peak * 1.0001
        out += ["// Wellen 0..31 (User-RAM des Originals) in der Fassung des Pro VS Mini,",
                "// 4 Mip-Stufen x 128 Samples wie in wavetables.h; Welle = Wert/32767 * Gain.",
                "constexpr int kPvmUserWaveCount = 32;",
                f"constexpr float kPvmUserWaveGain = {gain:.6f}f;",
                "inline constexpr int16_t kPvmUserWaves[32 * 4 * 128] = {"]
        for w in range(32):
            for m in range(4):
                v = np.clip(np.round(waves[w, m] / gain * 32767.0), -32768, 32767).astype(int)
                out.append("    " + ",".join(map(str, v)) + f", // wave {w} mip {m}")
        out += ["};", ""]
    else:
        out += ["constexpr int kPvmUserWaveCount = 0;",
                "constexpr float kPvmUserWaveGain = 1.0f;",
                "inline constexpr int16_t kPvmUserWaves[1] = {0};", ""]
    out += [
            "// Akkordsaetze des Chord-Modus (Firmware ab 2.0, benutzt in FUN_2400a694):",
            "// je Tonklasse der gespielten Taste drei Intervalle in Halbtoenen.",
            f"constexpr int kChordSetCount = {8 if chords else 0};",
            f"inline constexpr unsigned char kChordSets[{8 if chords else 1}][12][3] = {{"]
    if chords:
        for s_, n in enumerate(CHORD_NAMES):
            out.append('    {' + ', '.join('{%d,%d,%d}' % t_ for t_ in chords[0][s_]) + '},  // ' + n)
    else:
        out.append('    {},')
    out += ["};",
            f"inline constexpr const char* kChordSetNames[{8 if chords else 1}] = {{",
            '    ' + (', '.join('"%s"' % n for n in CHORD_NAMES) if chords else '""') + '};',
            "}  // namespace provs"]

    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'pvm_data.h'), 'w').write('\n'.join(out) + '\n')
    set_state(True)
    print(f"pvm_data.h: ADSR-Zeiten {'ja' if adsr else 'nein'}, {8 if chords else 0} Akkordsaetze, "
          f"{32 if waves is not None else 0} User-Wellen")
    return 0


if __name__ == '__main__':
    sys.exit(main())
