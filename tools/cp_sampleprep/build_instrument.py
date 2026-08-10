#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71

"""
build_instrument.py  --  stage 2: descriptor (_instrument.json) to C headers.

Writes the two files PicoFaceCP's engine includes:

  - <name>Data.h       : const int16_t <name>Data[] = { ... };  the samples,
                         every referenced one concatenated exactly once
  - <name>Keygroups.h  : the keygroup table (pos/end/loop/root/high) as a
                         const struct array, plus an mda-style assignment
                         snippet for the constructor

The engine plays pos..end and wraps at 'loop', in mda's semantics:
   while (pos > end) pos -= loop;   ->  loop = end + 1 - loop_start, a LENGTH
rather than a position. Getting that backwards is the classic way to make a
sample set that plays but throbs.

Three velocity layers per region (engine: k += 3, thresholds 48/80). root and
high are set on a region's first layer only, as mda does.

Usage:
    python3 build_instrument.py <converted>/<instr>/_instrument.json
    python3 build_instrument.py <converted>/<instr>/_instrument.json --outdir DIR

The output templates are deliberately unchanged from the version that produced
the committed headers, German comments included, so a regenerated voice
differs from what ships only where the data differs. See README.md.
"""
import os, re, sys, json, argparse, wave
import numpy as np


def load_int16_mono(path):
    """liest ein mono 16-bit WAV als int16-ndarray."""
    w = wave.open(path, "rb")
    if w.getnchannels() != 1:
        raise SystemExit(f"{path}: erwartet mono, hat {w.getnchannels()} Kanaele")
    n = w.getnframes()
    data = np.frombuffer(w.readframes(n), dtype=np.int16).copy()
    w.close()
    return data


def format_data_array(symbol, samples):
    """int16-Array wie bei mda (20 Werte/Zeile) formatieren."""
    out = [f"const int16_t {symbol}[] = {{"]
    for i in range(0, len(samples), 20):
        chunk = samples[i:i+20]
        out.append("  " + ",".join(str(int(v)) for v in chunk) + ",")
    # letzte Zeile ohne Komma + schliessende Klammer
    if out[-1].endswith(","):
        out[-1] = out[-1][:-1]
    out.append("};")
    return "\n".join(out)


