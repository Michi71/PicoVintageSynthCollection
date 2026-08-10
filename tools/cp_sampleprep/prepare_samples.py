#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71

"""
prepare_samples.py  --  stage 1: arbitrary WAV sets to mda-shaped samples.

Brings any WAV sample set to the format and level of mda-EPiano's own samples,
and cuts it to mda's characteristic "attack plus short loop" shape with loop
points found by the FindLoopPoints helper.

Per sample:
  1. Read (any channel count / rate) -> downmix to mono
  2. Resample -> target_sr (default 32 kHz, mda's native rate; the engine
     interpolates up to 44.1 kHz at playback)
  3. Normalise to the pitch-dependent mda target peak
        peak_dBFS(note) = a + b * note
     fitted from mda's own root samples, so the set inherits mda's habit of
     getting quieter towards the top of the keyboard. Per-sample peak norm.
  4. Trim:
       mode="loop"      : pre-cut to a window ending in strong sustain shortly
                          after the bloom peak, then hand each candidate window
                          to the FindLoopPoints binary, which locates the loop,
                          cuts at its end and returns loopStart. Result is
                          attack plus a looped sustain region, mda style.
       mode="transient" : attack and bloom only, to the smoothed maximum plus
                          post_peak_ms. No loop.
       mode="fixed_len" : blunt cut to a fixed length.
       mode="none"      : no trim.
  5. Write 16-bit mono PCM at target_sr, plus _mapping.json and
     _instrument.json (note / pos / end / loop / score per sample)

Loop scores come from the C++ tool: <=0.01 excellent, <=0.05 good, <=0.15
acceptable, >0.30 unusable -- the sample is then kept without a loop.

Usage:
    python3 prepare_samples.py CONFIG.json
    python3 prepare_samples.py configs/            # every config in the folder

Source recordings are not part of this repository; see README.md for why, and
for the file naming this expects.
"""
import os, re, sys, json, glob, shutil, argparse, subprocess, tempfile
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

DEFAULTS = {
    "target_sr": 32000,
    "normalize": {"mode": "mda_curve", "fixed_dbfs": -9.39},
    "trim": {
        "mode": "loop",           # "loop" | "transient" | "fixed_len" | "none"
        # gemeinsame Parameter
        "win_ms": 8, "hop_ms": 1, "smooth_ms": 40,
        "min_ms": 80, "max_ms": 500, "fade_out_ms": 6,
        # transient / fixed_len
        "post_peak_ms": 50, "fixed_len_ms": 500,
        # loop-Modus
        "loop_after_peak_ms": 150,   # Fensterende = Bloom-Peak + x
        "loop_min_window_ms": 250,
        "loop_max_window_ms": 500,
        "loop_absolute_max_ms": 800,   # harte Grenze; sehr tiefe Toene duerfen
                                       # darueber liegen, damit der Bloom erhalten bleibt
        "num_periods": 10,
        "min_loop_samples": 30,
        "min_loop_periods": 1,       # Loop muss >= N volle Perioden lang sein
                                      # (1 = eine Periode; Faktor 0.8 toleriert
                                      #  Inharmonizitaet, sortiert aber Halbperioden aus)
    },
    "loop_finder": {
        "binary": "tools/cp_sampleprep/FindLoopPoints",  # relative to the repo root
        "min_score": 0.15,                   # > galt als "akzeptabel" im C-Tool
    },
    "bit_depth": 16,
}

REPO_ROOT = None


# ===========================================================================
# mda-Referenzkurve  peak_dBFS(note) = a + b*note
# ===========================================================================
CP_DIR = os.path.join("instruments", "PicoFaceCP")


