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


def build_table(rs, rom, frontier, noise_a, noise_b, forced, topup):
    """Region list for PCM 1..100 given the attack/static frontier, forced
    attack boundaries (from ear review), and chosen top-up boundaries."""
    onsets = [p * PAGE for p in range(1, frontier // PAGE)
              if rs.attack_like(p * PAGE)]
    att = sorted(set([0] + onsets + list(forced) + list(topup)))[:47]
    att_regions = list(zip(att, att[1:] + [frontier]))

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
    ap.add_argument("--review", help="JSON with ear-review findings: "
                    '{"merged": [pcm, ...], "bad": [pcm, ...]}')
    ap.add_argument("--prev", help="previous hypothesis JSON the review refers to")
    args = ap.parse_args()

    rs = D5RomSet(args.romdir)
    rom = np.asarray(rs.audio)
    noise_a, noise_b = noise_extent(rs, rom)
    print(f"Noise block (PCM 76): words {noise_a}..{noise_b} "
          f"(pages {noise_a/PAGE:.2f}..{noise_b/PAGE:.2f})")

    # ear-review constraints: force a split inside every region reported as
    # containing two sounds (attack zone only; positions on the page grid)
    forced = []
    prev = None
    if args.review and args.prev:
        review = json.load(open(args.review))
        prev = json.load(open(args.prev))
        zone_flux = flux(rom, PAGE)
        for pcm in review.get("merged", []):
            e = prev[pcm - 1]
            pages = range(e["start"] // PAGE + 1, e["end"] // PAGE)
            if pages:
                p = max(pages, key=lambda q: zone_flux[q])
                forced.append(p * PAGE)
                print(f"review: forcing split inside PCM {pcm} at page {p}")

    def energetic(table):
        # peak-based: attacks carry long decay tails, so RMS would misjudge
        return all(float(np.max(np.abs(rom[e["start"]: e["end"]]), initial=0.0)) >= 0.02
                   for e in table)

    from itertools import combinations
    best = None
    for fp in range(84, 100):
        frontier = fp * PAGE
        onset_count = 1 + sum(1 for p in range(1, fp) if rs.attack_like(p * PAGE))
        need = 47 - onset_count - len([f for f in forced if f < frontier])
        if need < 0 or need > 3:
            continue          # implausible frontier
        cands = [c for c in topup_candidates(rs, rom, frontier, need)
                 if c not in forced][: need + 4] if need else []
        for topup in (combinations(cands, need) if need else [()]):
            regions = build_table(rs, rom, frontier, noise_a, noise_b, forced, topup)
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
            score = sum(1 for _, r in res if r and r[0])
            if best is None or score > best[0]:
                best = (score, frontier, table, res)

    if best is None:
        raise SystemExit("no candidate satisfied the energy constraint -- "
                         "inspect islands/thresholds")
    score, frontier, table, res = best
    print(f"best frontier (start of PCM 48): page {frontier//PAGE}  "
          f"({score}/{len(CHECKS)} checks pass, all regions energetic)")
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
