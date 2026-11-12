#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Optional: generate data of the Behringer Pro VS Mini for the provs engine.

The engine builds and runs without this data. If you have the files of the
Pro VS Mini on your computer, this additionally gives you its patches and the
chord mode. Output is written to engine/include/provs/gen/pvm_data.h; if that
file exists, the engine enables the Behringer parts by itself when building.

The script takes what it finds:

  QSPI image       -> ADSR times (required to play patches of the Mini) and
                      the waves 0..31, which on the original live in user
                      RAM
  program image    -> chord sets of the chord mode (firmware 2.0 and later)

Patches are not included: the engine loads patch banks of the Mini at run
time from SysEx files (Engine::loadPvmSysex).

The search order is:
  1. the files given with --qspi / --intflash
  2. the folders from --search (may be given several times)
  3. the project's roms/ folder (not in the repository)
  4. where the Synthtribe app stores the firmware after an update

Unencrypted images are read. Synthtribe stores the firmware encrypted; the
script reads such files only if the folder local/ (not part of the
repository) contains a module pvm_decrypt.py with a function
decrypt(bytes) -> bytes.
The tables are identified by their content, not by fixed addresses.

If the script finds nothing usable, it disables the Behringer parts and
exits without an error.
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
        ROMS,                                # the project's own folder (not in the repository)
        # Synthtribe (macOS, Windows): where the firmware is stored after an update
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
    """Files whose name contains all words (case-insensitive), newest first."""
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
    """Yield the image in a form that contains the tables: first raw, then decrypted."""
    raw = open(path, 'rb').read()
    yield raw, ''
    try:
        sys.path.insert(0, os.path.join(HERE, 'local'))
        sys.path.insert(0, ROMS)             # or next to the images (roms/ is never in the repository)
        import pvm_decrypt
        yield pvm_decrypt.decrypt(raw), ' (decrypted)'
    except ImportError:
        return


def find_adsr(paths):
    """Phase increment table of the ADSR: 257 decreasing uint32, first value 2^32/24."""
    first = struct.pack('<I', 178956970)
    for p in paths:
        for data, note in readable(p):
            i = data.find(first)
            while i >= 0:
                if i % 4 == 0 and i + 257 * 4 + 257 * 2 <= len(data):
                    t = np.frombuffer(data[i:i + 257 * 4], dtype='<u4').astype(float)
                    if np.all(np.diff(t) < 0):
                        curve = np.frombuffer(data[i + 257 * 4:i + 257 * 4 + 257 * 2], dtype='<u2') / 65535.0
                        return t, curve, f'{p}{note}, table @0x{i:X}', data
                i = data.find(first, i + 1)
    return None


