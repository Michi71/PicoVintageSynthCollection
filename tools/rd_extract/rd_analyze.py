# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
import json
import sys
from collections import defaultdict


def main():
    infile = sys.argv[1]
    outfile = sys.argv[2]

    records = []
    with open(infile, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            records.append(json.loads(line))

    groups = defaultdict(list)
    for r in records:
        key = (r["patch"], r["note"], r["vel"])
        groups[key].append(r)

    output = {}

    for (patch, note, vel), group_records in groups.items():
        noteoff_t = group_records[0].get("noteoff_t")

        part_groups = defaultdict(list)
        for r in group_records:
            part_groups[(r["voice"], r["part"])].append(r)

        parts = []
        for (voice, part), part_records in part_groups.items():
            part_records.sort(key=lambda x: x["t"])

            # First pitch write (field 0 or 1). Recycled voices get kill
            # writes (env_dest=0) BEFORE their new registers, so the old
            # "before first env_dest" heuristic dropped most parts.
            t_first_pitch = None
            for r in part_records:
                if r["field"] in (0, 1):
                    t_first_pitch = r["t"]
                    break

            # Static registers: LAST value over the whole window (pitch/wave
            # never change mid-note). Field 6 (flags) is voice-level on the
            # chip (always lands on part 0) -- still record last value here.
            static = {}
            for r in part_records:
                if r["field"] in (0, 1, 2, 3, 6, 7):
                    static[r["field"]] = r["val"]

            flags = static.get(6)
            env_offset = static.get(7)
            wave_loop = static.get(2)
            wave_high = static.get(3)
            pitch_hi = static.get(0)
            pitch_lo = static.get(1)

            if pitch_hi is not None and pitch_lo is not None:
                pitch_lut = (pitch_hi << 8) | pitch_lo
            else:
                pitch_lut = None
                if t_first_pitch is not None:
                    sys.stderr.write(
                        f"Warning: Part {part} voice {voice} patch {patch} "
                        f"note {note} vel {vel}: incomplete pitch\n"
                    )
                # else: pure residual part of the previous note -- silently
                # skipped later (no pitch), by design.

            # Sweep captures are single-note windows (4s+2s spacing, no voice
            # recycling), so leading env writes cannot be a recycled voice's
            # kill-tail -- every early env write belongs to THIS note. The
            # firmware emits a pre-attack env pulse BEFORE the first pitch
            # write (e.g. p3 n105: dest=31 then dest=0 freeze, pitch writes
            # ~t=13..43, real attack at t=44). Filtering env writes from the
            # first pitch write onward drops that pulse, losing the attack
            # head start (measured: p3 n105 r=0.23, attack ~40% too quiet,
            # decay skewed). So keep ALL env writes of the window for parts
            # that have a pitch; parts without pitch keep empty lists.
            if t_first_pitch is None:
                dest_writes = []
                speed_writes = []
            else:
                dest_writes = [(r["t"], r["val"]) for r in part_records
                               if r["field"] == 4]
                speed_writes = [(r["t"], r["val"]) for r in part_records
                                if r["field"] == 5]

            used_speed = [False] * len(speed_writes)
            segments = []
            release_segments = []

            for dt, dval in dest_writes:
                best_idx = None
                best_diff = None
                for i, (st, sval) in enumerate(speed_writes):
                    if used_speed[i]:
                        continue
                    diff = abs(st - dt)
                    if diff <= 5:
                        if best_diff is None or diff < best_diff:
                            best_diff = diff
                            best_idx = i
                if best_idx is not None:
                    st, sval = speed_writes[best_idx]
                    used_speed[best_idx] = True
                    seg = {"t": dt, "dest": dval, "speed": sval}
                    if noteoff_t is not None and dt >= noteoff_t:
                        seg["t"] = dt - noteoff_t
                        release_segments.append(seg)
                    else:
                        segments.append(seg)
                else:
                    sys.stderr.write(
                        f"Warning: dest write t={dt} without speed partner "
                        f"patch {patch} note {note} vel {vel} "
                        f"part {part} voice {voice}\n"
                    )

            for i, (st, sval) in enumerate(speed_writes):
                if not used_speed[i]:
                    sys.stderr.write(
                        f"Warning: speed write t={st} without dest partner "
                        f"patch {patch} note {note} vel {vel} "
                        f"part {part} voice {voice}\n"
                    )

            parts.append({
                "part": part,
                "voice": voice,
                "flags": flags,
                "env_offset": env_offset,
                "pitch_lut": pitch_lut,
                "wave_loop": wave_loop,
                "wave_high": wave_high,
                "segments": segments,
                "release_segments": release_segments,
            })

        voices = sorted(set(p["voice"] for p in parts))
        if len(voices) > 1:
            sys.stderr.write(
                f"Info: mehrere Voices {voices} in patch {patch} "
                f"note {note} vel {vel}\n"
            )

        parts.sort(key=lambda x: (x["part"], x["voice"]))

        patch_key = str(patch)
        if patch_key not in output:
            output[patch_key] = {}
        output[patch_key][f"{note}/{vel}"] = {
            "noteoff_t": noteoff_t,
            "parts": parts,
        }

        seg_counts = [len(p["segments"]) + len(p["release_segments"]) for p in parts]
        min_seg = min(seg_counts) if seg_counts else 0
        max_seg = max(seg_counts) if seg_counts else 0
        pitches = [p["pitch_lut"] for p in parts if p["pitch_lut"] is not None]
        pitch_span = (min(pitches), max(pitches)) if pitches else None
        print(
            f"patch={patch} note={note} vel={vel}: "
            f"parts={len(parts)} segments={min_seg}/{max_seg} "
            f"pitch_span={pitch_span}"
        )

    with open(outfile, "w") as f:
        json.dump(output, f, indent=2)


if __name__ == "__main__":
    main()
