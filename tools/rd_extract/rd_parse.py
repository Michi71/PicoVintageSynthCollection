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

STATE: not finished, but further than it was. Measured against the sixteen
captured packs, patch 0, all 352 (note, velocity) pairs and all 3520 parts:

    pitch                     100 %      3520 of 3520, exact
    attack chains complete   80.0 %
    individual segments      87.0 %      on the stretch both cover

The pitch being exact everywhere settles the zone path: note to zone, zone entry
to ten 16-bit pitches, all of it.

The chains are shorter in the packs than here, and that is not an error on
either side. A capture stops when the key comes up, so a pack holds however much
of the chain fitted in the window plus the release segment written at that
moment; this walks the list to its own end. On patch 0, note 60, velocity 110,
part 0 the two agree for seven segments and then the pack stops.

The velocity index is the raw MIDI velocity after all. An earlier note here
said it could not be, on the strength of solving one part of one note for the
weight that would fit; sweeping all 256 indices against every note instead puts
the raw index at the top for three of the four captured layers, and the fourth
was a different fault -- see the curve selector above.

What is still wrong sits in the remaining 13 %, and it is not the velocity
path: whatever it is, it leaves four velocities agreeing to within a tenth of a
percent of each other.
"""
import sys

PROG_BASE = 0xE000          # the program ROM runs in its $e000 mirror
CURVE_TABLE = 0xED9D        # eight pointers to 64-byte velocity curves


def hi(x):
    """The high byte of a 16-bit accumulator, which is what psha keeps."""
    return (x >> 8) & 0xFF


def interpolate(corner_lo, corner_hi, weight):
    """(256 - w) * lo + w * hi, as the firmware does it: two muls and an add."""
    return hi(((256 - weight) * corner_lo + weight * corner_hi) & 0xFFFF)


class Rom:
    def __init__(self, program, params, patch_off):
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

        segs, at = [], rom.cpu_to_off(ptr) + layer
        while len(segs) < 64:
            e = rom.prm[at:at + 4]
            dest = interpolate(e[0], e[2], c3)
            speed = interpolate(e[1], e[3], c2)
            segs.append((dest, speed))
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
        print("      " + "  ".join(f"{d}/{s}" for d, s in p["segments"]))


if __name__ == "__main__":
    main()
