#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Erzeugt die Daten-Header der provs-Engine.

Drei Wege, die Wellen und Tabellen zu bekommen:

  aus dem Repository   Die Wellen aus der Obertontabelle
                 data/vs_wave_harmonics.txt (geglaettete Rekonstruktion), die
                 Tabellen aus Bildungsregeln, ein eigener Init-Patch. Es wird
                 keine fremde Datei gebraucht.
  mit ROM        Ein eigenes Abbild des Betriebssystem-ROMs (OS r1.2): Wellen,
                 Tabellen und Init-Patch exakt wie im Original.
  --vswaves ...  Wellen aus einer Datei VS-WAVES.DAT, Tabellen aus Bildungsregeln.

Ohne Angaben schaut das Skript in den Ordner roms/ des Projekts (nicht im
Repository): Liegt dort ein passendes ROM-Abbild - eine 64-KiB-Datei oder die
beiden 32-KiB-EPROM-Abbilder, der Dateiname ist egal -, wird es benutzt,
sonst baut es aus dem Repository. --no-rom erzwingt den Bau aus dem Repository.

Geschrieben wird nach engine/include/provs/gen/:

  wavetables.h   Wellen 32..126 (12 Bit) und daraus gerechnete Mip-Stufen
  rom_tables.h   Tabellen der Firmware (Envelope, LFO, Velocity, Glide),
                 Programmformat (Bitbreiten), Init-Patch, Zeichensatz

Programme sind nicht dabei: Die Engine laedt Programmbaenke zur Laufzeit aus
SysEx-Dateien (Engine::loadVsSysex).

Aufruf:
  gen_data.py                                      ROM aus roms/, sonst aus dem Repository
  gen_data.py --no-rom                             aus dem Repository, auch wenn ein ROM da ist
  gen_data.py --rom vs_os12.bin                    64-KiB-Abbild (MSB/LSB gemischt)
  gen_data.py --msb msb.bin --lsb lsb.bin          die beiden 27256-EPROMs
  gen_data.py --vswaves VS-WAVES.DAT               Wellen aus dieser Datei

VS-WAVES.DAT: je Welle 128 Samples als 16 Bit Big-Endian (12 Bit, um 4 Bit
nach links geschoben), geordnet nach Wellennummer; Welle 32 ist der Sinus.

Was wo im ROM steht (OS r1.2, alle Adressen gegen die Firmware geprueft):
* Wellen: ROM-Welle n (32..126) bei 0xA0C0 + n*0xC0 als 192-Byte-Datensatz:
  128 Bytes (obere 8 Bit, signed) + 64 Bytes mit den unteren 4 Bit
  (High-Nibble = gerades Sample). Beleg: Upload-Routine @0x43BE.
  Die Wellen 0..31 sind User-RAM und stehen nicht im ROM; sie bleiben leer.
* Mip-Stufen 1..3: von diesem Skript aus den Wellen gerechnet (Fourier-Reihe,
  Obertoene oberhalb von 31/15/7 gestrichen).
* Bitbreiten der 116 Programmfelder @0x48F6, Init-Patch @0x496C.
* Tabellen: Envelope-Segmentdauer @0x6920, Release-Koeffizient @0x6A20,
  LFO-Rate @0x6790, Velocity @0x6690, Glide @0x6858, Zeichensatz @0x1A2F.
