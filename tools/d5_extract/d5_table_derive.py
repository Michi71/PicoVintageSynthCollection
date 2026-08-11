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


def pitch_of(x, key=None, skip=0):
    if key is not None and ("p", key, skip) in _feat_cache:
        return _feat_cache[("p", key, skip)]
    x = np.asarray(x[skip: skip + 8192], dtype=np.float64)
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
        _feat_cache[("p", key, skip)] = out
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
ATT_SIZES = {2048: 0.2, 4096: 0.0, 8192: 0.4, 16384: 1.0}
# regions confirmed correct by ear (Lpiano/Mpiano/Hpiano) -- fixed exactly,
# including their position in the numbering: (0-based index, start, end)
# round-6 ear review: "first something else, then the sound" -- the true
# starts of Clarnt (idx 30) and Breath (idx 31) lie beyond these positions
REVIEW_MIN_START = {30: 126976, 31: 131072}

ATT_PINS = [(2, 8192, 12288), (3, 12288, 16384),        # Xylo1/Xylo2
            (15, 49152, 57344), (16, 57344, 65536), (17, 65536, None),
            (23, 98304, None), (24, 102400, None), (25, 106496, None),
            (26, 110592, None), (27, 114688, None), (28, 118784, None),
            (29, 122880, None), (30, 126976, None), (31, 131072, None),
            # ^ Eguit1..Breath, ear-approved in round 8
            (32, 135168, None),     # Steam start, measured (web ref, 0.95)
            (33, 139264, None),     # FluteH start, ear-confirmed ("ab 033 ok")
            (38, 155648, None),     # Lips1 start, measured (web ref)
            (46, 184320, None)]     # Pizz start, measured (web ref, 1.00)


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
            if REVIEW_MIN_START.get(k, 0) > pos:
                continue          # ear review: this region starts later
            for sz, cost in (sizes or ATT_SIZES).items():
                if pin and pin[2] is not None and sz != pin[2] - pin[1]:
                    continue
                j = i + sz // ATT_GRID
                if j > n:
                    continue
                end = j * ATT_GRID
                bad = False
                for _, pa, pb in ATT_PINS:
                    if pin is not None and pa == pos:
                        continue
                    if pos < pa < end or (pb is not None and (
                            pos < pb < end or (pa <= pos and end <= pb))):
                        bad = True
                        break
                if bad:
                    continue      # regions may not cross a pinned boundary
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


LOOP_GRID = 256


def seam_cost(rom, a, b):
    """How badly [a, b) loops: what the loop plays after wrapping (the region
    start) versus what actually follows in ROM (the region end)."""
    k = 64
    if b + k > len(rom) or b - a < 2 * k:
        return 9.0
    d = float(np.mean(np.abs(rom[b: b + k] - rom[a: a + k])))
    scale = float(np.mean(np.abs(rom[a:b]))) + 1e-6
    return d / scale


