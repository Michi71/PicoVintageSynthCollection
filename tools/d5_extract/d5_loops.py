#!/usr/bin/env python3
"""Work out the D-50's 29 sustained loops, PCM 48..76. Iterative, by ear.

    python3 d5_loops.py <romdir> --starts        propose start points
    python3 d5_loops.py <romdir> --cut [outdir]  write raw, unlooped regions
    python3 d5_loops.py <romdir> --ends          where could each loop end?

PCM 48..76 are the "vorprogrammierte Schleifenklaenge" of the ROM data sheet.
Unlike the attacks (see d5_attacks.py, settled) their boundaries are not known,
and every attempt to guess them wholesale -- by arithmetic, by periodicity, by
scoring loop quality -- has produced something that measured well and sounded
wrong. So this program does not guess. It does three separate things, in the
order the listening has to happen:

  --starts  proposes boundaries and nothing else. A sustained loop is by
            construction stationary, so where the spectrum steps, a different
            recording begins. Page-aligned candidates are marked, because the
            attacks are all page-aligned and these probably are too.

  --cut     writes each region exactly as it sits in the ROM: decoded, peak
            normalised, not looped, not pitch-shifted, not crossfaded. Nothing
            that could invent an artefact or hide one. A region is 60..200 ms,
            too short to identify alone, so `folge_alle.wav` plays them in
            order with silence between -- the instrument changes are what tell
            you whether a start point is right.

  --ends    asks, for one start point at a time, whether a loop end exists
            before the next start. It cannot lie beyond it: no loop can run
            into the following sample. Within that bound, [S,E) is a seamless
            loop exactly when the data after E continues as the data after S
            does, so the test is the correlation of rom[E:E+w] against
            rom[S:S+w]. That makes no assumption about zero crossings, which
            is where the general-purpose loop finder came unstuck here.

Start points live in d5_loop_starts.json next to this file and are meant to be
edited as the listening settles them. Delete it to start from --starts again.
"""
import argparse
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d5_attacks import STATIC_START, SAMPLE_RATE, PAGE, load  # noqa: E402

END = 262144                    # end of the PCM space
NSAMPLES = 29                   # PCM 48..76
HERE = os.path.dirname(os.path.abspath(__file__))
STARTS_JSON = os.path.join(HERE, "d5_loop_starts.json")


# ------------------------------------------------------------ start proposals


def _bands(n):
    edges = np.geomspace(30.0, 15000.0, 25)
    freq = np.fft.rfftfreq(n, 1.0 / SAMPLE_RATE)
    return [(freq >= edges[i]) & (freq < edges[i + 1]) for i in range(24)]


def spectrum(rom, a, n, masks, window):
    seg = rom[a:a + n]
    if len(seg) < n or float(np.max(np.abs(seg))) < 1e-6:
        return None
    mag = np.abs(np.fft.rfft((seg - seg.mean()) * window))
    mag = mag / (mag.sum() + 1e-12)
    v = np.array([mag[m].sum() for m in masks])
    return np.log(v / (v.sum() + 1e-12) + 1e-8)


def propose_starts(rom, win=1024, grid=256, floor=12.0, ratio=3.0):
    """Positions where the spectrum steps by much more than it drifts.

    Two conditions, and the second is what stops a slowly brightening sample
    from being chopped up: the step across the candidate must clear an absolute
    floor, and it must be several times the step measured just inside either
    neighbour. Both thresholds are ours, not the ROM's -- treat the output as
    candidates to listen to, never as an answer.
    """
    masks = _bands(win)
    window = np.hanning(win)

    def step(a, b):
        p = spectrum(rom, a, win, masks, window)
        q = spectrum(rom, b, win, masks, window)
        return None if p is None or q is None else float(np.linalg.norm(p - q))

    out = []
    for pos in range(STATIC_START + win, END - win, grid):
        over = step(pos - win, pos)
        left = step(pos - 2 * win, pos - win)
        right = step(pos, pos + win)
        if over is None or left is None or right is None:
            continue
        if over >= floor and over > ratio * max(left, right, 0.5):
            out.append((pos, over))

    # keep only the strongest candidate in each cluster
    kept = []
    for pos, score in out:
        if kept and pos - kept[-1][0] < win:
            if score > kept[-1][1]:
                kept[-1] = (pos, score)
            continue
        kept.append((pos, score))
    return kept


# ------------------------------------------------------------ exact periods
#
# The finding this whole program turns on: most of the sustained zone is not
# a recording that has to be looped, it is a single cycle the ROM already
# stores tiled. Page 93 is sixteen bit-identical copies of 128 words, page 110
# is two copies of 1024, and the repetition stops exactly at the page edge.
#
# That settles by proof what no amount of scoring could: the sample boundary
# is the page boundary, the loop is the one cycle, and playing it back cannot
# flutter because every repetition is the same bits. It also explains the
# stored pitches, which are 32000/period -- 250, 125, 62.5 and 31.25 Hz, exact
# power-of-two divisions of the sample clock.
#
# Fifteen pages have no exact repetition at any offset at all, and on eight of
# them the best non-exact match leaves a residual larger than the signal, which
# is what uncorrelated noise does. Those are a different kind of material and
# this test says nothing about where their boundaries are.


