#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""ob_fxp_to_presets.py - convert OB-Xf factory patches to ob_presets.h.

Reads the .fxp files of an OB-Xf checkout (VST2 FPCh chunks with the state as
an XML attribute blob), maps every parameter onto PicoFaceOB's ob_params.h set
with the exact upstream control laws, and writes the categorised preset table.

Auto filter: a patch is skipped when it leans on something the port cannot
express -
  * both LFOs AUDIBLY active (effective depth past the thresholds below; the
    port has one global LFO, so an LFO2-only patch is merged onto it instead),
  * a master Tune away from centre,
  * a Transpose that cannot be folded into the oscillator semitone range.
Everything else translates: env->pitch/PW (invert folded into the bipolar
sign), PW offset, noise colour, LFO->volume, the bandpass blend, the Xpander
modes, tempo-synced LFO rates (fixed at 120 BPM), and Transpose (folded into
both osc pitches).

Usage:
    git clone --depth 1 https://github.com/surge-synthesizer/OB-Xf /tmp/obxf
    tools/ob_fxp_to_presets/ob_fxp_to_presets.py \
        "/tmp/obxf/assets/installer/Surge Synth Team/OB-Xf/Patches" \
        [-o instruments/PicoFaceOB/include/ob_presets.h] [-v]
"""

import argparse
import glob
import html
import math
import os
import re
import struct
import sys
import unicodedata

# ---------------------------------------------------------------------------
# ob_params.h order - MUST match the enum. One line per parameter.
# ---------------------------------------------------------------------------
PARAM_ORDER = [
    "OSC1_MIX", "OSC2_MIX", "OSC2_DETUNE", "OSC1_SAW", "OSC1_PULSE",
    "OSC2_SAW", "OSC2_PULSE", "PULSE_WIDTH", "PW_OFFSET", "OSC_SYNC",
    "CROSSMOD", "NOISE_MIX", "NOISE_COLOR", "RINGMOD_MIX", "OSC1_PITCH",
    "OSC2_PITCH", "BRIGHTNESS", "CUTOFF", "RESONANCE", "FOUR_POLE",
    "FILTER_ENV_AMT", "FILTER_KEYTRACK", "MULTIMODE", "PUSH_2POLE",
    "BP_BLEND", "XPANDER", "XPANDER_MODE", "FILT_ATTACK", "FILT_DECAY", "FILT_SUSTAIN",
    "FILT_RELEASE", "ENV_TO_PITCH", "ENV_PITCH_BOTH", "ENV_TO_PW",
    "ENV_PW_BOTH", "AMP_ATTACK", "AMP_DECAY", "AMP_SUSTAIN", "AMP_RELEASE",
    "LFO_RATE", "LFO_WAVE", "LFO_TO_PITCH", "LFO_TO_PW", "LFO_TO_CUTOFF",
    "LFO_TO_VOL", "PORTAMENTO", "VOICE_SLOP", "VOLUME", "BEND_RANGE",
]

# ObxfPort.h's tempo-synced rate table; index = round(norm * 20).
SYNCED_RATES = [1/12, 1/8, 1/6, 3/16, 1/4, 1/3, 3/8, 1/2, 2/3, 3/4,
                1.0, 3/2, 4/3, 2.0, 8/3, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0]

NAME_MAX = 15  # display width of the 128 px OLED

# An LFO counts as audible when any route's EFFECTIVE depth passes these -
# raw parameter values flag 3-cent wobbles as "active" and would throw
# usable patches out of the two-LFO rule.
AUDIBLE_PITCH_ST = 0.15   # semitones
AUDIBLE_CUTOFF_ST = 0.30  # semitones
AUDIBLE_PW = 0.03         # pulse width fraction
AUDIBLE_VOL = 0.03        # tremolo fraction


# --- the upstream control laws, exactly as in SynthEngine.h ----------------

def logsc(p, mn, mx, roll=19.0):
    return ((2.0 ** (p * math.log2(roll + 1.0)) - 1.0) / roll) * (mx - mn) + mn


def invlogsc(y, mn, mx, roll=19.0):
    t = (y - mn) / (mx - mn) * roll + 1.0
    if t < 1.0:
        return 0.0
    return math.log2(t) / math.log2(roll + 1.0)


def dlogsc(v):  # the amt1 double-log curve, 0..1 -> 0..60 semitones
    return logsc(logsc(v, 0.0, 1.0, 60.0), 0.0, 60.0, 10.0)


def inv_dlogsc(y):
    return invlogsc(invlogsc(y, 0.0, 60.0, 10.0), 0.0, 1.0, 60.0)


def remap(v):  # LFO route parameter: 0 -> 0, 0.5 -> +1, 1 -> -1
    return 2.0 * v if v <= 0.5 else -(2.0 * v - 1.0)


# --- .fxp reading -----------------------------------------------------------

def read_fxp(path):
    d = open(path, "rb").read()
    if len(d) < 68 or d[0:4] != b"CcnK" or d[8:12] != b"FPCh":
        return None
    (chunk_size,) = struct.unpack(">i", d[56:60])
    xml = d[68:60 + chunk_size].decode("utf-8", errors="replace")
    return dict(re.findall(r'([A-Za-z0-9_]+)="([^"]*)"', xml))


def clean_name(raw):
    # XML entities back to text, ASCII-fold for the OLED font, then clip to
    # the display width.
    s = html.unescape(raw)
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    s = "".join(c if 32 <= ord(c) < 127 else "?" for c in s).strip()
    s = s.replace("\\", "").replace('"', "'")
    return (s or "Unnamed")[:NAME_MAX]


# --- conversion of one patch ------------------------------------------------

class Skip(Exception):
    pass


def convert(attrs):
    def f(key, default=0.0):
        try:
            return float(attrs.get(key, default))
        except ValueError:
            return default

    def lfo_active(n):
        depth1 = dlogsc(f(f"LFO{n}ModAmount1"))
        amt2 = f(f"LFO{n}ModAmount2")
        eff_pitch = max(abs(remap(f(f"LFO{n}ToOsc1Pitch"))),
                        abs(remap(f(f"LFO{n}ToOsc2Pitch")))) * depth1
        eff_cut = abs(remap(f(f"LFO{n}ToFilterCutoff"))) * depth1
        eff_pw = max(abs(remap(f(f"LFO{n}ToOsc1PW"))),
                     abs(remap(f(f"LFO{n}ToOsc2PW")))) * amt2
        eff_vol = abs(remap(f(f"LFO{n}ToVolume"))) * amt2
        return (eff_pitch > AUDIBLE_PITCH_ST or eff_cut > AUDIBLE_CUTOFF_ST or
                eff_pw > AUDIBLE_PW or eff_vol > AUDIBLE_VOL)

    # ---- the auto filter ----
    if abs(f("Tune", 0.5) - 0.5) > 0.02:
        raise Skip("master tune off centre")

    l1, l2 = lfo_active(1), lfo_active(2)
    if l1 and l2:
        raise Skip("both LFOs active")
    src = 2 if (l2 and not l1) else 1

    transpose = round((f("Transpose", 0.5) * 2.0 - 1.0) * 24.0)
    p1 = round(f("Osc1Pitch") * 48.0) + transpose
    p2 = round(f("Osc2Pitch") * 48.0) + transpose
    if not (0 <= p1 <= 48 and 0 <= p2 <= 48):
        raise Skip("transpose outside the semitone range")

    # ---- LFO block (from whichever LFO carries the patch) ----
    if f(f"LFO{src}TempoSync") > 0.5:
        idx = round(min(max(f(f"LFO{src}Rate"), 0.0), 1.0) * (len(SYNCED_RATES) - 1))
        rate = invlogsc(2.0 * SYNCED_RATES[idx], 0.0, 250.0, 3775.0)  # at 120 BPM
    else:
        rate = f(f"LFO{src}Rate")

    blends = [2.0 * f(f"LFO{src}Wave1") - 1.0,
              2.0 * f(f"LFO{src}Wave2") - 1.0,
              2.0 * f(f"LFO{src}Wave3") - 1.0]
    cands = [(abs(blends[0]), 0 if blends[0] < 0 else 1),
             (abs(blends[1]), 2 if blends[1] > 0 else 3),
             (abs(blends[2]), 4)]
    mag, pos = max(cands)
    wave = 0.0 if mag < 0.05 else pos / 4.0

    amt1, amt2 = f(f"LFO{src}ModAmount1"), f(f"LFO{src}ModAmount2")
    r_pitch = max(abs(remap(f(f"LFO{src}ToOsc1Pitch"))),
                  abs(remap(f(f"LFO{src}ToOsc2Pitch"))))
    r_cut = abs(remap(f(f"LFO{src}ToFilterCutoff")))
    r_pw = max(abs(remap(f(f"LFO{src}ToOsc1PW"))),
               abs(remap(f(f"LFO{src}ToOsc2PW"))))
    r_vol = abs(remap(f(f"LFO{src}ToVolume")))
    lfo_pitch = inv_dlogsc(r_pitch * dlogsc(amt1)) if r_pitch * amt1 > 1e-4 else 0.0
    lfo_cut = inv_dlogsc(r_cut * dlogsc(amt1)) if r_cut * amt1 > 1e-4 else 0.0
    lfo_pw = r_pw * amt2   # both sides linear, so the route gain multiplies through
    lfo_vol = r_vol * amt2

    # ---- envelope targets: invert switches fold into the bipolar signs ----
    f_inv = f("FilterEnvInvert") > 0.5
    env_amt = 0.5 + (-0.5 if f_inv else 0.5) * f("FilterEnvAmount")
    s_pitch = -1.0 if (f_inv != (f("EnvToPitchInvert") > 0.5)) else 1.0
    env_pitch = 0.5 + 0.5 * s_pitch * f("EnvToPitchAmount")
    s_pw = -1.0 if (f_inv != (f("EnvToPWInvert") > 0.5)) else 1.0
    env_pw = 0.5 + 0.5 * s_pw * f("EnvToPWAmount")

    v = {
        "OSC1_MIX": f("Osc1Mix"), "OSC2_MIX": f("Osc2Mix"),
        "OSC2_DETUNE": f("Osc2Detune"),
        "OSC1_SAW": f("Osc1SawWave"), "OSC1_PULSE": f("Osc1PulseWave"),
        "OSC2_SAW": f("Osc2SawWave"), "OSC2_PULSE": f("Osc2PulseWave"),
        "PULSE_WIDTH": f("OscPW"), "PW_OFFSET": f("Osc2PWOffset"),
        "OSC_SYNC": f("OscSync"), "CROSSMOD": f("OscCrossmod"),
        "NOISE_MIX": f("NoiseMix"), "NOISE_COLOR": f("NoiseColor"),
        "RINGMOD_MIX": f("RingModMix"),
        "OSC1_PITCH": p1 / 48.0, "OSC2_PITCH": p2 / 48.0,
        "BRIGHTNESS": f("OscBrightness"),
        "CUTOFF": f("FilterCutoff"), "RESONANCE": f("FilterResonance"),
        "FOUR_POLE": f("Filter4PoleMode"), "FILTER_ENV_AMT": env_amt,
        "FILTER_KEYTRACK": f("FilterKeyFollow"), "MULTIMODE": f("FilterMode"),
        "PUSH_2POLE": f("Filter2PolePush"), "BP_BLEND": f("Filter2PoleBPBlend"),
        "XPANDER": f("Filter4PoleXpander"), "XPANDER_MODE": f("FilterXpanderMode"),
        "FILT_ATTACK": f("FilterEnvAttack"), "FILT_DECAY": f("FilterEnvDecay"),
        "FILT_SUSTAIN": f("FilterEnvSustain"), "FILT_RELEASE": f("FilterEnvRelease"),
        "ENV_TO_PITCH": env_pitch, "ENV_PITCH_BOTH": f("EnvToPitchBothOscs", 1.0),
        "ENV_TO_PW": env_pw, "ENV_PW_BOTH": f("EnvToPWBothOscs", 1.0),
        "AMP_ATTACK": f("AmpEnvAttack"), "AMP_DECAY": f("AmpEnvDecay"),
        "AMP_SUSTAIN": f("AmpEnvSustain"), "AMP_RELEASE": f("AmpEnvRelease"),
        "LFO_RATE": rate, "LFO_WAVE": wave,
        "LFO_TO_PITCH": lfo_pitch, "LFO_TO_PW": lfo_pw,
        "LFO_TO_CUTOFF": lfo_cut, "LFO_TO_VOL": lfo_vol,
        "PORTAMENTO": f("Portamento"),
        "VOICE_SLOP": f("FilterSlop"),  # the rule the first twelve used
        "VOLUME": f("Volume"),
        "BEND_RANGE": 0.0,  # performance switch, never part of a program
    }
    vals = [min(max(v[k], 0.0), 1.0) for k in PARAM_ORDER]
    return vals


# --- header emission --------------------------------------------------------

HEADER = """\
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ob_presets.h - factory patches converted from OB-Xf.
//
// GENERATED by tools/ob_fxp_to_presets/ob_fxp_to_presets.py - do not edit by
// hand, rerun the tool. Source: assets/installer/Surge Synth Team/OB-Xf/
// Patches in https://github.com/surge-synthesizer/OB-Xf (patch data largely
// CC0; per-patch author and licence sit in the .fxp files).
//
// {stats}
// Patches that lean on what the port cannot express (two audibly active
// LFOs, off-centre master tune, out-of-range transpose) are skipped by the
// converter; see its --verbose listing.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).
#ifndef OB_PRESETS_H
#define OB_PRESETS_H

