#!/usr/bin/env python3
"""D-50 ROM reader: identification, PCM decoding and PROM table parsing.

Imported by the other tools in this directory; also runnable directly to print
a summary of a ROM set:

    python3 tools/d5_extract/d5_rom.py <romdir>

Files are identified by content (CRC32), not by name, and doubled dumps
(a 256 KB mask ROM read out as 512 KB) are folded automatically. No ROM images
are part of this repository -- see README.md.
"""
import math
import os
import struct
import sys
import zlib

# ------------------------------------------------------------- reference CRCs
#
# CRC32 values of the known-good images (matching the preserved reference set),
# after de-doubling. The PROM list is not exhaustive; any 64 KB image that
# carries the PCM name table is accepted, unknown versions with a warning.

PCM_A_CRC = 0x1461C0FB          # TC532000P-7469, IC30, lower 128K words
PCM_B_CRC = 0xE50599BF          # TC532000P-7470, IC29, upper 128K words
PCM_COMBINED_CRC = 0xE2AED2D9   # TC534000P-7477, IC30 on late boards, A+B in one
INTERNAL_CRC = 0x9564903F       # uPD78312G-022 internal ROM, 8 KB, "ic25"
PROM_CRCS = {
    0xE92C69F9: "v2.22",
    0xCCBA4E46: "v1.06",
    0x3E72BDF0: "v2.21",
    0x5DB7C340: "v2.20",
    0xD1387C54: "v2.10",
    0x7FC199C5: "v1.10",
    0xD871451E: "v1.04",
}

PAGE = 2048                     # words; attack samples start on page boundaries
SAMPLE_RATE = 32000

# --------------------------------------------------------------- PCM decoding
#
# The PCM ROMs hold 16-bit big-endian words whose bits are permuted by the
# board routing, and whose value is a sign bit plus a 15-bit log2 magnitude.
# Format documented in the VOGONS thread on LA-synth sample ROMs (t=77094);
# this is an independent implementation of that description.


def _reorder_bits(raw):
    o = raw & 0x8000
    o |= (raw << 8) & 0x4000
    o |= (raw >> 1) & 0x3F80
    o |= (raw << 1) & 0x007E
    o |= (raw >> 7) & 0x0001
    return o


def _build_lut():
    lut = [0.0] * 65536
    for raw in range(65536):
        o = _reorder_bits(raw)
        mag = o & 0x7FFF
        amp = math.pow(2.0, (mag - 32767.0) / 2048.0)
        lut[raw] = -amp if o & 0x8000 else amp
    return lut


_LUT = None


def decode_pcm(data):
    """Decode raw PCM ROM bytes (16-bit BE words) to a list of floats."""
    global _LUT
    if _LUT is None:
        _LUT = _build_lut()
    lut = _LUT
    return [lut[data[i] << 8 | data[i + 1]] for i in range(0, len(data) - 1, 2)]


# -------------------------------------------------------------------- ROM set


