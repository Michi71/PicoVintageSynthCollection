#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Michi71
"""Read a local Korg M1 ROM set: PCM, coefficient ROM, OS and factory preload.

Nothing here contains ROM data; every table is read from the user's own files.
The derivations behind the offsets and formats are summarised in
instruments/PicoFaceM1/doc/ENGINE.md.

Files expected in the ROM directory (identified by content where the name
cannot be relied on):

    IC36.bin .. IC39.bin   PCM, low bytes  (4 x 512 KB)
    IC40.bin .. IC43.bin   PCM, high bytes (4 x 512 KB)
    IC30.bin               coefficient ROM (64 KB; IC20 and IC26 are the same
                           chip contents and are accepted in its place)
    two 64 KB OS halves    the ORIGINAL M1 OS (tested: Rev 19, IC32 + IC23);
                           which is the even half is found by content
    *.syx                  an M1 ALL DATA dump (the factory preload)
"""
import glob
import os
import struct

OS_SIZE = 0x20000          # the OS occupies E0000-FFFFF
OS_BASE = 0xE0000
REV19_DS = 0xF75C0         # DS = F75C in the original OS (Plus+1 OS: F715)
MS_TABLE = 0x6978          # DS offset of the multisound table (Rev 19)
MS_COUNT = 100             # the M1 itself; the Plus+1 entries do not exist here
MS_REC = 14                # u16 split list, u8 kind (2 = DWGS), u8 splits, name[10]
SPLIT_REC = 10             # u16 sample record, top, pan, tune, root, level, cutoff, 2 spare
SAMPLE_REC = 14            # register image: start, loop start, flags, end (absolute)
DWGS_FIRST = 77            # multisounds 77-99 are DWGS waves sharing one split list
DWGS_STRIDE = 0x3FC        # words per wave: mip chain 512+256+...+4
PCM_WORDS = 0x200000       # 2 M words of internal PCM


class RomError(Exception):
    pass


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _find(romdir, names):
    for n in names:
        for cand in (n, n.lower(), n.upper()):
            p = os.path.join(romdir, cand)
            if os.path.exists(p):
                return p
    return None


