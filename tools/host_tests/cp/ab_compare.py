#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""ab_compare.py -- compare two directories of render.cpp output, note by note.

    python3 ab_compare.py <dir_old> <dir_new>

Per render: how far into the note the two are still bit-identical (a shorter
sample diverges at its new loop point), the sustain level difference in three
200 ms windows, the spectral centroid change at 1.0 s, the loop ripple (std of
the 10 ms RMS over 1-2 s, in dB) of both, and the level difference 300 ms into
the release. Written for the 4 MB cut of the CP sample sets, where the claim
was: attack identical, sustain within 1 dB, ripple unchanged.
"""
import sys, os, re, glob
import numpy as np

SR = 44100
NAMES = {0: "Rd I", 1: "Rd II", 2: "Wr", 3: "Clv", 4: "Pno", 5: "CP"}

def load(p):
    return np.fromfile(p, dtype=np.int16).astype(np.float64) / 32768

def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-9)

def centroid(x):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return (S * f).sum() / (S.sum() + 1e-30)

def ripple(x):
    w = int(0.010 * SR)
    e = np.array([np.sqrt(np.mean(x[k:k + w] ** 2)) for k in range(0, len(x) - w, w)])
    return 20 * np.log10(1 + e.std() / (e.mean() + 1e-9))

def seg(x, t0, t1):
    return x[int(t0 * SR):int(t1 * SR)]

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    old_dir, new_dir = sys.argv[1], sys.argv[2]
    key = lambda q: tuple(int(v) for v in re.search(r"i(\d)_n(\d+)_v(\d+)", q).groups())
    files = sorted(glob.glob(os.path.join(old_dir, "*.raw")), key=key)
    print(f"{'voice':6s} {'note':>4s} {'vel':>3s} | {'same until':>10s} | "
          f"{'sustain dRMS dB @0.6/1.0/1.9 s':>30s} | {'dCent %':>7s} | {'ripple old/new':>14s} | {'rel dRMS':>8s}")
    worst = []
    for p in files:
        i, n, v = key(p)
        q = os.path.join(new_dir, os.path.basename(p))
        if not os.path.exists(q):
            continue
        a, b = load(p), load(q)
        m = min(len(a), len(b)); a, b = a[:m], b[:m]
        diff = np.nonzero(a != b)[0]
        same = f"{diff[0] / SR * 1000:7.0f} ms" if len(diff) else "       all"
        d = [rms_db(seg(b, t, t + 0.2)) - rms_db(seg(a, t, t + 0.2)) for t in (0.6, 1.0, 1.9)]
        dc = 100 * (centroid(seg(b, 1.0, 1.3)) / centroid(seg(a, 1.0, 1.3)) - 1)
        rp = ripple(seg(a, 1.0, 2.0)), ripple(seg(b, 1.0, 2.0))
        dr = rms_db(seg(b, 2.3, 2.5)) - rms_db(seg(a, 2.3, 2.5))
        print(f"{NAMES.get(i, str(i)):6s} {n:4d} {v:3d} | {same:>10s} | "
              f"{d[0]:+9.2f} {d[1]:+9.2f} {d[2]:+9.2f}   | {dc:+7.1f} | {rp[0]:6.2f} {rp[1]:7.2f} | {dr:+8.2f}")
        worst.append((max(abs(x) for x in d), NAMES.get(i, str(i)), n, v))
    worst.sort(reverse=True)
    print("\nlargest sustain level changes: " +
          ", ".join(f"{w[1]} n{w[2]} v{w[3]} {w[0]:.2f} dB" for w in worst[:6]))

if __name__ == "__main__":
    main()