def load_mda_curve(repo_root):
    # Was 34 exported WAVs plus the plugin source. Neither travelled with this
    # tool into PicoVintageSynthCollection, and the WAVs were themselves an
    # export of mdaEPianoData.h -- which is in the repository. Reading the
    # peaks straight out of the header removes the last data dependency, so
    # this runs on a plain checkout.
    #
    # The header data is pre-baked with the loop crossfade applied, unlike the
    # array the original plugin built at runtime, so the two are not identical
    # by construction. Checked against the WAVs before the switch: the fitted
    # a and b agree to all printed digits and the target peak differs by
    # 0.000000 dB over notes 21..108. The crossfade lives in the loop region;
    # the peak of an e-piano sample is in the attack.
    cpp = open(os.path.join(repo_root, CP_DIR, "src", "mdaEPiano.cpp")).read()
    root, pos, end = {}, {}, {}
    for name, table in (("root", root), ("pos", pos), ("end", end)):
        for m in re.finditer(r"kgrp\[\s*(\d+)\s*\]\." + name + r"\s*=\s*(\d+)", cpp):
            table[int(m.group(1))] = int(m.group(2))

    header = open(os.path.join(repo_root, CP_DIR, "include", "mdaEPianoData.h")).read()
    body = header[header.index("{") + 1: header.rindex("}")]
    data = np.array([int(v) for v in re.findall(r"-?\d+", body)], dtype=np.int16)

    notes, pdb = [], []
    for i in sorted(root):
        if i not in pos or i not in end:
            continue
        seg = data[pos[i]:end[i] + 1].astype(np.float64) / 32768.0
        if seg.size == 0:
            continue
        notes.append(root[i]); pdb.append(20*np.log10(np.max(np.abs(seg)) + 1e-12))
    notes = np.array(notes, float); pdb = np.array(pdb, float)
    A = np.vstack([np.ones_like(notes), notes]).T
    a, b = np.linalg.lstsq(A, pdb, rcond=None)[0]
    return float(a), float(b), notes, pdb


# ===========================================================================
# WAV-Metadaten: Root-Note + Loop aus smpl/cue-Chunk (Fallback: Dateiname)
# ===========================================================================
def parse_smpl(path):
    try:
        info = sf.info(path).extra_info
    except Exception:
        info = ""
    note = ls = le = None
    m = re.search(r"Midi Note\s*:\s*(\d+)", info)
    if m: note = int(m.group(1))
    m = re.search(r"Start\s*:\s*(\d+)\s*End\s*:\s*(\d+)", info)
    if m: ls, le = int(m.group(1)), int(m.group(2))
    return note, ls, le

def note_from_filename(name):
    m = re.match(r"\D*(\d{2,3})\b", name)
    return int(m.group(1)) if m else None


# ===========================================================================
# DSP-Hilfen
# ===========================================================================
def to_mono(data):
    return data.mean(axis=1) if data.ndim > 1 else data.copy()

