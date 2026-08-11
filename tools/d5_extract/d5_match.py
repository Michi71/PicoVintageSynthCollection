#!/usr/bin/env python3
"""Match a probe recording against the decoded PCM ROMs and emit the sample
table.

    python3 tools/d5_extract/d5_match.py <romdir> <recording.wav>
                                         [--no-calibration] [--out dir]

The recording is the D-50's dry output while playing d5_probe.mid: an
optional 14-note calibration block (PCM 1 under every structure/mute
combination), then PCM 1..100, one note each, separated by silence.

Every note is located inside the decoded 262144-word PCM space by envelope
correlation plus sample-exact cross-correlation. The result is the sample
table nobody's ROM contains (it lives in the MB87136's mask ROM): start,
end, and loop flag per PCM sample, written to d50_sample_table.json, with
the samples themselves cut from the ROM data as WAVs.

Needs numpy.
"""
import argparse
import json
import math
import os
import struct
import sys
import wave

import numpy as np

from d5_rom import D5RomSet, SAMPLE_RATE, PAGE


# ------------------------------------------------------------------ WAV load

def load_recording(path):
    with wave.open(path, "rb") as wf:
        ch, sw, rate, n = (wf.getnchannels(), wf.getsampwidth(),
                           wf.getframerate(), wf.getnframes())
        raw = wf.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 |
             b[:, 2].astype(np.int8).astype(np.int32) << 16).astype(np.float64) / (1 << 23)
    elif sw == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / (1 << 31)
    else:
        sys.exit(f"unsupported sample width {sw}")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    # resample to the ROM rate
    if rate != SAMPLE_RATE:
        t = np.arange(len(x)) * (SAMPLE_RATE / rate)
        n_out = int(len(x) * SAMPLE_RATE / rate)
        x = np.interp(np.arange(n_out), t, x)
    return x


# -------------------------------------------------------------- segmentation

def segment(x, min_len=0.3, max_gap=0.25):
    hop = SAMPLE_RATE // 100                     # 10 ms frames
    nf = len(x) // hop
    rms = np.sqrt((x[: nf * hop].reshape(nf, hop) ** 2).mean(axis=1))
    floor = np.percentile(rms, 10)
    thr = max(floor * 6, rms.max() * 0.004)
    active = rms > thr
    segs = []
    start = None
    quiet = 0
    for i, a in enumerate(active):
        if a:
            if start is None:
                start = i
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet * hop > max_gap * SAMPLE_RATE:
                segs.append((start * hop, (i - quiet + 1) * hop))
                start, quiet = None, 0
    if start is not None:
        segs.append((start * hop, nf * hop))
    return [(a, b) for a, b in segs if b - a >= min_len * SAMPLE_RATE]


# ----------------------------------------------------------------- matching

def norm_env(x, block=64):
    n = len(x) // block
    e = np.abs(x[: n * block]).reshape(n, block).mean(axis=1)
    return e


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.linalg.norm(a) * np.linalg.norm(b)
    return float(np.dot(a, b) / d) if d > 0 else 0.0


class Matcher:
    def __init__(self, rom_audio):
        self.rom = np.asarray(rom_audio)
        self.rom_env = norm_env(self.rom)

    def coarse(self, seg):
        se = norm_env(seg)
        if len(se) < 4 or len(se) >= len(self.rom_env):
            return []
        m = len(se)
        dots = np.correlate(self.rom_env, se, mode="valid")
        e2 = np.convolve(self.rom_env ** 2, np.ones(m), mode="valid")
        scores = dots / (np.sqrt(e2) * np.linalg.norm(se) + 1e-12)
        top = np.argsort(scores)[-5:][::-1]
        return [(int(o) * 64, float(scores[o])) for o in top]

    def fine(self, seg, word, span=512, win=8192):
        w = min(win, len(seg))
        s = seg[:w]
        best = (-1.0, word)
        lo = max(0, word - span)
        hi = min(len(self.rom) - w, word + span)
        if hi <= lo:
            return best[1], best[0]
        s0 = s - s.mean()
        ns = np.linalg.norm(s0) + 1e-12
        for o in range(lo, hi):
            r = self.rom[o: o + w]
            sc = float(np.dot(s0, r - r.mean()) / (ns * (np.linalg.norm(r - r.mean()) + 1e-12)))
            if sc > best[0]:
                best = (sc, o)
        return best[1], best[0]

    def locate(self, seg):
        cands = self.coarse(seg)
        best = (None, -1.0)
        for word, _ in cands:
            o, sc = self.fine(seg, word)
            if sc > best[1]:
                best = (o, sc)
        return best

    def extent(self, seg, start, step=1024):
        """Walk forward while the recording keeps tracking the ROM."""
        k = 0
        limit = min(len(seg), len(self.rom) - start)
        while k + step <= limit:
            if ncc(seg[k: k + step], self.rom[start + k: start + k + step]) < 0.45:
                break
            k += step
        end = start + k
        looped = False
        if k + step > limit - step and len(seg) > k + SAMPLE_RATE // 8:
            # recording continues past the matched span: check whether the
            # tail matches inside [start, end) again -> loop
            tail = seg[k: k + 8192]
            if len(tail) >= 2048:
                o, sc = self.locate(tail)
                if o is not None and sc > 0.5 and start <= o < max(end, start + step):
                    looped = True
        return end, looped


