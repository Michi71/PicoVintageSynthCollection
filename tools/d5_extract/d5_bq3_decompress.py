#!/usr/bin/env python3
"""Unpack a Roland Boutique BQ3 firmware update (as used by the D-05).

    python3 tools/d5_extract/d5_bq3_decompress.py <BQ3_UPD.BIN> <out_dir>

Writes one file per component. The container holds several named blocks
(`BQ3:Updater`, `BQ3:SUB-CPU`, `BQ3:Appli`, ...), each with a 0x40-byte header
that is also its descriptor:

    +0x00  16-byte ASCII name        +0x2C  u32  source offset (from block base)
    +0x28  u32  device base          +0x30  u32  compressed size
    +0x34  u32  checksum             +0x38  u32  device destination
                                     +0x3C  u32  decompressed size

The payload is Okumura LZSS, exactly as the updater's own routine decodes it
(disassembled from the plaintext ARM Thumb loader): a 4096-byte ring buffer
zero-filled, write pointer starting at N-F = 0x0FEE, an LSB-first flag byte
where a set bit means a literal, and a match of position `b1 | ((b2 & 0xF0)
<< 4)` and length `(b2 & 0x0F) + 3`.

Why this exists: the D-05 reproduces the D-50, so its firmware is where
Roland's own sample table and root pitches live -- the metadata this
directory otherwise reconstructs by ear. Decompressing gets the firmware out;
locating that table inside the ARM re-implementation is a further step (see
README). The PCM audio itself is not in the update file -- the D-05 has its
own sample ROM, exactly as the D-50 does.
"""
import os
import struct
import sys

N = 4096
F = 18


def decompress(src, start, out_size):
    buf = bytearray(N)
    r = N - F
    out = bytearray()
    i = start
    flags = 0
    while len(out) < out_size and i < len(src):
        flags >>= 1
        if (flags & 0x100) == 0:
            if i >= len(src):
                break
            flags = src[i] | 0xFF00
            i += 1
        if flags & 1:
            c = src[i]; i += 1
            out.append(c)
            buf[r] = c; r = (r + 1) & (N - 1)
        else:
            if i + 1 >= len(src):
                break
            b1 = src[i]; b2 = src[i + 1]; i += 2
            pos = b1 | ((b2 & 0xF0) << 4)
            length = (b2 & 0x0F) + 3
            for k in range(length):
                c = buf[(pos + k) & (N - 1)]
                out.append(c)
                buf[r] = c; r = (r + 1) & (N - 1)
    return bytes(out), i


def blocks(fw):
    """Yield (name, base) for every BQ3 block header in the container."""
    i = 0
    while True:
        j = fw.find(b"BQ3:", i)
        if j < 0:
            return
        name = fw[j:j + 16].split(b"\x00")[0].decode("latin1").strip()
        yield name, j
        i = j + 16


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    fw = open(sys.argv[1], "rb").read()
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    seen = set()
    for name, base in blocks(fw):
        if base in seen:
            continue
        seen.add(base)
        src_off = base + struct.unpack_from("<I", fw, base + 0x2C)[0]
        usize = struct.unpack_from("<I", fw, base + 0x30)[0]
        dsize = struct.unpack_from("<I", fw, base + 0x3C)[0]
        dst = struct.unpack_from("<I", fw, base + 0x38)[0]
        if not (0 < dsize < (8 << 20)) or src_off + usize > len(fw):
            continue
        out, end = decompress(fw, src_off, dsize)
        ok = len(out) == dsize
        safe = name.replace(":", "_").replace("/", "_")
        path = os.path.join(outdir, f"{safe}.bin")
        open(path, "wb").write(out)
        print(f"{name:16} src 0x{src_off:06X} -> {len(out)}/{dsize} bytes "
              f"@ device 0x{dst:08X}  {'ok' if ok else 'SHORT'}  -> {path}")


if __name__ == "__main__":
    main()
