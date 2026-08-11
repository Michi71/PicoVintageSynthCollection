#!/usr/bin/env python3
"""Derive a sample-table hypothesis from the PCM audio alone (no hardware).

    python3 tools/d5_extract/d5_table_derive.py <romdir> [--out dir]

The MB87136 resolves PCM numbers to addresses inside its mask ROM, so no
dump carries the table. This tool reconstructs it from the audio itself,
under the layout model that PCM 1..100 sit in the ROM in numeric order:
attacks 1..47 first, static loops 48..76 ending with the exactly-detectable
"Noise" block (76), and the factory-rendered combination loops 77..100
after it.

Within that model two things are estimated from the audio: the boundary
positions (page-grid onsets for attacks, 256-word-grid spectral flux for
the short loops) and the attack/static frontier, which is searched by
maximizing named-family validation checks (Horgan above Lorgan, EP_lp1
similar to EP_lp2, the seven Spect loops forming a similarity block, ...).

Output: d50_sample_table_hypothesis.json plus one WAV per sample under
samples_hypothesis/ -- reviewing those by ear against the known names is
the final verification. Needs numpy.
"""
import argparse
import json
import os
import wave

import numpy as np

from d5_rom import D5RomSet, SAMPLE_RATE, PAGE


# ------------------------------------------------------------------ features

