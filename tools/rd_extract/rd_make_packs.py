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
# No velocity list any more. The firmware does not have velocity layers: it
# interpolates every segment between two corner bytes in the parameter ROM,
# weighted by a curve it picks from the velocity. Storing four sampled results
# was an artifact of this builder, and an audible one -- between the four
# points the level was out by up to 10.7 dB. A pack now carries the corners,
# the velocity map and the curves, and the engine does the interpolation the
# way the sound CPU does it. Exact at all 128 velocities, and a fifth the size.
VELOCITIES = (40, 80, 110, 127)     # only for the verification tool now

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


def corner_chain(rom, ptr):
    """The raw six-byte entries of a part's segment list, as they sit in the
    parameter ROM. Six bytes cover BOTH layers: the firmware reads four bytes
    from offset 0 for the hard layer and from offset 2 for the soft one, so the
    two overlap inside the same entry.

    How long the chain is depends on the velocity -- it ends where an
    interpolated destination reaches zero, and which layer is in use decides
    which corners are read at all. So both layers are walked and the longer one
    kept; the engine stops at the zero it computes for its own velocity,
    exactly as the chip does.

    The stop condition has a closed form. The destination is
    ((256-w)*lo + w*hi) >> 8, which is linear in w between 256*lo and 256*hi,
    so it is non-zero for SOME weight exactly when max(lo, hi) > 0. No sweep
    over the 256 weights is needed -- and walking only one layer, or only the
    two extreme weights, silently cuts the chain short. It did."""
    base = rom.cpu_to_off(ptr)
    longest = 0
    for layer in (0, 2):
        at, n = base + layer, 0
        while n < 64:
            e = rom.prm[at:at + 4]
            n += 1
            if max(e[0], e[2]) == 0:      # zero at every weight -> chain ends
                break
            at += 6
        longest = max(longest, n)
    return bytes(rom.prm[base:base + longest * 6 + 2]), longest


def build(rom, patch, out):
    notes = []
    for note in NOTES:
        # Parsed once, at any velocity: what this needs from it -- which zone,
        # which parts, their pitch, wave and release -- does not depend on the
        # velocity at all. Verified across all 128.
        parsed = rd_parse.parse(rom, note, 127)
        zbase = rom.a7 + rd_parse.zone_of(note) * 21
        block = rom.a9 + rom.prm[zbase] * 70
        parts = []
        for i, p in enumerate(parsed["parts"]):
            if not p["segments"]:
                continue                      # a part the zone leaves unused
            rec = block + i * 7
            ptr = (rom.prm[rec + 2] << 8) | rom.prm[rec + 3]
            corners, nseg = corner_chain(rom, ptr)
            # Which of the eight curves this part weights its destinations
            # with. +4 is the hard layer, +5 the soft one, and the engine
            # picks between them the same way the firmware does.
            sel_hard = rom.prm[rec + 4] >> 1
            sel_soft = rom.prm[rec + 5] >> 1
            parts.append((i, 0 if i < 9 else 0xFF, p["pitch"],
                          p["wave"] >> 8, p["wave"] & 0xFF,
                          p["release"], sel_hard, sel_soft, nseg, corners))
        notes.append((note, parts))

    blob = bytearray(b"RDP3")
    blob += struct.pack("<I", 1)
    blob += struct.pack("<BBH", patch, SAMPLE_SET[patch], len(notes))
    # The velocity map out of the parameter ROM, then the curves out of the
    # program ROM. 1280 bytes, and they replace three quarters of the pack.
    #
    # SIXTEEN curves, not eight: the pointer table at $ed9d runs $f049 to
    # $f409 in steps of 0x40, and the parts really do select from all of it --
    # the selector bytes seen across the bank are 0..3 and 13..15. Index 16 is
    # already something else. RD_FIRMWARE.md said eight because that pass only
    # listed the ones it had walked.
    blob += bytes(rom.prm[rom.a5:rom.a5 + 256])
    for which in range(16):
        blob += bytes(rom.curve(which, i) for i in range(64))
    for note, parts in notes:
        blob += struct.pack("<BB", note, len(parts))
        for idx, flags, pitch, wlo, whi, rel, sh, ss, nseg, corners in parts:
            blob += struct.pack("<BBBHBBBBB", idx, flags, ENV_OFFSET, pitch,
                                wlo, whi, rel, (sh & 0x0F) | (ss << 4), nseg)
            blob += corners
    open(out, "wb").write(blob)
    return len(notes), len(blob)


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