def user_waves(qspi):
    """Waves 0..31 (user RAM on the original): bank 0 at the start of the QSPI
    image, 126 waves x 4 banks x 128 float32, time-reversed. It is checked that
    wave 32 there is the sine of the original. The mip levels are computed
    here, as for the ROM waves."""
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
    """Chord table: starts with twelve times (0, 12, 24), 8 sets x 12 x 3 bytes."""
    start = bytes([0, 12, 24]) * 12
    for p in paths:
        for data, note in readable(p):
            i = data.find(start)
            if i >= 0 and data[i + 36:i + 72] == bytes([0, 5, 10]) * 12:
                tab = [[tuple(data[i + s * 36 + 3 * k:i + s * 36 + 3 * k + 3]) for k in range(12)]
                       for s in range(8)]
                return tab, f'{p}{note}, table @0x{i:X}'
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--qspi', help='QSPI image of the Pro VS Mini')
    ap.add_argument('--intflash', help='program image of the Pro VS Mini (firmware 2.0 and later)')
    ap.add_argument('--search', action='append', default=[], help='additional search folder')
    ap.add_argument('--out', help='output folder for the headers (default: engine/include/provs/gen)')
    ap.add_argument('--roms', help='folder with your own files (default: roms/)')
    ap.add_argument('--no-auto', action='store_true',
                    help='use only the given files and folders, do not search on its own')
    args = ap.parse_args()
    global OUT, ROMS
    if args.out:
        OUT = args.out
    if args.roms:
        ROMS = args.roms
    for p in (args.qspi, args.intflash):
        if p and not os.path.exists(p):
            sys.exit(f'{p}: file not found')
    dirs = [d for d in args.search if os.path.isdir(d)] if args.no_auto else search_dirs(args.search)

    qspi = ([args.qspi] if args.qspi else []) + candidates(dirs, ('qspi',), ('.bin',))
    prog = ([args.intflash] if args.intflash else []) + candidates(dirs, ('intflash',), ('.bin',))

    adsr = find_adsr(qspi)
    chords = find_chords(prog)

    waves = user_waves(adsr[3]) if adsr else None
    print('ADSR times:  ' + (adsr[2] if adsr else 'no QSPI image found'))
    print('Waves 0-31:  ' + ('from the same QSPI image' if waves is not None else 'not available'))
    print('Chord sets:  ' + (chords[1] if chords else 'no program image of firmware 2.0 or later found'))
    def set_state(on):
        os.makedirs(OUT, exist_ok=True)
        text = ("// Auto-generated - do not edit by hand, do not commit.\n"
                f"#pragma once\n#define PROVS_HAS_PVM {1 if on else 0}\n")
        path = os.path.join(OUT, 'pvm_state.h')
        if not os.path.exists(path) or open(path).read() != text:
            open(path, 'w').write(text)

    if not (chords or adsr):
        data = os.path.join(OUT, 'pvm_data.h')
        if os.path.exists(data):
            os.remove(data)
        set_state(False)
        print('Nothing found - the engine builds without Behringer parts.')
        return 0

    out = ["// Auto-generated by gen_pvm.py - do not edit by hand, do not commit.",
           "// Data of the Behringer Pro VS Mini (optional).",
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
            "// Duration of an ADSR segment in seconds for the patch values 0..99.",
            "// Firmware (FUN_240130d4): index = value*660 >> 8 into the phase increment table",
            "// (257 x uint32 in the QSPI image); the segment ends after 2^32/increment steps.",
            "// [ANN] Converted with 48000 steps per second (0.5 ms .. 11.6 s). With",
            "// one step per 20-sample callback the result would be 10 ms .. 231 s; the",
            "// factory patches (organ, bass, plucked sounds with release values around 60)",
            "// fit only the short scale.",
            "inline constexpr float kPvmEnvSeconds[100] = {"]
    out += ['    ' + ', '.join(fl(v) for v in sec[i:i + 8]) + ',' for i in range(0, 100, 8)]
    out += ["};",
            "// The shape follows the curve table directly after the phase increment table;",
            "// it matches 1 - exp(-k * t/T) well with this k.",
            f"constexpr float kPvmEnvCurveK = {k:.2f}f;",
            "// Calls of the audio callback per second (20-sample block at 48 kHz);",
            "// applies to the mix envelope times",
            "constexpr float kPvmCallsPerSecond = 2400.0f;", ""]
    # Waves 0..31
    if waves is not None:
        peak = float(np.abs(waves).max())
        gain = 1.0 if peak <= 1.0 else peak * 1.0001
        out += ["// Waves 0..31 (user RAM of the original) in the Pro VS Mini's version,",
                "// 4 mip levels x 128 samples as in wavetables.h; wave = value/32767 * gain.",
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
            "// Chord sets of the chord mode (firmware 2.0 and later, used in FUN_2400a694):",
            "// for each pitch class of the played key three intervals in semitones.",
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
    print(f"pvm_data.h: ADSR times {'yes' if adsr else 'no'}, {8 if chords else 0} chord sets, "
          f"{32 if waves is not None else 0} user waves")
    return 0


if __name__ == '__main__':
    sys.exit(main())
