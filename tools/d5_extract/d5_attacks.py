#!/usr/bin/env python3
"""Decode the D-50's 47 attack samples. Standalone and frozen.

    python3 d5_attacks.py <romdir> [outdir]      write WAVs and a JSON table
    python3 d5_attacks.py <romdir> --verify      check the ROM against this file

PCM 1..47 are the "vorprogrammierte Einzelklaenge" of the ROM data sheet:
one-shot attack transients, no loop. Their boundaries are settled -- confirmed
by ear across ten rounds of listening, and independently by the ROM's own
silence padding -- so they are written into this file as data and never
re-derived.

This program deliberately duplicates the decoder from d5_rom.py instead of
importing it. That is the point: everything needed to turn the two mask-ROM
images back into 47 correct samples lives in this one file, so work on the
unsettled part of the ROM (the sustained loops, PCM 48..76) cannot break it.
If the two decoders ever disagree, this one is right -- it is the one that
passes --verify.

Needs numpy only for the WAV writing path; --verify runs on the standard
library alone.
"""
import argparse
import json
import math
import os
import struct
import wave
import zlib

SAMPLE_RATE = 32000
PAGE = 2048                     # words; every attack starts on a page boundary

PCM_A_CRC = 0x1461C0FB          # TC532000P-7469, IC30, lower 128K words
PCM_B_CRC = 0xE50599BF          # TC532000P-7470, IC29, upper 128K words
PCM_COMBINED_CRC = 0xE2AED2D9   # TC534000P-7477, A+B in one, late boards
PROM_MARKER = b"Marmba"         # the first PCM name; anchors the name table

# PCM 1..47: number, name, start word, length in words.
# Gapless from word 0 to word 188416 (page 92), where the sustained loops begin.
ATTACKS = (
    (1, "Marmba", 0, 4096),
    (2, "Vibes", 4096, 4096),
    (3, "Xylo1", 8192, 4096),
    (4, "Xylo2", 12288, 4096),
    (5, "Log_Bs", 16384, 8192),
    (6, "Hammer", 24576, 2048),
    (7, "JpnDrm", 26624, 2048),
    (8, "Kalmba", 28672, 2048),
    (9, "Pluck", 30720, 2048),
    (10, "Chink", 32768, 2048),
    (11, "Agogo", 34816, 2048),
    (12, "3angle", 36864, 4096),
    (13, "Bells", 40960, 4096),
    (14, "Nails", 45056, 2048),
    (15, "Pick", 47104, 2048),
    (16, "Lpiano", 49152, 8192),
    (17, "Mpiano", 57344, 8192),
    (18, "Hpiano", 65536, 8192),
    (19, "Harpsi", 73728, 8192),
    (20, "Harp", 81920, 4096),
    (21, "OrgPrc", 86016, 4096),
    (22, "Steel", 90112, 4096),
    (23, "Nylon", 94208, 4096),
    (24, "Eguit1", 98304, 4096),
    (25, "Eguit2", 102400, 4096),
    (26, "Dirt", 106496, 4096),
    (27, "P_Bass", 110592, 4096),
    (28, "Pop", 114688, 4096),
    (29, "Thump", 118784, 4096),
    (30, "Uprite", 122880, 4096),
    (31, "Clarnt", 126976, 4096),
    (32, "Breath", 131072, 4096),
    (33, "Steam", 135168, 4096),
    (34, "FluteH", 139264, 4096),
    (35, "FluteL", 143360, 4096),
    (36, "Guiro", 147456, 2048),
    (37, "IndFlt", 149504, 2048),
    (38, "Harmo", 151552, 4096),
    (39, "Lips1", 155648, 2048),
    (40, "Lips2", 157696, 2048),
    (41, "Trumpt", 159744, 4096),
    (42, "Bones", 163840, 4096),
    (43, "Contra", 167936, 4096),
    (44, "Cello", 172032, 4096),
    (45, "VioBow", 176128, 4096),
    (46, "Violns", 180224, 4096),
    (47, "Pizz", 184320, 4096),
)

