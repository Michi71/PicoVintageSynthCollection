#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Render the same front panel through Roland's JUNO-60 VST and through the
J6 engine, and compare: level under the note, the tail after the key, the
harmonic profile, the spectral centroid and the stereo width.

  compare.py [--note 60] [--hold 1.5] [--tail 2.0] [--dry]
             [--csv out.csv] [--keep DIR] IDX [IDX ...] | --all

Both sides run at 44.1 kHz, the engine's own rate, so neither is resampled.
--dry switches the chorus off on both sides, which is the form to reach for
when the question is about the voice rather than about the chorus.
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
from scipy.signal import butter, sosfilt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from j6vst import J6VST, read_bank, NPARAM, CHORUS

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 44100


_HP = None


def subsonic_cut(x):
    """Drop everything under 20 Hz before measuring anything.

    A pulse whose width the LFO moves carries a wandering DC term, and the
    plugin has nothing to block it: on Organ 3 that subsonic wobble is 99 % of
    the total energy, at a few hertz, where neither a speaker nor an ear will
    ever find it. Measured raw it buries the patch, and comparing two engines
    that block it differently compares their DC blockers rather than their
    sound.
    """
    global _HP
    if _HP is None:
        _HP = butter(2, 20.0, 'highpass', fs=SR, output='sos')
    return sosfilt(_HP, x, axis=0).astype(np.float32)