#include "ob_params.h"

struct ObPreset
{{
    const char* name;
    float       value[OB_PARAM_COUNT];
}};

// The presets are stored flat and sorted; a category is a window [first,
// first + count) into that table.
struct ObPresetCategory
{{
    const char* name;
    uint16_t    first;
    uint8_t     count;
}};

"""

FOOTER = """\

inline constexpr int OB_NPRESETS = (int)(sizeof(obPresets) / sizeof(obPresets[0]));
inline constexpr int OB_NPRESET_CATS =
    (int)(sizeof(obPresetCategories) / sizeof(obPresetCategories[0]));
// Sizes the UI's entry-name buffer (plus its "<< BACK" slot).
inline constexpr int OB_MAX_CAT_PRESETS = {maxcat};

#endif // OB_PRESETS_H
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("patches", help="OB-Xf Patches directory (one subdir per category)")
    ap.add_argument("-o", "--output",
                    default=os.path.join(os.path.dirname(__file__), "..", "..",
                                         "instruments", "PicoFaceOB", "include",
                                         "ob_presets.h"))
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every skipped patch with its reason")
    args = ap.parse_args()

    categories = []  # (name, [(display_name, vals), ...])
    skipped = {}
    total = 0
    for catdir in sorted(os.listdir(args.patches)):
        full = os.path.join(args.patches, catdir)
        if not os.path.isdir(full):
            continue
        entries = []
        for fxp in sorted(glob.glob(os.path.join(full, "*.fxp"))):
            total += 1
            attrs = read_fxp(fxp)
            stem = os.path.splitext(os.path.basename(fxp))[0]
            if attrs is None:
                skipped.setdefault("not an FPCh chunk file", []).append(stem)
                continue
            try:
                vals = convert(attrs)
            except Skip as e:
                skipped.setdefault(str(e), []).append(f"{catdir}/{stem}")
                continue
            entries.append((clean_name(attrs.get("programName", stem)), vals))
        if entries:
            categories.append((catdir, entries))

    kept = sum(len(e) for _, e in categories)
    maxcat = max(len(e) for _, e in categories)
    stats = (f"{kept} of {total} factory patches in {len(categories)} categories "
             f"(largest: {maxcat}); {total - kept} skipped by the auto filter.")

    lines = [HEADER.format(stats=stats)]
    lines.append("inline const ObPreset obPresets[] = {\n")
    for cat, entries in categories:
        lines.append(f"    // --- {cat} ({len(entries)}) ---\n")
        for name, vals in entries:
            row = ", ".join(f"{x:.4f}f" for x in vals)
            lines.append(f'    {{"{name}", {{{row}}}}},\n')
    lines.append("};\n\n")
    lines.append("inline const ObPresetCategory obPresetCategories[] = {\n")
    first = 0
    for cat, entries in categories:
        lines.append(f'    {{"{cat}", {first}, {len(entries)}}},\n')
        first += len(entries)
    lines.append("};\n")
    lines.append(FOOTER.format(maxcat=maxcat))

    with open(os.path.abspath(args.output), "w") as f:
        f.write("".join(lines))

    print(stats)
    for reason, names in sorted(skipped.items()):
        print(f"  skipped ({reason}): {len(names)}")
        if args.verbose:
            for n in names:
                print(f"    {n}")
    print(f"geschrieben: {os.path.abspath(args.output)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
