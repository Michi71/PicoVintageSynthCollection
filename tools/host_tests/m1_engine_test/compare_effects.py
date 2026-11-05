#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Compare the PicoFaceM1 effect section against the Python reference.

    compare_effects.py <gendir> <reference_dir>

Runs every factory effect block (programs and combinations of the preload)
over the same input -- a decaying noise burst followed by silence -- through
the engine's EffectSection (m1_render fxblock) and the reference's
M1EffectSection (m1_effects.py / m1_presets.py of the analysis, not in this
repository). Prints the correlation and the level difference per effect type.

Agreement is not expected to be exact: the engine reads its coefficients
from the OS ROM, the reference partly from other sources, and the two
disagree in documented places (doc/ENGINE.md, "Effects").
"""
import os
import subprocess
import sys
import wave
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def write_wav(path, l, r):
    x = np.clip(np.stack([l, r], 1), -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(31250)
        w.writeframes((x * 32767).astype("<i2").tobytes())


def read_wav(path):
    with wave.open(path) as w:
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").reshape(-1, 2).astype(float) / 32767


def main():
    gendir, refdir = sys.argv[1:3]
    sys.path.insert(0, refdir)
    import m1_presets as P
    sys.path.insert(0, os.path.join(HERE, "..", "..", "m1_extract"))
    from m1_rom import M1RomSet, load_preload
    pre = load_preload(M1RomSet(os.path.join(HERE, "..", "..", "..", "instruments", "PicoFaceM1", "roms"))
                       .preload_path)
    rng = np.random.default_rng(1)
    n = 31250 * 2
    env = np.exp(-np.arange(n) / 3000.0) * (np.arange(n) < 15000)
    l = 0.25 * rng.standard_normal(n) * env
    r = 0.25 * rng.standard_normal(n) * env
    inp = os.path.join(HERE, "out", "fx_in.wav")
    os.makedirs(os.path.dirname(inp), exist_ok=True)
    write_wav(inp, l, r)
    l, r = read_wav(inp).T
    blocks = [p[38:63] for p in pre["programs"]] + [c[11:36] for c in pre["combis"]]
    per = defaultdict(list)
    seen = set()
    for b in blocks:
        b = bytes(b)
        if b in seen:
            continue
        seen.add(b)
        out = os.path.join(HERE, "out", "fx_out.wav")
        subprocess.run([os.path.join(HERE, "m1_render"), gendir, "fxblock", b.hex(), inp, out], check=True)
        c = read_wav(out)
        sec = P.build_effect_section(P.parse_effect_block(b))
        ref = np.stack(sec.process(l, r)[:2], 1)
        cc = np.corrcoef(c.ravel(), ref.ravel())[0, 1]
        db = 20 * np.log10((np.sqrt((c ** 2).mean()) + 1e-12) / (np.sqrt((ref ** 2).mean()) + 1e-12))
        key = (b[0], b[1], "ser" if b[8] & 0x10 else "par")
        per[key].append((cc, db))
    for key in sorted(per):
        v = np.array(per[key])
        print("%-13s -> %-13s %s  n=%2d  r min %.3f  level %+.2f dB" % (
            P.EFFECT_NAMES[key[0]], P.EFFECT_NAMES[key[1]], key[2], len(v), v[:, 0].min(), v[:, 1].mean()))


if __name__ == "__main__":
    sys.exit(main())
