#!/usr/bin/env python3
"""d5_demo_midi.py -- a five-minute tour of the D-50 bank as a MIDI file.

Selects patches over CC0 + program change and plays each a phrase that suits
it, chosen so that between them they show what is peculiar about this machine:
the PCM attack over a synth sustain, the combination waves, ring modulation,
the pulse width that only contracts above the middle, the tone-global LFOs and
their key sync, aftertouch on three destinations plus its own bend, the reverb
family that is not reverb at all (cross delays, gates), the solo key modes,
and portamento, which no factory patch switches on -- so the file switches it
on itself.

Everything is written at a fixed 120 BPM / 480 ppq, i.e. 960 ticks per second,
and composed in seconds; there is no tempo map to keep in step.
"""

import struct, sys

PPQ = 480
TPS = PPQ * 2                      # ticks per second at 120 BPM
CH = 0                             # channel 1; the instrument boots in Omni

# ---------------------------------------------------------------- chords ----
QUAL = {
    'maj':  (0, 4, 7),
    'min':  (0, 3, 7),
    'maj7': (0, 4, 7, 11),
    'min7': (0, 3, 7, 10),
    'dom7': (0, 4, 7, 10),
    'sus4': (0, 5, 7),
    'add9': (0, 4, 7, 14),
    'min9': (0, 3, 7, 14),
    '5':    (0, 7),
}

def chord(root, qual, oct_shift=0, drop=False):
    ns = [root + i + 12 * oct_shift for i in QUAL[qual]]
    if drop and len(ns) > 2:        # root an octave down: room in the middle
        ns = [ns[0] - 12] + ns[1:]
    return ns

# progressions as (root, quality); roots around C4 = 60
P_CINE   = [(57, 'min'),  (53, 'maj'),  (48, 'maj'),  (55, 'maj')]     # Am F C G
P_JAZZ   = [(50, 'min7'), (55, 'dom7'), (48, 'maj7'), (57, 'min7')]    # Dm7 G7 Cmaj7 Am7
P_OPEN   = [(48, 'maj7'), (57, 'min7'), (53, 'maj7'), (55, 'add9')]    # Cmaj9 Am9 Fmaj7 G
P_FILM   = [(52, 'min'),  (48, 'maj7'), (55, 'maj'),  (50, 'min')]     # Em Cmaj7 G Dm
P_FALL   = [(53, 'maj7'), (52, 'min7'), (50, 'min7'), (48, 'maj7')]    # Fmaj7 Em7 Dm7 Cmaj7
P_MODAL  = [(50, 'sus4'), (50, 'min'),  (55, 'sus4'), (55, 'maj')]     # Dsus D Gsus G

A_MINOR  = [57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76]
D_DORIAN = [50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69]


# ------------------------------------------------------------- the events ---
class Song:
    def __init__(self):
        self.ev = []               # (tick, order, bytes)
        self.seq = 0

    def _add(self, t, prio, data):
        self.seq += 1
        self.ev.append((max(0, int(round(t * TPS))), prio, self.seq, data))

    def note(self, t, dur, n, vel=90):
        n = int(n)
        if not 0 <= n <= 127:
            raise ValueError('note out of range: %d' % n)
        self._add(t, 2, bytes([0x90 | CH, n, int(vel)]))
        self._add(t + dur, 1, bytes([0x80 | CH, n, 64]))

    def cc(self, t, num, val):
        self._add(t, 0, bytes([0xB0 | CH, num, max(0, min(127, int(val)))]))

    def prog(self, t, bank, slot):
        self._add(t, 0, bytes([0xB0 | CH, 0, bank]))
        self._add(t, 0, bytes([0xC0 | CH, slot]))

    def bend(self, t, semis, rng):
        """rng = the patch's own bender range, so semis land where intended."""
        v = 8192 + int(8191 * (semis / float(rng))) if rng else 8192
        v = max(0, min(16383, v))
        self._add(t, 0, bytes([0xE0 | CH, v & 0x7F, (v >> 7) & 0x7F]))

    def press(self, t, val):
        self._add(t, 0, bytes([0xD0 | CH, max(0, min(127, int(val)))]))

    def marker(self, t, text):
        b = text.encode('ascii', 'replace')
        self._add(t, 0, b'\xff\x06' + vlq(len(b)) + b)

    # --- shapes over time -------------------------------------------------
    def ramp(self, t0, t1, fn, v0, v1, steps=24):
        for k in range(steps + 1):
            f = k / float(steps)
            fn(t0 + (t1 - t0) * f, v0 + (v1 - v0) * f)

    def swell(self, t0, t1, fn, peak, base=0):
        mid = (t0 + t1) / 2.0
        self.ramp(t0, mid, fn, base, peak, 16)
        self.ramp(mid, t1, fn, peak, base, 16)


