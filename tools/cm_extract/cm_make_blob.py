#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michi71
"""Turns a CM-32L ROM set into what PicoFaceCM links.

    cm_make_blob.py <rom dir> <out dir>

Writes into <out dir>:
    cm_control.bin  the control ROM, unchanged (64 KiB)
    cm_pcm.bin      the PCM ROM with its bit scrambling undone: 524288 words,
                    16 bit little endian, exactly what the engine otherwise
                    builds in RAM when it starts (1 MiB)
    cm_blob.S       assembly that pulls both in with .incbin

The two images are found by content, not by name: control ROM v1.02 of the
CM-32L / LAPC-I and the 1 MiB PCM ROM of the CM-32L / CM-64 / LAPC-I. Nothing
from them is written anywhere but <out dir>, which is the build directory.
"""
import hashlib
import pathlib
import sys

CONTROL_SHA1 = "a439fbb390da38cada95a7cbb1d6ca199cd66ef8"   # CM-32L / LAPC-I control v1.02
PCM_SHA1 = "289cc298ad532b702461bfc738009d9ebe8025ea"       # CM-32L / CM-64 / LAPC-I PCM

# Bit u (15 = top) of an unscrambled word is bit ORDER[15 - u] of the ROM
# word, counting the first byte's bits 0..7 from its top and the second
# byte's 8..15 likewise. Same table as mt32emu's Synth::loadPCMROM.
ORDER = (0, 9, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 8)


def unscramble(rom: bytes) -> bytes:
    lut_s = []
    lut_c = []
    for value in range(256):
        s = c = 0
        for u, src in enumerate(ORDER):
            if src < 8:
                s |= ((value >> (7 - src)) & 1) << (15 - u)
            else:
                c |= ((value >> (7 - (src - 8))) & 1) << (15 - u)
        lut_s.append(s)
        lut_c.append(c)
    out = bytearray(len(rom))
    for i in range(0, len(rom), 2):
        word = lut_s[rom[i]] | lut_c[rom[i + 1]]
        out[i] = word & 0xFF
        out[i + 1] = word >> 8
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    rom_dir = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])

    control = pcm = None
    for path in sorted(rom_dir.iterdir()):
        if not path.is_file() or path.stat().st_size not in (0x10000, 0x100000):
            continue
        data = path.read_bytes()
        digest = hashlib.sha1(data).hexdigest()
        if digest == CONTROL_SHA1:
            control = data
        elif digest == PCM_SHA1:
            pcm = data
    missing = [name for name, data in (("control ROM v1.02 (64 KiB)", control),
                                       ("PCM ROM (1 MiB)", pcm)) if data is None]
    if missing:
        print(f"no CM-32L {' and no '.join(missing)} in {rom_dir}", file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    control_bin = out_dir / "cm_control.bin"
    pcm_bin = out_dir / "cm_pcm.bin"
    control_bin.write_bytes(control)
    pcm_bin.write_bytes(unscramble(pcm))
    (out_dir / "cm_blob.S").write_text(f"""\
    .section .rodata
    .balign 4
    .global cm_control_rom
    .global cm_control_rom_end
cm_control_rom:
    .incbin "{control_bin.as_posix()}"
cm_control_rom_end:
    .balign 4
    .global cm_pcm_words
    .global cm_pcm_words_end
cm_pcm_words:
    .incbin "{pcm_bin.as_posix()}"
cm_pcm_words_end:
""")
    print("CM-32L control v1.02 and PCM ROM converted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