def build(desc, outdir):
    name       = desc["name"]
    symbol     = desc["data_symbol"]           # z.B. "Rd_IIData" (bereits C-sicher)
    base       = symbol[:-4] if symbol.endswith("Data") else symbol  # z.B. "Rd_II"
    sr         = desc["sample_rate"]
    regions    = desc["regions"]
    samples_db = desc["samples"]
    nlayers    = desc["engine"]["velocity_layers"]
    thresholds = desc["engine"]["velocity_thresholds"]
    guard = re.sub(r"\W", "_", name).upper()

    # 1) eindeutige Samples sammeln (Reihenfolge: Vorkommen)
    unique = []
    seen = set()
    for r in regions:
        for s in r["layers"]:
            if s not in seen:
                seen.add(s); unique.append(s)

    # 2) Daten-Array aufbauen + Pos/End/Loop je Sample
    all_samples = []
    offset = 0
    posmap = {}   # sample_name -> (pos, end, loop)
    for s in unique:
        info = samples_db[s]
        wav = load_int16_mono(info["file"])
        pos = offset                       # kumulative Sample-Position (nicht Listenindex!)
        frames = len(wav)
        end = pos + frames - 1
        ls = info["loop_start"]
        loop = (frames - ls) if (ls is not None and 0 <= ls < frames) else frames
        posmap[s] = (pos, end, loop)
        all_samples.append(wav)
        offset += frames
    all_samples = np.concatenate(all_samples) if all_samples else np.zeros(0, dtype=np.int16)
    # Padding: die Engine liest waves[pos+1] (Linear-Interpolation); am Ende des
    # letzten Samples waere pos+1 sonst out-of-bounds. Wie beim mda-Original
    # (dort ~11 Samples Padding) legen wir Padding-Nullen an.
    PADDING = 8
    if len(all_samples):
        all_samples = np.concatenate([all_samples, np.zeros(PADDING, dtype=np.int16)])

    # 3) Keygroup-Einträge: pro Region nlayers Eintraege (k += nlayers),
    #    root/high nur auf dem ersten Eintrag jeder Region.
    kgrp = []   # (root, high, pos, end, loop)
    for r in regions:
        for li in range(nlayers):
            s = r["layers"][li]
            p, e, lp = posmap[s]
            root = r["root"] if li == 0 else 0
            high = r["high"] if li == 0 else 0
            kgrp.append((root, high, p, e, lp))

    nkgrp = len(kgrp)
    total_frames = len(all_samples)

    # Keygroup-Tabelle + globale Pos/End in den Deskriptor eintragen (JSON)
    desc["keygroups"] = []
    for i, (root, high, p, e, lp) in enumerate(kgrp):
        desc["keygroups"].append({
            "index": i, "region": i // nlayers, "layer": i % nlayers,
            "root": root, "high": high, "pos": p, "end": e, "loop": lp,
            "loop_start_global": e + 1 - lp,
        })
    for nm, (p, e, lp) in posmap.items():
        desc["samples"][nm].update({"pos": p, "end": e, "loop_global": lp,
                                    "loop_start_global": e + 1 - lp})
    desc["data"] = {"symbol": symbol, "samples": total_frames, "bytes": total_frames * 2,
                    "nkgrp": nkgrp, "nregions": len(regions)}
    os.makedirs(outdir, exist_ok=True)
    json_path = os.path.join(outdir, f"{base}_instrument.json")
    with open(json_path, "w") as fh:
        json.dump(desc, fh, indent=2)

    # 4) Daten-Header
    data_h = os.path.join(outdir, f"{base}Data.h")
    with open(data_h, "w") as fh:
        fh.write(f"/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN\n"
                 f"   Instrument: {name}   Sample-Rate: {sr} Hz   Samples: {total_frames}\n"
                 f"   Daten groesse: {total_frames*2} Bytes ({total_frames*2/1024:.1f} kB) */\n"
                 f"#ifndef {guard}_DATA_H\n#define {guard}_DATA_H\n"
                 f"#include <stdint.h>\n\n")
        fh.write(format_data_array(symbol, all_samples))
        fh.write(f"\n#define {guard}_DATA_SAMPLES {total_frames}\n")
        fh.write(f"#endif /* {guard}_DATA_H */\n")

    # 5) Keygroup-Header
    kg_h = os.path.join(outdir, f"{base}Keygroups.h")
    with open(kg_h, "w") as fh:
        fh.write(f"/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN\n"
                 f"   Instrument: {name}   Keygroups: {nkgrp} ({len(regions)} Regionen x "
                 f"{nlayers} Velocity-Layer)\n"
                 f"   Engine: k += {nlayers};  Velocity-Schwellen {thresholds}\n"
                 f"   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)\n"
                 f"   -> kgrp[] muss >= {nkgrp} sein (mda hat 34). */\n"
                 f"#ifndef {guard}_KEYGROUPS_H\n#define {guard}_KEYGROUPS_H\n"
                 f"#include <stdint.h>\n\n")
        fh.write("/* spiegelt struct KGRP aus mdaEPiano.h */\n")
        fh.write(f"static const struct {{ int32_t root; int32_t high; int32_t pos;\n"
                 f"                         int32_t end; int32_t loop; }} {base}Kgrp[{nkgrp}] = {{\n")
        for i, (root, high, p, e, lp) in enumerate(kgrp):
            fh.write(f"  /* {i:2d} */ {{ {root:3d}, {high:3d}, {p:7d}, {e:7d}, {lp:6d} }},\n")
        fh.write("};\n")
        fh.write(f"#define {guard}_NKGRP {nkgrp}\n\n")

        # mda-Stil Zuweisungs-Snippet zum Einfuegen in den Konstruktor
        fh.write(f"/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===\n"
                 f"   waves = (short*){symbol};\n")
        fh.write(f"   kgrp muss >= {nkgrp} sein (z.B. KGRP kgrp[{max(nkgrp, 34)}];)\n")
        fh.write(f"   noteon: while(note > (kgrp[k].high + s)) k += {nlayers};  "
                 f"dann if(vel>{thresholds[0]}) k++; if(vel>{thresholds[1]}) k++;\n\n")
        for i, (root, high, p, e, lp) in enumerate(kgrp):
            if i % nlayers == 0:
                fh.write(f"  kgrp[{i:2d}].root={root:3d}; kgrp[{i:2d}].high={high:3d}; "
                         f"kgrp[{i:2d}].pos={p:7d}; kgrp[{i:2d}].end={e:7d}; kgrp[{i:2d}].loop={lp:6d};\n")
            else:
                fh.write(f"  kgrp[{i:2d}].pos={p:7d}; kgrp[{i:2d}].end={e:7d}; kgrp[{i:2d}].loop={lp:6d};\n")
        fh.write("*/\n")
        fh.write(f"#endif /* {guard}_KEYGROUPS_H */\n")

    print(f"Instrument: {name}")
    print(f"  Regionen      : {len(regions)}   (Roots {regions[0]['root']}..{regions[-1]['root']})")
    print(f"  Velocity-Layer: {nlayers}   Keygroups: {nkgrp}")
    print(f"  Daten         : {total_frames} Samples = {total_frames*2/1024:.1f} kB "
          f"({len(unique)} eindeutige Sample-Sets)")
    print(f"  -> {data_h}")
    print(f"  -> {kg_h}")
    print(f"  Engine-Hinweis: kgrp[] >= {nkgrp} (mda hat 34); noteon 'k += {nlayers}'.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("descriptor", help="_instrument.json (von prepare_samples.py)")
    ap.add_argument("--outdir", default=None,
                    help="Ziel-Ordner (Default: <Dir-des-JSON>/generated)")
    args = ap.parse_args()
    desc = json.load(open(args.descriptor))
    outdir = args.outdir or os.path.join(os.path.dirname(os.path.abspath(args.descriptor)),
                                         "generated")
    build(desc, outdir)

if __name__ == "__main__":
    main()
