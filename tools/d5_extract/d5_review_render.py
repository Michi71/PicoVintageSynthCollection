#!/usr/bin/env python3
"""Render the frozen sample table for review by ear.

    python3 tools/d5_extract/d5_review_render.py <romdir> [outdir]

Writes one file per sample, named by PCM number and name, plus a single
`all_samples.wav` that plays the lot in order. The rendering is what ten
rounds of listening taught us it has to be, because a raw cut is unjudgeable:

  - one-shots get leading and trailing silence, so a 60 ms transient is not a
    click that has come and gone before the ear arrives;
  - sustained loops are tiled, pitch-normalised toward 200 Hz and levelled by
    RMS, because a 16 ms single-cycle waveform at its stored rate is a buzz
    and tells nobody whether it is a cello. A wrong boundary then announces
    itself as a click or a warble once per revolution.

Needs numpy.
"""
import json
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d5_rom import D5RomSet, SAMPLE_RATE  # noqa: E402

TARGET_HZ = 200.0


def estimate_period(x):
    a = np.asarray(x, dtype=np.float64)
    a = a - a.mean()
    if len(a) < 64 or np.max(np.abs(a)) < 1e-6:
        return 0
    n = 1 << int(np.ceil(np.log2(len(a) * 2)))
    f = np.fft.rfft(a, n)
    ac = np.fft.irfft(f * np.conj(f))[:len(a)]
    if ac[0] <= 0:
        return 0
    ac = ac / ac[0]
    lo, hi = 16, min(768, len(ac) // 2)
    if hi <= lo:
        return 0
    k = int(np.argmax(ac[lo:hi])) + lo
    for d in (4, 3, 2):          # the shortest lag that is nearly as strong
        c = k // d
        if c >= lo and ac[c] > 0.93 * ac[k]:
            k = c
            break
    return k


def render_loop(loop, seconds=2.5):
    period = estimate_period(loop)
    rate = 1.0
    if period:
        rate = (SAMPLE_RATE / period) / TARGET_HZ
        while rate < 0.25:
            rate *= 2
        while rate > 4.0:
            rate /= 2
    n = int(SAMPLE_RATE * seconds)
    out = np.zeros(n)
    for r, g in ((1.0, 0.6), (1.003, 0.4), (0.5, 0.5)):   # octave and detune
        t = (np.arange(n) * r * rate) % len(loop)
        out += g * np.interp(t, np.arange(len(loop)), loop)
    env = np.minimum(1.0, np.arange(n) / (0.3 * SAMPLE_RATE))
    env *= np.minimum(1.0, (n - np.arange(n)) / (0.4 * SAMPLE_RATE))
    out *= env
    return out / (np.sqrt(np.mean(out ** 2)) + 1e-9) * 0.22


def render_oneshot(cut):
    lead = np.zeros(int(SAMPLE_RATE * 0.1))
    tail = np.zeros(int(SAMPLE_RATE * 0.5))
    peak = np.max(np.abs(cut)) + 1e-9
    return np.concatenate([lead, cut / peak * 0.85, tail])


def write_wav(path, x):
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(np.clip(x * 32767, -32767, 32767).astype("<i2").tobytes())


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    rs = D5RomSet(sys.argv[1])
    outdir = sys.argv[2] if len(sys.argv) == 3 else "tools/d5_extract/out/review"
    os.makedirs(outdir, exist_ok=True)

    here = os.path.dirname(os.path.abspath(__file__))
    table = json.load(open(os.path.join(here, "d5_sample_table.json")))["samples"]
    rom = np.asarray(rs.audio, dtype=np.float64)

    joined = []
    for e in table:
        if e["start"] is None:
            continue
        cut = rom[e["start"]:e["end"]]
        looped = e["looped"]
        audio = render_loop(cut) if looped else render_oneshot(cut)
        name = f"{e['pcm']:03d}_{e['name']}.wav"
        write_wav(os.path.join(outdir, name), audio)
        joined.append(audio)
        joined.append(np.zeros(int(SAMPLE_RATE * 0.35)))
        period = estimate_period(cut)
        hz = SAMPLE_RATE / period if period else 0.0
        print(f"{e['pcm']:3} {e['name']:6} {e['end']-e['start']:6} words  "
              f"{'loop    ' if looped else 'one-shot'}  "
              f"{hz:7.1f} Hz  {e['basis']}")

    write_wav(os.path.join(outdir, "all_samples.wav"), np.concatenate(joined))
    print(f"\nwrote {len(joined)//2} samples into {outdir}/ plus all_samples.wav")


if __name__ == "__main__":
    main()