def resample_to(mono, sr_in, sr_out):
    g = np.gcd(sr_out, sr_in)
    return resample_poly(mono, sr_out // g, sr_in // g)

def rms_env(x, W, hop):
    return np.array([np.sqrt(np.mean(x[k:k+W]**2)) if k+W <= len(x) else 0.0
                     for k in range(0, len(x), hop)])

def smooth(e, W):
    return np.convolve(e, np.ones(max(1, W))/max(1, W), mode='same') if W > 1 else e

def bloom_peak_ms(x, sr, p):
    """Zeit (ms) des geglaetteten Energie-Maximums."""
    hop = max(1, int(p["hop_ms"]*1e-3*sr)); W = max(1, int(p["win_ms"]*1e-3*sr))
    sm = max(1, int(p["smooth_ms"]*1e-3*sr/hop))
    e = smooth(rms_env(x, W, hop), sm)
    if len(e) == 0:
        return 0.0
    return float(np.argmax(e) * hop / sr * 1000.0)

def apply_fade_out(x, sr, ms):
    n = int(ms*1e-3*sr)
    if n <= 0 or n >= len(x):
        return x
    x[-n:] *= np.sin(np.pi/2 * np.linspace(1.0, 0.0, n))
    return x


# ===========================================================================
# FindLoopPoints-Binary aufrufen
# ===========================================================================
def run_loop_finder(samples, sr, binary, num_periods, work):
    """Schreibt samples als temp. WAV, ruft das C-Binary, liest das getrimmte
    Ergebnis + loopStart zurueck. Liefert (trimmed_int16, loop_start, score)."""
    tmp_wav = os.path.join(work, "in.wav")
    sf.write(tmp_wav, samples.astype(np.float32), sr, subtype="PCM_16")
    loop_file = os.path.splitext(tmp_wav)[0] + ".loop"
    if os.path.exists(loop_file):
        os.remove(loop_file)

    cmd = [binary, tmp_wav, str(num_periods)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout
    except Exception as e:
        return None, None, None
    if not os.path.exists(loop_file):
        return None, None, None

    trimmed, sr2 = sf.read(tmp_wav, dtype="int16")
    trimmed = trimmed.astype(np.float64) / 32768.0   # int16 -> [-1,1]
    loop_start = int(open(loop_file).read().strip() or 0)
    m = re.search(r"Final score:\s*([0-9.eE+-]+)", out)
    score = float(m.group(1)) if m else None
    return trimmed, loop_start, score


# ===========================================================================
# Hauptverarbeitung eines Instruments
# ===========================================================================
def process_instrument(cfg, repo_root):
    c = json.loads(json.dumps(DEFAULTS))   # tiefe Kopie
    for k in ("target_sr", "normalize", "trim", "loop_finder", "bit_depth"):
        if k in cfg:
            if isinstance(cfg[k], dict) and isinstance(c[k], dict):
                c[k] = {**c[k], **cfg[k]}
            else:
                c[k] = cfg[k]

    src_dir = os.path.join(repo_root, cfg["source_dir"])
    out_dir = os.path.join(repo_root, cfg.get("output_dir",
                os.path.join(os.path.dirname(src_dir), "converted",
                             cfg.get("name", os.path.basename(src_dir.rstrip("/"))))))
    os.makedirs(out_dir, exist_ok=True)
    sr_out = int(c["target_sr"])

    norm = c["normalize"]
    a = b = None
    if norm["mode"] == "mda_curve":
        a, b, notes, pdb = load_mda_curve(repo_root)
        print(f"[{cfg.get('name','?')}] mda-Kurve: peak_dBFS = {a:.3f} {b:+.5f} * note  (n={len(notes)})")

    p = c["trim"]
    binary = c["loop_finder"]["binary"]
    if not os.path.isabs(binary):
        binary = os.path.join(repo_root, binary)
    if p["mode"] == "loop" and not os.path.exists(binary):
        print(f"[{cfg.get('name','?')}] WARN FindLoopPoints-Binary nicht gefunden ({binary}) -> auf 'transient' zurueckgefallen")
        p["mode"] = "transient"

    mapping = {}
    files = sorted(glob.glob(os.path.join(src_dir, "*.wav")))
    if not files:
        print(f"[{cfg.get('name','?')}] keine WAVs in {src_dir}")
        return
    total_in = total_out = 0
    work = tempfile.mkdtemp(prefix="sampleprep_")
    try:
        for f in files:
            name = os.path.splitext(os.path.basename(f))[0]
            data, sr = sf.read(f, dtype="float64", always_2d=True)
            n0 = data.shape[0]; total_in += n0
            mono = resample_to(to_mono(data), sr, sr_out)
            scale = sr_out / sr

            note, ls, le = parse_smpl(f)
            if note is None:
                note = note_from_filename(os.path.basename(f))

            # Normalisierung
            cur_peak = np.max(np.abs(mono))
            if norm["mode"] == "mda_curve":
                tgt_db = a + b*note
            elif norm["mode"] == "fixed":
                tgt_db = norm["fixed_dbfs"]
            else:
                tgt_db = 20*np.log10(cur_peak+1e-12)
            mono *= 10**(tgt_db/20.0) / cur_peak if cur_peak > 1e-9 else 1.0
            mono = np.clip(mono, -1.0, 1.0)

            t_peak = bloom_peak_ms(mono, sr_out, p)

            # ---- Trim ----------------------------------------------------
            if p["mode"] == "loop":
                # Kandidaten-Fenster: Suche ZENTRIERT auf "Bloom + after_peak"
                # (Loop liegt im Sustain kurz nach dem Bloom). Das Fenster muss
                # den Bloom-Peak enthalten -> definierende Transiente + Ziel-Pegel.
                base = t_peak + p["loop_after_peak_ms"]
                hi = max(p["loop_max_window_ms"], p["loop_absolute_max_ms"])
                cand = []
                for d in (0, 50, -50, 100, -100, 150, -150, 200):
                    w = float(np.clip(base + d, p["loop_min_window_ms"], hi))
                    cand.append(w)
                cand = sorted(set(round(x) for x in cand), reverse=True)[:8]
                good = 0.02            # exzellent -> Fruehabbruch
                accept = c["loop_finder"]["min_score"]   # akzeptabel (Default 0.15)
                hard = 0.30            # darueber: kein verwertbarer Loop
                # Periodenlaenge aus Midi-Note (um degenerierte Kurz-Loops auszusortieren)
                freq = 440.0 * 2 ** ((note - 69) / 12.0)
                period = sr_out / freq if freq > 0 else 0
                min_len = max(p["min_loop_samples"],
                              int(0.8 * p["min_loop_periods"] * period)) if period > 0 else p["min_loop_samples"]
                best = None  # (trimmed, loop_start, score, win_ms)
                for wms in cand:
                    pre = mono[:int(round(wms*1e-3*sr_out))].astype(np.float32)
                    trimmed, loop_start, score = run_loop_finder(
                            pre, sr_out, binary, p["num_periods"], work)
                    ok = (trimmed is not None and loop_start is not None
                          and 0 <= loop_start < len(trimmed)
                          and (len(trimmed)-1 - loop_start) >= min_len)
                    if ok and (best is None or (score is not None and score < best[2])):
                        best = (trimmed, int(loop_start), score if score is not None else 9e9, wms)
                        if best[2] <= good:      # exzellent -> sofort uebernehmen
                            break
                if best is not None and best[2] <= hard:
                    final = best[0].astype(np.float64)
                    loop_start = best[1]; loop_end = int(len(final)-1)
                    score = best[2] if best[2] < 9e9 else None
                    loop_pending = False
                    flag = "  *POOR*" if (score is not None and score > accept) else ""
                    extra = f"loop[{loop_start}..{loop_end}] ({loop_end-loop_start+1}sm, >={min_len}) score={score} win={best[3]}ms{flag}"
                else:
                    final = mono[:int(round(cand[0]*1e-3*sr_out))].astype(np.float64)
                    final = apply_fade_out(final, sr_out, p["fade_out_ms"])
                    loop_start = loop_end = None; loop_pending = True; score = None
                    extra = "LOOP FAIL -> kein Loop (Fenster ohne Loop)"
            elif p["mode"] == "transient":
                end_ms = float(np.clip(t_peak + p["post_peak_ms"], p["min_ms"], p["max_ms"]))
                final = mono[:int(round(end_ms*1e-3*sr_out))].copy()
                final = apply_fade_out(final, sr_out, p["fade_out_ms"])
                loop_start = loop_end = None; loop_pending = True; score = None
                extra = f"transient@{t_peak:.0f}ms"
            elif p["mode"] == "fixed_len":
                final = mono[:int(round(p["fixed_len_ms"]*1e-3*sr_out))].copy()
                final = apply_fade_out(final, sr_out, p["fade_out_ms"])
                loop_start = loop_end = None; loop_pending = True; score = None
                extra = f"fixed {p['fixed_len_ms']}ms"
            else:
                final = mono.copy()
                loop_start = loop_end = None; loop_pending = True; score = None
                extra = "no trim"

            total_out += len(final)
            out = os.path.join(out_dir, f"{name}.wav")
            sf.write(out, final.astype(np.float32), sr_out, subtype=f"PCM_{c['bit_depth']}")

            # Original-Loop (nur Info)
            if ls is not None and le is not None:
                ls_o = int(round(ls*scale)); le_o = int(round(le*scale))
            else:
                ls_o = le_o = None

            new_peak = float(np.max(np.abs(final)))
            mapping[name] = {
                "root_note": note,
                "sample_rate": sr_out,
                "frames": int(len(final)),
                "duration_ms": round(len(final)/sr_out*1000.0, 2),
                "pos": 0, "end": int(len(final)-1),
                "loop_start": loop_start, "loop_end": loop_end,
                "loop_len": (loop_end - loop_start + 1) if loop_start is not None else None,
                "loop_pending": loop_pending,
                "loop_score": score,
                "target_peak_dbfs": round(tgt_db, 2),
                "peak_dbfs": round(20*np.log10(new_peak+1e-12), 2),
                "bloom_peak_ms": round(t_peak, 1),
                "orig_loop_target": [ls_o, le_o],
            }
            print(f"  {name:<10} note={note:>3}  {n0/sr:6.2f}s -> "
                  f"{len(final)/sr_out*1000:5.0f}ms  peak {20*np.log10(cur_peak):+5.1f}"
                  f"->{20*np.log10(new_peak):+5.1f}dB  bloom@{t_peak:4.0f}ms  {extra}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    with open(os.path.join(out_dir, "_mapping.json"), "w") as fh:
        json.dump(mapping, fh, indent=2)
    write_instrument_descriptor(cfg, out_dir, mapping, sr_out)
    print(f"\n[{cfg.get('name','?')}] {len(mapping)} Samples -> {out_dir}")
    print(f"  Speicher: {total_in*2/1024:.0f} -> {total_out*2/1024:.0f} kB Audio "
          f"({100*total_out/max(total_in,1):.1f}%)")
    print(f"  Mapping: {os.path.join(out_dir,'_mapping.json')}")




# ===========================================================================
# Instrument-Deskriptor (fuer build_instrument.py) aus dem Mapping ableiten
# ===========================================================================
def parse_note_vel(name):
    m = re.match(r"(\d{2,3})-(\d{2,3})", name)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)

def derive_layer_mapping(tags_sorted, layers, thresholds=(48, 80)):
    """Ordnet jeder Engine-Velocity-Layer das verfuegbare Sample zu.

    thresholds sind die Velocity-GRENZEN, bei denen die Engine (noteOn) die
    Layer wechselt (mda: [48, 80] -> Layer0 0..48, Layer1 49..80, Layer2 81..127).
    NICHT die Sample-Velocities!

    Regel pro Layer: Sample, dessen Velocity in den Layer-Bereich faellt (bei
    mehreren das dem Center naechste); falls keines im Bereich liegt, das
    global naechste zum Center."""
    if not tags_sorted:
        return []
    if layers <= 1:
        return [tags_sorted[0]]
    t = list(thresholds)
    while len(t) < layers - 1:
        t.append(127)
    lo = [0] + [x + 1 for x in t[:layers-1]]           # Layer-Untergrenzen (exkl.)
    hi = t[:layers-1] + [127]                            # Layer-Obergrenzen (inkl.)
    centers = [(lo[i] + hi[i]) / 2.0 for i in range(layers)]
    out = []
    for i in range(layers):
        inside = [v for v in tags_sorted if lo[i] <= v <= hi[i]]
        pool = inside if inside else tags_sorted
        out.append(min(pool, key=lambda v: abs(v - centers[i])))
    return out

def write_instrument_descriptor(cfg, out_dir, mapping, sr_out):
    # Filename-Note + Velocity-Tag pro Sample
    by_note = {}            # file_note -> {vel_tag: sample_name}
    tags = set()
    for nm, e in mapping.items():
        fn, vel = parse_note_vel(nm)
        if fn is None:
            continue
        by_note.setdefault(fn, {})[vel] = nm
        tags.add(vel)
    tags_sorted = sorted(tags)
    eng = cfg.get("engine", {})
    layers = eng.get("velocity_layers", 3)
    thresh = eng.get("velocity_thresholds", [48, 80])
    if "layer_to_tag" in eng:                       # manuelle Vorgabe (Liste von Velocity-Tags)
        layer_to_tag = list(eng["layer_to_tag"])
        if len(layer_to_tag) != layers:
            raise SystemExit(f"layer_to_tag hat {len(layer_to_tag)} Eintraege, "
                             f"velocity_layers={layers}")
    else:
        layer_to_tag = derive_layer_mapping(tags_sorted, layers, thresh)

    regions = []
    for fn in sorted(by_note):
        avail = by_note[fn]                       # {vel_tag: sample_name}
        layers_list = []
        for intended in layer_to_tag:
            chosen = min(avail, key=lambda t: abs(t-intended))
            layers_list.append(avail[chosen])
        regions.append({"root": fn, "layers": layers_list})
    # high = naechste root - 1; letzte = 999
    for i, r in enumerate(regions):
        r["high"] = (regions[i+1]["root"] - 1) if i+1 < len(regions) else 999
        r.pop("high", None) if False else None
    for i, r in enumerate(regions):
        r["high"] = (regions[i+1]["root"] - 1) if i+1 < len(regions) else 999

    samples = {}
    for nm, e in mapping.items():
        fn, vel = parse_note_vel(nm)
        samples[nm] = {
            "file": os.path.join(out_dir, nm + ".wav"),
            "frames": e["frames"],
            "loop_start": e["loop_start"],
            "loop_len": e.get("loop_len"),
            "root_note": e["root_note"],
            "file_note": fn,
            "velocity_tag": vel,
        }

    desc = {
        "name": cfg.get("name", "instrument"),
        "data_symbol": re.sub(r"\W", "_", cfg.get("name", "instrument")) + "Data",
        "sample_rate": sr_out,
        "engine": {
            "velocity_layers": layers,
            "velocity_thresholds": cfg.get("engine", {}).get("velocity_thresholds", [48, 80]),
            "noteon_step": layers,           # k += noteon_step  (mda: 3)
        },
        "velocity_tags": tags_sorted,
        "layer_to_tag": layer_to_tag,
        "regions": regions,
        "samples": samples,
        "note": "root = Filename-Note (Sample-Sets mit smpl-Midi-Note-Differenzen "
                "werden auf die Filename-Note normiert). high = naechste root-1 (letzte=999). "
                "Fehlende Velocity-Layer werden auf den verfuegbaren Tag mit naehester Velocity gemappt.",
    }
    path = os.path.join(out_dir, "_instrument.json")
    with open(path, "w") as fh:
        json.dump(desc, fh, indent=2)
    print(f"  Instrument-Deskriptor: {path}  ({len(regions)} Regionen, "
          f"{layers} Layer, Tags {tags_sorted} -> {layer_to_tag})")

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config", help="JSON-Config-Datei oder Ordner mit Configs")
    ap.add_argument("--repo", default=None, help="Repo-Root (Default: auto)")
    args = ap.parse_args()
    repo = args.repo or os.path.abspath(
             os.path.join(os.path.dirname(__file__), "..", ".."))
    if os.path.isdir(args.config):
        cfgs = sorted(glob.glob(os.path.join(args.config, "*.json")))
    else:
        cfgs = [args.config]
    for c in cfgs:
        cfg = json.load(open(c))
        if "name" not in cfg:
            cfg["name"] = os.path.splitext(os.path.basename(c))[0]
        print(f"\n==== Instrument: {cfg['name']} ====")
        process_instrument(cfg, repo)

if __name__ == "__main__":
    main()