# -------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("romdir")
    ap.add_argument("recording")
    ap.add_argument("--no-calibration", action="store_true",
                    help="recording starts directly with PCM 1 (no 14-note prelude)")
    ap.add_argument("--out", default="tools/d5_extract/out")
    args = ap.parse_args()

    rs = D5RomSet(args.romdir)
    x = load_recording(args.recording)
    segs = segment(x)
    print(f"{len(segs)} notes found in the recording")

    n_cal = 0 if args.no_calibration else 14
    if len(segs) < n_cal + 100:
        sys.exit(f"expected {n_cal}+100 notes, found {len(segs)} -- check the "
                 f"recording level/gaps or use --no-calibration")

    m = Matcher(rs.audio)

    # Global playback-rate calibration: the probe plays C4, which need not be
    # the ROM-native rate. Search a semitone grid, then fine-tune, on the
    # first main-run note; then resample the whole recording once.
    a, b = segs[n_cal]
    probe = x[a:b][: 4 * SAMPLE_RATE]
    best = (0.0, -1.0)
    for cents in range(-1200, 1300, 100):
        r = 2 ** (cents / 1200)
        t = np.arange(int(len(probe) / r)) * r
        seg_r = np.interp(t, np.arange(len(probe)), probe)
        _, sc = m.locate(seg_r)
        if sc > best[1]:
            best = (cents, sc)
    coarse_cents = best[0]
    for cents in [coarse_cents + c / 4 for c in range(-200, 201, 25)]:
        r = 2 ** (cents / 1200)
        t = np.arange(int(len(probe) / r)) * r
        seg_r = np.interp(t, np.arange(len(probe)), probe)
        _, sc = m.locate(seg_r)
        if sc > best[1]:
            best = (cents, sc)
    rate = 2 ** (best[0] / 1200)
    print(f"playback rate: {best[0]:+.0f} cents vs ROM ({rate:.4f}x), score {best[1]:.2f}")
    if abs(best[0]) > 1:
        t = np.arange(int(len(x) / rate)) * rate
        x = np.interp(t, np.arange(len(x)), x)
        segs = segment(x)

    if n_cal:
        print("calibration block:")
        combos = [(s, mu) for s in range(1, 8) for mu in (1, 2)]
        good = []
        for (structure, mute), (a, b) in zip(combos, segs[:n_cal]):
            o, sc = m.locate(x[a:b])
            verdict = "PCM" if sc > 0.6 else "not PCM"
            if sc > 0.6:
                good.append((structure, mute))
            print(f"  structure {structure} mute {mute}: score {sc:.2f} ({verdict})")
        if not good:
            sys.exit("no calibration combination produced bare PCM playback -- "
                     "check cabling/levels before the main run is trusted")
        print(f"  bare-PCM combinations: {good}")

    table = []
    for i, (a, b) in enumerate(segs[n_cal: n_cal + 100]):
        seg = x[a:b]
        start, score = m.locate(seg)
        if start is None or score < 0.4:
            table.append({"pcm": i + 1, "name": rs.names[i], "start": None,
                          "end": None, "looped": False, "score": round(score, 3)})
            print(f"PCM{i+1:3} {rs.names[i]:6}: NO MATCH (score {score:.2f})")
            continue
        end, looped = m.extent(seg, start)
        table.append({"pcm": i + 1, "name": rs.names[i], "start": int(start),
                      "end": int(end), "looped": bool(looped),
                      "score": round(score, 3)})
        print(f"PCM{i+1:3} {rs.names[i]:6}: words {start:6}..{end:6} "
              f"(pages {start/PAGE:6.2f}..{end/PAGE:6.2f}) "
              f"{'loop' if looped else 'one-shot'}  score {score:.2f}")

    os.makedirs(args.out, exist_ok=True)
    jpath = os.path.join(args.out, "d50_sample_table.json")
    with open(jpath, "w") as f:
        json.dump(table, f, indent=1)
    print(f"wrote {jpath}")

    sdir = os.path.join(args.out, "samples")
    os.makedirs(sdir, exist_ok=True)
    for e in table:
        if e["start"] is None:
            continue
        cut = rs.audio[e["start"]: e["end"]]
        path = os.path.join(sdir, f"{e['pcm']:03d}_{e['name']}.wav")
        with wave.open(path, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(b"".join(
                struct.pack("<h", max(-32767, min(32767, int(v * 32767))))
                for v in cut))
    print(f"cut {sum(1 for e in table if e['start'] is not None)} samples into {sdir}/")


if __name__ == "__main__":
    main()
