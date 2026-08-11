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


def rms_of(x):
    x = np.asarray(x)
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def islands(rom, zone, hop=256, thr=0.003, min_len=512):
    """Signal islands inside a zone: maximal runs with energy, gaps removed.
    Region boundaries may only fall inside islands, and region starts snap
    to island starts, so silence gaps never become 'samples'."""
    a, b = zone
    n = (b - a) // hop
    r = np.sqrt(np.mean(rom[a: a + n * hop].reshape(n, hop) ** 2, axis=1))
    out = []
    start = None
    for i, v in enumerate(r > thr):
        if v and start is None:
            start = i
        elif not v and start is not None:
            if (i - start) * hop >= min_len:
                out.append((a + start * hop, a + i * hop))
            start = None
    if start is not None and (n - start) * hop >= min_len:
        out.append((a + start * hop, b))
    return out


def island_boundaries(rom, zone, count, hop=256):
    """Split a zone into `count` regions: islands first, then the strongest
    flux positions inside islands until the count is reached."""
    isl = islands(rom, zone)
    if not isl:
        return []
    starts = [s for s, _ in isl]
    need = count - len(isl)
    if need > 0:
        cands = []
        for s, e in isl:
            f = flux(rom[s:e], hop)
            for i in range(2, len(f) - 2):
                cands.append((f[i], s + i * hop))
        cands.sort(reverse=True)
        picked = []
        for _, pos in cands:
            if all(abs(pos - q) >= 2 * hop for q in picked + starts):
                picked.append(pos)
            if len(picked) == need:
                break
        starts += picked
    return sorted(starts)[:count]


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

ATT_GRID = PAGE
# region sizes in words with placement cost; ear review round 2 established
# the page-grid slot quantization (most attacks 2 pages, pianos bigger)
ATT_SIZES = {2048: 0.3, 4096: 0.0, 6144: 0.3, 8192: 0.7, 10240: 1.2, 12288: 1.4}
# regions confirmed correct by ear (Lpiano/Mpiano/Hpiano) -- fixed exactly,
# including their position in the numbering: (0-based index, start, end)
ATT_PINS = [(15, 49152, 57344), (16, 57344, 65536), (17, 65536, 71680)]


def attack_regions_dp(rs, frontier, sizes=None, onset_pen=2.0, span_pen=0.6):
    """Choose exactly 47 attack regions on the page grid, sizes weighted
    toward the observed slot classes, starts preferring onsets, ending
    exactly at the frontier, with the ear-confirmed piano regions pinned.
    Dense odd-position onsets are internal re-attacks (drum hits), so
    spanning an onset is only mildly penalized."""
    n = frontier // ATT_GRID
    onset = [rs.attack_like(g * ATT_GRID) for g in range(n + 1)]
    INF = float("inf")
    dp = [[INF] * 48 for _ in range(n + 1)]
    par = [[None] * 48 for _ in range(n + 1)]
    dp[0][0] = 0.0
    for i in range(n):
        for k in range(47):
            if dp[i][k] == INF:
                continue
            pos = i * ATT_GRID
            base = dp[i][k] + (0.0 if onset[i] else onset_pen)
            pin = next((p for p in ATT_PINS if p[1] == pos), None)
            if pin and k != pin[0]:
                continue          # a pinned start must be reached at its index
            for sz, cost in (sizes or ATT_SIZES).items():
                if pin and sz != pin[2] - pin[1]:
                    continue
                j = i + sz // ATT_GRID
                if j > n:
                    continue
                end = j * ATT_GRID
                if pin is None and any(pos < pa < end or pos < pb < end
                                       or (pa <= pos and end <= pb)
                                       for _, pa, pb in ATT_PINS):
                    continue      # regions may not overlap a pinned region
                c = base + cost + sum(span_pen for t in range(i + 1, j) if onset[t])
                if c < dp[j][k + 1]:
                    dp[j][k + 1] = c
                    par[j][k + 1] = i
    if dp[n][47] == INF:
        return None
    bounds = []
    i, k = n, 47
    while k:
        i = par[i][k]
        bounds.append(i * ATT_GRID)
        k -= 1
    bounds.reverse()
    return list(zip(bounds, bounds[1:] + [frontier]))


