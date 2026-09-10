#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71

"""
loop_reference.py  --  sustain fingerprint of a converted sample set.

Reads the _mapping.json prepare_samples.py wrote for a set and stores, per
sample, the level, spectral centroid and band levels of its loop cycle - the
waveform the engine repeats for as long as a key is held. A later run of
prepare_samples.py with loop_finder.reference pointing at this file will only
shorten a sample when the shorter window's loop cycle stays within tolerance
of the fingerprint, so the cut-down set keeps the sustain of the set it was
cut from. See README.md, "Fitting a 4 MB flash".

Usage:
    python3 loop_reference.py <converted>/<instr>/_mapping.json -o reference/<name>.json
"""
import os, sys, json, argparse

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mapping", help="_mapping.json of the reference set")
    ap.add_argument("-o", "--out", required=True, help="fingerprint JSON to write")
    args = ap.parse_args()
    mapping = json.load(open(args.mapping))
    samples = {}
    for name, e in sorted(mapping.items()):
        samples[name] = {"frames": e["frames"], "loop_start": e["loop_start"],
                         "loop_score": e.get("loop_score"), "loop": e.get("loop")}
    with_loop = sum(1 for s in samples.values() if s["loop"])
    out = {"source": os.path.relpath(os.path.abspath(args.mapping)),
           "samples": samples}
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=1)
    print(f"{args.out}: {len(samples)} samples, {with_loop} with a loop cycle")

if __name__ == "__main__":
    main()
