#!/usr/bin/env python3
"""JV-880 ROM reader: descrambling, table parsing and DPCM decoding.

Imported by the other tools in this directory; also runnable directly to print
a summary of a ROM set:

    python3 tools/jv_extract/jv_rom.py <romdir>

No ROM images are part of this repository -- see README.md.
"""
import os
import struct
import sys

# ---------------------------------------------------------------- descrambling

_ADDR_PERM = [2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19]
_DATA_PERM = [2, 0, 4, 5, 7, 6, 3, 1]


def descramble(src: bytes) -> bytes:
    """Undo the wave-ROM address/data bit permutation.

    Only the low 20 address bits are permuted, so the transform is per 1 MB
    page -- which is also why each page carries its own exponent-nibble area.
    """
    # Address permutation is a fixed 20-bit shuffle: build it once, then reuse
    # it for every page instead of redoing the bit loop per byte.
    addr = [0] * (1 << 20)
    for i in range(1 << 20):
        a = 0
        for j in range(20):
            if i & (1 << j):
                a |= 1 << _ADDR_PERM[j]
        addr[i] = a
    dmap = bytes(
        sum(1 << j for j in range(8) if v & (1 << _DATA_PERM[j])) for v in range(256)
    )
    out = bytearray(len(src))
    for page in range(0, len(src), 1 << 20):
        end = min(page + (1 << 20), len(src))
        for i in range(end - page):
            out[page + i] = dmap[src[page + addr[i]]]
    return bytes(out)


# -------------------------------------------------------------------- ROM set

MULTI_STRIDE = 60
SAMPLE_STRIDE = 18
PATCH_SIZE = 0x16A
TONE_OFFSET = 26
TONE_SIZE = 84
PATCH_BANKS = {"User": 0x008CE0, "A": 0x010CE0, "B": 0x018CE0}


class JvRom:
    def __init__(self, romdir, cache=True):
        self.rom2 = open(os.path.join(romdir, "jv880_rom2.bin"), "rb").read()
        self.wave = b"".join(
            self._wave(romdir, n, cache) for n in ("jv880_waverom1.bin", "jv880_waverom2.bin")
        )
        self._parse()

    @staticmethod
    def _wave(romdir, name, cache):
        cached = os.path.join(romdir, "Cache", name)
        if cache and os.path.exists(cached):
            return open(cached, "rb").read()
        raw = open(os.path.join(romdir, name), "rb").read()
        out = descramble(raw)
        if cache:
            os.makedirs(os.path.join(romdir, "Cache"), exist_ok=True)
            open(cached, "wb").write(out)
        return out

    def _parse(self):
        r = self.rom2
        n_multi = r[0]
        multi_end = int.from_bytes(r[1:4], "big")
        self.multis = []
        for i in range(n_multi):
            o = 4 + i * MULTI_STRIDE
            splits = list(r[o + 12 : o + 27])
            idx = [int.from_bytes(r[o + 28 + 2 * k : o + 30 + 2 * k], "big") for k in range(16)]
            self.multis.append(
                dict(
                    name=r[o : o + 12].decode("ascii", "replace").strip(),
                    splits=[s for s in splits if s != 0x7F] + [127],
                    samples=[x for x in idx if x != 0xFFFF],
                )
            )
        # sample table follows the multisample table, one byte later
        sb = multi_end + 1
        self.samples = []
        i = 0
        while True:
            o = sb + i * SAMPLE_STRIDE
            if o + SAMPLE_STRIDE > len(r):
                break
            s = dict(
                start=int.from_bytes(r[o : o + 3], "big"),
                loop=int.from_bytes(r[o + 3 : o + 6], "big"),
                end=int.from_bytes(r[o + 6 : o + 9], "big"),
                flag=r[o + 11],
                root=r[o + 12],
                tune=int.from_bytes(r[o + 13 : o + 15], "big"),
                level=r[o + 17],
            )
            if not (s["start"] < s["loop"] <= s["end"] <= 0x400000):
                break
            self.samples.append(s)
            i += 1
        self.sample_table = (sb, sb + len(self.samples) * SAMPLE_STRIDE)

    # ------------------------------------------------------------- patches
    def patch(self, bank, index):
        o = PATCH_BANKS[bank] + index * PATCH_SIZE
        return self.rom2[o : o + PATCH_SIZE]

    def patch_name(self, bank, index):
        return self.patch(bank, index)[:12].decode("ascii", "replace").strip()

    def tone(self, bank, index, t):
        p = self.patch(bank, index)
        return p[TONE_OFFSET + t * TONE_SIZE : TONE_OFFSET + (t + 1) * TONE_SIZE]

    # -------------------------------------------------------------- decoding
    def decode(self, sample_index):
        """DPCM -> 20-bit signed PCM.

        8-bit signed deltas, scaled by a 4-bit exponent shared by 16 samples.
        The exponent nibbles live in the first 32 KB of the same 1 MB page as
        the sample data, addressed at (offset >> 5) with bit 4 picking the
        nibble -- which is why a page holds 32 KB of nibbles + 992 KB of data.
        """
        s = self.samples[sample_index]
        w = self.wave
        page = s["start"] & 0xF00000
        ref = 0
        out = []
        for a in range(s["start"], s["end"]):
            d = w[a]
            if d > 127:
                d -= 256
            nb = w[page | ((a & 0xFFFFF) >> 5)]
            nib = (nb >> 4) & 15 if (a & 0x10) else nb & 15
            shifted = (d << 11) >> ((10 - nib) & 15)
            ref = max(-0x80000, min(0x7FFFF, ref + (shifted >> 1)))
            out.append(ref)
        return out

    def write_wav(self, sample_index, path, rate=32000):
        import wave

        pcm = self.decode(sample_index)
        with wave.open(path, "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(rate)
            f.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, v >> 4))) for v in pcm))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rom = JvRom(sys.argv[1])
    print(f"multisamples : {len(rom.multis)}")
    print(f"samples      : {len(rom.samples)}  table 0x{rom.sample_table[0]:x}..0x{rom.sample_table[1]:x}")
    total = sum(s["end"] - s["start"] for s in rom.samples)
    print(f"sample bytes : {total:,} ({total / 1024 / 1024:.2f} MB)")
    print(f"roots        : {min(s['root'] for s in rom.samples)}..{max(s['root'] for s in rom.samples)}")
    for bank in PATCH_BANKS:
        names = [rom.patch_name(bank, j) for j in range(64)]
        print(f"bank {bank:<5}: {names[0]!r} .. {names[-1]!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
