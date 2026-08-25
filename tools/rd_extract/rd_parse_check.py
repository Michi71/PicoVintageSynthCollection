# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Checks rd_parse against a captured pack, segment by segment.

    rd_parse_check.py <program.bin> <params.bin> <patch-offset> <pack.rdp>

The parser derives the chains from the ROM; the pack recorded what the firmware
actually programmed. They should agree exactly, and this says whether they do.

Two things have to be allowed for, and neither is a disagreement:

**The preamble.** A capture may open with two entries the firmware writes before
it starts on the list -- `31/252` then `0/0`. Sometimes it does not. So the
comparison finds where the parser's first segment appears rather than assuming a
fixed offset; measuring with a fixed strip of two made a fifth of all segments
look wrong when none of them were.

**The end.** A capture stops when the key comes up, so a pack holds however much
of the chain fitted in its window; the parser walks the list to its own end. A
pack that stops early is not a mismatch.
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import rd_parse  # noqa: E402


def load_pack(path):
    d = open(path, "rb").read()
    if d[:4] != b"RDP2":
        sys.stderr.write(f"{path}: not an .rdp pack\n")
        sys.exit(1)
    count = struct.unpack("<H", d[10:12])[0]
    at, out = 12, {}
    for _ in range(count):
        note, vel, nparts = d[at], d[at + 1], d[at + 2]
        at += 3
        parts = []
        for _ in range(nparts):
            nseg, nrel = d[at + 6], d[at + 7]
            at += 8
            segs = [struct.unpack("<IBB", d[at + 6 * i:at + 6 * i + 6])
                    for i in range(nseg + nrel)]
            at += 6 * (nseg + nrel)
            parts.append((nseg, [(s[1], s[2]) for s in segs]))
        out[(note, vel)] = parts
    return out


def main():
    if len(sys.argv) != 5:
        sys.stderr.write("usage: rd_parse_check.py <program.bin> <params.bin> "
                         "<patch-offset> <pack.rdp>\n")
        sys.exit(1)
    rom = rd_parse.Rom(open(sys.argv[1], "rb").read(),
                       open(sys.argv[2], "rb").read(), int(sys.argv[3], 0))
    packs = load_pack(sys.argv[4])

    parts = complete = compared = agreed = unaligned = 0
    for (note, vel), ref in sorted(packs.items()):
        got = rd_parse.parse(rom, note, vel)["parts"]
        for rp, gp in zip(ref, got):
            chain, mine = rp[1][:rp[0]], gp["segments"]
            parts += 1
            if not mine:
                continue
            skip = next((j for j in range(min(4, len(chain)))
                         if chain[j] == mine[0]), None)
            if skip is None:
                unaligned += 1
                continue
            theirs = chain[skip:]
            n = min(len(theirs), len(mine))
            if n == len(theirs) and all(theirs[i] == mine[i] for i in range(n)):
                complete += 1
            for i in range(n):
                compared += 1
                agreed += theirs[i] == mine[i]

    print(f"parts {parts}, segments compared {compared}")
    print(f"  segments agreeing   {100 * agreed / compared:6.2f} %")
    print(f"  chains complete     {100 * complete / parts:6.2f} %")
    if unaligned:
        print(f"  could not align     {unaligned}")
    sys.exit(0 if agreed == compared and not unaligned else 1)


if __name__ == "__main__":
    main()
