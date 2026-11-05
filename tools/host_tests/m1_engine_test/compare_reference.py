#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Compare PicoFaceM1 host renders against the Python reference engine.

    compare_reference.py <gendir> <reference_dir> <reference_romdir> [prog ...]

<reference_dir> holds the analysis' m1_engine.py (not part of this repository);
<reference_romdir> the ROM set it loads (it reads the Plus+1 OS, whose
multisound content is identical to Rev 19's). For each single-oscillator
factory program the C++ engine renders OSC 1 in reference mode (oscillator,
IC30 kernel, VDF, VDA and EGs only, effects off) and the reference renders the
same multisound, note, EGs, cutoff, EG intensity and level. The pitch step is
quantised to the TG's 4.12 register on both sides.

Prints, per program, the peak difference relative to the reference's peak and
the correlation. Exit status 1 if any relative error exceeds 5e-3.
"""
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FS = 31250


def s8(v):
    return v - 256 if v >= 128 else v


def read_wav(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(float) / 32767.0
    return x.reshape(-1, 2)


def main():
    gendir, refdir, romdir = sys.argv[1:4]
    progs = [int(a) for a in sys.argv[4:]] or None
    sys.path.insert(0, refdir)
    import m1_engine as R  # the reference

    orig = R.pitch_step
    R.pitch_step = lambda note, split, fine_128=0: round(orig(note, split, fine_128) * 4096) / 4096
    rom = R.M1Rom(romdir)
    mss = rom.multisounds()
    tables = R.M1Tables(rom, romdir)

    sys.path.insert(0, os.path.join(HERE, "..", "..", "m1_extract"))
    from m1_rom import M1RomSet, load_preload
    pre = load_preload(M1RomSet(os.path.join(HERE, "..", "..", "..", "instruments", "PicoFaceM1", "roms"))
                       .preload_path)
    gate_blk = 58 * 650                          # the reference releases on a block edge
    worst = 0.0
    for i, p in enumerate(pre["programs"]):
        if progs is not None and i not in progs:
            continue
        if p[10] != 0:
            continue                             # single programs only
        o = p[63:103]
        ms = p[12]
        note = 60
        patch = dict(cutoff=o[8], vdf_eg_int=o[11],
                     vdf_eg=dict(start=0, seq=[(o[15], s8(o[16])), (o[17], s8(o[18])), (o[19], s8(o[20]))],
                                 release=(o[21], s8(o[22]))),
                     vda_eg=dict(start=0, seq=[(o[29], o[30]), (o[31], o[32]), (o[33], o[34])],
                                 release=(o[35], 0)),
                     level=o[23])
        ref = R.render_voice(rom, tables, mss[ms], note, seconds=2.0, gate=1.2, patch=patch)
        out = os.path.join(HERE, "out", "cmp_p%02d.wav" % i)
        subprocess.run([os.path.join(HERE, "m1_render"), gendir, "prog", str(i), str(note), "100",
                        str(gate_blk / FS), "2.0", out, "ref"], check=True, capture_output=True)
        c = read_wav(out)[:, 0] / (45.0 / 63.0)  # pan 5:5 -> bus A = 45/63
        n = min(len(c), len(ref))
        c, r = c[:n], ref[:n].astype(float)
        pk = max(np.abs(r).max(), 1e-9)
        err = np.abs(c - r).max() / pk
        cor = np.corrcoef(c, r)[0, 1] if r.std() > 0 else 1.0
        worst = max(worst, err)
        print("P%02d %-10s ms %2d %-10s peak %.4f  max|diff|/peak %.2e  r %.6f" % (
            i, bytes(p[:10]).decode("latin1"), ms, mss[ms]["name"], pk, err, cor))
    print("worst relative error %.2e" % worst)
    return 1 if worst > 5e-3 else 0


if __name__ == "__main__":
    sys.exit(main())
