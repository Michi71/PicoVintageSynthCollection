import sys
import json
import struct


def warn(msg):
    sys.stderr.write("WARNING: " + msg + "\n")


def main():
    if len(sys.argv) < 4:
        sys.stderr.write("Usage: rd_pack.py <input.json> <output.rdp> <bank>\n")
        sys.exit(1)

    in_path = sys.argv[1]
    out_path = sys.argv[2]
    bank = int(sys.argv[3]) & 0xFF

    with open(in_path, "r") as f:
        data = json.load(f)

    keys = list(data.keys())
    if len(keys) != 1:
        sys.stderr.write("ERROR: expected single top-level key, got %d\n" % len(keys))
        sys.exit(1)
    patch_name = keys[0]
    try:
        patch_id = int(patch_name) & 0xFF
    except ValueError:
        patch_id = 0
        warn("patch_id not parseable from '%s', using 0" % patch_name)

    entries_data = data[patch_name]

    entries = []
    for key, val in entries_data.items():
        parts = key.split("/")
        note = int(parts[0]) & 0xFF
        vel = int(parts[1]) & 0xFF
        entries.append((note, vel, val))

    entries.sort(key=lambda e: (e[0], e[1]))
    entry_count = len(entries)

    out = bytearray()
    out += b"RDP2"
    out += struct.pack("<I", 1)
    out += struct.pack("<B", patch_id)
    out += struct.pack("<B", bank)
    out += struct.pack("<H", entry_count)

    total_parts = 0
    total_segments = 0

    for note, vel, val in entries:
        parts_raw = val.get("parts", []) or []
        parts = []
        for p in parts_raw:
            if p.get("pitch_lut") is None:
                warn("note=%d vel=%d: part has pitch_lut=None, skipping" % (note, vel))
                continue
            parts.append(p)

        part_count = len(parts)
        out += struct.pack("<B", note)
        out += struct.pack("<B", vel)
        out += struct.pack("<B", part_count & 0xFF)

        for p in parts:
            flags = p.get("flags")
            if flags is None:
                warn("note=%d vel=%d: flags is None, using 0" % (note, vel))
                flags = 0
            env_offset = p.get("env_offset")
            if env_offset is None:
                warn("note=%d vel=%d: env_offset is None, using 0" % (note, vel))
                env_offset = 0
            pitch_lut = p.get("pitch_lut")
            wave_loop = p.get("wave_loop")
            if wave_loop is None:
                warn("note=%d vel=%d: wave_loop is None, using 0" % (note, vel))
                wave_loop = 0
            wave_high = p.get("wave_high")
            if wave_high is None:
                warn("note=%d vel=%d: wave_high is None, using 0" % (note, vel))
                wave_high = 0

            segments = p.get("segments", []) or []
            release_segments = p.get("release_segments", []) or []
            nseg = len(segments)
            nrel = len(release_segments)

            out += struct.pack("<B", flags & 0xFF)
            out += struct.pack("<B", env_offset & 0xFF)
            out += struct.pack("<H", pitch_lut & 0xFFFF)
            out += struct.pack("<B", wave_loop & 0xFF)
            out += struct.pack("<B", wave_high & 0xFF)
            out += struct.pack("<B", nseg & 0xFF)
            out += struct.pack("<B", nrel & 0xFF)

            for s in segments:
                t = s.get("t", 0) & 0xFFFFFFFF
                dest = s.get("dest", 0) & 0xFF
                speed = s.get("speed", 0) & 0xFF
                out += struct.pack("<I", t)
                out += struct.pack("<B", dest)
                out += struct.pack("<B", speed)

            for s in release_segments:
                t = s.get("t", 0) & 0xFFFFFFFF
                dest = s.get("dest", 0) & 0xFF
                speed = s.get("speed", 0) & 0xFF
                out += struct.pack("<I", t)
                out += struct.pack("<B", dest)
                out += struct.pack("<B", speed)

            total_parts += 1
            total_segments += nseg + nrel

    with open(out_path, "wb") as f:
        f.write(out)

    file_size = len(out)
    print("entries=%d parts=%d segments=%d size=%d" % (entry_count, total_parts, total_segments, file_size))


if __name__ == "__main__":
    main()
