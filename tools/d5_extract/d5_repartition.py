#!/usr/bin/env python3
"""Re-partition the stretches that fail the loop audit.

    python3 tools/d5_extract/d5_repartition.py

The nine failures fall into five stretches, each bounded by samples the audit
rates excellent. Within a stretch the number of samples is fixed and so are
its outer edges; only the boundaries between them may move, on the 512-word
grid. FindLoopPoints scores every candidate segment, and a dynamic program
picks the partition with the lowest total.
"""
import json
import subprocess
import sys
import wave
from functools import lru_cache

import numpy as np

sys.path.insert(0, 'tools/d5_extract')
from d5_rom import D5RomSet, SAMPLE_RATE  # noqa: E402

TMP = 'tools/d5_extract/out/_repartition.wav'
TOOL = './tools/cp_sampleprep/FindLoopPoints'
PERIODS = (3, 5, 8, 12)
GRID = 512

import os
os.makedirs('tools/d5_extract/out', exist_ok=True)
rs = D5RomSet(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/develop/Roland_D50'))
x = np.asarray(rs.audio, dtype=np.float64)


@lru_cache(maxsize=None)
def loop_score(a, b):
    seg = x[a:b]
    if len(seg) < 512:
        return 9.0
    pk = float(np.max(np.abs(seg))) + 1e-9
    with wave.open(TMP, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(np.clip(seg / pk * 0.9 * 32767, -32767, 32767)
                       .astype('<i2').tobytes())
    best = None
    for p in PERIODS:
        try:
            out = subprocess.run([TOOL, TMP, str(p)], capture_output=True,
                                 text=True, timeout=60).stdout
        except Exception:
            continue
        for line in out.splitlines():
            if 'Score:' in line:
                try:
                    v = float(line.split('Score:')[1].split()[0])
                except ValueError:
                    continue
                if v > 1e30:
                    continue
                if best is None or v < best:
                    best = v
    return 3.0 if best is None else min(best, 3.0)


def repartition(a, b, n, max_words=8192):
    """Best n-way split of [a,b) on the 512 grid, by total loop score.

    A bare score minimum is degenerate: a 512-word segment loops trivially,
    so the search makes the first samples tiny and the last one huge. Sizes
    are therefore held to a plausible band and deviation from the stretch's
    average size costs, so a split has to earn its irregularity."""
    avg = (b - a) / n
    pos = list(range(a, b + 1, GRID))
    m = len(pos) - 1
    INF = float('inf')
    dp = [[INF] * (n + 1) for _ in range(m + 1)]
    par = [[None] * (n + 1) for _ in range(m + 1)]
    dp[0][0] = 0.0
    span = max_words // GRID
    for i in range(m):
        for k in range(n):
            if dp[i][k] == INF:
                continue
            for j in range(i + 1, min(i + span, m) + 1):
                size = pos[j] - pos[i]
                if size < 1024 or size > 3.0 * avg:
                    continue
                import math
                irregular = abs(math.log2(size / avg))
                c = dp[i][k] + loop_score(pos[i], pos[j]) + 0.35 * irregular
                if c < dp[j][k + 1]:
                    dp[j][k + 1] = c
                    par[j][k + 1] = i
    if dp[m][n] == INF:
        return None
    cuts = []
    i, k = m, n
    while k:
        i2 = par[i][k]
        cuts.append(pos[i2])
        i, k = i2, k - 1
    cuts.reverse()
    return cuts + [b]


tab = json.load(open('tools/d5_extract/d5_sample_table.json'))['samples']
STRETCHES = [(58, 60), (62, 65), (66, 68), (69, 72), (73, 75)]

result = {}
for first, last in STRETCHES:
    a = tab[first - 1]['start']
    b = tab[last - 1]['end']
    n = last - first + 1
    names = [tab[p - 1]['name'] for p in range(first, last + 1)]
    print(f"\n=== PCM {first}..{last} ({', '.join(names)}) "
          f"{a}..{b}, {b-a} Worte, {n} Samples ===")
    print("  bisher:")
    old_total = 0.0
    for p in range(first, last + 1):
        e = tab[p - 1]
        s = loop_score(e['start'], e['end'])
        old_total += s
        print(f"    {p:3} {e['name']:7} {e['start']:6}..{e['end']:6} "
              f"({e['end']-e['start']:5} W)  Score {s:7.4f}")
    cuts = repartition(a, b, n)
    if not cuts:
        print("  keine Zerlegung gefunden")
        continue
    print("  neu:")
    new_total = 0.0
    entries = []
    for idx, p in enumerate(range(first, last + 1)):
        aa, bb = cuts[idx], cuts[idx + 1]
        s = loop_score(aa, bb)
        new_total += s
        mark = '' if (aa, bb) == (tab[p-1]['start'], tab[p-1]['end']) else '  <== neu'
        print(f"    {p:3} {tab[p-1]['name']:7} {aa:6}..{bb:6} "
              f"({bb-aa:5} W)  Score {s:7.4f}{mark}")
        entries.append((p, aa, bb))
    print(f"  Summe Score: {old_total:.3f} -> {new_total:.3f}"
          + ("   VERBESSERUNG" if new_total < old_total - 1e-9 else "   keine Verbesserung"))
    if new_total < old_total - 1e-9:
        for p, aa, bb in entries:
            result[p] = (aa, bb)

import os
os.makedirs('tools/d5_extract/out', exist_ok=True)
json.dump({str(k): v for k, v in result.items()},
          open('tools/d5_extract/out/repartition.json', 'w'), indent=1)
print(f"\n{len(result)} Samples mit neuen Grenzen")
