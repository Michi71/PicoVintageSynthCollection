# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Builds .rdp packs from a ROM set, with no emulator in the loop.

    rd_make_packs.py <program.bin> <params.bin> <outdir> <patch> <offset> ...

Each patch is given as a pair: its index and where its parameter block starts,
which the patch table supplies -- (address - $4000) | (bank << 15). See
RD_FIRMWARE.md.

This is make_packs.sh's job done by arithmetic instead of by capture, and it is
not byte-identical to one. Two things account for the difference, both of them
timing and neither of them audible:

**The onsets.** A capture opens with the writes the firmware makes before it
starts on a part's list, at the moment it makes them. That is the MCU walking
its ten parts, not anything in the ROM, so the constants in ONSET below are
taken from the captured packs -- there is nowhere else to take them from.

**The first segment.** Its ramp starts from whatever the note-on preamble left
behind. Approximating that as zero puts every later timestamp a constant ten
samples late, which at this machine's rates is a third of a millisecond.

Everything that decides how a note *sounds* is exact: the chains, the wave
addresses, the pitches and every segment duration after the first.
rd_parse_check.py is what says so.

The chains also run to their own end here rather than stopping where a capture's
window did, so a generated pack is a little smaller and a little more complete
than a recorded one.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rd_parse  # noqa: E402

NOTES = range(21, 109)
VELOCITIES = (40, 80, 110, 127)

# What the firmware writes before it starts on a part's list, and when. A
# capture records it; it is the MCU walking its ten parts, so it is a per-part
# constant rather than anything in the ROM. Taken from the captured packs
# because there is nowhere else to take it from.
ONSET = (30, 32, 34, 36, 37, 39, 41, 42, 44, 21)
PREAMBLE = ((5, 31, 252), (7, 0, 0))          # t, dest, speed
ENV_OFFSET = 0xFF


def build_part(rom, part, index):
    """One part's record and its segment list, as the pack format wants them."""
    segs = []
    t = ONSET[index]
    for i, (dest, speed, dur) in enumerate(part["segments"]):
        segs.append((t, dest, speed))
        t += dur if dur is not None else 0
    return segs


def build(rom, patch, bank, out):
    entries = []
    for note in NOTES:
        for vel in VELOCITIES:
            parsed = rd_parse.parse(rom, note, vel)
            parts = []
            for i, p in enumerate(parsed["parts"]):
                if not p["segments"]:
                    continue                  # a part the zone leaves unused
                segs = build_part(rom, p, i)
                parts.append((0 if i < 9 else 0xFF, p["pitch"],
                              p["wave"] >> 8, p["wave"] & 0xFF, segs))
            entries.append((note, vel, parts))

    blob = bytearray(b"RDP2")
    blob += struct.pack("<I", 1)
    blob += struct.pack("<BBH", patch, bank, len(entries))
    for note, vel, parts in entries:
        blob += struct.pack("<BBB", note, vel, len(parts))
        for flags, pitch, wlo, whi, segs in parts:
            # The last segment is the one that ramps to nothing, which is what
            # the machine treats as the release.
            nseg = max(0, len(segs) - 1)
            blob += struct.pack("<BBHBBBB", flags, ENV_OFFSET, pitch,
                                wlo, whi, nseg, len(segs) - nseg)
            for t, dest, speed in segs:
                blob += struct.pack("<IBB", t, dest, speed)
    open(out, "wb").write(blob)
    return len(entries), len(blob)


def main():
    if len(sys.argv) < 6 or (len(sys.argv) - 4) % 2:
        sys.stderr.write(__doc__.split("\n\n")[1] + "\n")
        sys.exit(1)
    prog = open(sys.argv[1], "rb").read()
    prm = open(sys.argv[2], "rb").read()
    outdir = sys.argv[3]
    os.makedirs(outdir, exist_ok=True)
    args = sys.argv[4:]
    for i in range(0, len(args), 2):
        patch, off = int(args[i], 0), int(args[i + 1], 0)
        rom = rd_parse.Rom(prog, prm, off)
        path = os.path.join(outdir, f"pack_p{patch}.rdp")
        n, size = build(rom, patch, off >> 15, path)
        print(f"rd_make_packs: patch {patch}: {n} entries, {size} bytes -> {path}")


if __name__ == "__main__":
    main()
