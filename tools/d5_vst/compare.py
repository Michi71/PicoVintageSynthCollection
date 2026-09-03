#!/usr/bin/env python3
"""Render the same factory patch through Roland's D-50 VST and through the
D5 engine, and compare: level under the note, tail one second after the
key, harmonic profile, spectral centroid, stereo correlation.

  compare.py [--note 60] [--vel 127] [--hold 1.5] [--tail 2.0] [--dry]
             [--csv out.csv] [--keep DIR] IDX [IDX ...] | --all | --bank N

Needs: tools/d5_vst/render_note (build_render.sh), the ROM build's
d5_pcm.bin, and a venv with requirements.txt. The VST renders at 32 kHz,
the engine's own rate, after rendering at 44.1 kHz (see below).
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
from scipy.signal import resample_poly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d5vst import D50VST, name_of

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SR = 32000

def metrics(x, hold, f0):
    """x: (n, 2) float32 at 32 kHz. Levels in dB, profile re h1."""
    # Levels and spectra as the power mean of both channels, never the mono
    # fold: a reverb with inverted sides (Large Room) cancels in L+R.
    def lev(a, b):
        seg = x[int(a*SR):int(b*SR)]; return 10*np.log10((seg**2).mean()+1e-20)
    t1 = max(0.1, hold - 0.6)
    seg = x[int(t1*SR):int(hold*SR)]
    w = np.hanning(len(seg))[:, None]
    S = (np.abs(np.fft.rfft(seg*w, axis=0))**2).sum(axis=1); f = np.fft.rfftfreq(len(seg), 1/SR)
    prof = [10*np.log10(S[(f > k*f0*0.97) & (f < k*f0*1.03)].max()+1e-20) for k in range(1, 11)]
    band = (f > 30) & (f < 12000); cent = (f[band]*S[band]).sum()/(S[band].sum()+1e-20)
    l, r = x[int(t1*SR):int(hold*SR), 0], x[int(t1*SR):int(hold*SR), 1]
    corr = float(np.corrcoef(l, r)[0, 1]) if l.std() > 0 and r.std() > 0 else 1.0
    env = [lev(t, t+0.1) for t in np.arange(0.0, hold+1.0, 0.1)]
    return dict(hold=lev(t1, hold), attack=lev(0.0, 0.1), tail1=lev(hold+1.0, hold+1.5), tail2=lev(hold+1.5, hold+2.0),
                centroid=cent, prof=[p-prof[0] for p in prof], corr=corr, peak=float(np.abs(x).max()), env=env)

def render_ours(pcm, idx, note, vel, hold, tail, dry, out):
    cmd = [os.path.join(HERE, 'render_note'), pcm, out, str(idx), str(note), str(vel), str(hold), str(tail)] + (['dry'] if dry else [])
    subprocess.run(cmd, check=True)
    return np.fromfile(out, dtype=np.float32).reshape(-1, 2)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('idx', nargs='*', type=int)
    ap.add_argument('--all', action='store_true'); ap.add_argument('--bank', type=int)
    ap.add_argument('--note', type=int, default=60); ap.add_argument('--vel', type=int, default=127)
    ap.add_argument('--hold', type=float, default=1.5); ap.add_argument('--tail', type=float, default=2.0)
    ap.add_argument('--dry', action='store_true', help='reverb and chorus balance 0 on both sides')
    ap.add_argument('--csv'); ap.add_argument('--keep', help='directory to keep the renders (f32 stereo)')
    ap.add_argument('--pcm', default=os.path.join(ROOT, 'build-d5', 'PicoFaceD5_rom', 'd5_pcm.bin'))
    a = ap.parse_args()
    tmp = a.keep or tempfile.mkdtemp(prefix='d5vst_')
    os.makedirs(tmp, exist_ok=True)
    bankfile = os.path.join(tmp, 'bank.bin')
    subprocess.run([os.path.join(HERE, 'render_note'), 'dumpbank', bankfile], check=True)
    bank = open(bankfile, 'rb').read(); n_patches = len(bank)//448
    idxs = list(range(n_patches)) if a.all else (list(range((a.bank-1)*64, a.bank*64)) if a.bank else a.idx)
    vst = D50VST()
    f0 = 440.0*2**((a.note-69)/12)
    rows = []
    print(f'{"idx":>4} {"name":20s} {"hold V/O":>13} {"dLev":>6} {"tail1 V/O":>13} {"dTail":>6} {"cent V/O":>11} {"h2 V/O":>11} {"h5 V/O":>11} {"corr V/O":>11} {"clamped":>7}')
    for idx in idxs:
        b = bank[idx*448:(idx+1)*448]
        if a.dry:
            b = bytearray(b); b[384+31] = 0; b[128+10+35] = 0; b[320+10+35] = 0; b = bytes(b)
        log = vst.set_patch(b)
        # The VST renders silence at 32 kHz once a patch is set; 44.1 kHz
        # works, so it renders there and the comparison resamples (320/441).
        x44 = vst.render(a.note, a.vel, a.hold, a.tail, 44100.0)
        x_v = np.ascontiguousarray(resample_poly(x44, 320, 441, axis=1).T.astype(np.float32))
        x_o = render_ours(a.pcm, idx, a.note, a.vel, a.hold, a.tail, a.dry, os.path.join(tmp, f'ours_{idx}.f32'))
        if a.keep: x_v.tofile(os.path.join(tmp, f'vst_{idx}.f32'))
        mv, mo = metrics(x_v, a.hold, f0), metrics(x_o, a.hold, f0)
        name = name_of(b)
        print(f'{idx:4d} {name:20s} {mv["hold"]:6.1f}/{mo["hold"]:6.1f} {mo["hold"]-mv["hold"]:6.1f} {mv["tail1"]:6.1f}/{mo["tail1"]:6.1f} {mo["tail1"]-mv["tail1"]:6.1f} {mv["centroid"]:5.0f}/{mo["centroid"]:5.0f} {mv["prof"][1]:5.1f}/{mo["prof"][1]:5.1f} {mv["prof"][4]:5.1f}/{mo["prof"][4]:5.1f} {mv["corr"]:5.2f}/{mo["corr"]:5.2f} {len(log):7d}', flush=True)
        rows.append((idx, name, mv, mo, len(log)))
    if a.csv:
        with open(a.csv, 'w') as f:
            f.write('idx,name,hold_vst,hold_ours,tail1_vst,tail1_ours,tail2_vst,tail2_ours,attack_vst,attack_ours,centroid_vst,centroid_ours,corr_vst,corr_ours,peak_vst,peak_ours,clamped,' + ','.join(f'h{k}_vst' for k in range(2, 11)) + ',' + ','.join(f'h{k}_ours' for k in range(2, 11)) + '\n')
            for idx, name, mv, mo, cl in rows:
                f.write(f'{idx},"{name}",{mv["hold"]:.2f},{mo["hold"]:.2f},{mv["tail1"]:.2f},{mo["tail1"]:.2f},{mv["tail2"]:.2f},{mo["tail2"]:.2f},{mv["attack"]:.2f},{mo["attack"]:.2f},{mv["centroid"]:.0f},{mo["centroid"]:.0f},{mv["corr"]:.3f},{mo["corr"]:.3f},{mv["peak"]:.4f},{mo["peak"]:.4f},{cl},' + ','.join(f'{v:.1f}' for v in mv['prof'][1:]) + ',' + ','.join(f'{v:.1f}' for v in mo['prof'][1:]) + '\n')
    if len(rows) > 1:
        d = np.array([mo['hold']-mv['hold'] for _, _, mv, mo, _ in rows]); dt = np.array([mo['tail1']-mv['tail1'] for _, _, mv, mo, _ in rows])
        dc = np.array([1200*np.log2(mo['centroid']/mv['centroid']) for _, _, mv, mo, _ in rows if mv['centroid'] > 0 and mo['centroid'] > 0])
        print(f'\n{len(rows)} patches: level ours-VST median {np.median(d):+.1f} dB (p10 {np.percentile(d,10):+.1f}, p90 {np.percentile(d,90):+.1f}); tail+1s median {np.median(dt):+.1f} dB; centroid median {np.median(dc):+.0f} cents (p10 {np.percentile(dc,10):+.0f}, p90 {np.percentile(dc,90):+.0f})')
if __name__ == '__main__':
    main()
