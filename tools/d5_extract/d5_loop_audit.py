#!/usr/bin/env python3
"""Audit the static loops with cp_sampleprep's FindLoopPoints.

    python3 tools/d5_extract/d5_loop_audit.py

A sustained loop that cannot be looped is not a sustained loop, so the tool
that finds loop points for PicoFaceCP doubles as a verdict on these
boundaries -- and it is a far better judge than the period estimator in
d5_table_derive.py, which locks onto high harmonics. It found the
harpsichord's real period of 156 words where the estimator had bottomed out
at its 16-word floor.

Each region is offered at several period counts and judged by its best score,
because the tool's default of ten periods rejects any sample too short to
contain that many. Two loops looked unloopable until that was allowed for.
Its own scale: <=0.01 excellent, <=0.05 good, <=0.15 acceptable, >0.30
unusable. Regions failing it are re-tested in halves and quarters, since a
region that merges two samples usually has one half that loops cleanly.

Needs the FindLoopPoints binary; build it with
tools/cp_sampleprep/build_loop_finder.sh.
"""
import json
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, 'tools/d5_extract')
from d5_rom import D5RomSet, SAMPLE_RATE  # noqa: E402

TMP = ('/private/tmp/claude-501/-Users-michael-GitHub-PicoVintageSynthCollection/'
       '6bac39c9-dc59-4655-a523-35d2a3f8df69/scratchpad/probe/audit.wav')
TOOL = './tools/cp_sampleprep/FindLoopPoints'
PERIODS = (2, 3, 4, 5, 6, 8, 10, 14)


def write(seg):
    pk = float(np.max(np.abs(seg))) + 1e-9
    with wave.open(TMP, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(np.clip(seg / pk * 0.9 * 32767, -32767, 32767)
                       .astype('<i2').tobytes())


def best_score(seg):
    if len(seg) < 256:
        return None, None
    write(seg)
    best, per = None, None
    for p in PERIODS:
        try:
            r = subprocess.run([TOOL, TMP, str(p)], capture_output=True,
                               text=True, timeout=60).stdout
        except Exception:
            continue
        for line in r.splitlines():
            if 'Score:' in line:
                try:
                    v = float(line.split('Score:')[1].split()[0])
                except ValueError:
                    continue
                if v > 1e30:
                    continue
                if best is None or v < best:
                    best = v
                    per = p
    return best, per


def verdict(s):
    if s is None:
        return 'KEIN LOOP FINDBAR'
    if s <= 0.01:
        return 'exzellent'
    if s <= 0.05:
        return 'gut'
    if s <= 0.15:
        return 'brauchbar'
    if s <= 0.30:
        return 'grenzwertig'
    return 'UNBRAUCHBAR'


def main():
    rs = D5RomSet('/Users/michael/develop/Roland_D50')
    x = np.asarray(rs.audio, dtype=np.float64)
    tab = json.load(open('tools/d5_extract/d5_sample_table.json'))['samples']

    print(f"{'PCM':>4} {'Name':7} {'Worte':>6} {'bester Score':>13}  Urteil")
    bad = []
    for e in tab[47:76]:
        s, p = best_score(x[e['start']:e['end']])
        v = verdict(s)
        if s is None or s > 0.15:
            bad.append(e['pcm'])
        st = f"{s:13.5f}" if s is not None else f"{'-':>13}"
        print(f"{e['pcm']:4} {e['name']:7} {e['end']-e['start']:6} {st}  {v}")

    print(f"\nProblematisch: {bad}")
    for pcm in bad:
        e = tab[pcm - 1]
        a, b = e['start'], e['end']
        print(f"\n  PCM {pcm} {e['name']} ({a}..{b}) — Teilbereiche:")
        for div in (2, 4):
            step = (b - a) // div
            if step < 512:
                continue
            for k in range(div):
                aa, bb = a + k * step, a + (k + 1) * step
                s, p = best_score(x[aa:bb])
                st = f"{s:.5f}" if s is not None else 'kein Loop'
                tag = '  <== gut' if s is not None and s <= 0.05 else ''
                print(f"     {aa}..{bb} ({bb-aa:5} Worte): {st}{tag}")


if __name__ == '__main__':
    main()
