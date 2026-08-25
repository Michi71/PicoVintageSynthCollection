# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Undoes the scrambling on an MKS-20 / MK-80 ROM set.

Roland ran the address and data lines to these ROMs in a permuted order, and
the sample chips are read three at a time with their bits interleaved. Which
line goes where is a fact about the board, and the fact was established by
**Giulio Zausa** in [giulioz/rdpiano](https://github.com/giulioz/rdpiano) --
this is that reading written out as tables rather than as expressions, so a
build does not need his emulator compiled and running just to read a ROM.

Removing a build dependency is not removing the credit. Without that work none
of this would be readable at all, and RD_FIRMWARE.md and the instrument's
README say so at more length.

Checked byte for byte against what the emulator produces; see
rd_descramble_check.py.
"""

import os
import sys

# ---------------------------------------------------------------- line orders
#
# Each entry is the source bit that feeds an output bit, highest first --
# MAME's bitswap order, which is where these were read.

ADDR_CPUB = (13, 12, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0)
ADDR_PARAMS = (16, 15, 13, 12, 14, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0)
DATA_CPUB = (7, 0, 6, 1, 5, 2, 4, 3)

# The wave ROMs' address lines, same convention.
ADDR_WAVE = (16, 15, 14, 1, 4, 9, 5, 10, 3, 0, 6, 11, 7, 2, 12, 8, 13)

# Reading a sample permutes the address a second time -- five lines inverted,
# the rest straight.
SAMPLE_ADDR_INVERT = (1, 3, 5, 8, 9)

# Which chip and bit feeds each output bit, and whether it arrives inverted.
# (chip, bit, inverted), highest output bit first.
EXP_BITS = ((0, 0, False), (1, 4, False), (2, 4, False), (1, 0, True),
            (2, 7, False), (0, 7, False), (0, 5, True), (1, 2, False),
            (2, 2, False), (2, 1, False), (0, 1, True), (0, 3, False),
            (1, 5, False), (1, 7, True))
EXP_SIGN = (2, 3, True)
DELTA_BITS = ((2, 6, True), (0, 4, False), (2, 0, False), (1, 3, True),
              (0, 2, False), (0, 6, True), (1, 6, False), (2, 5, False),
              (1, 7, True))
DELTA_SIGN = (1, 1, False)


def bitswap(value, order):
    """MAME's bitswap: the result's top bit is value's order[0], and so on."""
    out = 0
    for shift, src in enumerate(reversed(order)):
        out |= ((value >> src) & 1) << shift
    return out


def _permute(data, order, data_order=None, size=None):
    n = size if size is not None else len(data)
    out = bytearray(n)
    for i in range(n):
        b = data[bitswap(i, order)]
        out[i] = bitswap(b, data_order) if data_order else b
    return bytes(out)


def program_rom(raw):
    """The 8 KB sound-CPU ROM, address and data both permuted."""
    return _permute(raw, ADDR_CPUB, DATA_CPUB, size=len(raw))


def params_rom(raw):
    """The 128 KB parameter ROM."""
    return _permute(raw, ADDR_PARAMS, DATA_CPUB, size=len(raw))


def _sample_address(i):
    out = i
    for bit in SAMPLE_ADDR_INVERT:
        out ^= 1 << bit
    return out


def _gather(bits, ic, at):
    out = 0
    for shift, (chip, bit, invert) in enumerate(reversed(bits)):
        v = (ic[chip][at] >> bit) & 1
        out |= (v ^ 1 if invert else v) << shift
    return out


def sample_bank(raw5, raw6, raw7):
    """Three sample chips -> exponent, its sign, delta, its sign, per sample.

    The chips are unscrambled first, then read through a second permutation --
    the same address with five of its lines inverted -- and each output value
    is assembled bit by bit from all three.
    """
    n = len(raw5)
    ic = [_permute(r, ADDR_WAVE, size=n) for r in (raw5, raw6, raw7)]
    exp = [0] * n
    exp_sign = bytearray(n)
    delta = [0] * n
    delta_sign = bytearray(n)
    for i in range(n):
        at = _sample_address(i)
        exp[i] = _gather(EXP_BITS, ic, at)
        delta[i] = _gather(DELTA_BITS, ic, at)
        c, b, inv = EXP_SIGN
        exp_sign[i] = ((ic[c][at] >> b) & 1) ^ (1 if inv else 0)
        c, b, inv = DELTA_SIGN
        delta_sign[i] = ((ic[c][at] >> b) & 1) ^ (1 if inv else 0)
    return exp, bytes(exp_sign), delta, bytes(delta_sign)


def _read(path, size):
    try:
        data = open(path, "rb").read()
    except OSError:
        sys.stderr.write(f"rd_descramble: cannot open {path}\n")
        sys.exit(2)
    if len(data) != size:
        sys.stderr.write(f"rd_descramble: {path} is {len(data)} bytes, "
                         f"expected {size}\n")
        sys.exit(2)
    return data


def main():
    """Descrambles a program and parameter ROM pair for reading, the same two
    files rd_unscramble.sh used to get out of the emulator. rd_parse.py and the
    firmware disassembly take it from here."""
    if len(sys.argv) != 6:
        sys.stderr.write("usage: rd_descramble.py <romdir> <progrom> "
                         "<paramsrom> <out-program> <out-params>\n")
        sys.exit(1)
    romdir, prog, params, out_prog, out_params = sys.argv[1:]
    open(out_prog, "wb").write(
        program_rom(_read(os.path.join(romdir, prog), 0x2000)))
    open(out_params, "wb").write(
        params_rom(_read(os.path.join(romdir, params), 0x20000)))
    print(f"rd_descramble: {out_prog}, {out_params}")


if __name__ == "__main__":
    main()
