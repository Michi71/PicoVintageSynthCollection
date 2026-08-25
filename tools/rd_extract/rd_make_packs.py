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

Checked by putting all sixteen into roms/ and running run_regression.sh, which
compares the engine playing them against the reference emulator:

    A/B p0  n60   0.999640   against a frozen 0.999640
    A/B p0  n36   0.998891   against 0.963959 -- better, see below
    A/B p3  n60   0.991473   against 0.993413
    A/B p4  n60   0.998967   against 0.998520
    A/B p8  n60   0.998612   against 0.999158
    A/B p15 n60   0.999899   against 0.999897
    stress x3     tailRMS 0.0

Every cell matches or beats the captured packs. The one the runner still calls a
failure is p0 n36, which leaves the frozen band *upward*: that is the cell the
stale reference emulator reads low on, and computed packs do not inherit the
problem. Its expected value wants re-freezing once the reference is current.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rd_parse  # noqa: E402

NOTES = range(21, 109)
VELOCITIES = (40, 80, 110, 127)

# Which sample set each patch plays. This is what the pack's "bank" byte means
# -- the emulator's patchToRomSet, not the parameter bank the patch table gives
# -- and getting the two confused sends a patch to entirely the wrong waveforms.
SAMPLE_SET = (0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2)

# When the firmware writes the release after the key comes up. Its own time
# base starts at note-off, so this is small and constant.
RELEASE_AT = 4

# What the firmware writes before it starts on a part's list, and when. A
# capture records it; it is the MCU walking its ten parts, so it is a per-part
# constant rather than anything in the ROM. Taken from the captured packs
# because there is nowhere else to take it from.
ONSET = (30, 32, 34, 36, 37, 39, 41, 42, 44, 21)
# What the firmware writes before it starts on a part's list: snap the envelope
# hard downward, then freeze it with a zero-speed segment. Identical on every
# patch measured, so it is the MCU rather than the ROM -- taken from the
# captured packs because there is nowhere else to take it from. Part 9 gets
# only the first, and is written first.
PREAMBLE = (((5, 31, 252), (7, 0, 0)),
            ((5, 31, 252), (9, 0, 0)),
            ((5, 31, 252), (10, 0, 0)),
            ((5, 31, 252), (12, 0, 0)),
            ((5, 31, 252), (13, 0, 0)),
            ((5, 31, 252), (15, 0, 0)),
            ((6, 31, 252), (16, 0, 0)),
            ((6, 31, 252), (18, 0, 0)),
            ((6, 31, 252), (19, 0, 0)),
            ((6, 31, 252),))
ENV_OFFSET = 0xFF


def build_part(rom, part, index):
    """One part's record and its segment list, as the pack format wants them."""
    segs = list(PREAMBLE[index])
    t = ONSET[index]
    for i, (dest, speed, dur) in enumerate(part["segments"]):
        segs.append((t, dest, speed))
        t += dur if dur is not None else 0
    return segs


def build(rom, patch, out):
    entries = []
    for note in NOTES:
        for vel in VELOCITIES:
            parsed = rd_parse.parse(rom, note, vel)
            parts = []
            for i, p in enumerate(parsed["parts"]):
                if not p["segments"]:
                    continue                  # a part the zone leaves unused
                segs = build_part(rom, p, i)
                # The release is one segment, and it is not the chain's own ramp
                # to nothing: the firmware writes destination zero at the speed
                # in the record's seventh byte, when the key comes up. Its
                # timestamps run from note-off, not from note-on.
                rel = [(RELEASE_AT, 0, p["release"])]
                parts.append((0 if i < 9 else 0xFF, p["pitch"],
                              p["wave"] >> 8, p["wave"] & 0xFF, segs, rel))
            entries.append((note, vel, parts))

    blob = bytearray(b"RDP2")
    blob += struct.pack("<I", 1)
    blob += struct.pack("<BBH", patch, SAMPLE_SET[patch], len(entries))
    for note, vel, parts in entries:
        blob += struct.pack("<BBB", note, vel, len(parts))
        for flags, pitch, wlo, whi, segs, rel in parts:
            blob += struct.pack("<BBHBBBB", flags, ENV_OFFSET, pitch,
                                wlo, whi, len(segs), len(rel))
            for t, dest, speed in segs + rel:
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
        n, size = build(rom, patch, path)
        print(f"rd_make_packs: patch {patch}: {n} entries, {size} bytes -> {path}")


if __name__ == "__main__":
    main()
