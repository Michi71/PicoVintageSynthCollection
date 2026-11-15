#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""
compress_sf2.py -- shrink unlooped Mellotron SF2 sets for a 16 MB flash.

Every 7-8 s recording becomes "attack plus short loop" (loop points from the
FindLoopPoints helper in tools/cp_sampleprep), and of the 35 recordings (one
per key) only every --step-th is kept; the neighbouring keys are mapped onto
it by key range.

    python3 compress_sf2.py -o compressed/ source/*.sf2
    python3 compress_sf2.py --step 4 --max-ms 600 -o compressed/ "M400 Cello.sf2"

Special cases:
  * a file with one long recording (a gong): one-shot, cut to --oneshot-s and
    faded out, no loop.
  * recordings under 1 s (a key click): the file is copied unchanged.
"""
import os, sys, glob, struct, shutil, argparse, subprocess, tempfile
from concurrent.futures import ThreadPoolExecutor
import numpy as np

FLP_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cp_sampleprep",
                           "FindLoopPoints")
G_KEYRANGE, G_SAMPLEID, G_SAMPLEMODES, G_ROOTKEY = 43, 53, 54, 58
TAIL = 8          # samples kept after the loop end for interpolation
PAD = 46          # SF2: at least 46 zero samples after each sample


# --------------------------------------------------------------------------
# Reading SF2
# --------------------------------------------------------------------------
def _chunks(b, off, end):
    while off < end:
        cid = b[off:off + 4]
        sz = struct.unpack("<I", b[off + 4:off + 8])[0]
        yield cid, off + 8, sz
        off += 8 + sz + (sz & 1)


def read_sf2(path):
    b = open(path, "rb").read()
    lists = {}
    for cid, o, sz in _chunks(b, 12, len(b)):
        lists[b[o:o + 4]] = [(c, b[p:p + s]) for c, p, s in _chunks(b, o + 4, o + sz)]
    pd = dict(lists[b"pdta"])
    smpl = np.frombuffer(dict(lists[b"sdta"])[b"smpl"], dtype="<i2")

    samples = []
    sh = pd[b"shdr"]
    for i in range(len(sh) // 46 - 1):
        name, st, en, ls, le, sr, key, corr, link, typ = struct.unpack(
            "<20sIIIIIBbHH", sh[i * 46:(i + 1) * 46])
        samples.append(dict(name=name, data=smpl[st:en].copy(), sr=sr, key=key,
                            corr=corr, typ=typ, loop=(ls - st, le - st)))

    inst = pd[b"inst"]
    assert len(inst) // 22 == 2, "only SF2 files with exactly one instrument"
    b0 = struct.unpack("<H", inst[20:22])[0]
    b1 = struct.unpack("<H", inst[42:44])[0]
    bag, gen, mod = pd[b"ibag"], pd[b"igen"], pd[b"imod"]
    zones = []
    for z in range(b0, b1):
        g0, m0 = struct.unpack("<HH", bag[z * 4:z * 4 + 4])
        g1, m1 = struct.unpack("<HH", bag[z * 4 + 4:z * 4 + 8])
        gens = [struct.unpack("<Hh", gen[g * 4:g * 4 + 4]) for g in range(g0, g1)]
        zones.append(dict(gens=gens, mods=mod[m0 * 10:m1 * 10]))
    return dict(info=lists[b"INFO"], pd=pd, samples=samples, zones=zones,
                inst_name=inst[:20])


# --------------------------------------------------------------------------
# Writing SF2
# --------------------------------------------------------------------------
def _ck(cid, data):
    return cid + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def write_sf2(path, sf):
    smpl, shdr = [], b""
    pos = 0
    for s in sf["samples"]:
        d = s["data"].astype("<i2")
        ls, le = s["loop"] if s["loop"] else (0, len(d))
        shdr += struct.pack("<20sIIIIIBbHH", s["name"], pos, pos + len(d),
                            pos + ls, pos + le, s["sr"], s["key"], s["corr"], 0, s["typ"])
        smpl += [d.tobytes(), b"\0" * (PAD * 2)]
        pos += len(d) + PAD
    shdr += struct.pack("<20sIIIIIBbHH", b"EOS", 0, 0, 0, 0, 0, 0, 0, 0, 0)

    ibag = igen = imod = b""
    ng = nm = 0
    for z in sf["zones"]:
        ibag += struct.pack("<HH", ng, nm)
        for op, val in z["gens"]:
            igen += struct.pack("<Hh", op, val)
        imod += z["mods"]
        ng += len(z["gens"])
        nm += len(z["mods"]) // 10
    ibag += struct.pack("<HH", ng, nm)
    igen += b"\0" * 4
    imod += b"\0" * 10
    inst = struct.pack("<20sH", sf["inst_name"], 0) + struct.pack("<20sH", b"EOI", len(sf["zones"]))

    pd = sf["pd"]
    pdta = b"pdta" + b"".join([
        _ck(b"phdr", pd[b"phdr"]), _ck(b"pbag", pd[b"pbag"]), _ck(b"pmod", pd[b"pmod"]),
        _ck(b"pgen", pd[b"pgen"]), _ck(b"inst", inst), _ck(b"ibag", ibag),
        _ck(b"imod", imod), _ck(b"igen", igen), _ck(b"shdr", shdr)])
    info = b"INFO" + b"".join(_ck(c, d) for c, d in sf["info"])
    sdta = b"sdta" + _ck(b"smpl", b"".join(smpl))
    body = b"sfbk" + _ck(b"LIST", info) + _ck(b"LIST", sdta) + _ck(b"LIST", pdta)
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", len(body)) + body)


# --------------------------------------------------------------------------
# Finding the loop
# --------------------------------------------------------------------------
def _write_wav(path, x):
    d = x.astype("<i2").tobytes()
    with open(path, "wb") as fh:   # rate is irrelevant: the finder works in samples
        fh.write(b"RIFF" + struct.pack("<I", 36 + len(d)) + b"WAVEfmt " +
                 struct.pack("<IHHIIHH", 16, 1, 1, 32000, 64000, 2, 16) +
                 b"data" + struct.pack("<I", len(d)) + d)


def run_finder(binary, x, periods, tmpbase):
    """-> (loop_start, loop_end, score) or None. The loop end is the last zero crossing."""
    wav = tmpbase + ".wav"
    _write_wav(wav, x)
    out = subprocess.run([binary, wav, str(periods)], capture_output=True, text=True,
                         timeout=60).stdout
    score = None
    for line in out.splitlines():
        if line.startswith("Final score:"):
            score = float(line.split(":")[1])
    try:
        ls = int(open(tmpbase + ".loop").read().strip())
        le = (os.path.getsize(wav) - 44) // 2 - 1
    except (OSError, ValueError):
        return None
    if score is None or le - ls < 16:
        return None
    return ls, le, score


def period_at(x, pos, sr, key):
    """Fundamental period (fractional samples) just before pos, by autocorrelation."""
    p0 = sr / (440.0 * 2 ** ((key - 69) / 12))
    n = int(max(2048, 8 * p0))
    seg = x[max(0, pos - n):pos].astype(float)
    seg -= seg.mean()
    lo, hi = int(p0 * 0.94), int(p0 * 1.06) + 2
    lags = np.arange(lo, hi)
    ac = np.array([np.dot(seg[:-l], seg[l:]) / (len(seg) - l) for l in lags])
    i = int(np.argmax(ac))
    if 0 < i < len(ac) - 1:
        a, b, c = ac[i - 1], ac[i], ac[i + 1]
        den = a - 2 * b + c
        return lags[i] + (0.5 * (a - c) / den if den else 0.0)
    return float(lags[i])


def refine_start(x, ls, le, sr, key):
    """Moves the loop start by at most +-0.6 periods to where the waveform best
    matches the one at the loop end. The finder compares zero-crossing spacings
    only; on ensemble sounds (strings, choir) that often just misses the
    fundamental period, which is heard as detuning and buzz."""
    xf = x.astype(float)
    P = period_at(x, le, sr, key)
    N = int(2 * P)
    if le + N > len(xf):
        return ls
    b = xf[le - N:le + N]
    nb = np.linalg.norm(b) + 1e-9
    best, best_c = ls, -2.0
    r = int(0.6 * P)
    for c in range(max(N, ls - r), min(le - int(2 * P), ls + r) + 1):
        a = xf[c - N:c + N]
        cc = float(np.dot(a, b) / ((np.linalg.norm(a) + 1e-9) * nb))
        if cc > best_c:
            best, best_c = c, cc
    return best


def rate_loop(x, ls, le, sr, key, sustain_rms):
    """Independent check of what the finder returned: seam, tuning, level."""
    xf = x.astype(float)
    L = le - ls
    P = period_at(x, le, sr, key)
    n_per = max(1, round(L / P))
    cents = 1200 * np.log2(L / (n_per * P))
    N = int(2 * P)
    if ls < N or le + N > len(xf):
        return None
    a, b = xf[ls - N:ls + N], xf[le - N:le + N]
    seam = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))
    db = 20 * np.log10((np.sqrt(np.mean(xf[ls:le] ** 2)) + 1e-9) / sustain_rms)
    return dict(seam=seam, cents=float(cents), db=float(db), n_per=n_per)


def loop_sample(s, a, tmpdir, tag):
    """Best loop for one recording over several window ends."""
    x, sr, key = s["data"], s["sr"], s["key"]
    xf = x.astype(float)
    sustain = np.sqrt(np.mean(xf[int(0.3 * sr):int(3.0 * sr)] ** 2))
    best = None
    seen = set()
    for ms in range(a.min_ms, a.max_ms + 1, a.win_step_ms):
        for periods in a.seeds:
            r = run_finder(a.finder, x[:int(ms * sr / 1000)], periods,
                           os.path.join(tmpdir, tag))
            if r is None or r[:2] in seen:
                continue
            seen.add(r[:2])
            ls, le, score = r
            if not a.no_refine:
                ls = refine_start(x, ls, le, sr, key)
            q = rate_loop(x, ls, le, sr, key, sustain)
            if q is None:
                continue
            ok = (abs(q["cents"]) <= a.max_cents and abs(q["db"]) <= a.max_db
                  and q["n_per"] >= a.min_periods
                  and ls >= 0.2 * sr and q["seam"] >= a.min_seam)
            # Fallback when nothing meets the tolerance: rather a longer loop with
            # a weaker seam (it is crossfaded) than one or two buzzing periods.
            usable = (q["n_per"] >= a.min_periods and abs(q["cents"]) <= 2 * a.max_cents
                      and abs(q["db"]) <= 2 * a.max_db)
            # Merit: clean seam, matching level; longer loops sound less
            # static, shorter windows save space.
            g = (q["seam"] - 0.03 * abs(q["db"]) - 0.01 * abs(q["cents"]) - 0.3 * score
                 + 0.004 * min(q["n_per"], 16)
                 - 0.05 * (ms - a.min_ms) / max(1, a.max_ms - a.min_ms))
            cand = dict(ls=ls, le=le, score=score, ok=ok, tier=ok * 2 + usable, g=g, ms=ms, **q)
            if best is None or (cand["tier"], cand["g"]) > (best["tier"], best["g"]):
                best = cand
    return best


def bake_loop(x, ls, le):
    """Cuts at the loop end, crossfades the seam, appends the read-ahead tail."""
    y = x[:le].astype(float)
    n = min((le - ls) // 2, ls, 256)
    if n > 0:
        w = np.linspace(0.0, 1.0, n, endpoint=False) + 1.0 / n      # -> 1 at the loop end
        y[le - n:le] = y[le - n:le] * (1 - w) + x[ls - n:ls].astype(float) * w
    y = np.concatenate([y, y[ls:ls + TAIL]])
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


# --------------------------------------------------------------------------
# One file
# --------------------------------------------------------------------------
def zone_parts(z):
    kr = sid = None
    rest = []
    for op, val in z["gens"]:
        if op == G_KEYRANGE:
            kr = (val & 0xFF, (val >> 8) & 0xFF)
        elif op == G_SAMPLEID:
            sid = val
        elif op != G_SAMPLEMODES:
            rest.append((op, val))
    return kr, sid, rest


def process(path, out, a):
    sf = read_sf2(path)
    S = sf["samples"]
    name = os.path.basename(path)

    if all(len(s["data"]) < s["sr"] for s in S):                 # key click
        shutil.copyfile(path, out)
        print(f"{name}: short samples, copied unchanged")
        return

    if len(S) == 1:                                              # one-shot
        s = S[0]
        n = min(len(s["data"]), int(a.oneshot_s * s["sr"]))
        y = s["data"][:n].astype(float)
        f = min(n, int(a.oneshot_fade_s * s["sr"]))
        y[n - f:] *= np.cos(np.linspace(0, np.pi / 2, f)) ** 2
        s["data"] = np.round(y).astype(np.int16)
        s["loop"] = None
        write_sf2(out, sf)
        print(f"{name}: one-shot cut to {n / s['sr']:.1f} s")
        return

    # Sort the zones by key and split them into groups of --step keys
    zs = []
    for z in sf["zones"]:
        kr, sid, rest = zone_parts(z)
        if sid is not None:
            kr = kr or (0, 127)
            zs.append(dict(key=kr[0], hi=kr[1], sid=sid, rest=rest, mods=z["mods"]))
    glob_zones = [z for z in sf["zones"] if zone_parts(z)[1] is None]
    zs.sort(key=lambda z: z["key"])
    groups = [zs[i:i + a.step] for i in range(0, len(zs), a.step)]

    with tempfile.TemporaryDirectory() as tmp:
        with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
            loops = list(ex.map(lambda z: loop_sample(S[z["sid"]], a, tmp, f"s{z['sid']}"), zs))
    for z, lp in zip(zs, loops):
        z["lp"] = lp

    new_samples, new_zones = [], list(glob_zones)
    print(f"{name}:")
    for grp in groups:
        centre = grp[len(grp) // 2]["key"] if len(grp) % 2 else None

        def rank(z):
            lp = z["lp"]
            if lp is None:
                return (-1, -9.0)
            return (lp["tier"], lp["g"] + (0.03 if z["key"] == centre else 0.0))
        z = max(grp, key=rank)
        s, lp = S[z["sid"]], z["lp"]
        lo, hi = grp[0]["key"], grp[-1]["hi"]
        if lp is None:          # no loop found: cut to the longest window and fade
            n = int(a.max_ms * s["sr"] / 1000)
            y = s["data"][:n].astype(float)
            y[n // 2:] *= np.linspace(1, 0, n - n // 2)
            data, loop, note = np.round(y).astype(np.int16), None, "NO LOOP"
        else:
            data = bake_loop(s["data"], lp["ls"], lp["le"])
            loop = (lp["ls"], lp["le"])
            note = (f"loop {lp['ls']:6d}-{lp['le']:6d} ({lp['n_per']:2d} per.) score {lp['score']:.3f} "
                    f"seam {lp['seam']:.3f} {lp['cents']:+5.1f} ct {lp['db']:+4.1f} dB"
                    + ("" if lp["ok"] else "  (!) outside tolerance"))
        new_zones.append(dict(
            gens=[(G_KEYRANGE, struct.unpack("<h", bytes([lo, hi]))[0])] + z["rest"]
                 + ([(G_SAMPLEMODES, 1)] if loop else []) + [(G_SAMPLEID, len(new_samples))],
            mods=z["mods"]))
        new_samples.append(dict(s, data=data, loop=loop))
        print(f"  keys {lo}-{hi} <- {s['name'].split(bytes(1))[0].decode('latin1'):12s} "
              f"(Root {s['key']}) {len(data) / s['sr'] * 1000:4.0f} ms  {note}")

    sf["samples"], sf["zones"] = new_samples, new_zones
    write_sf2(out, sf)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("files", nargs="*", help="SF2 files (default: all next to the script)")
    ap.add_argument("-o", "--outdir", default=os.path.join(here, "compressed"))
    ap.add_argument("--step", type=int, default=3, help="one recording per N keys (default 3)")
    ap.add_argument("--min-ms", type=int, default=400, help="earliest loop end")
    ap.add_argument("--max-ms", type=int, default=800, help="latest loop end")
    ap.add_argument("--win-step-ms", type=int, default=20)
    ap.add_argument("--max-cents", type=float, default=6.0, help="tolerated detuning of the loop")
    ap.add_argument("--max-db", type=float, default=2.0, help="tolerated level error of the loop")
    ap.add_argument("--min-periods", type=int, default=6, help="minimum loop length in periods")
    ap.add_argument("--min-seam", type=float, default=0.9,
                    help="minimum correlation of the waveform at loop start and end")
    ap.add_argument("--no-refine", action="store_true",
                    help="keep the loop start exactly as FindLoopPoints returns it")
    ap.add_argument("--oneshot-s", type=float, default=3.0)
    ap.add_argument("--oneshot-fade-s", type=float, default=1.5)
    ap.add_argument("--finder", default=FLP_DEFAULT)
    # The finder counts zero-crossing spacings, not fundamental periods; on
    # sounds rich in overtones 30 of them are only 1-3 real periods. Larger
    # seeds give longer loops; rate_loop() makes the choice.
    ap.add_argument("--seeds", type=int, nargs="+", default=[10, 30, 60, 100, 160, 250],
                    help="seeds for the finder's num_periods")
    a = ap.parse_args()
    if not os.access(a.finder, os.X_OK):
        sys.exit(f"FindLoopPoints not found: {a.finder}")
    files = a.files or sorted(glob.glob(os.path.join(here, "*.sf2")))
    os.makedirs(a.outdir, exist_ok=True)
    total_in = total_out = 0
    for f in files:
        out = os.path.join(a.outdir, os.path.basename(f))
        process(f, out, a)
        total_in += os.path.getsize(f)
        total_out += os.path.getsize(out)
        print(f"  -> {os.path.getsize(f) / 1024:.0f} kB -> {os.path.getsize(out) / 1024:.0f} kB\n")
    print(f"total: {total_in / 1e6:.1f} MB -> {total_out / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
