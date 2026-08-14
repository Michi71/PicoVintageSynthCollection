#!/usr/bin/env python3
"""Generate the hardware probe sequence: a Standard MIDI File that makes a
real D-50 play every PCM sample bare, for recording and matching.

    python3 tools/d5_extract/d5_probe_midi.py [--structure N] [--mute N]
                                              [--device-id N] [--no-calibration]

Writes tools/d5_extract/out/d5_probe.mid. Play it into the D-50 (MIDI channel
1, memory protect OFF is not needed -- only the temporary area is written),
record the audio output dry, then run d5_match.py on the recording.

The sequence starts with a calibration block: PCM 1 (Marmba) played under
every Structure 1..7 and both partial-mute polarities. d5_match.py uses it to
verify that the chosen --structure/--mute really produce bare PCM playback;
if not, it names a combination that does, and this generator is re-run with
those values.

Patch design: everything neutral -- TVF open and static, TVA envelope
rectangular at full level, pitch keyfollow 0 (constant playback rate), all
LFO depths, chorus and reverb at zero. Only the temporary area is addressed,
nothing is written to the D-50's memory.
"""
import argparse
import os
import struct

TPQN = 480
TEMPO = 500000          # 0.5 s per beat -> 1 tick ~ 1.0417 ms

def ticks(seconds):
    return int(round(seconds * 1e6 / TEMPO * TPQN))


# ------------------------------------------------------------ SysEx building

def dt1(device, addr, data):
    body = list(addr) + list(data)
    checksum = (128 - sum(body) % 128) % 128
    return bytes([0xF0, 0x41, device, 0x14, 0x12] + body + [checksum, 0xF7])


def partial_block(pcm_no):
    """64-byte partial: bare PCM playback, everything else neutral."""
    b = [0] * 64
    b[0] = 36        # WG Pitch Coarse: C4
    b[1] = 50        # WG Pitch Fine: 0
    b[2] = 3         # WG Pitch Keyfollow: 0 (constant rate)
    b[6] = 1         # WG Waveform: sawtooth (irrelevant for PCM partials)
    b[7] = pcm_no    # WG PCM Wave No., 0-based
    b[8] = 50        # pulse width center
    b[13] = 100      # TVF cutoff fully open
    b[15] = 3        # TVF keyfollow 0
    b[17] = 7        # TVF bias level 0
    b[27] = 100      # TVF ENV L1
    b[28] = 100      # L2
    b[29] = 100      # L3
    b[30] = 100      # sustain (depth is 0 anyway)
    b[31] = 1        # end level 100
    b[35] = 100      # TVA level
    b[36] = 50       # TVA velocity range 0
    b[38] = 12       # TVA bias level 0 dB
    b[44] = 100      # TVA ENV L1
    b[45] = 100      # L2
    b[46] = 100      # L3
    b[47] = 100      # sustain
    b[48] = 1        # end level 100 (keep loops sounding until note-off)
    return b


def common_block(structure, mute):
    b = [0] * 64
    for i, c in enumerate("PROBE "):
        b[i] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-".index(c)
    b[10] = structure - 1   # Structure No. 1..7 -> 0..6
    for off in range(17, 22):
        b[off] = 50         # P-ENV levels at 0
    b[38] = 12              # low EQ gain 0 dB
    b[41] = 12              # high EQ gain 0 dB
    b[45] = 0               # chorus balance dry
    b[46] = mute            # partial mute
    b[47] = 50              # partial balance center
    return b


def patch_block():
    b = [0] * 64
    for i, c in enumerate("D5 PCM PROBE      "):
        b[i] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-".index(c)
    b[18] = 0        # key mode Whole
    b[19] = 30       # split point (unused)
    b[24] = 50       # upper fine tune 0
    b[25] = 50       # lower fine tune 0
    b[27] = 12       # aftertouch bend 0
    b[30] = 0        # reverb type 1
    b[31] = 0        # reverb balance dry
    b[32] = 100      # total volume
    b[33] = 50       # tone balance
    return b


def setup_messages(device, pcm_no, structure, mute):
    """DT1s that configure the temporary area for one bare PCM sound."""
    p = partial_block(pcm_no)
    return [
        dt1(device, (0x00, 0x00, 0x00), p),               # upper partial 1
        dt1(device, (0x00, 0x00, 0x40), p),               # upper partial 2
        dt1(device, (0x00, 0x01, 0x00), common_block(structure, mute)),
        dt1(device, (0x00, 0x03, 0x00), patch_block()),
    ]


# --------------------------------------------------------------- SMF writing

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def render_track(events):
    out = bytearray()
    last = 0
    for at, data in sorted(events, key=lambda e: e[0]):
        out += vlq(at - last)
        last = at
        out += data
    out += vlq(0) + b"\xff\x2f\x00"
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--structure", type=int, default=3, help="Structure No. 1..7 for the main run")
    ap.add_argument("--mute", type=int, default=0, help="partial mute value 0..3 for the main run")
    ap.add_argument("--device-id", type=int, default=0)
    ap.add_argument("--no-calibration", action="store_true")
    ap.add_argument("--out", default="tools/d5_extract/out/d5_probe.mid")
    args = ap.parse_args()

    events = []
    at = [ticks(2.0)]      # lead-in silence

    def emit(data):
        events.append((at[0], data))

    def wait(seconds):
        at[0] += ticks(seconds)

    def sysex(msg):
        emit(b"\xf0" + vlq(len(msg) - 1) + msg[1:])
        wait(0.03)          # >= 20 ms between exclusive messages

    def note(hold):
        emit(bytes([0x90, 60, 127]))
        wait(hold)
        emit(bytes([0x80, 60, 0]))

    emit(b"\xff\x51\x03" + struct.pack(">I", TEMPO)[1:])

    if not args.no_calibration:
        for structure in range(1, 8):
            for mute in (1, 2):
                for m in setup_messages(args.device_id, 0, structure, mute):
                    sysex(m)
                wait(0.15)
                note(1.2)
                wait(0.6)
        wait(1.5)           # longer gap separates calibration from main run

    for pcm in range(100):
        for m in setup_messages(args.device_id, pcm, args.structure, args.mute):
            sysex(m)
        wait(0.15)
        note(2.2)
        wait(0.8)

    data = render_track(events)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQN))
        f.write(b"MTrk" + struct.pack(">I", len(data)) + data)
    total = at[0] * TEMPO / 1e6 / TPQN
    print(f"wrote {args.out}: {'calibration + ' if not args.no_calibration else ''}"
          f"100 samples, {total/60:.1f} min, structure {args.structure}, mute {args.mute}")
    print("Record the D-50 output dry while playing this file, then run:")
    print("  python3 tools/d5_extract/d5_match.py <romdir> <recording.wav>")


if __name__ == "__main__":
    main()