"""
import argparse
import os
import struct
import sys

import numpy as np

HERE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
OUT = os.path.join(HERE, 'engine', 'include', 'provs', 'gen')

# Ordner fuer eigene ROM-Abbilder (steht in der .gitignore)
ROMS = os.path.join(HERE, 'roms')

# Programmformat des Prophet VS: Bitbreite der 116 Felder und Zeichensatz der
# Namen. Das sind Angaben zum Dateiformat (SysEx-Dump); mit ROM werden sie
# gegen das ROM geprueft.
FIELD_BITS = [
    7, 7, 7, 7, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 7, 3, 3, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 3, 2,
    7, 7, 5, 7, 3, 7, 2, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 7, 2, 3, 2, 2, 2, 1, 1, 1, 1, 7, 7, 8, 8,
    8, 8, 2, 8, 2, 8, 7, 8, 0, 5, 0, 4, 0, 2, 0, 3,
]
NAME_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ12345 '

N_WAVES, FRAMES, MIPS = 127, 128, 4
MIP_HARMONICS = (64, 31, 15, 7)
N_FIELDS = 116
HEAD = "// Auto-generiert von tools/gen_data.py - nicht von Hand aendern, nicht einchecken."


def first_existing(paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def find_rom():
    """ROM-Abbild in roms/ suchen, erkannt am Inhalt: (rom, Quelle) oder None."""
    if not os.path.isdir(ROMS):
        return None
    files = sorted(os.path.join(ROMS, f) for f in os.listdir(ROMS))
    files = [f for f in files if os.path.isfile(f)]
    for f in files:
        if os.path.getsize(f) == 65536:
            rom = open(f, 'rb').read()
            if not rom_problems(rom):
                return rom, f
    halves = [f for f in files if os.path.getsize(f) == 32768]
    for a in halves:
        for b in halves:
            if a != b:
                x, y = open(a, 'rb').read(), open(b, 'rb').read()
                rom = bytes(v for pair in zip(x, y) for v in pair)
                if not rom_problems(rom):
                    return rom, f'{a} + {b}'
    return None


def load_rom(args):
    """ROM aus den Angaben (--rom oder --msb/--lsb), sonst aus roms/."""
    for path in (args.rom, args.msb, args.lsb):
        if path and not os.path.exists(path):
            sys.exit(f'{path}: Datei nicht gefunden')
    if args.msb or args.lsb:
        if not (args.msb and args.lsb):
            sys.exit('--msb und --lsb gehoeren zusammen')
        msb, lsb = open(args.msb, 'rb').read(), open(args.lsb, 'rb').read()
        if len(msb) != 32768 or len(lsb) != 32768:
            sys.exit('die EPROM-Abbilder muessen je 32768 Bytes gross sein')
        rom, src = bytes(b for pair in zip(msb, lsb) for b in pair), f'{args.msb} + {args.lsb}'
    elif args.rom:
        rom, src = open(args.rom, 'rb').read(), args.rom
    else:
        found = find_rom()
        if not found:
            sys.exit('Kein ROM gefunden. Bitte ein Abbild des Prophet-VS-Betriebssystems r1.2 nach '
                     'roms/ legen oder --rom DATEI bzw. --msb/--lsb angeben.')
        return found
    check_rom(rom)
    return rom, src


def check_rom(rom):
    """Stellt sicher, dass es das erwartete ROM ist - die Adressen gelten nur dafuer."""
    problems = rom_problems(rom)
    if problems:
        sys.exit('Das ROM wird nicht erkannt:\n  ' + '\n  '.join(problems))


def rom_problems(rom):
    problems = []
    if len(rom) != 65536:
        problems.append(f'Groesse {len(rom)} statt 65536 Bytes')
    else:
        ssp, pc = struct.unpack('>II', rom[:8])
        if (ssp, pc) != (0xFFFFD000, 0x100):
            problems.append('Reset-Vektoren passen nicht (MSB/LSB vertauscht?)')
        if b'PROPHET VS' not in rom or b'r1.2' not in rom:
            problems.append('Kennung "PROPHET VS ... r1.2" fehlt (andere OS-Version?)')
        if list(rom[0x48F6:0x48F6 + N_FIELDS]) != FIELD_BITS:
            problems.append('Bitbreiten-Tabelle nicht bei 0x48F6')
        if rom[0x1A2F:0x1A2F + 32] != NAME_CHARS.encode():
            problems.append('Zeichensatz nicht bei 0x1A2F')
        sine = rom_wave(rom, 32)
        ref = 2047 * np.cos(2 * np.pi * np.arange(128) / 128)
        if np.abs(sine - ref).max() > 1.0:
            problems.append('Welle 32 ist kein Sinus')
    return problems


def rom_wave(rom, n):
    a = 0xA0C0 + n * 0xC0
    hi = np.frombuffer(rom[a:a + 128], dtype=np.int8).astype(int)
    lo = rom[a + 128:a + 192]
    nib = np.array([(lo[k // 2] >> 4) if k % 2 == 0 else (lo[k // 2] & 15)
                    for k in range(128)])
    return hi * 16 + nib            # 12 Bit signed, -2048..2047


def load_vswaves(path):
    """Wellen aus VS-WAVES.DAT: Liste mit 127 Eintraegen (12 Bit) oder None."""
    data = open(path, 'rb').read()
    if len(data) % 256 or len(data) < 126 * 256:
        sys.exit(f'{path}: keine VS-WAVES.DAT (erwartet 256 Bytes je Welle, mindestens 126 Wellen)')
    count = len(data) // 256
    waves = [None] * N_WAVES
    for n in range(32, N_WAVES):
        if n < count:
            v = np.frombuffer(data[n * 256:(n + 1) * 256], dtype='>i2').astype(int)
            waves[n] = v >> 4
    spec = np.abs(np.fft.rfft(waves[32]))
    if spec[1] < 100000 or spec[2:].max() > 0.001 * spec[1]:
        sys.exit(f'{path}: Welle 32 ist kein Sinus - andere Anordnung oder anderes Format')
    return waves


# Obertontabelle: neben dem Skript (Einbau in ein anderes Projekt) oder in data/
HARMONICS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vs_wave_harmonics.txt')
if not os.path.exists(HARMONICS):
    HARMONICS = os.path.join(HERE, 'data', 'vs_wave_harmonics.txt')


def read_harmonics(path, waves):
    """Obertontabelle lesen und die Wellen rechnen (Format: Kopf der Datei)."""
    n = np.arange(FRAMES)
    count = 0
    for line in open(path):
        line = line.split('#')[0].split()
        if not line:
            continue
        idx, shift, ref_db = int(line[0]), float(line[1]), float(line[2])
        w = np.zeros(FRAMES)
        for part in line[3:]:
            h, db, quad = part.split(':')
            w += 10 ** ((ref_db + float(db)) / 20) * np.cos(
                2 * np.pi * int(h) * (n - shift) / FRAMES + int(quad) * np.pi / 2)
        waves[idx] = w * 2048.0
        count += 1
    return count


def load_harmonics(path=None):
    path = path or HARMONICS
    """Wellen 32..125 aus der Obertontabelle des Repositorys."""
    if not os.path.exists(path):
        sys.exit(f'{path} fehlt - die Obertontabelle gehoert zum Repository')
    waves = [None] * N_WAVES
    if read_harmonics(path, waves) != 94:
        sys.exit(f'{path}: erwartet werden die 94 Wellen 32..125')
    return waves


def bandlimit(x, hmax):
    X = np.fft.rfft(x)
    X[hmax + 1:] = 0
    return np.fft.irfft(X, len(x))


def write(name, lines):
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, name), 'w').write('\n'.join(lines) + '\n')


def gen_waves(waves, source):
    tab = np.zeros((N_WAVES, MIPS, FRAMES))
    for n in range(N_WAVES):
        if waves[n] is None:
            continue
        base = waves[n] / 2048.0
        tab[n, 0] = base
        for m in range(1, MIPS):
            tab[n, m] = bandlimit(base, MIP_HARMONICS[m])
    # Die Bandbegrenzung schwingt an Flanken ueber; alles gemeinsam skalieren
    peak = float(np.abs(tab).max())
    gain = 1.0 if peak <= 1.0 else peak * 1.0001
    out = [HEAD,
           "// 127 Prophet-VS-Wellen x 4 Mip-Stufen x 128 Samples (int16).",
           f"//   Wellen 32..126, Stufe 0: {source}",
           "//   Stufen 1..3: daraus gerechnet, bandbegrenzt auf 31/15/7 Obertoene",
           "//   Wellen 0..31: leer (User-RAM des Originals, nicht im ROM)",
           "//   Welle 127 (Rauschen) erzeugt die Engine selbst.",
           "#pragma once", "#include <cstdint>", "namespace provs {",
           f"constexpr int kWaveCountVS = {N_WAVES};",
           f"constexpr int kWaveFrames  = {FRAMES};",
           f"constexpr int kWaveMips    = {MIPS};",
           "// hoechster Oberton je Mip-Stufe",
           "constexpr int kMipHarmonics[kWaveMips] = {64, 31, 15, 7};",
           "// Welle = Wert / 32767 * kWaveDataGain",
           f"constexpr float kWaveDataGain = {gain:.6f}f;",
           "// Welle n, Stufe m = kWaveDataVS[(n*kWaveMips + m)*kWaveFrames ...]",
           "inline constexpr int16_t kWaveDataVS[kWaveCountVS * kWaveMips * kWaveFrames] = {"]
    for n in range(N_WAVES):
        for m in range(MIPS):
            v = np.clip(np.round(tab[n, m] / gain * 32767.0), -32768, 32767).astype(int)
            out.append("    " + ",".join(map(str, v)) + f", // wave {n} mip {m}")
    out += ["};", "}  // namespace provs"]
    write('wavetables.h', out)
    print(f'wavetables.h: Wellen 32..126 aus {source}, Spitzenwert {peak:.4f}')


def formula_tables():
    """Die fuenf Tabellen der Firmware aus eigenen Bildungsregeln.

    Alle fuenf Kurven sind Exponentialkurven, in ganzen Zahlen schrittweise
    gebildet. Zwei Formen kommen vor:
      Summe:  Wert[n+1] = Wert[n] + gerundet(a * r^n)
      Kette:  Wert[n+1] = ganzzahliger Teil von (Wert[n] * r + b)
    Mit Startwert und zwei bis drei Zahlen ist jede Tabelle neu gerechnet.
    Abweichung gegen die Tabellen des ROMs (OS r1.2), nachzumessen mit
    --check-formulas: Envelope, Release, LFO und Glide keine; Velocity
    hoechstens 3 von 32767.
    """
    def sums(start, a, r, n=100, sign=1):
        out = [start]
        for k in range(n - 1):
            out.append(out[-1] + sign * int(np.floor(a * r ** k + 0.5)))
        return out

    def chain(start, r, b, n=100):
        out = [start]
        for _ in range(n - 1):
            out.append(max(1, int(np.floor(out[-1] * r + b))))
        return out
    # Segmentdauer in Ticks: jeder Schritt 6,8 % laenger als der vorige
    ticks = sums(1, 1.0, 1.068)
    # Release: der Abstand zu 32768 schrumpft je Rate um rund 7,6 %
    release = [0] + [32768 - g for g in chain(29375, 0.92353, -0.34, n=99)]   # Rate 0: sofort
    # LFO-Rate: rund 3,5 % schneller je Schritt
    lfo = chain(40, 1.03498, 8.10)
    velocity = [min(32767, v) for v in sums(864, 102.46, 1.01266, n=128)]
    # Glide: jeder Schritt 2,9 % kleiner als der vorige
    glide = sums(32767, 1000.0, 0.971, sign=-1)
    return ticks, release, lfo, velocity, glide


def own_init_program():
    """Eigener Init-Patch fuer den Bau ohne ROM: vier Grundwellen, Filter offen."""
    f = [0] * N_FIELDS
    f[0:4] = [32, 33, 34, 35]            # Wellen A-D
    f[12] = 99                           # Cutoff
    f[17] = f[18] = 60                   # LFO-Raten
    f[24:28] = [99, 99, 99, 99]          # Amp-Envelope: Pegel 0-3
    f[60] = 60                           # Split-Punkt
    f[67], f[68], f[69] = 40, 40, 80     # Chorus Rate/Depth, Lautstaerke
    f[78:86] = [NAME_CHARS.index(c) for c in 'INIT    ']
    f[86] = 70                           # Arpeggiator-Rate
    f[92] = f[93] = 1
    f[103] = 15
    f[115] = 1                           # Modulationsrad -> LFO 1
    return f


def gen_tables(rom):
    def words(addr, n):
        return struct.unpack(f'>{n}H', rom[addr:addr + 2 * n])

    def arr(name, vals, per=10):
        rows = [', '.join(map(str, vals[i:i + per])) for i in range(0, len(vals), per)]
        return [f'inline constexpr uint16_t {name}[{len(vals)}] = {{'] + \
               ['    ' + r + ',' for r in rows] + ['};']

    def bytes_(name, vals):
        return [f'inline constexpr uint8_t {name}[{len(vals)}] = {{'] + \
               ['    ' + ', '.join(map(str, vals[i:i + 20])) + ',' for i in range(0, len(vals), 20)] + ['};']
    if rom is not None:
        ticks, release, lfo = words(0x6920, 100), words(0x6A20, 100), words(0x6790, 100)
        velocity, glide = words(0x6690, 128), words(0x6858, 100)
        init, where = list(rom[0x496C:0x496C + N_FIELDS]), "Init-Patch der Firmware (@0x496C)"
        title = "// Tabellen aus dem ROM des Original-Prophet-VS (OS r1.2)."
    else:
        ticks, release, lfo, velocity, glide = formula_tables()
        init, where = own_init_program(), "eigener Init-Patch (Bau ohne ROM)"
        title = ("// Bau ohne ROM: Tabellen aus Bildungsregeln gerechnet (tools/gen_data.py, formula_tables).")
    out = [HEAD, title,
           "#pragma once", "#include <cstdint>", "namespace provs {", "",
           "// true: Tabellen aus dem ROM gelesen; false: aus Bildungsregeln gerechnet",
           f"constexpr bool kTablesFromRom = {'true' if rom is not None else 'false'};", "",
           "// Envelope, Raten 1-3: Segmentdauer in Ticks (Firmware @0x6920)"]
    out += arr('kEnvSegmentTicks', ticks)
    out += ["", "// Envelope, Raten 4/4A: Release-Koeffizient k, je Tick v = L4 + (v-L4)*k/32768 (@0x6A20)"]
    out += arr('kEnvReleaseCoef', release)
    out += ["", "// LFO-Rate 0..99 -> Phasenschritt je Tick (@0x6790)"]
    out += arr('kLfoRateStep', lfo)
    out += ["", "// Velocity 0..127 -> Modulationswert, 32767 = voll (@0x6690)"]
    out += arr('kVelocityCurve', velocity, 8)
    out += ["", "// Glide 1..99 -> Tempo, Wert/64 Cent je Tick (@0x6858)"]
    out += arr('kGlideRate', glide)
    out += ["", "// Programmformat: Bitbreite der 116 Felder (@0x48F6); ein Programm sind",
            "// 82 Bytes, die Felder stehen darin MSB-first hintereinander"]
    out += bytes_('kProgramFieldBits', FIELD_BITS)
    out += ["", f"// {where}, ein Byte je Feld"]
    out += bytes_('kInitProgram', init)
    out += ["", "// Zeichensatz der Programmnamen, 5 Bit je Zeichen (@0x1A2F)",
            f'inline constexpr char kNameChars[33] = "{NAME_CHARS}";', "",
            "}  // namespace provs"]
    write('rom_tables.h', out)
    print('rom_tables.h: ' + ('5 Tabellen und Init-Patch aus dem ROM' if rom is not None
                              else '5 Tabellen aus Bildungsregeln gerechnet, eigener Init-Patch'))


def compare_formulas(rom):
    """--check-formulas: eigene Formeln gegen die Tabellen des ROMs messen."""
    def words(addr, n):
        return np.array(struct.unpack(f'>{n}H', rom[addr:addr + 2 * n]), dtype=float)
    ticks, release, lfo, velocity, glide = [np.array(v, dtype=float) for v in formula_tables()]
    for name, own, ref in (('Envelope-Segmentdauer', ticks, words(0x6920, 100)),
                           ('LFO-Rate', lfo, words(0x6790, 100)),
                           ('Velocity', velocity, words(0x6690, 128)),
                           ('Glide', glide, words(0x6858, 100))):
        d = np.abs(own - ref)
        print(f'{name:24s} groesste Abweichung {d.max():.0f}, relativ {100 * np.max(d / np.maximum(ref, 1)):.2f} %, '
              f'{int((d == 0).sum())} von {len(ref)} Werten gleich')
    ref = words(0x6A20, 100)
    tau = lambda k: -1.0 / np.log(k / 32768.0)
    e = np.abs(tau(release[1:]) / tau(ref[1:]) - 1)
    print(f'{"Release-Zeitkonstante":24s} groesste Abweichung {100 * e.max():.2f} %, '
          f'{int((release == ref).sum())} von 100 Werten gleich')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom', help='64-KiB-Abbild des OS-ROMs (MSB/LSB gemischt)')
    ap.add_argument('--no-rom', action='store_true', help='aus dem Repository bauen, auch wenn in roms/ ein ROM liegt')
    ap.add_argument('--msb', help='EPROM mit den oberen Datenbits (32 KiB)')
    ap.add_argument('--lsb', help='EPROM mit den unteren Datenbits (32 KiB)')
    ap.add_argument('--vswaves', help='Wellen aus einer Datei VS-WAVES.DAT statt aus der Obertontabelle')
    ap.add_argument('--out', help='Zielordner fuer die Header (sonst engine/include/provs/gen)')
    ap.add_argument('--roms', help='Ordner mit eigenen ROM-Abbildern (sonst roms/)')
    ap.add_argument('--check-formulas', action='store_true',
                    help='eigene Formeln gegen die Tabellen des ROMs messen, nichts schreiben')
    args = ap.parse_args()
    if args.out:
        OUT = args.out
    if args.roms:
        ROMS = args.roms
    use_rom = args.rom or args.msb or args.lsb or args.check_formulas
    if not use_rom and not args.vswaves and not args.no_rom and find_rom():
        use_rom = True
    if args.vswaves:
        if use_rom:
            sys.exit('--vswaves bitte nicht zusammen mit --rom/--msb/--lsb')
        if not os.path.exists(args.vswaves):
            sys.exit(f'{args.vswaves}: Datei nicht gefunden')
        print(f'Ohne ROM. Wellen: {args.vswaves}')
        gen_waves(load_vswaves(args.vswaves), 'VS-WAVES.DAT')
        gen_tables(None)
    elif use_rom:
        rom, src = load_rom(args)
        print(f'ROM: {src}')
        if args.check_formulas:
            compare_formulas(rom)
            sys.exit(0)
        waves = [None] * 32 + [rom_wave(rom, n) for n in range(32, N_WAVES)]
        gen_waves(waves, 'Original-ROM')
        gen_tables(rom)
    else:
        print('Ohne ROM. Wellen: Obertontabelle vs_wave_harmonics.txt')
        gen_waves(load_harmonics(), 'Rekonstruktion aus der Obertontabelle')
        gen_tables(None)
    old = os.path.join(OUT, 'vs_presets.hpp')      # frueher einkompilierte Bank
    if os.path.exists(old):
        os.remove(old)