def vlq(n):
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def write_smf(song, path, title):
    trk = bytearray()
    tb = title.encode('ascii', 'replace')
    trk += b'\x00\xff\x03' + vlq(len(tb)) + tb
    trk += b'\x00\xff\x51\x03' + struct.pack('>I', 500000)[1:]     # 120 BPM
    trk += b'\x00\xff\x58\x04\x04\x02\x18\x08'                     # 4/4
    last = 0
    for tick, _prio, _seq, data in sorted(song.ev, key=lambda e: (e[0], e[1], e[2])):
        trk += vlq(tick - last) + data
        last = tick
    trk += vlq(0) + b'\xff\x2f\x00'
    hdr = b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQ)
    with open(path, 'wb') as f:
        f.write(hdr + b'MTrk' + struct.pack('>I', len(trk)) + bytes(trk))
    return last / float(TPS)


# ------------------------------------------------------------- the styles ---
# Every style gets (song, t0, length, patch-info) and returns nothing; it may
# use the whole length, and should leave the last half second free of new
# attacks so the tail is audible before the next patch is selected.

def st_pad(s, t0, L, pi):
    prog, oct_, vel = pi.get('prog', P_CINE), pi.get('oct', 0), pi.get('vel', 78)
    n = len(prog)
    step = (L - 1.2) / n
    for k, (r, q) in enumerate(prog):
        t = t0 + k * step
        for note in chord(r, q, oct_, drop=True):
            s.note(t, step * 1.35, note, vel + (6 if k % 2 else 0))
    s.swell(t0 + step * 0.5, t0 + L - 1.0, lambda t, v: s.cc(t, 1, v), pi.get('mod', 40))


def st_stab(s, t0, L, pi):
    prog, oct_ = pi.get('prog', P_CINE), pi.get('oct', 0)
    beat = pi.get('beat', 0.5)
    pat = [0, 1.5, 2, 3, 3.5]                       # in beats, per chord
    step = (L - 1.0) / len(prog)
    for k, (r, q) in enumerate(prog):
        base = t0 + k * step
        for j, b in enumerate(pat):
            t = base + b * beat
            if t > base + step - 0.1:
                continue
            for note in chord(r, q, oct_):
                s.note(t, beat * pi.get('gate', 0.55), note,
                       pi.get('vel', 104) if j == 0 else pi.get('vel', 104) - 20)
    if pi.get('bendto'):                            # the wheel over the last chord
        rng = pi.get('range', 2)
        t1 = t0 + step * (len(prog) - 1)
        s.ramp(t1, t1 + step * 0.55, lambda t, v: s.bend(t, v, rng), 0, pi['bendto'], 22)
        s.ramp(t1 + step * 0.6, t1 + step * 0.9, lambda t, v: s.bend(t, v, rng), pi['bendto'], 0, 14)
        s.bend(t0 + L - 0.3, 0, rng)


def st_arp(s, t0, L, pi):
    prog, oct_ = pi.get('prog', P_OPEN), pi.get('oct', 0)
    step = (L - 1.2) / len(prog)
    d = pi.get('rate', 0.16)
    for k, (r, q) in enumerate(prog):
        ns = chord(r, q, oct_)
        ns = ns + [n + 12 for n in ns]
        base = t0 + k * step
        j = 0
        t = base
        while t < base + step - d:
            idx = j % (2 * len(ns) - 2)
            if idx >= len(ns):
                idx = 2 * len(ns) - 2 - idx
            s.note(t, d * pi.get('gate', 2.5), ns[idx], 96 if j % 4 == 0 else 78)
            t += d
            j += 1