def _fold(data):
    """Fold a doubled dump (identical halves) down to one copy."""
    while len(data) >= 2 and data[: len(data) // 2] == data[len(data) // 2:]:
        data = data[: len(data) // 2]
    return data


class D5RomSet:
    """Identifies the images in a directory and decodes the PCM space.

    Attributes:
        prom            64 KB program EPROM (bytes)
        prom_version    "v2.22" etc., or "unknown"
        internal        8 KB uPD78312 internal ROM (bytes), or None
        audio           decoded PCM space, 262144 floats (chip A then chip B)
        names           the 100 PCM sample names from the PROM
    """

    def __init__(self, romdir):
        self.prom = None
        self.prom_version = None
        self.internal = None
        self._pcm_a = None
        self._pcm_b = None
        pcm_combined = None
        proms = {}

        candidates = []
        for root, _dirs, files in os.walk(romdir):
            candidates += [os.path.join(root, fn) for fn in files]
        for path in sorted(candidates):
            if os.path.getsize(path) > 2 << 20:
                continue
            data = _fold(open(path, "rb").read())
            crc = zlib.crc32(data)
            if crc == PCM_A_CRC:
                self._pcm_a = data
            elif crc == PCM_B_CRC:
                self._pcm_b = data
            elif crc == PCM_COMBINED_CRC:
                pcm_combined = data
            elif crc == INTERNAL_CRC:
                self.internal = data
            elif len(data) == 65536 and b"Marmba" in data:
                proms[PROM_CRCS.get(crc, "unknown")] = data

        if self._pcm_a is None and pcm_combined is not None:
            self._pcm_a = pcm_combined[: len(pcm_combined) // 2]
            self._pcm_b = pcm_combined[len(pcm_combined) // 2:]
        if self._pcm_a is None or self._pcm_b is None:
            raise FileNotFoundError(f"no PCM ROM pair identified in {romdir}")
        if not proms:
            raise FileNotFoundError(f"no program EPROM identified in {romdir}")
        # prefer the newest known version
        self.prom_version = sorted(proms, reverse=True)[0]
        self.prom = proms[self.prom_version]

        self.audio = decode_pcm(self._pcm_a) + decode_pcm(self._pcm_b)
        noff = self.prom.find(b"Marmba")
        self.names = [
            self.prom[noff + 6 * i: noff + 6 * i + 6].decode("ascii").strip()
            for i in range(100)
        ]
        self.name_table_offset = noff

    # -------------------------------------------------- acoustic classification
    #
    # These are the anchors used to validate any sample-table hypothesis:
    # attacks (PCM 1..47) start on page boundaries after near-silence or with a
    # sharp level step; static loops (48..76) are stationary; PCM 76 "Noise" is
    # spectrally flat.

    def _rms(self, s, e):
        s, e = max(0, s), min(len(self.audio), e)
        if e <= s:
            return 0.0
        seg = self.audio[s:e]
        return math.sqrt(sum(v * v for v in seg) / len(seg))

    def attack_like(self, w):
        if not 0 <= w < len(self.audio):
            return False
        pre = self._rms(w - 256, w)
        post = self._rms(w, w + 512)
        return post > 0.008 and (pre < 0.004 or post > 3 * pre)

    def steady_at(self, w, span=PAGE):
        if not 0 <= w < len(self.audio) - span:
            return False
        subs = [self._rms(w + k, w + k + 256) for k in range(0, span, 256)]
        m = sum(subs) / len(subs)
        return m > 0.005 and (max(subs) - min(subs)) / m < 0.6

    def noisy_at(self, w, span=PAGE):
        if not 0 <= w < len(self.audio) - span:
            return False
        x = self.audio[w: w + span]
        r = math.sqrt(sum(v * v for v in x) / len(x))
        if r < 0.02:
            return False
        d = sum(abs(x[i] - x[i - 1]) for i in range(1, len(x))) / (len(x) - 1)
        return d / r > 1.3

    def page_map(self):
        """One classification char per 2048-word page: A attack, N noise,
        S steady, . other signal, q quiet."""
        out = []
        for p in range(len(self.audio) // PAGE):
            w = p * PAGE
            if self.noisy_at(w):
                c = "N"
            elif self.attack_like(w):
                c = "A"
            elif self.steady_at(w):
                c = "S"
            elif self._rms(w, w + PAGE) > 0.003:
                c = "."
            else:
                c = "q"
            out.append(c)
        return "".join(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rs = D5RomSet(sys.argv[1])
    print(f"PROM        : {rs.prom_version}, name table at 0x{rs.name_table_offset:04X}")
    print(f"internal ROM: {'present' if rs.internal else 'MISSING (see README)'}")
    print(f"PCM space   : {len(rs.audio)} words = {len(rs.audio)/SAMPLE_RATE:.1f} s")
    print(f"names       : {rs.names[0]} .. {rs.names[46]} | {rs.names[47]} .. "
          f"{rs.names[75]} | {rs.names[76]} .. {rs.names[99]}")
    pm = rs.page_map()
    for i in range(0, len(pm), 64):
        print(f"pages {i:3}..{i+63:3}: {pm[i:i+64]}")


if __name__ == "__main__":
    main()
