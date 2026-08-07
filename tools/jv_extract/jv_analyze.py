#!/usr/bin/env python3
"""Analyse the CSV that jv_probe writes.

    python3 jv_analyze.py effects <csv>        which offsets do anything
    python3 jv_analyze.py curves  <csv> [off..] response curve per offset
    python3 jv_analyze.py shape   <csv> [off..] envelope shape over the note

jv_probe emits probe rows P_<off>_<val> (byte set on the live tone) and control
rows C_<off>_<val> (same byte on a switched-off tone). Everything here compares
those two, never a probe against a global baseline -- see README.md, "Two traps".
"""
import csv
import sys
from collections import defaultdict

SPARK = "▁▂▃▄▅▆▇█"


def load(path):
    rows = {}
    for r in csv.DictReader(open(path)):
        rows[r["label"]] = r
    pairs = defaultdict(dict)
    for lab, r in rows.items():
        kind, _, rest = lab.partition("_")
        if kind not in ("P", "C"):
            continue
        off, _, val = rest.partition("_")
        pairs[int(off)][(int(val), kind)] = r
    return pairs


def num(r, k):
    return float(r[k])


def reldiff(a, b):
    return abs(a - b) / (abs(b) + 1e-12)


def spark(vals):
    lo, hi = min(vals), max(vals)
    if hi - lo < 1e-9:
        return "─" * len(vals)
    return "".join(SPARK[min(7, int((v - lo) / (hi - lo) * 7.999))] for v in vals)


def monotonic(v):
    d = [b - a for a, b in zip(v, v[1:])]
    if not d:
        return "?"
    up = sum(1 for x in d if x > 0)
    dn = sum(1 for x in d if x < 0)
    if up >= len(d) * 0.8:
        return "rising"
    if dn >= len(d) * 0.8:
        return "falling"
    return "non-monotonic"


def shape_of(r):
    b = [num(r, f"b{i}") for i in range(8)]
    s = sum(b) + 1e-12
    return [x / s for x in b]


def cmd_effects(pairs):
    print(f"{'off':>5}  effect")
    live = 0
    for off in sorted(pairs):
        vals = sorted({v for v, _ in pairs[off]})
        found = []
        for v in vals:
            p, c = pairs[off].get((v, "P")), pairs[off].get((v, "C"))
            if not p or not c or p["hash"] == c["hash"]:
                continue  # byte-identical render -> parameter is inert here
            d = []
            if reldiff(num(p, "rms"), num(c, "rms")) > 0.02:
                d.append(f"lvl{num(p, 'rms') / (num(c, 'rms') + 1e-12):.2f}x")
            if num(c, "pitch") > 1 and reldiff(num(p, "pitch"), num(c, "pitch")) > 0.01:
                d.append(f"pitch{num(p, 'pitch'):.0f}Hz")
            if reldiff(num(p, "bright"), num(c, "bright")) > 0.03:
                d.append(f"brt{num(p, 'bright') / (num(c, 'bright') + 1e-12):.2f}x")
            if sum(abs(a - b) for a, b in zip(shape_of(p), shape_of(c))) > 0.03:
                d.append("env")
            if abs(num(p, "balance") - num(c, "balance")) > 0.01:
                d.append(f"pan{num(p, 'balance'):.2f}")
            if reldiff(num(p, "rel"), num(c, "rel")) > 0.06:
                d.append(f"rel{num(p, 'rel') / (num(c, 'rel') + 1e-12):.2f}x")
            found.append((v, d or ["<subtle>"]))
        if found:
            live += 1
            print(f"  +{off:02d}  " + "   ".join(f"v{v}:{'+'.join(d)}" for v, d in found))
    print(f"\n{live}/{len(pairs)} probed offsets affect the sound")


def cmd_curves(pairs, want):
    print(f"{'off':>5}  {'metric':<8} {'curve':<19} {'min..max':<16} monotonicity")
    for off in sorted(pairs):
        if want and off not in want:
            continue
        vals = sorted({v for v, _ in pairs[off]})
        rows = [(pairs[off].get((v, "P")), pairs[off].get((v, "C"))) for v in vals]
        if any(p is None or c is None for p, c in rows):
            continue
        for met, key in (("level", "rms"), ("release", "rel"), ("bright", "bright"), ("attack", "t90")):
            pv = [num(p, key) / (num(c, key) + 1e-12) for p, c in rows]
            if max(pv) - min(pv) < 0.06 * max(abs(x) for x in pv) + 1e-9:
                continue
            print(f"  +{off:02d}  {met:<8} {spark(pv):<19} {min(pv):6.2f}..{max(pv):<7.2f} {monotonic(pv)}")


def cmd_shape(pairs, want):
    print("envelope over the held note (8 windows), probe only")
    for off in sorted(pairs):
        if want and off not in want:
            continue
        vals = sorted({v for v, _ in pairs[off]})
        picks = [vals[0], vals[len(vals) // 4], vals[len(vals) // 2], vals[-1]]
        line = f"  +{off:02d}  "
        for v in picks:
            p = pairs[off].get((v, "P"))
            if not p:
                line += "  ----      "
                continue
            b = [num(p, f"b{i}") for i in range(8)]
            m = max(b) + 1e-12
            line += "".join(SPARK[min(7, int(x / m * 7.999))] for x in b) + f"(v{v})  "
        print(line)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd, path = sys.argv[1], sys.argv[2]
    want = {int(x) for x in sys.argv[3:]}
    pairs = load(path)
    {"effects": lambda: cmd_effects(pairs),
     "curves": lambda: cmd_curves(pairs, want),
     "shape": lambda: cmd_shape(pairs, want)}[cmd]()
    return 0


if __name__ == "__main__":
    sys.exit(main())