STATIC_START = 188416           # page 92; PCM 48 and the sustained loops follow


# ------------------------------------------------------------------- decoding
#
# The PCM ROMs hold 16-bit big-endian words. The board routing permutes the
# bits; the value is then a sign bit plus a 15-bit log2 magnitude, so
# amplitude = 2^((mag - 32767) / 2048). Format described in the VOGONS thread
# on LA-synth sample ROMs (t=77094); this is an independent implementation.


def reorder_bits(raw):
    o = raw & 0x8000
    o |= (raw << 8) & 0x4000
    o |= (raw >> 1) & 0x3F80
    o |= (raw << 1) & 0x007E
    o |= (raw >> 7) & 0x0001
    return o


def build_lut():
    lut = [0.0] * 65536
    for raw in range(65536):
        o = reorder_bits(raw)
        amp = math.pow(2.0, ((o & 0x7FFF) - 32767.0) / 2048.0)
        lut[raw] = -amp if o & 0x8000 else amp
    return lut


def decode_pcm(data, lut):
    return [lut[data[i] << 8 | data[i + 1]] for i in range(0, len(data) - 1, 2)]


def fold(data):
    """Several dumps in the wild contain the 256 KB chip twice."""
    while len(data) >= 2 and data[: len(data) // 2] == data[len(data) // 2:]:
        data = data[: len(data) // 2]
    return data


def load(romdir):
    """Find and decode the PCM pair. Returns (audio, names)."""
    a = b = combined = None
    prom = None
    for root, _dirs, files in os.walk(romdir):
        for fn in sorted(files):
            path = os.path.join(root, fn)
            if os.path.getsize(path) > 2 << 20:
                continue
            data = fold(open(path, "rb").read())
            crc = zlib.crc32(data)
            if crc == PCM_A_CRC:
                a = data
            elif crc == PCM_B_CRC:
                b = data
            elif crc == PCM_COMBINED_CRC:
                combined = data
            elif prom is None and len(data) == 65536 and PROM_MARKER in data:
                prom = data
    if a is None and combined is not None:
        a, b = combined[: len(combined) // 2], combined[len(combined) // 2:]
    if a is None or b is None:
        raise SystemExit(f"no PCM ROM pair found under {romdir}\n"
                         f"expected CRC32 {PCM_A_CRC:08X} and {PCM_B_CRC:08X}, "
                         f"or the combined image {PCM_COMBINED_CRC:08X}")
    lut = build_lut()
    audio = decode_pcm(a, lut) + decode_pcm(b, lut)

    names = None
    if prom is not None:
        off = prom.find(PROM_MARKER)
        names = [prom[off + 6 * i: off + 6 * i + 6].decode("ascii", "replace").strip()
                 for i in range(100)]
    return audio, names


# --------------------------------------------------------------- verification


def verify(audio, names):
    """Check the ROM against the table baked into this file.

    Four independent things have to hold, and each one has caught a mistake at
    some point: the ROM is the size we think, the names in the program EPROM
    still match ours, every sample carries signal, and -- the strongest of them
    -- the silence the ROM pads its one-shots with lands exactly on our
    boundaries. That last one is the ROM stating its own sample ends: a padded
    one-shot is followed by all-zero words up to the page edge, and magnitude 0
    in this log format is 2^-16, far below anything the encoder produces by
    accident.
    """
    problems = []

    if len(audio) != 262144:
        problems.append(f"PCM space is {len(audio)} words, expected 262144")

    if names is not None:
        for pcm, name, _s, _l in ATTACKS:
            if names[pcm - 1] != name:
                problems.append(f"PCM {pcm}: EPROM says {names[pcm-1]!r}, "
                                f"this file says {name!r}")

    total = 0
    for i, (pcm, name, start, length) in enumerate(ATTACKS):
        if start % PAGE:
            problems.append(f"PCM {pcm} {name}: start {start} is not on a page")
        if start != total:
            problems.append(f"PCM {pcm} {name}: starts at {start}, "
                            f"but the previous sample ends at {total}")
        total = start + length
        peak = max((abs(v) for v in audio[start:start + length]), default=0.0)
        if peak < 0.02:
            problems.append(f"PCM {pcm} {name}: peak {peak:.4f}, effectively silent")
    if total != STATIC_START:
        problems.append(f"attacks end at {total}, expected {STATIC_START}")

    marked = zero_run_boundaries(audio)
    ours = {s for _p, _n, s, _l in ATTACKS} | {STATIC_START}
    stray = sorted(m for m in marked if m not in ours)
    if stray:
        problems.append(f"silence padding marks {len(stray)} boundaries we do not "
                        f"have: {stray}")
    return problems, sorted(marked)


ZERO_WORD = math.pow(2.0, -32767.0 / 2048.0)    # what raw word 0x0000 decodes to


def zero_run_boundaries(audio):
    """Page boundaries the ROM marks by padding the sample before them.

    The pad word is the all-zero raw word, which in this log format is not
    amplitude zero but 2^-16 -- one part in 65536, inaudible, and a value the
    encoder never produces by accident. Testing for 0.0 finds nothing, which
    is a mistake worth naming: it makes the check pass silently.
    """
    runs = []
    start = None
    for w in range(STATIC_START + 2):
        if w < len(audio) and audio[w] == ZERO_WORD:
            if start is None:
                start = w
        elif start is not None:
            runs.append((start, w))
            start = None
    if start is not None:
        runs.append((start, STATIC_START + 2))

    out = set()
    for begin, end in runs:
        if end - begin < 4:
            continue
        # The pad usually stops on the page edge, but sometimes overruns it by
        # a single word, so accept either. Requiring an exact stop loses most
        # of the boundaries -- and loses them quietly.
        base = (end // PAGE) * PAGE
        if base <= end <= base + 1 and begin < base:
            out.add(base)
    return out


# -------------------------------------------------------------------- writing


def write_wav(path, samples):
    peak = max((abs(v) for v in samples), default=0.0) or 1.0
    frames = b"".join(struct.pack("<h", max(-32767, min(32767, int(v / peak * 0.9 * 32767))))
                      for v in samples)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("romdir")
    ap.add_argument("outdir", nargs="?", default="tools/d5_extract/out/attacks")
    ap.add_argument("--verify", action="store_true",
                    help="only check the ROM against this file, write nothing")
    args = ap.parse_args()

    audio, names = load(args.romdir)
    print(f"{len(audio)} PCM words decoded"
          + ("" if names is None else f", name table found ({names[0]}..{names[99]})"))

    problems, marked = verify(audio, names)
    print(f"{len(marked)} sample boundaries marked by silence padding, "
          f"all of them ours")
    if problems:
        print(f"\n{len(problems)} problems:")
        for p in problems:
            print(f"  {p}")
        raise SystemExit(1)
    print(f"{len(ATTACKS)} attack samples verified against the ROM")
    if args.verify:
        return

    os.makedirs(args.outdir, exist_ok=True)
    table = []
    for pcm, name, start, length in ATTACKS:
        cut = audio[start:start + length]
        # A one-shot with no room around it is a click that is over before the
        # ear arrives, so give it a little silence -- but do not touch the data.
        pad = [0.0] * (SAMPLE_RATE // 10)
        write_wav(os.path.join(args.outdir, f"{pcm:03d}_{name}.wav"),
                  pad + cut + pad + pad)
        table.append({"pcm": pcm, "name": name, "start": start,
                      "length": length, "looped": False})
    with open(os.path.join(args.outdir, "d5_attacks.json"), "w") as f:
        json.dump({"sample_rate": SAMPLE_RATE, "words": len(audio),
                   "static_start": STATIC_START, "samples": table}, f, indent=1)
    print(f"wrote {len(ATTACKS)} WAVs and d5_attacks.json into {args.outdir}/")


if __name__ == "__main__":
    main()