def flux(rom, hop):
    """Spectral flux on a hop-word grid."""
    n = len(rom) // hop
    win = np.hanning(hop * 2)
    mags = []
    for i in range(n - 1):
        x = rom[i * hop:(i + 2) * hop] * win
        mags.append(np.log(np.abs(np.fft.rfft(x))[1: hop // 2] + 1e-9))
    out = np.zeros(n)
    for i in range(1, n - 1):
        out[i] = np.linalg.norm(mags[i] - mags[i - 1])
    return out


def band_spectrum(x):
    x = np.asarray(x[:4096], dtype=np.float64)
    if len(x) < 512:
        x = np.pad(x, (0, 512 - len(x)))
    m = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    bands = []
    for k in range(3, 10):
        seg = m[min(1 << k, len(m) - 1): min(1 << (k + 1), len(m))]
        bands.append(seg.mean() if len(seg) else 0.0)
    return np.log(np.array(bands) + 1e-9)


def similar(a, b):
    return float(np.dot(a - a.mean(), b - b.mean()) /
                 (np.linalg.norm(a - a.mean()) * np.linalg.norm(b - b.mean()) + 1e-9))


_feat_cache = {}


def pitch_of(x, key=None):
    if key is not None and ("p", key) in _feat_cache:
        return _feat_cache[("p", key)]
    x = np.asarray(x[:8192], dtype=np.float64)
    x = x - x.mean()
    out = 0.0
    if np.max(np.abs(x)) >= 1e-3:
        # FFT autocorrelation, O(n log n)
        n = 1 << (len(x) * 2 - 1).bit_length()
        f = np.fft.rfft(x, n)
        ac = np.fft.irfft(f * np.conj(f))[: len(x)]
        ac /= ac[0] + 1e-12
        lo, hi = SAMPLE_RATE // 2000, SAMPLE_RATE // 40
        lag = int(np.argmax(ac[lo:hi])) + lo
        if ac[lag] > 0.3:
            out = SAMPLE_RATE / lag
    if key is not None:
        _feat_cache[("p", key)] = out
    return out


def noise_extent(rs, rom):
    pages = [p for p in range(len(rom) // PAGE) if rs.noisy_at(p * PAGE)]
    p = pages[0]
    a, b = p * PAGE, (p + 1) * PAGE
    while a > 512 and rs.noisy_at(a - 512, span=512):
        a -= 512
    while b < len(rom) - 512 and rs.noisy_at(b, span=512):
        b += 512
    return a, b


def top_boundaries(rom, zone, count, hop):
    a, b = zone
    if b - a < (count + 1) * hop:
        return []
    f = flux(rom[a:b], hop)
    picks = []
    for i in np.argsort(f)[::-1]:
        pos = a + int(i) * hop
        if all(abs(pos - q) >= 2 * hop for q in picks):
            picks.append(pos)
        if len(picks) == count:
            break
    return sorted(picks)


# ----------------------------------------------------------------- assembly

def build_table(rs, rom, frontier, noise_a, st_bounds, comp_bounds, topup):
    """Region list for PCM 1..100 given the attack/static frontier and a
    chosen set of extra attack boundaries."""
    onsets = [p * PAGE for p in range(1, frontier // PAGE)
              if rs.attack_like(p * PAGE)]
    att = sorted(set([0] + onsets + list(topup)))[:47]
    bounds = sorted(set(att + [frontier] + st_bounds + [noise_a] + comp_bounds))
    ends = bounds[1:] + [len(rom)]
    return list(zip(bounds, ends))


def topup_candidates(rs, rom, frontier, need):
    """Strongest flux positions below the frontier that are not onsets."""
    zone_flux = flux(rom, PAGE)
    onsets = {p * PAGE for p in range(1, frontier // PAGE)
              if rs.attack_like(p * PAGE)}
    extra = [(zone_flux[p], p * PAGE) for p in range(1, frontier // PAGE)
             if p * PAGE not in onsets]
    extra.sort(reverse=True)
    return [pos for _, pos in extra[: max(need * 4, 8)]]


CHECKS = [
    ("Lpiano<Mpiano<Hpiano", (16, 17, 18), "pitch_asc"),
    ("FluteH>FluteL", (34, 35), "pitch_desc"),
    ("Horgan>Lorgan", (49, 50), "pitch_desc"),
    ("EP_lp1~EP_lp2", (51, 52), "similar"),
    ("SAXlp1~SAXlp2", (63, 64), "similar"),
    ("Spect1..7 block", tuple(range(68, 75)), "block"),
]


def validate(rom, table):
    results = []
    for name, pcms, kind in CHECKS:
        entries = [table[p - 1] for p in pcms]
        if any(e["start"] is None for e in entries):
            results.append((name, None))
            continue
        keys = [(e["start"], e["end"]) for e in entries]
        cuts = [rom[a:b] for a, b in keys]
        if kind in ("pitch_asc", "pitch_desc"):
            ps = [pitch_of(c, k) for c, k in zip(cuts, keys)]
            ok = all(p > 0 for p in ps) and (
                all(a < b for a, b in zip(ps, ps[1:])) if kind == "pitch_asc"
                else all(a > b for a, b in zip(ps, ps[1:])))
            results.append((name, (ok, [round(p) for p in ps])))
        else:
            specs = []
            for c, k in zip(cuts, keys):
                if ("s", k) not in _feat_cache:
                    _feat_cache[("s", k)] = band_spectrum(c)
                specs.append(_feat_cache[("s", k)])
            if kind == "similar":
                s = similar(specs[0], specs[1])
                results.append((name, (s > 0.85, round(s, 2))))
            else:
                sims = [similar(a, b) for a, b in zip(specs, specs[1:])]
                results.append((name, (min(sims) > 0.6, round(float(np.mean(sims)), 2))))
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("romdir")
    ap.add_argument("--out", default="tools/d5_extract/out")
    args = ap.parse_args()

    rs = D5RomSet(args.romdir)
    rom = np.asarray(rs.audio)
    noise_a, noise_b = noise_extent(rs, rom)
    print(f"Noise block (PCM 76): words {noise_a}..{noise_b} "
          f"(pages {noise_a/PAGE:.2f}..{noise_b/PAGE:.2f})")

    from itertools import combinations
    best = None
    comp_bounds = [noise_b] + top_boundaries(rom, (noise_b, len(rom)), 23, 256)
    for fp in range(84, 100):
        frontier = fp * PAGE
        st_bounds = top_boundaries(rom, (frontier, noise_a), 27, 256)
        onset_count = 1 + sum(1 for p in range(1, fp) if rs.attack_like(p * PAGE))
        need = 47 - onset_count
        if need < 0 or need > 3:
            continue          # implausible frontier
        cands = topup_candidates(rs, rom, frontier, need)[: need + 4] if need else []
        for topup in (combinations(cands, need) if need else [()]):
            regions = build_table(rs, rom, frontier, noise_a, st_bounds, comp_bounds, topup)
            if len(regions) != 100:
                continue
            table = []
            for i, (a, b) in enumerate(regions):
                table.append({"pcm": i + 1, "name": rs.names[i], "start": int(a),
                              "end": int(b), "looped": i >= 47,
                              "basis": "order-hypothesis"})
            res = validate(rom, table)
            score = sum(1 for _, r in res if r and r[0])
            if best is None or score > best[0]:
                best = (score, frontier, table, res)

    score, frontier, table, res = best
    print(f"best frontier (start of PCM 48): page {frontier//PAGE}  "
          f"({score}/{len(CHECKS)} checks pass)")
    for name, r in res:
        if r is None:
            print(f"  {name}: SKIP")
        else:
            print(f"  {name}: {'PASS' if r[0] else 'FAIL'} {r[1]}")

    os.makedirs(args.out, exist_ok=True)
    jpath = os.path.join(args.out, "d50_sample_table_hypothesis.json")
    with open(jpath, "w") as f:
        json.dump(table, f, indent=1)
    print(f"wrote {jpath}")

    sdir = os.path.join(args.out, "samples_hypothesis")
    os.makedirs(sdir, exist_ok=True)
    for e in table:
        cut = rom[e["start"]: e["end"]]
        with wave.open(os.path.join(sdir, f"{e['pcm']:03d}_{e['name']}.wav"), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(np.clip(cut * 32767, -32767, 32767).astype("<i2").tobytes())
    print(f"cut 100 samples into {sdir}/ -- listen and check against the names")


if __name__ == "__main__":
    main()