def metrics(x, hold, f0):
    """x: (n, 2) float32 at 44.1 kHz. Levels in dB, the profile re h1."""
    x = subsonic_cut(x)
    def lev(a, b):
        seg = x[int(a * SR):int(b * SR)]
        return 10 * np.log10((seg ** 2).mean() + 1e-20) if len(seg) else -200.0
    t1 = max(0.1, hold - 0.6)
    seg = x[int(t1 * SR):int(hold * SR)]
    w = np.hanning(len(seg))[:, None]
    S = (np.abs(np.fft.rfft(seg * w, axis=0)) ** 2).sum(axis=1)
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    prof = [10 * np.log10(S[(f > k * f0 * 0.97) & (f < k * f0 * 1.03)].max() + 1e-20)
            for k in range(1, 11)]
    band = (f > 30) & (f < 16000)
    cent = (f[band] * S[band]).sum() / (S[band].sum() + 1e-20)
    l, r = x[int(t1 * SR):int(hold * SR), 0], x[int(t1 * SR):int(hold * SR), 1]
    corr = float(np.corrcoef(l, r)[0, 1]) if l.std() > 0 and r.std() > 0 else 1.0
    # Attack: how long to within 3 dB of the peak of the first half second.
    a = np.abs(x[:int(0.5 * SR)]).mean(axis=1)
    k = 64
    e = a[:len(a) // k * k].reshape(-1, k).mean(axis=1)
    pk = e.max() if len(e) else 0.0
    t_att = (np.argmax(e >= pk * 0.7) * k / SR) if pk > 0 else 0.0
    env = [lev(t, t + 0.1) for t in np.arange(0.0, hold + 1.5, 0.1)]
    return dict(hold=lev(t1, hold), attack=lev(0.0, 0.1), t_att=t_att,
                tail1=lev(hold + 0.3, hold + 0.6), tail2=lev(hold + 1.0, hold + 1.5),
                centroid=cent, prof=[p - prof[0] for p in prof], corr=corr,
                peak=float(np.abs(x).max()), env=env)


ENGINE = os.path.join(HERE, 'render_note')


def render_ours(idx, q, note, hold, tail, dry, out):
    ps = ','.join(f'{v:.6f}' for v in q)
    subprocess.run([ENGINE, out, '--params', ps,
                    str(note), '100', str(hold), str(tail)] + (['dry'] if dry else []),
                   check=True)
    return np.fromfile(out, dtype=np.float32).reshape(-1, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('idx', nargs='*', type=int)
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--note', type=int, default=60)
    ap.add_argument('--hold', type=float, default=1.5)
    ap.add_argument('--tail', type=float, default=2.0)
    ap.add_argument('--dry', action='store_true', help='chorus off on both sides')
    ap.add_argument('--csv')
    ap.add_argument('--keep', help='directory to keep the renders (f32 stereo)')
    ap.add_argument('--engine', help='a different render_note binary, to compare two engine builds')
    a = ap.parse_args()
    global ENGINE
    if a.engine:
        ENGINE = os.path.abspath(a.engine)

    tmp = a.keep or tempfile.mkdtemp(prefix='j6vst_')
    os.makedirs(tmp, exist_ok=True)
    bankfile = os.path.join(tmp, 'bank.txt')
    subprocess.run([ENGINE, 'dumpbank', bankfile], check=True)
    bank = read_bank(bankfile)
    idxs = list(range(len(bank))) if a.all else a.idx
    if not idxs:
        ap.error('name some patches, or --all')

    vst = J6VST()
    f0 = 440.0 * 2 ** ((a.note - 69) / 12)
    rows = []
    print(f'{"idx":>3} {"name":16s} {"hold V/O":>13} {"dLev":>6} {"tail V/O":>13} '
          f'{"dTail":>6} {"cent V/O":>11} {"h2 V/O":>11} {"h5 V/O":>11} {"corr V/O":>11} {"clamp":>5}')
    for idx in idxs:
        name, q = bank[idx]
        if a.dry:
            q = list(q); q[CHORUS] = 0.0
        log = vst.set_patch(q, dry=a.dry)
        x_v = np.ascontiguousarray(vst.render(q, a.note, 100, a.hold, a.tail).T.astype(np.float32))
        x_o = render_ours(idx, q, a.note, a.hold, a.tail, a.dry,
                          os.path.join(tmp, f'ours_{idx}.f32'))
        n = min(len(x_v), len(x_o))
        x_v, x_o = x_v[:n], x_o[:n]
        if a.keep:
            x_v.tofile(os.path.join(tmp, f'vst_{idx}.f32'))
        mv, mo = metrics(x_v, a.hold, f0), metrics(x_o, a.hold, f0)
        print(f'{idx:3d} {name:16s} {mv["hold"]:6.1f}/{mo["hold"]:6.1f} {mo["hold"]-mv["hold"]:6.1f} '
              f'{mv["tail1"]:6.1f}/{mo["tail1"]:6.1f} {mo["tail1"]-mv["tail1"]:6.1f} '
              f'{mv["centroid"]:5.0f}/{mo["centroid"]:5.0f} {mv["prof"][1]:5.1f}/{mo["prof"][1]:5.1f} '
              f'{mv["prof"][4]:5.1f}/{mo["prof"][4]:5.1f} {mv["corr"]:5.2f}/{mo["corr"]:5.2f} '
              f'{len(log):5d}', flush=True)
        rows.append((idx, name, mv, mo, len(log)))

    if a.csv:
        with open(a.csv, 'w') as f:
            f.write('idx,name,hold_vst,hold_ours,tail1_vst,tail1_ours,tail2_vst,tail2_ours,'
                    'attack_vst,attack_ours,tatt_vst,tatt_ours,centroid_vst,centroid_ours,'
                    'corr_vst,corr_ours,peak_vst,peak_ours,clamped,'
                    + ','.join(f'h{k}_vst' for k in range(2, 11)) + ','
                    + ','.join(f'h{k}_ours' for k in range(2, 11)) + '\n')
            for idx, name, mv, mo, cl in rows:
                f.write(f'{idx},"{name}",{mv["hold"]:.2f},{mo["hold"]:.2f},{mv["tail1"]:.2f},'
                        f'{mo["tail1"]:.2f},{mv["tail2"]:.2f},{mo["tail2"]:.2f},{mv["attack"]:.2f},'
                        f'{mo["attack"]:.2f},{mv["t_att"]:.3f},{mo["t_att"]:.3f},{mv["centroid"]:.0f},'
                        f'{mo["centroid"]:.0f},{mv["corr"]:.3f},{mo["corr"]:.3f},{mv["peak"]:.4f},'
                        f'{mo["peak"]:.4f},{cl},'
                        + ','.join(f'{v:.1f}' for v in mv['prof'][1:]) + ','
                        + ','.join(f'{v:.1f}' for v in mo['prof'][1:]) + '\n')

    if len(rows) > 1:
        d = np.array([mo['hold'] - mv['hold'] for _, _, mv, mo, _ in rows])
        dt = np.array([mo['tail1'] - mv['tail1'] for _, _, mv, mo, _ in rows])
        dc = np.array([1200 * np.log2(mo['centroid'] / mv['centroid'])
                       for _, _, mv, mo, _ in rows if mv['centroid'] > 0 and mo['centroid'] > 0])
        print(f'\n{len(rows)} patches: level ours-VST median {np.median(d):+.1f} dB '
              f'(p10 {np.percentile(d,10):+.1f}, p90 {np.percentile(d,90):+.1f}, sd {d.std():.1f}); '
              f'tail median {np.median(dt):+.1f} dB; '
              f'centroid median {np.median(dc):+.0f} cents '
              f'(p10 {np.percentile(dc,10):+.0f}, p90 {np.percentile(dc,90):+.0f})')


if __name__ == '__main__':
    main()