def clipped_regions(starts, hard_end, isl):
    """(start, end) per start; ends clip to the island holding the start."""
    out = []
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else hard_end
        for ia, ib in isl:
            if ia <= s < ib:
                e = min(e, ib)
                break
        out.append((s, e))
    return out


def build_table(rs, rom, frontier, noise_a, noise_b, **dp_kw):
    """Region list for PCM 1..100 given the attack/static frontier."""
    att_regions = attack_regions_dp(rs, frontier, **dp_kw)
    if att_regions is None:
        return []

    st_zone = (frontier, noise_a)
    st_isl = islands(rom, st_zone)
    st_starts = island_boundaries(rom, st_zone, 28)
    st_regions = clipped_regions(st_starts, noise_a, st_isl)
    st_regions.append((noise_a, noise_b))

    cp_zone = (noise_b, len(rom))
    cp_isl = islands(rom, cp_zone)
    cp_starts = island_boundaries(rom, cp_zone, 24)
    cp_regions = clipped_regions(cp_starts, len(rom), cp_isl)

    return att_regions + st_regions + cp_regions


def topup_candidates(rs, rom, frontier, need):
    """Strongest flux positions below the frontier that are not onsets."""
    zone_flux = flux(rom, PAGE)
    onsets = {p * PAGE for p in range(1, frontier // PAGE)
              if rs.attack_like(p * PAGE)}
    extra = [(zone_flux[p], p * PAGE) for p in range(1, frontier // PAGE)
             if p * PAGE not in onsets]
    extra.sort(reverse=True)
    return [pos for _, pos in extra[: max(need * 4, 8)]]


CHECKS = [    # (label, pcm numbers, kind, weight)
    ("Lpiano<Mpiano<Hpiano", (16, 17, 18), "pitch_asc", 2.0),
    ("FluteH>FluteL", (34, 35), "pitch_desc", 2.0),
    ("Horgan>Lorgan", (49, 50), "pitch_desc", 3.0),   # frontier sentinel
    ("EP_lp1~EP_lp2", (51, 52), "similar", 1.0),
    ("SAXlp1~SAXlp2", (63, 64), "similar", 1.0),
    ("Spect1..7 block", tuple(range(68, 75)), "block", 1.0),
    ("Xylo1~Xylo2", (3, 4), "similar", 1.0),
    ("Eguit1~Eguit2", (24, 25), "similar", 1.0),
    ("Lips1~Lips2", (39, 40), "similar", 1.0),
    ("EB_lp1/2/3 block", (55, 57, 58), "block", 1.0),
    ("Aah_lp~Ooh_lp", (65, 66), "similar_loose", 1.0),
    ("Breath noisy", (32,), "noisy", 1.5),
    ("Steam noisy", (33,), "noisy", 1.5),
    ("3angle bright", (12,), "bright", 1.5),
]


def flatness(x):
    x = np.asarray(x[:4096], dtype=np.float64)
    if len(x) < 512:
        return 0.0
    m = np.abs(np.fft.rfft(x * np.hanning(len(x))))[1:] + 1e-12
    return float(np.exp(np.mean(np.log(m))) / np.mean(m))


def centroid(x):
    x = np.asarray(x[:4096], dtype=np.float64)
    if len(x) < 512:
        return 0.0
    m = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.arange(len(m)) * SAMPLE_RATE / 2 / (len(m) - 1)
    return float(np.sum(f * m) / (np.sum(m) + 1e-12))


def validate(rom, table):
    results = []
    for name, pcms, kind, weight in CHECKS:
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
            elif kind == "similar_loose":
                # same source, different vowel/register: moderate similarity
                s = similar(specs[0], specs[1])
                results.append((name, (0.5 < s < 0.97, round(s, 2))))
            elif kind == "noisy":
                fl = flatness(cuts[0])
                results.append((name, (fl > 0.25, round(fl, 2))))
            elif kind == "bright":
                c = centroid(cuts[0])
                results.append((name, (c > 3500, round(c))))
            else:
                sims = [similar(a, b) for a, b in zip(specs, specs[1:])]
                results.append((name, (min(sims) > 0.6, round(float(np.mean(sims)), 2))))
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("romdir")
    ap.add_argument("--out", default="tools/d5_extract/out")
    ap.add_argument("--review", help="JSON with ear-review findings: "
                    '{"merged": [pcm, ...], "bad": [pcm, ...]}')
    ap.add_argument("--prev", help="previous hypothesis JSON the review refers to")
    args = ap.parse_args()

    rs = D5RomSet(args.romdir)
    rom = np.asarray(rs.audio)
    noise_a, noise_b = noise_extent(rs, rom)
    print(f"Noise block (PCM 76): words {noise_a}..{noise_b} "
          f"(pages {noise_a/PAGE:.2f}..{noise_b/PAGE:.2f})")

    prev = json.load(open(args.prev)) if args.prev else None

    def energetic(table):
        # peak-based: attacks carry long decay tails, so RMS would misjudge
        return all(float(np.max(np.abs(rom[e["start"]: e["end"]]), initial=0.0)) >= 0.02
                   for e in table)

    # cost-jitter sampling: the DP has near-ties the family checks must
    # arbitrate, so validate several perturbed DP optima per frontier
    import random
    rng = random.Random(50)
    variants = [dict()]
    for _ in range(30):
        variants.append(dict(
            sizes={sz: c + rng.uniform(-0.4, 0.4) for sz, c in ATT_SIZES.items()},
            onset_pen=rng.uniform(1.2, 3.0),
            span_pen=rng.uniform(0.3, 1.0)))
    best = None
    for fp in range(84, 100):
        frontier = fp * PAGE
        for dp_kw in variants:
            regions = build_table(rs, rom, frontier, noise_a, noise_b, **dp_kw)
            if len(regions) != 100:
                continue
            table = []
            for i, (a, b) in enumerate(regions):
                table.append({"pcm": i + 1, "name": rs.names[i], "start": int(a),
                              "end": int(b), "looped": i >= 47,
                              "basis": "order-hypothesis"})
            if not energetic(table):
                continue
            res = validate(rom, table)
            score = sum(w for (_, r), (_, _, _, w) in zip(res, CHECKS) if r and r[0])
            if best is None or score > best[0]:
                best = (score, frontier, table, res)
                print(f"  new best: frontier page {fp}, weighted {score:.1f}, "
                      f"{sum(1 for _, r in res if r and r[0])}/{len(CHECKS)} pass")

    if best is None:
        raise SystemExit("no candidate satisfied the energy constraint -- "
                         "inspect islands/thresholds")
    score, frontier, table, res = best
    print(f"best frontier (start of PCM 48): page {frontier//PAGE}  "
          f"({score}/{len(CHECKS)} checks pass, all regions energetic)")
    from collections import Counter
    hist = Counter(e["end"] - e["start"] for e in table[:47])
    print("attack size histogram:",
          {k: v for k, v in sorted(hist.items())})
    pianos = [(e["end"] - e["start"]) for e in table[15:18]]
    print(f"piano sizes (PCM 16-18): {pianos}")
    if prev:
        changed = [e["pcm"] for e, p in zip(table, prev)
                   if (e["start"], e["end"]) != (p["start"], p["end"])]
        print(f"changed vs previous table ({len(changed)}): {changed}")
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
        if e["looped"] and len(cut):
            # a static/combination loop may be a 16 ms single-cycle waveform:
            # tile it to 2 s so it is audible -- and so a wrong boundary is
            # audible too, as a click or warble on every revolution
            cut = np.tile(cut, max(1, int(2 * SAMPLE_RATE / len(cut))))
        with wave.open(os.path.join(sdir, f"{e['pcm']:03d}_{e['name']}.wav"), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(np.clip(cut * 32767, -32767, 32767).astype("<i2").tobytes())
    print(f"cut 100 samples into {sdir}/ (loops tiled to ~2 s) -- "
          f"a click or warble in a looped sample means its boundary is off")


if __name__ == "__main__":
    main()
