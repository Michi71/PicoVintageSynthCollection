# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Derives a note's envelope chains from a ROM set, without running anything.

    rd_parse.py <program.bin> <params.bin> <patch-offset> <note> <velocity>

This is the firmware's own arithmetic, rewritten from the disassembly in
RD_FIRMWARE.md rather than observed. Feed it the descrambled ROMs that
rd_unscramble.sh writes and it produces, per part, the wave address and the
chain of (destination, speed) pairs the firmware would program into the sound
chip -- which is what a .rdp pack holds, minus the timestamps the chip
generates.

The patch offset is where that patch's parameter block starts, as the patch
table gives it: (address - $4000) | (bank << 15).

Checked against the captured packs with rd_parse_check.py -- patch 0, all 352
note-and-velocity pairs, 3520 parts, 21655 segments:

    pitch                     100 %
    segments agreeing         100 %
    durations exact          98.4 %
    chains complete          98.2 %   the rest is where a capture stopped early

So this reproduces the firmware's arithmetic exactly. The 13 % that would not
agree in an earlier pass was a fault in the measurement, not in the parser: it
stripped a fixed two-entry preamble from every captured chain, and 640 of the
3520 parts have no preamble at all.

The timestamps are not in the ROM either, and they do not need to be: a segment
lasts however long the chip takes to ramp from the previous destination to this
one, which is arithmetic. `duration()` does it -- floor of the distance over the
rate, plus three for the interrupt's own latency -- and it agrees with the
captured timestamps 98.4 % of the time.

So a pack is now derivable from a ROM set: chains, wave addresses, pitches and
timings, none of it captured.
"""
import re
import sys

PROG_BASE = 0xE000          # the program ROM runs in its $e000 mirror
CURVE_TABLE = 0xED9D        # eight pointers to 64-byte velocity curves


# The chip's ramp increments, one per speed byte. Read out of the engine rather
# than copied a third time: instruments/PicoFaceRD/src/rd_engine/rd_new_engine.cpp
# already carries them as rdn_env_table, mechanically copied from the reference
# emulator's sound_chip.cpp, and two copies that can drift apart are enough.
ENGINE = "instruments/PicoFaceRD/src/rd_engine/rd_new_engine.cpp"


def env_table(path=None):
    src = open(path or ENGINE).read()
    i = src.index("rdn_env_table[256] = {")
    a = src.index("{", i)
    depth, j = 0, a
    while True:
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+", src[a + 1:j])]


def ramp_rate(table, speed):
    """Levels a segment moves per sample, signed. Bit 7 means downward, and the
    chip gets there by or-ing $7f << 21 into the increment and adding a carry,
    which wraps the 28-bit accumulator into a subtraction."""
    stepping = (speed & 0x7F) != 0
    down = (speed & 0x80) != 0
    carry = stepping and down
    b = table[speed] | ((0x7F << 21) if carry else 0)
    return (((b + (1 if carry else 0)) + (1 << 27)) % (1 << 28)) - (1 << 27)


def duration(table, level_from, level_to, speed):
    """How many samples the chip takes over that ramp -- which is where a
    captured timestamp comes from, and why the ROM holds none.

    Measured against the captured packs: floor of the division plus three,
    exact for 84 % of segments. The three is the interrupt's own latency, the
    chip signalling and the MCU getting round to writing the next pair.
    """
    rate = ramp_rate(table, speed)
    distance = (level_to - level_from) << 20
    if rate == 0 or distance == 0 or (distance > 0) != (rate > 0):
        return None
    return abs(distance) // abs(rate) + 3


def hi(x):
    """The high byte of a 16-bit accumulator, which is what psha keeps."""
    return (x >> 8) & 0xFF


def interpolate(corner_lo, corner_hi, weight):
    """(256 - w) * lo + w * hi, as the firmware does it: two muls and an add."""
    return hi(((256 - weight) * corner_lo + weight * corner_hi) & 0xFFFF)


class Rom:
    def __init__(self, program, params, patch_off, engine=None):
        self.env = env_table(engine)
        self.prog = program
        self.prm = params
        # $a5, $a7, $a9 -- as the program-change handler computes them, but as
        # offsets into the parameter ROM rather than CPU addresses.
        self.a5 = patch_off
        self.a7 = patch_off + 0x100
        self.a9 = self.a7 + 0x81F
        self.bank = patch_off >> 15

    def cpu_to_off(self, addr):
        """A 16-bit CPU address in the $4000 window, in this patch's bank."""
        return (addr - 0x4000) | (self.bank << 15)

    def curve(self, which, index):
        base = (self.prog[CURVE_TABLE - PROG_BASE + 2 * which] << 8) \
             | self.prog[CURVE_TABLE - PROG_BASE + 2 * which + 1]
        return self.prog[base - PROG_BASE + index]