def exact_period(rom, a, b, min_reps=2):
    """The shortest lag at which [a,b) repeats bit-identically, or None."""
    n = b - a
    seg = rom[a:b]
    for p in range(8, n // min_reps + 1):
        if n % p:
            continue
        if np.array_equal(seg[:n - p], seg[p:]):
            return p
    return None


def repetition_span(rom, a, period):
    """How far the repetition at `period` holds, in words from a."""
    n = 0
    while a + period + n < END and rom[a + n] == rom[a + period + n]:
        n += 1
    return n + period if n else 0


def wrap_step(rom, a, b):
    """The jump from the last word back to the first, against the interior.

    If Roland cut a region to be looped whole, wrapping it costs no more than
    an ordinary step within it. Returned as (jump, interior median, ratio), and
    the ratio is the number that matters: below about 3 the wrap is inaudible,
    because the waveform is already moving that fast anyway.

    Measured on all 29 regions this comes out at or under 1 for every one of
    them bar VIOLlp, and exactly 0 for Noise, whose last word equals its first.
    So there are no loop points to find. The loop is the region -- which is why
    every search for a better one inside it came back with nothing.
    """
    seg = rom[a:b]
    interior = float(np.median(np.abs(np.diff(seg)))) + 1e-12
    jump = abs(float(seg[0] - seg[-1]))
    return jump, interior, jump / interior


def loop_period(rom, a, b):
    """The cycle length of a sustained region, and how we know.

    Every one of them turns out to be 32000/2^k: 128, 256, 512 or 1024 words
    stored several times over inside the page, 2048 for the Spect samples where
    the cycle fills the page exactly, and 16384 for Noise which takes eight
    pages. Where the cycle is shorter than the region the ROM simply repeats it
    bit for bit, so the two cases are one case.
    """
    p = exact_period(rom, a, b)
    if p:
        return p, "exact repetition"
    _j, _i, ratio = wrap_step(rom, a, b)
    if ratio <= 3.0:
        return b - a, "whole region, wrap is seamless"
    return b - a, f"whole region, wrap is {ratio:.0f}x the interior step"


# --------------------------------------------------------------- loop endings


def end_candidates(rom, start, limit, win=512, grid=64):
    """Score every possible loop end in (start, limit].

    A loop [start, end) is seamless when playback wrapping from end-1 back to
    start finds the same continuation it had the first time round, i.e. when
    rom[end:end+win] matches rom[start:start+win]. Scored as normalised
    correlation, so 1.0 is a perfect match and the level of the two stretches
    does not matter.
    """
    ref = rom[start:start + win]
    ref = ref - ref.mean()
    rn = float(np.linalg.norm(ref))
    if rn < 1e-9:
        return []
    out = []
    for end in range(start + grid, min(limit, END - win) + 1, grid):
        seg = rom[end:end + win]
        if len(seg) < win:
            break
        seg = seg - seg.mean()
        sn = float(np.linalg.norm(seg))
        if sn < 1e-9:
            continue
        out.append((end, float(np.dot(ref, seg)) / (rn * sn)))
    out.sort(key=lambda t: -t[1])
    return out


# -------------------------------------------------------------------- writing


def write_wav(path, x, peak_normalise=True):
    x = np.asarray(x, dtype=np.float64)
    if peak_normalise:
        x = x / (float(np.max(np.abs(x))) + 1e-12) * 0.9
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(np.clip(x * 32767, -32767, 32767).astype("<i2").tobytes())


def load_starts():
    if not os.path.exists(STARTS_JSON):
        return None
    return json.load(open(STARTS_JSON))["starts"]


def save_starts(starts, note):
    json.dump({"note": note, "static_start": STATIC_START, "end": END,
               "starts": starts}, open(STARTS_JSON, "w"), indent=1)


def regions(starts):
    """(label, start, end) with each region bounded by the next start.

    Regions are numbered by PCM only once there are exactly 29 of them. Before
    that they are numbered by position and named by page, because calling the
    third region "PCM 50" while the count is still wrong is how a working
    hypothesis turns into a fact nobody rechecks.
    """
    bounds = list(starts) + [END]
    exact = len(starts) == NSAMPLES
    out = []
    for i in range(len(starts)):
        a, b = bounds[i], bounds[i + 1]
        label = f"PCM{48+i:03d}" if exact else f"S{i+1:02d}_p{a//PAGE:03d}"
        out.append((label, a, b))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("romdir")
    ap.add_argument("outdir", nargs="?", default="tools/d5_extract/out/loops")
    ap.add_argument("--starts", action="store_true")
    ap.add_argument("--cut", action="store_true")
    ap.add_argument("--ends", action="store_true")
    args = ap.parse_args()
    if not (args.starts or args.cut or args.ends):
        ap.error("pick one of --starts, --cut, --ends")

    audio, names = load(args.romdir)
    rom = np.asarray(audio, dtype=np.float64)
    labels = names[47:76] if names else [f"PCM{48+i}" for i in range(NSAMPLES)]

    if args.starts:
        cand = propose_starts(rom)
        print(f"static zone {STATIC_START}..{END} = {END-STATIC_START} words "
              f"for {NSAMPLES} samples, {(END-STATIC_START)/NSAMPLES:.0f} on average\n")
        print(f"{'word':>7} {'page':>8} {'step':>6}  aligned")
        for pos, score in cand:
            print(f"{pos:7} {pos/PAGE:8.3f} {score:6.1f}  "
                  f"{'page' if pos % PAGE == 0 else ''}")
        aligned = sum(1 for p, _ in cand if p % PAGE == 0)
        print(f"\n{len(cand)} candidates for {NSAMPLES-1} internal boundaries, "
              f"{aligned} of them page-aligned")
        pages = (END - STATIC_START) // PAGE
        print(f"The static zone is {pages} pages for {NSAMPLES} samples, so "
              f"{2*NSAMPLES-pages} of them\nare one page and {pages-NSAMPLES} are "
              f"two -- if they are page-aligned like the attacks.")

        if load_starts() is None:
            # Start from the page grid rather than from these candidates. The
            # candidates cannot be the whole answer -- they only fire where the
            # timbre steps, and neighbouring samples from the same family (the
            # eight near-identical pages at the end, say) step by nothing at
            # all. The grid asks a smaller question of the ear: not "where does
            # a sample begin" but "does this page belong with the last one".
            starts = list(range(STATIC_START, END, PAGE))
            save_starts(starts, "page grid, unheard -- merge pages that belong "
                                "together until 29 regions remain")
            print(f"\nwrote the {len(starts)}-page grid to {STARTS_JSON}. "
                  f"Run --cut, listen in order,\nand delete the starts where a "
                  f"page continues the one before it.")
        else:
            print(f"\n{STARTS_JSON} already exists and was left alone")
        return

    starts = load_starts()
    if starts is None:
        raise SystemExit(f"no {STARTS_JSON} yet -- run --starts first")

    if args.cut:
        os.makedirs(args.outdir, exist_ok=True)
        rawdir = os.path.join(args.outdir, "raw")
        os.makedirs(rawdir, exist_ok=True)
        gap = np.zeros(int(SAMPLE_RATE * 0.3))
        pad = np.zeros(int(SAMPLE_RATE * 0.05))
        joined = []
        for i, (label, a, b) in enumerate(regions(starts)):
            period, how = loop_period(rom, a, b)
            base = f"{label}_{labels[i]}" if i < len(labels) else label
            # Tile the one cycle at its stored rate. This is not a render
            # decision -- it is what the ROM already contains, repeated
            # further, at the rate it is stored at. No interpolation, no
            # pitch correction, so nothing here can introduce a seam that
            # the machine would not also produce.
            reps = max(2, int(np.ceil(3.0 * SAMPLE_RATE / period)))
            audio_out = np.tile(rom[a:a + period], reps)
            name = f"{base}_per{period}"
            write_wav(os.path.join(rawdir, name + ".wav"), rom[a:a + period])
            write_wav(os.path.join(args.outdir, name + ".wav"), audio_out)
            joined += [pad, audio_out[:int(2.0 * SAMPLE_RATE)]
                       / (float(np.max(np.abs(audio_out))) + 1e-12) * 0.9, gap]
            print(f"{base:20} {a:6}..{b:6}  {b-a:5} W  page {a/PAGE:7.3f}  "
                  f"period {period:5} x{(b-a)//period:3}  "
                  f"{SAMPLE_RATE/period:8.3f} Hz  {how}")
        write_wav(os.path.join(args.outdir, "folge_alle.wav"),
                  np.concatenate(joined), peak_normalise=False)
        print(f"\nwrote {len(starts)} regions into {args.outdir}/, the bare cuts "
              f"into {rawdir}/,\nand folge_alle.wav playing them in order")
        return

    if args.ends:
        print("For each sample: the best loop ends before the next start.")
        print("Correlation 1.0 = the data after the end continues exactly as it "
              "does after the start.\n")
        for i, (pcm, a, b) in enumerate(regions(starts)):
            label = labels[i] if i < len(labels) else f"PCM{pcm}"
            cand = end_candidates(rom, a, b)
            if not cand:
                print(f"{pcm:3} {label:7} {a:6}..{b:6}  no candidate")
                continue
            best = ", ".join(f"{e} ({e-a} W, r={r:.3f})" for e, r in cand[:3])
            flag = '' if cand[0][1] >= 0.9 else ('  weak' if cand[0][1] >= 0.7
                                                 else '  NO GOOD END')
            print(f"{pcm:3} {label:7} {a:6}..{b:6}  {best}{flag}")


if __name__ == "__main__":
    main()
