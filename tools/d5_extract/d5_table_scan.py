#!/usr/bin/env python3
"""Search a binary image for the D-50 PCM sample table.

    python3 tools/d5_extract/d5_table_scan.py <romdir> [image.bin]

Without an image argument the uPD78312 internal ROM from the set is scanned
(the 64 KB program EPROM has already been searched exhaustively -- it does not
contain the table; see README.md).

Every hypothesis is scored against acoustic ground truth derived from the
decoded PCM ROMs themselves:
  - PCM 1..47 are attack transients: their start must look like an onset,
    and attacks sit on 2048-word page boundaries;
  - PCM 48..76 are static loops: their region must be stationary;
  - PCM 76 "Noise" must land in the one spectrally flat page;
  - PCM 77..100 are combinations: they must lie inside space used by 1..76.
A table candidate is CONFIRMED only if all four gates pass.
"""
import sys

from d5_rom import D5RomSet, PAGE


def u16(data, o, endian):
    return (data[o] | data[o + 1] << 8) if endian == "le" else (data[o] << 8 | data[o + 1])


def score(rs, words):
    """Score 100 decoded start addresses against the acoustic anchors."""
    atk = sum(1 for w in words[:47] if w % PAGE < 64 and rs.attack_like(w - w % PAGE))
    steady = sum(1 for w in words[47:76] if rs.steady_at(w))
    noise = rs.noisy_at(words[75] - words[75] % PAGE)
    used = set(w // PAGE for w in words[:76])
    comb = sum(1 for w in words[76:] if w // PAGE in used)
    return atk, steady, noise, comb


def scan(rs, data, label):
    nw = len(rs.audio)
    hits = []
    variants = [("u16", e, u, s) for e in ("le", "be") for u in (1, 2, 4, 8)
                for s in (2, 3, 4, 5, 6, 8)]
    variants += [("u8", None, u, s) for u in (512, 1024, 2048) for s in (1, 2, 3, 4)]
    for kind, endian, unit, stride in variants:
        width = 2 if kind == "u16" else 1
        for base in range(0, len(data) - 100 * stride - width):
            words = []
            ok = True
            for i in range(100):
                o = base + i * stride
                v = u16(data, o, endian) if kind == "u16" else data[o]
                w = v * unit
                if w >= nw:
                    ok = False
                    break
                words.append(w)
            if not ok or len(set(words)) < 85:
                continue
            atk, steady, noise, comb = score(rs, words)
            if atk >= 38:
                hits.append((atk, steady, noise, comb, kind, endian, unit, stride, base, words))
    hits.sort(key=lambda h: -(h[0] + h[1] + 12 * h[2] + h[3] / 2))
    print(f"--- {label}: {len(hits)} candidates with >= 38/47 attacks ---")
    for atk, steady, noise, comb, kind, endian, unit, stride, base, words in hits[:6]:
        conf = atk >= 44 and noise and steady >= 20 and comb >= 20
        print(f"  {'CONFIRMED' if conf else 'candidate'} {kind}{endian or ''}*{unit} "
              f"stride {stride} at 0x{base:04X}: atk {atk}/47 steady {steady}/29 "
              f"noise {noise} comb {comb}/24")
        if conf:
            for i, w in enumerate(words):
                print(f"    PCM{i+1:3} {rs.names[i]:6}: word {w:6} (page {w/PAGE:.2f})")
    return hits


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    rs = D5RomSet(sys.argv[1])
    if len(sys.argv) == 3:
        data = open(sys.argv[2], "rb").read()
        scan(rs, data, sys.argv[2])
    elif rs.internal is not None:
        scan(rs, rs.internal, "uPD78312 internal ROM")
    else:
        sys.exit("no internal ROM in the set and no image given -- nothing to scan.\n"
                 "The program EPROM does not contain the table (see README.md).")


if __name__ == "__main__":
    main()
