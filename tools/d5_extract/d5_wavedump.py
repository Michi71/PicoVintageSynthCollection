#!/usr/bin/env python3
"""Render the decoded D-50 PCM space to WAV for listening and inspection.

    python3 tools/d5_extract/d5_wavedump.py <romdir> [outdir]

Writes d50_pcm_full.wav (both chips, 8.2 s) plus one WAV per chip, 32 kHz
mono 16-bit. Once the sample table is known this tool will grow per-sample
cutting; until then the full dump is the listenable artifact.
"""
import os
import struct
import sys
import wave

from d5_rom import D5RomSet, SAMPLE_RATE


def write_wav(path, samples):
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(
            b"".join(
                struct.pack("<h", max(-32767, min(32767, int(v * 32767))))
                for v in samples
            )
        )


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    rs = D5RomSet(sys.argv[1])
    outdir = sys.argv[2] if len(sys.argv) == 3 else "tools/d5_extract/out"
    os.makedirs(outdir, exist_ok=True)
    half = len(rs.audio) // 2
    for name, seg in (
        ("d50_pcm_full.wav", rs.audio),
        ("d50_pcm_chip_a.wav", rs.audio[:half]),
        ("d50_pcm_chip_b.wav", rs.audio[half:]),
    ):
        path = os.path.join(outdir, name)
        write_wav(path, seg)
        print(f"wrote {path} ({len(seg)/SAMPLE_RATE:.1f} s)")


if __name__ == "__main__":
    main()