def loop_regions_dp(rom, zone, count, seam_w=3.0):
    """Split a zone into `count` seamlessly loopable regions: boundaries on a
    256-word grid inside signal islands, sizes preferring 512-multiples,
    seam quality as the dominant cost. Regions never span silence gaps."""
    isl = islands(rom, zone)
    if not isl:
        return None
    positions = []          # (word, island_id)
    for iid, (ia, ib) in enumerate(isl):
        pos = [ia]
        g = (ia // LOOP_GRID + 1) * LOOP_GRID
        while g < ib:
            pos.append(g)
            g += LOOP_GRID
        pos.append(ib)
        positions += [(w, iid) for w in sorted(set(pos))]
    n = len(positions)
    INF = float("inf")
    dp = [[INF] * (count + 1) for _ in range(n)]
    par = [[None] * (count + 1) for _ in range(n)]
    # entry: first island start; islands are chained via their edges
    dp[0][0] = 0.0
    for i in range(n):
        wa, ia = positions[i]
        for k in range(count + 1):
            if dp[i][k] == INF:
                continue
            # gap jump: island end -> next island start, no region consumed
            if i + 1 < n and positions[i + 1][1] != ia:
                if dp[i][k] < dp[i + 1][k]:
                    dp[i + 1][k] = dp[i][k]
                    par[i + 1][k] = (i, k, False)
            if k == count:
                continue
            for j in range(i + 1, n):
                wb, ib_ = positions[j]
                if ib_ != ia:
                    break
                size = wb - wa
                if size < 512:
                    continue
                if size > 8192:
                    break
                c = (dp[i][k] + seam_w * seam_cost(rom, wa, wb)
                     + (0.0 if size % 512 == 0 else 0.3)
                     + {512: 0.0, 1024: 0.1, 2048: 0.2}.get(size, 0.5))
                if c < dp[j][k + 1]:
                    dp[j][k + 1] = c
                    par[j][k + 1] = (i, k, True)
    if dp[n - 1][count] == INF:
        return None
    regions = []
    i, k = n - 1, count
    while par[i][k] is not None:
        pi, pk, is_region = par[i][k]
        if is_region:
            regions.append((positions[pi][0], positions[i][0]))
        i, k = pi, pk
    regions.reverse()
    return regions


def refine_loop_boundaries(rom, regions, span=192):
    """Word-exact seam tuning: shift each internal boundary within +/-span
    to minimize the seam costs of the two adjacent loops. Kills the click
    per revolution without re-partitioning."""
    regions = [list(r) for r in regions]
    for i in range(len(regions) - 1):
        a0 = regions[i][0]
        b = regions[i][1]
        e1 = regions[i + 1][1]
        best = (seam_cost(rom, a0, b) + seam_cost(rom, b, e1), b)
        for d in range(-span, span + 1, 4):
            nb = b + d
            if nb - a0 < 384 or e1 - nb < 384:
                continue
            c = seam_cost(rom, a0, nb) + seam_cost(rom, nb, e1)
            if c < best[0]:
                best = (c, nb)
        regions[i][1] = regions[i + 1][0] = best[1]
    return [tuple(r) for r in regions]


ST_SIZES = {2048: 0.0, 4096: 0.2, 8192: 0.6}   # power-of-two pages, like attacks


def static_regions_dp(rom, zone, count=29):
    """Split the static zone into `count` page-aligned regions whose sizes
    follow the sibling-chip table law (2048 << n), scored by loop seam
    quality."""
    a, b = zone
    n = (b - a) // PAGE
    INF = float("inf")
    dp = [[INF] * (count + 1) for _ in range(n + 1)]
    par = [[None] * (count + 1) for _ in range(n + 1)]
    dp[0][0] = 0.0
    seams = {}
    for i in range(n):
        for k in range(count):
            if dp[i][k] == INF:
                continue
            for sz, cost in ST_SIZES.items():
                j = i + sz // PAGE
                if j > n:
                    continue
                key = (i, j)
                if key not in seams:
                    seams[key] = seam_cost(rom, a + i * PAGE,
                                           min(a + j * PAGE, len(rom) - 128))
                c = dp[i][k] + cost + 2.0 * seams[key]
                if c < dp[j][k + 1]:
                    dp[j][k + 1] = c
                    par[j][k + 1] = i
    if dp[n][count] == INF:
        return None
    bounds = []
    i, k = n, count
    while k:
        i = par[i][k]
        bounds.append(a + i * PAGE)
        k -= 1
    bounds.reverse()
    return list(zip(bounds, bounds[1:] + [b]))


def build_table(rs, rom, frontier, **dp_kw):
    """Region list for PCM 1..76. Statics 48..76 fill the ROM to its end
    (the combination loops 77..100 are address ranges over this material,
    not stored data -- established by web-reference matching). All region
    sizes follow the sibling-chip table law: 2048 << n words, page-aligned
    (munt, ControlROMPCMStruct: addr = pos * 0x800, len = 0x800 << exp)."""
    att_regions = attack_regions_dp(rs, frontier, **dp_kw)
    if att_regions is None:
        return []
    st_regions = static_regions_dp(rom, (frontier, len(rom)))
    if st_regions is None:
        return []
    return att_regions + st_regions


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
    ("FluteH>FluteL", (34, 35), "flutes", 2.0),
    ("Horgan/Lorgan octave", (49, 50), "pitch_octave", 3.0),  # frontier sentinel
    ("EP_lp1~EP_lp2", (51, 52), "similar", 1.0),
    ("SAXlp1~SAXlp2", (63, 64), "similar", 1.0),
    ("Spect2..7 block", tuple(range(69, 75)), "block", 1.0),
    ("Lips1~Lips2", (39, 40), "similar", 1.0),
    ("EB_lp1/2/3 block", (55, 57, 58), "block", 1.0),
    ("Aah_lp~Ooh_lp", (65, 66), "similar_loose", 1.0),
    ("Uprite is a bass", (30,), "pitch_max:130", 2.0),
    ("Clarnt reedy register", (31,), "pitch_range:140:800", 2.0),
    ("Breath noisy", (32,), "noisy", 1.5),
    ("Steam noisy", (33,), "noisy", 1.5),
    ("3angle bright", (12,), "bright", 1.5),
    ("Noise is noise", (76,), "noisy", 3.0),
    ("attack regions pure", (), "purity", 3.0),
    ("vocal cluster formants", (65, 66, 67), "formant_min:1000", 4.0),
    ("Spects are spectral", tuple(range(68, 75)), "flat_min:0.3", 3.0),
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


def region_impure(rs, rom, a, b):
    """An internal onset with a timbre change means two sounds in one region;
    an internal onset with the same timbre is a legitimate re-attack."""
    for g in range(a // PAGE + 1, b // PAGE):
        w = g * PAGE
        if w - a < PAGE or b - w < PAGE:
            continue
        if rs.attack_like(w):
            if similar(band_spectrum(rom[a:w]), band_spectrum(rom[w:b])) < 0.5:
                return True
    return False


_RS_FOR_CHECKS = [None]


def validate(rom, table):
    results = []
    for name, pcms, kind, weight in CHECKS:
        if kind == "purity":
            rs = _RS_FOR_CHECKS[0]
            impure = [e["pcm"] for e in table[:47]
                      if e["start"] is not None
                      and region_impure(rs, rom, e["start"], e["end"])]
            results.append((name, (not impure, impure)))
            continue
        entries = [table[p - 1] for p in pcms]
        if any(e["start"] is None for e in entries):
            results.append((name, None))
            continue
        if kind.startswith("rms_min:"):
            lim = float(kind.split(":")[1])
            vals = [round(rms_of(rom[e["start"]: e["end"]]), 3) for e in entries]
            results.append((name, (all(v >= lim for v in vals), vals)))
            continue
        if kind.startswith("formant_min:"):
            lim = float(kind.split(":")[1])
            ratios = []
            for e in entries:
                x = np.asarray(rom[e["start"]: e["end"]][:8192])
                m = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
                f = np.arange(len(m)) * (SAMPLE_RATE / 2) / (len(m) - 1)
                low = m[f < 250].sum() + 1e-9
                mid = m[(f >= 500) & (f < 3000)].sum()
                ratios.append(round(float(mid / low), 1))
            results.append((name, (all(r > lim for r in ratios), ratios)))
            continue
        if kind.startswith("flat_min:"):
            lim = float(kind.split(":")[1])
            vals = [round(flatness(rom[e["start"]: e["end"]]), 2) for e in entries]
            results.append((name, (float(np.mean(vals)) > lim, vals)))
            continue
        if kind == "formants":
            ratios = []
            for e in entries:
                x = np.asarray(rom[e["start"]: e["end"]][:8192])
                if len(x) < 512:
                    ratios.append(0.0)
                    continue
                m = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
                f = np.arange(len(m)) * (SAMPLE_RATE / 2) / (len(m) - 1)
                low = m[f < 250].sum() + 1e-9
                mid = m[(f >= 500) & (f < 3000)].sum()
                ratios.append(round(float(mid / low), 2))
            results.append((name, (all(r > 1.0 for r in ratios), ratios)))
            continue
        keys = [(e["start"], e["end"]) for e in entries]
        cuts = [rom[a:b] for a, b in keys]
        if kind == "spect112":
            holder = next((x for x in table if x["start"] is not None
                           and x["start"] <= 229376 < x["end"]), None)
            ok = holder is not None and 68 <= holder["pcm"] <= 75
            results.append((name, (ok, holder["pcm"] if holder else None)))
            continue
        skip = 1024 if all(p <= 47 for p in pcms) else 0
        if kind.startswith("pitch_max:"):
            lim = float(kind.split(":")[1])
            pv = pitch_of(cuts[0], keys[0], skip)
            results.append((name, (0 < pv <= lim, round(pv))))
            continue
        if kind.startswith("pitch_range:"):
            lo, hi = (float(t) for t in kind.split(":")[1:])
            pv = pitch_of(cuts[0], keys[0], skip)
            results.append((name, (lo <= pv <= hi, round(pv))))
            continue
        if kind == "flutes":
            skip_f = 1024
            pa = pitch_of(cuts[0], keys[0], skip_f)
            pb = pitch_of(cuts[1], keys[1], skip_f)
            ok = 250 <= pb <= 2000 and 250 <= pa <= 2000 and pa / max(pb, 1) >= 1.2
            results.append((name, (ok, [round(pa), round(pb)])))
            continue
        if kind == "pitch_octave":
            pa = pitch_of(cuts[0], keys[0])
            pb = pitch_of(cuts[1], keys[1])
            ok = pb > 0 and 1.88 <= pa / pb <= 2.12
            results.append((name, (ok, [round(pa), round(pb)])))
            continue
        if kind in ("pitch_asc", "pitch_desc"):
            ps = [pitch_of(c, k, skip) for c, k in zip(cuts, keys)]
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
                results.append((name, (fl > 0.10, round(fl, 2))))
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
    ap.add_argument("--frontier", type=int, default=None,
                    help="lock the attack/static frontier to this page")
    ap.add_argument("--review", help="JSON with ear-review findings: "
                    '{"merged": [pcm, ...], "bad": [pcm, ...]}')
    ap.add_argument("--prev", help="previous hypothesis JSON the review refers to")
    args = ap.parse_args()

    rs = D5RomSet(args.romdir)
    rom = np.asarray(rs.audio)
    _RS_FOR_CHECKS[0] = rs

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
    fp_range = [args.frontier] if args.frontier else range(84, 100)
    for fp in fp_range:
        frontier = fp * PAGE
        for dp_kw in variants:
            regions = build_table(rs, rom, frontier, **dp_kw)
            if len(regions) != 76:
                continue
            table = []
            for i, (a, b) in enumerate(regions):
                table.append({"pcm": i + 1, "name": rs.names[i], "start": int(a),
                              "end": int(b), "looped": i >= 47,
                              "basis": "order-hypothesis"})
            for i in range(76, 100):
                table.append({"pcm": i + 1, "name": rs.names[i], "start": None,
                              "end": None, "looped": True,
                              "basis": "range-unresolved"})
            if not energetic(table[:76]):
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

    # ---- local repair
    def weighted(res):
        return sum(w for (_, r), (_, _, _, w) in zip(res, CHECKS) if r and r[0])

    # window recompose: enumerate every slot composition of the attack
    # stretch spanned by failing checks (plus one region each side)
    from itertools import product as iproduct

    def failing_attack_span(res):
        idxs = []
        for (name, r), (_, pcms, _, _) in zip(res, CHECKS):
            if r and not r[0]:
                idxs += [p - 1 for p in pcms if p <= 47]
        if not idxs:
            return None
        return max(0, min(idxs) - 1), min(46, max(idxs) + 1)

    span = failing_attack_span(res)
    if span:
        lo, hi = span
        w_start = table[lo]["start"]
        w_end = table[hi]["end"]
        count = hi - lo + 1
        total_pages = (w_end - w_start) // PAGE
        pin_ok = not any(w_start < pa < w_end
                         or (pb is not None and w_start < pb < w_end)
                         for _, pa, pb in ATT_PINS)
        if pin_ok and count >= 2 and total_pages >= count:
            best_local = (weighted(res), table, res)
            sizes_range = range(1, 7)
            tried = 0
            for comp in iproduct(sizes_range, repeat=count - 1):
                rest = total_pages - sum(comp)
                if rest < 1 or rest > 6:
                    continue
                tried += 1
                if tried > 30000:
                    break
                trial = [dict(e) for e in table]
                pos = w_start
                ok_rev = True
                for off, pages in enumerate(list(comp) + [rest]):
                    if REVIEW_MIN_START.get(lo + off, 0) > pos:
                        ok_rev = False
                        break
                    trial[lo + off]["start"] = pos
                    pos += pages * PAGE
                    trial[lo + off]["end"] = pos
                if not ok_rev:
                    continue
                tres = validate(rom, trial)
                sc = weighted(tres)
                if sc > best_local[0] + 1e-9:
                    best_local = (sc, trial, tres)
            if best_local[0] > weighted(res):
                score, table, res = best_local[0], best_local[1], best_local[2]
                print(f"window recompose PCM {lo+1}..{hi+1}: weighted {score:.1f}")

    pinned_words = set()
    for _, pa, pb in ATT_PINS:
        pinned_words.update((pa, pb))
    improved = True
    rounds = 0
    while improved and rounds < 12:
        improved = False
        rounds += 1
        for bi in range(1, 76):     # every internal boundary, attacks + statics
            cur = table[bi]["start"]
            if cur in pinned_words:
                continue
            deltas = (-2 * PAGE, -PAGE, PAGE, 2 * PAGE)
            min_sz = PAGE
            for delta in deltas:
                nw = cur + delta
                if not (table[bi - 1]["start"] + min_sz <= nw
                        <= table[bi]["end"] - min_sz):
                    continue
                trial = [dict(e) for e in table]
                trial[bi - 1]["end"] = nw
                trial[bi]["start"] = nw
                tres = validate(rom, trial)
                if weighted(tres) > weighted(res) + 1e-9:
                    table, res = trial, tres
                    score = weighted(res)
                    improved = True
                    break
    # chain rotation: merge one static region with its neighbor and split
    # another -- relocates a boundary across the chain, which single-boundary
    # moves cannot do
    improved = True
    while improved:
        improved = False
        for merge_at in range(48, 76):      # boundary index to remove
            if table[merge_at]["start"] in pinned_words:
                continue
            for split_at in range(47, 76):  # region index to split
                if abs(split_at - merge_at) > 12 or split_at == merge_at:
                    continue
                a, b = table[split_at]["start"], table[split_at]["end"]
                if b - a < 1024:
                    continue
                cands = range(a + 512, b - 511, 256)
                best_pos, best_seam = None, None
                for cpos in cands:
                    sc_ = seam_cost(rom, a, cpos) + seam_cost(rom, cpos, b)
                    if best_seam is None or sc_ < best_seam:
                        best_seam, best_pos = sc_, cpos
                if best_pos is None:
                    continue
                trial = [dict(e) for e in table]
                # remove boundary at merge_at
                trial[merge_at - 1]["end"] = trial[merge_at]["end"]
                del trial[merge_at]
                # split region (index shifts if split_at > merge_at)
                si = split_at if split_at < merge_at else split_at - 1
                left = dict(trial[si])
                right = dict(trial[si])
                left["end"] = best_pos
                right["start"] = best_pos
                trial = trial[:si] + [left, right] + trial[si + 1:]
                for idx, e in enumerate(trial):
                    e["pcm"] = idx + 1
                    e["name"] = rs.names[idx]
                    e["looped"] = idx >= 47
                tres = validate(rom, trial)
                if weighted(tres) > weighted(res) + 1e-9:
                    table, res = trial, tres
                    score = weighted(res)
                    improved = True
                    print(f"  rotation: merged boundary {merge_at}, "
                          f"split region {split_at} -> weighted {score:.1f}")
                    break
            if improved:
                break

    print(f"after local repair: weighted {score:.1f}, "
          f"{sum(1 for _, r in res if r and r[0])}/{len(CHECKS)} pass "
          f"({rounds} rounds)")
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
        if e["start"] is None:
            continue
        cut = rom[e["start"]: e["end"]]
        if e["looped"] and len(cut):
            # a static loop may be a 16 ms single-cycle waveform: tile it to
            # ~2 s so it is audible -- and so a wrong boundary is audible too,
            # as a click or warble on every revolution
            cut = np.tile(cut, max(2, int(np.ceil(2 * SAMPLE_RATE / len(cut)))))
            fade = min(1600, len(cut) // 4)
            cut = cut.copy()
            cut[-fade:] *= np.linspace(1, 0, fade)
        elif len(cut):
            # short one-shots are hard to judge as a bare blip: give the
            # player room with a little leading and trailing silence
            cut = np.concatenate([np.zeros(3200), cut, np.zeros(12800)])
        with wave.open(os.path.join(sdir, f"{e['pcm']:03d}_{e['name']}.wav"), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(np.clip(cut * 32767, -32767, 32767).astype("<i2").tobytes())
    print(f"cut 100 samples into {sdir}/ (loops tiled to ~2 s) -- "
          f"a click or warble in a looped sample means its boundary is off")


if __name__ == "__main__":
    main()