def st_lead(s, t0, L, pi):
    """A monophonic phrase, legato, with the wheel and pressure on it."""
    sc = pi.get('scale', A_MINOR)
    shape = pi.get('shape', [0, 2, 4, 3, 5, 4, 2, 0])
    oct_ = pi.get('oct', 0)
    dur = (L - 1.4) / len(shape)
    for k, d in enumerate(shape):
        n = sc[d % len(sc)] + 12 * (oct_ + d // len(sc))
        s.note(t0 + k * dur, dur * 1.12, n, 96)     # overlap = legato
    if pi.get('mod', 1):
        s.swell(t0 + dur * 2, t0 + L - 1.2, lambda t, v: s.cc(t, 1, v), 70)
    if pi.get('at'):
        s.swell(t0 + dur * 4, t0 + L - 1.2, lambda t, v: s.press(t, v), pi['at'])
    if pi.get('bendto'):
        rng = pi.get('range', 2)
        t1 = t0 + dur * (len(shape) - 1)
        s.ramp(t1, t1 + dur * 0.8, lambda t, v: s.bend(t, v, rng), 0, pi['bendto'], 20)
        s.ramp(t1 + dur * 0.9, t1 + dur * 1.4, lambda t, v: s.bend(t, v, rng), pi['bendto'], 0, 12)
        s.bend(t0 + L - 0.3, 0, rng)


def st_bass(s, t0, L, pi):
    roots = pi.get('roots', [40, 40, 45, 43])
    d = pi.get('rate', 0.25)
    step = (L - 0.8) / len(roots)
    for k, r in enumerate(roots):
        base = t0 + k * step
        fig = [0, 0, 12, 0, 7, 0, 12, 3]
        t = base
        j = 0
        while t < base + step - d:
            s.note(t, d * 0.8, r + fig[j % len(fig)], 108 if j % 4 == 0 else 88)
            t += d
            j += 1


def st_bell(s, t0, L, pi):
    prog, oct_ = pi.get('prog', P_OPEN), pi.get('oct', 1)
    step = (L - 1.6) / len(prog)
    for k, (r, q) in enumerate(prog):
        ns = chord(r, q, oct_)
        for j, n in enumerate(ns):
            s.note(t0 + k * step + j * 0.19, step * 0.9, n, 100 - 6 * j)


def st_organ(s, t0, L, pi):
    prog, oct_ = pi.get('prog', P_MODAL), pi.get('oct', 0)
    step = (L - 1.4) / len(prog)
    for k, (r, q) in enumerate(prog):
        for note in chord(r, q, oct_, drop=True):
            s.note(t0 + k * step, step * 0.97, note, 92)
    s.swell(t0 + 0.5, t0 + L - 1.2, lambda t, v: s.press(t, v), pi.get('at', 90))


def st_fx(s, t0, L, pi):
    ns = pi.get('notes', [48, 55])
    for k, n in enumerate(ns):
        s.note(t0 + k * 1.2, L - 1.4 - k * 1.2, n, 100)
    s.swell(t0 + 0.4, t0 + L - 1.0, lambda t, v: s.cc(t, 1, v), 90)
    if pi.get('at'):
        s.swell(t0 + 1.4, t0 + L - 1.0, lambda t, v: s.press(t, v), 110)
    if pi.get('bendto'):
        rng = pi.get('range', 12)
        s.ramp(t0 + 1.0, t0 + L - 1.6, lambda t, v: s.bend(t, v, rng), 0, pi['bendto'], 28)
        s.bend(t0 + L - 0.3, 0, rng)


def st_split(s, t0, L, pi):
    """Left hand under the split point, right hand above it."""
    sp = pi['split']
    low = pi.get('low', [sp - 17, sp - 17, sp - 12, sp - 15])
    prog = pi.get('prog', P_JAZZ)
    step = (L - 1.0) / len(prog)
    for k, (r, q) in enumerate(prog):
        base = t0 + k * step
        for j in range(4):                          # walking left hand
            s.note(base + j * step / 4, step / 4 * 0.85,
                   low[k % len(low)] + (0 if j % 2 == 0 else 7), pi.get('vel', 94))
        ns = [n for n in chord(r + 12, q) if n >= sp] or [sp + 4, sp + 7]
        s.note(base + step * 0.5, step * 0.45, ns[0], 92)
        for n in ns:
            s.note(base, step * 0.42, n, 88)


def st_porta(s, t0, L, pi):
    """The one thing no factory patch switches on: glide."""
    s.cc(t0 - 0.15, 65, 127)                        # portamento on
    s.cc(t0 - 0.14, 5, pi.get('time', 42))          # and its time
    notes = pi.get('notes', [45, 57, 50, 62, 55, 43, 45])
    d = (L - 1.6) / len(notes)
    for k, n in enumerate(notes):
        s.note(t0 + k * d, d * 1.25, n, pi.get('vel', 88))   # overlapping = legato
    s.cc(t0 + L - 0.35, 65, 0)                      # off again for the next patch
    s.cc(t0 + L - 0.34, 5, 0)


def st_hold(s, t0, L, pi):
    """Hold pedal: the keys come up, the notes must not."""
    prog = pi.get('prog', P_OPEN)
    s.cc(t0 - 0.1, 64, 127)
    step = (L - 1.6) / len(prog)
    for k, (r, q) in enumerate(prog):
        for n in chord(r, q, pi.get('oct', 0), drop=True):
            s.note(t0 + k * step, step * 0.35, n, 90)   # short keys, long sound
    s.cc(t0 + L - 0.9, 64, 0)


STYLES = dict(pad=st_pad, stab=st_stab, arp=st_arp, lead=st_lead, bass=st_bass,
              bell=st_bell, organ=st_organ, fx=st_fx, split=st_split,
              porta=st_porta, hold=st_hold)


# ------------------------------------------------------------ the setlist ---
# (patch index 0..383, style, seconds, what it is there to show, params)
SET = [
    # -- the machine's own voice: PCM attack over a synthesized sustain -------
    (0,   'pad',   11, "PCM attack over a synth sustain, the machine's calling card",
     dict(prog=P_OPEN, mod=50)),
    (51,  'pad',   10, "the stereo width: chorus into two separate reverb networks",
     dict(prog=P_FALL, mod=35)),
    (43,  'pad',   10, "tape delay 248 ms, and an LFO that keys on every attack",
     dict(prog=P_FILM, mod=55)),
    (23,  'organ', 11, "chapel reverb, and pressure where an organ has no lever",
     dict(prog=P_MODAL, oct=-1, at=100)),

    # -- solo voices ---------------------------------------------------------
    (21,  'lead',  10, "pulse width 0 is an honest square: the flute rank in front",
     dict(scale=A_MINOR, shape=[7, 9, 8, 7, 5, 4, 2, 0], at=0)),
    (45,  'lead',  10, "the breath sample under a sawtooth, two ranks of one flute",
     dict(scale=D_DORIAN, shape=[4, 5, 7, 6, 4, 2, 1, 0], at=70)),
    (5,   'lead',  11, "reverb 23 is a cross delay, not a room; lever and pressure on it",
     dict(scale=D_DORIAN, shape=[7, 6, 4, 3, 4, 6, 7, 9], at=90)),

    # -- what the sawtooth and the wheel do ----------------------------------
    (20,  'stab',  10, "four bare sawtooths, on the octave the chip gives them",
     dict(prog=P_CINE, beat=0.42)),
    (40,  'stab',  10, "bender range 12 -- a whole octave under the wheel",
     dict(prog=P_OPEN, beat=0.45, bendto=11)),
    (22,  'pad',   11, "pulse width 82 on all four partials, and PW on velocity",
     dict(prog=P_FALL, mod=30, vel=64)),
    (30,  'pad',   11, "whole mode: all sixteen slots on one tone, the CPU's hard case",
     dict(prog=P_OPEN, mod=60)),

    # -- aftertouch ----------------------------------------------------------
    (19,  'pad',   10, "aftertouch on the filter -- lean on the keys",
     dict(prog=P_CINE, mod=0)),
    (29,  'lead',  10, "aftertouch as a second lever, on a whole-mode lead",
     dict(scale=A_MINOR, shape=[4, 5, 7, 9, 7, 5, 4, 2], at=110, bendto=2)),

    # -- the PCM side, and the waves that were silent until this month -------
    (27,  'arp',   10, "PCM pluck against two narrow squares, in a large hall",
     dict(prog=P_OPEN, rate=0.15, gate=1.6)),
    (8,   'arp',   10, "combination waves 57/71/67/94 -- the ones that were silent",
     dict(prog=P_MODAL, rate=0.15)),
    (17,  'bell',   9, "an inharmonic bell out of ring modulation",
     dict(prog=P_OPEN, oct=1)),
    (65,  'bell',   9, "ring modulation with the carrier muted away (bank 2)",
     dict(prog=P_OPEN, oct=1)),
    (286, 'lead',   9, "the same trick as a reed, in whole mode (bank 5)",
     dict(scale=D_DORIAN, shape=[4, 6, 7, 6, 4, 3, 1, 0], at=60)),
    (201, 'hold',  10, "the hold pedal: keys up, notes on (bank 4)",
     dict(prog=P_JAZZ)),

    # -- split keyboards and the low end -------------------------------------
    (26,  'split', 10, "a split keyboard, with aftertouch bend at minus twelve",
     dict(prog=P_JAZZ)),
    (50,  'split', 10, "split again, and a gate reverb 200 ms long on the brass",
     dict(prog=P_CINE)),
    (110, 'bass',   9, "solo key mode on a bass -- silent until we read the mode right",
     dict(roots=[40, 40, 45, 43], rate=0.22)),
    (102, 'porta', 11, "portamento, switched on from outside: no factory patch has it",
     dict(notes=[33, 45, 40, 52, 45, 36, 33], time=38)),

    # -- the rest of the library ---------------------------------------------
    (74,  'lead',  10, "the other one that was silent: solo mode, upper tone muted",
     dict(scale=A_MINOR, shape=[0, 2, 4, 5, 7, 5, 4, 2], at=80, bendto=2)),
    (169, 'lead',  10, "a lead out of bank 3, ring-modulated, whole mode",
     dict(scale=A_MINOR, shape=[7, 5, 4, 5, 7, 9, 11, 12], at=70, bendto=2)),
    (378, 'fx',    10, "an effects patch: cross delay, and aftertouch bend downward",
     dict(notes=[45, 52], at=1, bendto=-11, range=12)),
    (369, 'stab',  10, "Reso Release -- the filter jumps open when the key comes up",
     dict(prog=P_CINE, beat=0.5, vel=118, gate=0.35)),

    # -- out ------------------------------------------------------------------
    (63,  'pad',   11, "and out on the PCM piano, the sound the machine sold on",
     dict(prog=P_OPEN, mod=25)),
]


def build(path):
    import bank
    s = Song()
    t = 1.0
    for idx, style, L, why, pi in SET:
        d = bank.info(idx)
        s.marker(t - 0.55, "%d-%02d %s -- %s" % (d['bank'], d['prog'], d['name'], why))
        # clean slate: the tail of the last patch out, controllers neutral
        s.cc(t - 0.5, 123, 0)
        s.cc(t - 0.45, 1, 0)
        s.cc(t - 0.44, 64, 0)
        s.cc(t - 0.43, 65, 0)
        s.bend(t - 0.42, 0, 2)
        s.press(t - 0.41, 0)
        s.prog(t - 0.35, d['bank'] - 1, d['prog'] - 1)
        pi.setdefault('range', d['bend'])
        pi.setdefault('split', d['split'])
        STYLES[style](s, t, L, pi)
        t += L + 0.75                       # room for the tail before the next
    s.cc(t - 0.3, 123, 0)
    total = write_smf(s, path, "PicoFaceD5 -- a tour of the bank")
    return total, len(SET)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'PicoFaceD5-Demo.mid'
    dur, n = build(out)
    print("%s: %d patches, %.1f s (%d:%02d)" % (out, n, dur, int(dur // 60), int(dur % 60)))