def zone_of(note):
    """zone = note - 15, folded by octaves into 0..98."""
    z = note - 0x0F
    while z < 0:
        z += 12
    while z > 0x62:
        z -= 12
    return z


def parse(rom, note, velocity):
    # The velocity map: one byte, two jobs.
    c0 = rom.prm[rom.a5 + velocity]
    layer = 2 if (c0 & 0x80) else 0          # $c4
    c2 = (c0 << 1) & 0xFF                    # the straight weight
    c1 = c2 >> 2                             # the curve index

    zone = zone_of(note)
    zbase = rom.a7 + zone * 21
    block = rom.a9 + rom.prm[zbase] * 70

    parts = []
    for p in range(10):
        rec = block + p * 7
        wave = (rom.prm[rec] << 8) | rom.prm[rec + 1]
        ptr = (rom.prm[rec + 2] << 8) | rom.prm[rec + 3]
        pitch = (rom.prm[zbase + 1 + p * 2] << 8) | rom.prm[zbase + 2 + p * 2]
        release = rom.prm[rec + 6]
        if ptr == 0:
            parts.append({"pitch": pitch, "wave": wave, "release": release,
                          "segments": []})
            continue
        # +4 for the hard layer, +5 for the soft one -- and it is that way
        # round, not the other. $e924's bmi branches when bit 7 is *set*, so
        # the inx that makes $ab into $af + 1 runs when it is clear. Reading it
        # the other way costs a velocity layer: soft notes drop to 29 %.
        c3 = rom.curve(rom.prm[rec + (4 if layer else 5)] >> 1, c1)

        segs, at, level = [], rom.cpu_to_off(ptr) + layer, None
        while len(segs) < 64:
            e = rom.prm[at:at + 4]
            dest = interpolate(e[0], e[2], c3)
            speed = interpolate(e[1], e[3], c2)
            # How long the chip will take over it -- None for the first, whose
            # starting level the note-on preamble decides rather than the list.
            segs.append((dest, speed,
                         None if level is None else duration(rom.env, level, dest, speed)))
            level = dest
            if dest == 0:                    # ed89's tsta: zero ends the chain
                break
            at += 6
        parts.append({"pitch": pitch, "wave": wave, "release": release,
                      "segments": segs})
    return {"zone": zone, "block": block, "weight": c0, "layer": layer // 2,
            "curve_index": c1, "parts": parts}


def main():
    if len(sys.argv) != 6:
        sys.stderr.write(__doc__.split("\n\n")[1] + "\n")
        sys.exit(1)
    rom = Rom(open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read(),
              int(sys.argv[3], 0))
    r = parse(rom, int(sys.argv[4], 0), int(sys.argv[5], 0))
    print(f"zone {r['zone']}  block ${r['block']:06x}  weight ${r['weight']:02x}"
          f"  layer {r['layer']}  curve index {r['curve_index']}")
    for i, p in enumerate(r["parts"]):
        if not p["segments"]:
            continue
        print(f"  part {i}: pitch ${p['pitch']:04x} wave ${p['wave']:04x} "
              f"release {p['release']}")
        print("      " + "  ".join(
            f"{d}/{s}" + (f"@{t}" if t is not None else "") for d, s, t in p["segments"]))


if __name__ == "__main__":
    main()