class M1RomSet:
    def __init__(self, romdir):
        self.romdir = romdir
        self.pcm = self._load_pcm()
        self.coef = self._load_coef()
        self.os = self._load_os()
        self.preload_path = self._find_preload()

    # ------------------------------------------------------------------ PCM
    def _load_pcm(self):
        out = bytearray(PCM_WORDS * 2)
        for k, (lo_n, hi_n) in enumerate(((36, 40), (37, 41), (38, 42), (39, 43))):
            lo_p = _find(self.romdir, [f"IC{lo_n}.bin"])
            hi_p = _find(self.romdir, [f"IC{hi_n}.bin"])
            if not lo_p or not hi_p:
                raise RomError(f"missing IC{lo_n}.bin / IC{hi_n}.bin (PCM)")
            lo, hi = _read(lo_p), _read(hi_p)
            if len(lo) != 0x80000 or len(hi) != 0x80000:
                raise RomError(f"IC{lo_n}/IC{hi_n}: expected 512 KB each")
            base = k * 0x100000
            out[base:base + 0x100000:2] = lo
            out[base + 1:base + 0x100000:2] = hi
        return bytes(out)

    # ------------------------------------------------------- coefficient ROM
    def _load_coef(self):
        p = _find(self.romdir, ["IC30.bin", "IC26.bin", "IC20.bin"])
        if not p:
            raise RomError("missing IC30.bin (coefficient ROM; IC20/IC26 also do)")
        d = _read(p)
        if len(d) != 0x10000:
            raise RomError(f"{os.path.basename(p)}: expected 64 KB")
        # The interpolation kernel's tap sum is constant over all 4096 phases
        # (analysis section 5.2). That is the cheapest check that this is the
        # right chip and not, say, an OS half.
        sums = set()
        for ph in range(0, 4096, 97):
            r = d[ph * 8:ph * 8 + 8]
            s = (_s8(r[0]) + _s8(r[1])) * 1 + _s8(r[2]) * 2 + (r[3] + r[4]) * 8 \
                + _s8(r[5]) * 2 + (_s8(r[6]) + _s8(r[7]))
            sums.add(s)
        if sums != {1226}:
            raise RomError(f"{os.path.basename(p)} is not the M1 coefficient ROM")
        return d

    # -------------------------------------------------------------------- OS
    def _load_os(self):
        skip = {"ic20.bin", "ic26.bin", "ic30.bin"}
        cands = [p for p in sorted(glob.glob(os.path.join(self.romdir, "*.bin"))
                                   + glob.glob(os.path.join(self.romdir, "*.BIN")))
                 if os.path.getsize(p) == 0x10000 and os.path.basename(p).lower() not in skip]
        for a in cands:
            for b in cands:
                if a == b:
                    continue
                img = bytearray(OS_SIZE)
                img[0::2], img[1::2] = _read(a), _read(b)
                img = bytes(img)
                o = REV19_DS - OS_BASE + MS_TABLE
                if img[o + 4:o + 14] == b"Piano     " and img[o + 14 + 4:o + 28] == b"E.Piano 1 ":
                    self.os_files = (os.path.basename(a), os.path.basename(b))
                    return img
        raise RomError("no original M1 OS found: need the two 64 KB halves of the "
                       "Rev 19 OS (e.g. rev19-IC32-880319.bin + rev19-IC23-880219.bin). "
                       "The Plus+1 OS is laid out differently and is not supported.")

    def ds(self, off, n):
        a = REV19_DS - OS_BASE + off
        return self.os[a:a + n]

    def lin(self, off, n):
        return self.os[off:off + n]

    def _find_preload(self):
        for p in sorted(glob.glob(os.path.join(self.romdir, "*.syx"))
                        + glob.glob(os.path.join(self.romdir, "*.SYX"))):
            d = _read(p)
            if len(d) > 8 and d[0] == 0xF0 and d[1] == 0x42 and (d[2] & 0xF0) == 0x30 \
                    and d[3] == 0x19 and d[4] == 0x50:
                return p
        return None

    # ----------------------------------------------------------- multisounds
    def multisounds(self):
        """100 multisounds -> list of dict(name, dwgs, splits=[...]).
        Split addresses are absolute word addresses into the 2 M-word PCM, the
        DWGS wave offset already applied."""
        out = []
        for i in range(MS_COUNT):
            ptr, kind, nsplit = struct.unpack("<HBB", self.ds(MS_TABLE + MS_REC * i, 4))
            name = self.ds(MS_TABLE + MS_REC * i + 4, 10).decode("latin1").rstrip()
            dwgs = kind == 2
            off = (i - DWGS_FIRST) * DWGS_STRIDE if dwgs else 0
            splits = []
            for k in range(nsplit):
                rec, top, pan, tune, root, level, cutoff, decay = struct.unpack(
                    "<HBBbBbbb", self.ds(ptr + SPLIT_REC * k, 9))
                r = self.ds(rec, SAMPLE_REC)
                tune128 = struct.unpack_from("<h", r, 0)[0]
                start = r[2] | r[3] << 8 | r[4] << 16
                loop = r[6] | r[7] << 8 | r[8] << 16
                flags = r[9]
                end = r[10] | r[11] << 8 | r[12] << 16
                bank = (end >> 20) & 0xF
                if bank > 1:
                    raise RomError(f"multisound {i} split {k}: bank {bank} is not internal PCM")
                a = lambda x: ((bank << 20) | (x & 0xFFFFF)) + off
                s = dict(top=top, pan=pan, tune=tune, root=root & 0x7F, nt=bool(root & 0x80),
                         level=level, cutoff=cutoff, decay=decay, tune128=tune128, start=a(start), loop=a(loop), end=a(end),
                         loop_on=bool(flags & 4))
                if not (s["start"] <= s["end"] < PCM_WORDS and s["loop"] <= s["end"]):
                    raise RomError(f"multisound {i} split {k}: addresses out of range")
                splits.append(s)
            out.append(dict(index=i, name=name, dwgs=dwgs, splits=splits))
        return out

    def drum_sounds(self):
        """The 44 internal drum sounds: sample records at DS:5B9E (14 bytes each,
        same register-image format as the multisound samples)."""
        out = []
        for i in range(44):
            r = self.ds(0x5B9E + SAMPLE_REC * i, SAMPLE_REC)
            start = r[2] | r[3] << 8 | r[4] << 16
            loop = r[6] | r[7] << 8 | r[8] << 16
            end = r[10] | r[11] << 8 | r[12] << 16
            bank = (end >> 20) & 0xF
            a = lambda x: (bank << 20) | (x & 0xFFFFF)
            d = dict(start=a(start), loop=a(loop), end=a(end), loop_on=bool(r[9] & 4),
                     tune128=struct.unpack_from("<h", r, 0)[0])
            if bank > 1 or not d["start"] <= d["end"] < PCM_WORDS:
                raise RomError(f"drum sound {i + 1}: addresses out of range")
            if d["loop_on"] and not d["loop"] <= d["end"]:
                raise RomError(f"drum sound {i + 1}: loop past the end")
            out.append(d)
        name = self.ds(0x6F48, 10)
        if name != b"Kick 1    ":
            raise RomError("drum-sound names not where Rev 19 has them")
        return out

    # --------------------------------------------------------- coefficient ROM
    def kernel_bytes(self):
        """IC30 TG-II interpolation kernel: 4096 phases x 8 raw bytes (lanes 3/4
        unsigned, the rest signed; see the engine for the lane weights)."""
        return self.coef[:0x8000]

    def vdf_k(self):
        """(k1, k2): the 4096-entry Q13 one-pole coefficients of VDF 1 (IC26
        half) and VDF 2 (IC20 half). Table index = cutoff register >> 1."""
        d = self.coef

        def tab(base):
            o = base + 0x2000          # second table (T1) of the 16 KB area
            return [d[o + i] | d[o + 0x1000 + i] << 8 for i in range(4096)]
        return tab(0x8000), tab(0xC000)


def _s8(v):
    return v - 256 if v >= 128 else v


def unpack7(b):
    """Korg 7-bit packing: groups of 8, first byte holds bit 7 of the next 7."""
    out = bytearray()
    for i in range(0, len(b) // 8 * 8, 8):
        m = b[i]
        out += bytes(x | (((m >> j) & 1) << 7) for j, x in enumerate(b[i + 1:i + 8]))
    return bytes(out)


def load_preload(path):
    """M1 ALL DATA dump -> dict(global_raw, kits[4] (210 B), combis[100] (124 B),
    programs[100] (143 B)). Layout from the owner's manual, MIDI note 12."""
    d = _read(path)
    end = d.index(0xF7, 8)
    u = unpack7(d[8:end])
    if len(u) < 13261 + 143 * 100:
        raise RomError(f"{os.path.basename(path)}: ALL DATA dump too short")
    return dict(global_raw=u[:21],
                kits=[u[21 + 210 * k:21 + 210 * (k + 1)] for k in range(4)],
                combis=[u[861 + 124 * k:861 + 124 * (k + 1)] for k in range(100)],
                programs=[u[13261 + 143 * k:13261 + 143 * (k + 1)] for k in range(100)])
