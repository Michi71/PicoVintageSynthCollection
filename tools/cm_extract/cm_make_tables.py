#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michi71
"""Writes TablesData.inc, the lookup tables of PicoFaceCM's mt32emu port, from
the formulas in upstream's Tables.cpp.

    cm_make_tables.py <out file>

Upstream evaluates these formulas in C++ when the library starts, in single
precision, with whatever log(), sin() and exp2f() the C library provides. The
port takes the results as data so that every build renders the same samples.
This script is where the data comes from: the same formulas, with every
single-precision step rounded the way the C++ does it, and the math functions
taken in double precision and rounded once. Nothing is read from a ROM.

The output is text-identical to what gen_tables.cpp (in the cm32l project)
prints from upstream itself, and is checked against a digest of that text.
"""
import hashlib
import math
import struct
import sys

VERSION = "2.8.6"

# SHA-256 of the text this script must produce - the tables as upstream 2.8.6
# computes them, confirmed by gen_tables.cpp. Python takes log(), log10() and
# sin() from the platform's C library; should one ever round differently, the
# build stops here instead of producing an engine that sounds slightly off.
EXPECTED_SHA256 = "3813804b6379b83e63c2a880fc170bee94c79d3585c8891eb9770091bcd0fb70"


def f32(x: float) -> float:
    """x rounded to the nearest single-precision value."""
    return struct.unpack("f", struct.pack("f", x))[0]


LN_2 = f32(0.6931472)       # FLOAT_LN_2
PI = f32(3.1415927)         # FLOAT_PI


def log2f(x: float) -> float:
    """LOG2F: logf(x) / FLOAT_LN_2."""
    return f32(f32(math.log(x)) / LN_2)


def tables():
    # (2.0f - log10f(lf + 1.0f)) * 128.0f, then + 1.0 in double, truncated.
    level_to_amp = []
    for lf in range(101):
        v = f32(f32(2.0 - f32(math.log10(lf + 1.0))) * 128.0)
        level_to_amp.append(min(int(v + 1.0), 255))

    # ceil(64.0f + LOG2F(lf) * 8.0f)
    env_time = [64]
    for lf in range(1, 256):
        env_time.append(int(math.ceil(f32(64.0 + f32(log2f(float(lf)) * 8.0)))))

    # 106.31 - 16.0f * LOG2F(masterVol): the product in single, the difference in double.
    master_vol = [255]
    for mv in range(1, 101):
        master_vol.append(int(106.31 - f32(16.0 * log2f(float(mv)))))

    # i * 255 / 100.0f + 0.5f
    pulse_width = [int(f32(f32(i * 255 / f32(100.0)) + 0.5)) for i in range(101)]

    # 8191.5f - exp2f(13.0f + ~i / 512.0f); ~i is -(i + 1), the argument is exact.
    exp9 = [int(f32(8191.5 - f32(2.0 ** (13.0 + -(i + 1) / 512.0)))) for i in range(512)]

    # 0.5f - LOG2F(sinf((i + 0.5f) / 1024.0f * FLOAT_PI)) * 1024.0f; entry 0 is clamped.
    logsin9 = [8191]
    for i in range(1, 512):
        arg = f32(f32((i + 0.5) / 1024.0) * PI)
        logsin9.append(int(f32(0.5 - f32(log2f(f32(math.sin(arg))) * 1024.0))))

    return (("Bit8u", "levelToAmpSubtraction", level_to_amp),
            ("Bit8u", "envLogarithmicTime", env_time),
            ("Bit8u", "masterVolToAmpSubtraction", master_vol),
            ("Bit8u", "pulseWidth100To255", pulse_width),
            ("Bit16u", "exp9", exp9),
            ("Bit16u", "logsin9", logsin9))


def render() -> str:
    out = [f"// Lookup tables of upstream mt32emu {VERSION}, computed from its formulas. Generated; do not edit.\n\n"]
    for ctype, name, values in tables():
        out.append(f"static const {ctype} CM32_TABLE_{name}[{len(values)}] = {{")
        for i, v in enumerate(values):
            out.append(("\n\t" if i % 16 == 0 else " ") + str(v) + ("," if i + 1 < len(values) else ""))
        out.append("\n};\n\n")
    return "".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    text = render()
    if hashlib.sha256(text.encode()).hexdigest() != EXPECTED_SHA256:
        print("cm_make_tables.py: the tables computed on this machine differ from upstream mt32emu's",
              file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], "w") as f:
        f.write(text)
