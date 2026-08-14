#!/usr/bin/env python3
"""Extract the D-05's embedded patch bank table into D-50 SysEx card dumps.

    python3 tools/d5_extract/d5_bq3_extract_banks.py <BQ3_Appli.bin> <out_dir>

The D-05 (Boutique) application image carries its whole patch memory as a
384-slot table of raw D-50 patches at file offset 0xA09B1 -- six banks of 64
patches, 448 bytes each, seven 64-byte blocks per patch in exactly the layout
`d5_syx_to_patches.py` consumes. (The ARM dispatcher indexes it at code
offset 0x14C2E: `0x600A09B0 + slot * 448`; the anchor word at 0xA09B0 is not
part of the data, hence the one-byte slide.)

Bank 1 is the original D-50 factory 64, byte-identical to the PND50-00 card
image this repository already embeds (verified 28672/28672 bytes), so only
banks 2..6 are written: bank 2 is Roland's new D-05 preset bank, banks 3..6
carry the D-50 card library (PN-D50-0x family content).

Each bank becomes one standard D-50 bulk dump: DT1 messages
(F0 41 10 14 12 aa bb cc ...) covering the patch area 02-00-00 upwards,
256 data bytes per message, checksums per Roland's rule. The files feed
`d5_syx_to_patches.py` like any card dump.

Source: the D-05 firmware update BQ3 (Roland's public download), unpacked
with `d5_bq3_decompress.py`. The update file itself is not part of this
repository.
"""
import os
import sys

TABLE_OFF = 0xA09B1
PATCH_SIZE = 448
BANK_PATCHES = 64
BANK_COUNT = 6
PATCH_BASE = 0x8000          # 02-00-00, the D-50's patch area
MSG_DATA = 256               # data bytes per DT1 message

CHARS = (" ABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "abcdefghijklmnopqrstuvwxyz1234567890-")


def name_of(body):
    base = 6 * 64
    return "".join(CHARS[b & 0x3F] for b in body[base:base + 18]).rstrip()


def dt1(addr, data):
    """One DT1 message with a valid Roland checksum."""
    a1, a2, a3 = (addr >> 14) & 0x7F, (addr >> 7) & 0x7F, addr & 0x7F
    chk = (128 - (a1 + a2 + a3 + sum(data)) % 128) % 128
    return bytes([0xF0, 0x41, 0x10, 0x14, 0x12, a1, a2, a3]) + data + \
        bytes([chk, 0xF7])


def bank_syx(bank):
    """Encode 64 patches as DT1 messages over the patch address space."""
    out = bytearray()
    for off in range(0, len(bank), MSG_DATA):
        out += dt1(PATCH_BASE + off, bank[off:off + MSG_DATA])
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, outdir = sys.argv[1], sys.argv[2]
    raw = open(src, "rb").read()
    need = TABLE_OFF + BANK_COUNT * BANK_PATCHES * PATCH_SIZE
    if len(raw) < need:
        sys.exit(f"{src} is too short for the patch table "
                 f"({len(raw)} bytes, need {need})")

    table = raw[TABLE_OFF:need]
    patches = [table[i * PATCH_SIZE:(i + 1) * PATCH_SIZE]
               for i in range(BANK_COUNT * BANK_PATCHES)]

    # The sanity net from d5_syx_to_patches.py: structure, chorus type,
    # key mode, reverb type, PCM wave number, waveform, TVF resonance all
    # have documented ranges; a wrong offset breaks them immediately.
    for label, blk, off, hi in [("structure (upper)", 2, 10, 6),
                                ("structure (lower)", 5, 10, 6),
                                ("chorus type", 2, 42, 7),
                                ("key mode", 6, 18, 8),
                                ("reverb type", 6, 30, 31),
                                ("PCM wave number", 0, 7, 99),
                                ("waveform", 0, 6, 1),
                                ("TVF resonance", 0, 14, 30)]:
        vals = [p[blk * 64 + off] for p in patches]
        if max(vals) > hi:
            sys.exit(f"{label} out of range ({min(vals)}..{max(vals)}), "
                     f"allowed 0..{hi} -- wrong table offset")

    os.makedirs(outdir, exist_ok=True)
    written = []
    for bank in range(1, BANK_COUNT):        # bank 0 ships as PND50-00.syx
        body = b"".join(patches[bank * BANK_PATCHES:
                                (bank + 1) * BANK_PATCHES])
        path = os.path.join(outdir, f"D05-Bank{bank + 1}.syx")
        with open(path, "wb") as f:
            f.write(bank_syx(body))
        written.append(path)
        first = name_of(patches[bank * BANK_PATCHES])
        last = name_of(patches[(bank + 1) * BANK_PATCHES - 1])
        print(f"bank {bank + 1}: {os.path.basename(path)} "
              f"({first} ... {last})")

    print(f"{len(written)} banks, 5 x 64 = 320 patches, "
          f"checksums encoded for 02-00-00 upward")


if __name__ == "__main__":
    main()
