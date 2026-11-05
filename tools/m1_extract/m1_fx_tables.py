#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Michi71
"""Korg M1 effect tables, read from the original Rev 19 OS ROM.

    from m1_fx_tables import fx_tables, load_rev19, UNRESOLVED
    t = fx_tables(load_rev19(ic32_path, ic23_path))

`fx_tables(os_image)` takes the 128 KiB interleaved OS image (IC32 = even
bytes, IC23 = odd bytes; offset 0 = linear E0000) and returns every table
the Python reference port of the M1 effects needs, in the units and sign
conventions that port uses, so a C++ port can consume them one to one.
Nothing in here comes from Korg's 2017 plugin: each value is read from the
image, computed with the firmware's own integer arithmetic from constants in
the image, or is a structural constant whose provenance is noted. Items the
firmware does not hold are None and listed in UNRESOLVED.

Addressing used in the comments (Rev 19 only):
  DS:xxxx   data segment F75C (linear F75C0 + xxxx)
  FX:xxxx   effect compiler code segment F2C3 (linear F2C30 + xxxx)
  T<n>.w<k> word k of the 57-word MDE microcode template of effect type n
            (template segment F428; offsets from the descriptor table DS:836C)
MDE words are 48 bit: address in bits 0-14 (15-bit ring offset), coefficient
in bits 16-25 as sign-magnitude Q9 (bit 25 = sign, 512 = 1.0).

The per-word assignment of template fields to the port's parameters (which
word is which allpass gain, which address pair is which delay) is structural
knowledge from the firmware analysis; the values themselves are read here.
Where the port's semantics needed a pipeline offset (+1 for a tap read in the
sample after the write) it is stated next to the field.
"""
import math
import struct

IMAGE_SIZE = 0x20000
BASE = 0xE0000
DS = 0xF75C0                  # data segment F75C
FX = 0xF2C30                  # effect compiler F2C3
TPL = 0xF4280                 # MDE templates F428
EDIT_BUF = 0xC67D             # DS offset of the effect edit buffer (FX:0A64 cmp di, C67D)
TPL_WORDS = 57
FS = 31250.0                  # MDE sample rate (hardware: 2 MHz V50 TCU domain, 64 x oversampled clocks)

UNRESOLVED = {
    'wt': 'LFO sine/triangle table (0x102+130 Q9 values). Not in the OS ROM: the '
          'MDE runs the LFO itself (template words with bit 26 set). The triangle '
          'part is exactly 8*min(i,127-i); no closed form reproduces the sine part '
          '(best: round(511*sin^2(pi*(i+0.5)/128)) is off by 1/512 at 12 of 128 '
          'points of the analysis\' table).',
    'wt2': 'Second LFO wave (130 Q9 values, irregular rectified-sine shape). Not in '
           'the OS ROM (MDE-internal) and not a closed form.',
    'rotary.c2c': 'Drum low-pass pole. No template coefficient '
                  'or product of coefficients matches; the MDE drum filter is not '
                  'decoded.',
    'rotary.depth_k': 'Depth scaling of AM and Doppler. The firmware patches two '
                      'words only (FX:109B: v*0x200/100 -> T24.w18, v*0x400/1000 -> '
                      'T24.w27); the port uses four factors in its own units '
                      'that need the MDE LFO modulation scale, which is not decoded.',
    'sym.mix2': 'Second-LFO mix. Not a template coefficient (template has '
                '30/512 and 409/512 around the LFO words); MDE LFO maths not decoded.',
    'sym.baseL': 'Address differences in T23 give the voice spreads (154/157/0 and '
                 '94/217/60 samples) but the common offset of the modulated '
                 'taps needs the MDE LFO semantics.',
    'sym.baseR': 'see sym.baseL',
    'sym.off': 'Phase offsets. FX:1290 initialises the LFO phase words of T23 to '
               '2000/A000/C000/E000h; their mapping onto the port\'s 128-step '
               'table offsets is not decoded.',
    'crev.eq': 'Fixed pre-filter of Delay/Hall and Delay/Room. The firmware never '
               'patches these words (FX:0C55/0C80), so the template defaults '
               '(T25/T26 words 2-9, same as the reverb EQ template) apply; '
               'decoding the template words '
               'into (Z, P, s) needs the MDE EQ word layout.',
    'combo_lo/combo_hi (None entries)': 'Parameter bytes without an edit record on '
               'the combination\'s edit page (high bytes of 16-bit delays, unused '
               'bytes, Delay/Phaser byte 7 "manual", Delay/Tremolo byte 6) and the '
               'waveform bytes, whose editor (handler 5E3F) is not a plain 0..max '
               'range.',
}


def load_rev19(ic32_path, ic23_path):
    """Interleave the two Rev 19 EPROM dumps (IC32 = even bytes, IC23 = odd)."""
    lo = open(ic32_path, 'rb').read()
    hi = open(ic23_path, 'rb').read()
    img = bytearray(IMAGE_SIZE)
    img[0::2], img[1::2] = lo, hi
    return bytes(img)


class _Img:
    def __init__(self, b):
        if len(b) != IMAGE_SIZE:
            raise ValueError('M1 OS image must be %d bytes, got %d' % (IMAGE_SIZE, len(b)))
        self.b = b

    def lin(self, a, n):
        o = a - BASE
        if o < 0 or o + n > IMAGE_SIZE:
            raise ValueError('address %05X outside the OS image' % a)
        return self.b[o:o + n]

    def u8(self, a): return self.lin(a, 1)[0]
    def u16(self, a): return struct.unpack('<H', self.lin(a, 2))[0]
    def ds8(self, off): return self.u8(DS + off)
    def ds16(self, off): return self.u16(DS + off)
    def ds16s(self, off, n): return list(struct.unpack('<%dH' % n, self.lin(DS + off, 2 * n)))

    def expect(self, a, hexbytes, what):
        """Content check: the bytes at linear a must be hexbytes (Rev 19 signature)."""
        want = bytes.fromhex(hexbytes)
        if self.lin(a, len(want)) != want:
            raise ValueError('not a Korg M1 Rev 19 OS image: %s at %05X' % (what, a))

    def fx(self, off, hexbytes, what):
        """Check effect-compiler code at FX:off and return the following bytes' address."""
        self.expect(FX + off, hexbytes, what)
        return FX + off

    def imm16(self, off, opcode, what):
        """16-bit immediate of the instruction `opcode imm16` at FX:off (opcode checked)."""
        op = bytes.fromhex(opcode)
        if self.lin(FX + off, len(op)) != op:
            raise ValueError('not a Korg M1 Rev 19 OS image: %s at FX:%04X' % (what, off))
        return self.u16(FX + off + len(op))

    def imm8(self, off, opcode, what):
        op = bytes.fromhex(opcode)
        if self.lin(FX + off, len(op)) != op:
            raise ValueError('not a Korg M1 Rev 19 OS image: %s at FX:%04X' % (what, off))
        return self.u8(FX + off + len(op))


# ---------------------------------------------------------------------------
# MDE template access
# ---------------------------------------------------------------------------
DESC_TABLE = 0x836C           # DS:836C, 34 near pointers to effect descriptors (+0 = template offset)


def _templates(m):
    ptrs = m.ds16s(DESC_TABLE, 34)
    offs = [m.ds16(p) for p in ptrs]
    for t, o in enumerate(offs[:33]):
        if o % 0x15C or o >= 22 * 0x15C:
            raise ValueError('not a Korg M1 Rev 19 OS image: descriptor of type %d' % t)
    cache = {}

    def tpl(t):
        o = offs[t]
        if o not in cache:
            raw = m.lin(TPL + o, 6 * TPL_WORDS)
            cache[o] = [int.from_bytes(raw[6 * k:6 * k + 6], 'little') for k in range(TPL_WORDS)]
        return cache[o]
    return tpl


def _coef(w):
    """Coefficient field: bits 16-24 magnitude, bit 25 sign (sign-magnitude Q9)."""
    c = (w >> 16) & 0x3FF
    return (-(c & 0x1FF) if c & 0x200 else (c & 0x1FF)) / 512.0


def _addr(w):
    return w & 0x7FFF


def _dist(T, src, dst):
    """Address difference T.w[dst] - T.w[src] in the 15-bit ring (samples)."""
    return (_addr(T[dst]) - _addr(T[src])) & 0x7FFF


# ---------------------------------------------------------------------------
# Firmware arithmetic
# ---------------------------------------------------------------------------
def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _q15mul(a, b):
    """imul a,b ; shl ax,1 ; rcl dx,1  -> dx (FX:0A0B ff.)"""
    p = (_s16(a) * _s16(b)) & 0xFFFFFFFF
    return ((p << 1) >> 16) & 0xFFFF


def _comb_feedback(W, sign_byte, pow_tab, rt, k1ba1):
    """Comb feedback coefficient of FX:09D5..0A56 for Reverb Time byte rt.

    W / (rt+2) is the comb's delay in units of the reverb time; the firmware
    evaluates 2^-x as table[int] * (Horner polynomial of the fraction) and
    returns |coef| * 16 / 0x1EF in Q9.  The magnitude exceeds 511 for a few
    Room-group combs above the edit limit 47 (DS:7B7A); the firmware then
    writes an overflowing field, here it is saturated to 511/512.
    Returns the feedback in the port's sign convention (fb = -plugin value).
    """
    rt = min(rt, 0x62)
    q = (W // (rt + 2)) & 0xFFFF
    P = q * k1ba1
    ax = (P >> 8) & 0xFFFF
    n = (P >> 24) & 0xFF
    ax >>= 1
    cx = ax
    ax >>= 1
    ax //= 24
    ax = (ax - 0xAAA) & 0xFFFF
    dx = _q15mul(ax, cx)
    dx = _q15mul((dx + 0x2000) & 0xFFFF, cx)
    dx = _q15mul((dx - 0x4000) & 0xFFFF, cx)
    dx = (dx + 0x4000) & 0xFFFF
    p = (dx * pow_tab[n]) & 0xFFFFFFFF
    dx = ((p << 1) >> 16) & 0xFFFF
    mag = min((16 * dx) // 0x1EF, 511)
    negative = sign_byte < 0x80             # FX:0A4B: or ah,2 unless the sign byte is negative
    return (-mag if negative else mag) / 512.0


def _comb_damp(Wd, hd, k1ef):
    """High-damp coefficient a of FX:0988..09BB: ((Wd*0x1EF/0x200)*hd/100) >> 5, Q9."""
    hd = min(hd, 100)
    dx = (Wd * k1ef) // 0x200
    return (((dx * hd) // 100) >> 5) / 512.0


def _reverb_tables(m):
    """DS tables shared by all comb reverbs (types 0-5 and the combo halves 25/26)."""
    # Code references prove the table addresses (Plus+1: DS:8940/894E/898E/89CE).
    m.fx(0x098C, '8b97d684', 'damp table ref DS:84D6')
    m.fx(0x09D9, '8b871685', 'feedback table ref DS:8516')
    m.fx(0x0A2F, '8b8fc884', 'power table ref DS:84C8')
    m.fx(0x0A47, '8a8f5685', 'sign table ref DS:8556')
    k1ef = m.imm16(0x0990, 'b8', 'MDE factor 0x1EF')
    k1ba1 = m.imm16(0x09EE, 'b9', 'feedback scale 0x1BA1')
    if k1ef != 0x1EF:
        raise ValueError('not a Korg M1 Rev 19 OS image: MDE factor %X' % k1ef)
    return dict(
        pow=m.ds16s(0x84C8, 7),                     # DS:84C8  2^-n, Q15 (7 words)
        wd=m.ds16s(0x84D6, 32),                     # DS:84D6  damping scale per comb, Q14
        wf=m.ds16s(0x8516, 32),                     # DS:8516  comb delay / reverb time
        sign=list(m.lin(DS + 0x8556, 32)),          # DS:8556  feedback sign per comb (byte)
        k1ef=k1ef, k1ba1=k1ba1)


# ---------------------------------------------------------------------------
# Edit pages (combo parameter ranges)
# ---------------------------------------------------------------------------
PAGE_MAP = 0x2FBC             # DS:2FBC  34 words: effect type -> page entry (6 bytes: code, page, code)


def _page_records(m, t):
    entry = m.ds16(PAGE_MAP + 2 * t)
    page = m.ds16(entry + 2)
    if m.ds16(page) != 0x7616:                      # common page header word
        raise ValueError('not a Korg M1 Rev 19 OS image: edit page of type %d' % t)
    out = []
    for r in m.ds16s(page + 16, 8):
        h, desc, addr, _ = m.ds16s(r, 4)
        out.append((h, desc, addr - EDIT_BUF))
    return out


def _string(m, off, n=24):
    s = m.lin(DS + off, n)
    return s.split(b'\0')[0].decode('latin-1')


def _combo_ranges(m):
    # The delay "High Damp" editor (handler CE11) has a descriptor that lists only
    # display strings (short name 'HD', long names 'High Damp [%]' / '... L/R [%]');
    # its range is taken from the plain "High Damp [%]" descriptor of the reverb
    # page (type 0, byte 2), linked by the identical short name.
    damp_max = damp_name = damp_short = None
    for slot, (h, desc, byte) in enumerate(_page_records(m, 0)):
        if slot != 7 and byte == 2 and (h >> 8) == 0x5E:  # slot 7 = balance editor
            damp_max = m.ds16(desc)
            damp_short = _string(m, m.ds16(desc + 2))
            damp_name = _string(m, m.ds16(desc + 2) + len(damp_short) + 1)
    if damp_name != 'High Damp [%]':
        raise ValueError('not a Korg M1 Rev 19 OS image: reverb High Damp editor')
    lo, hi = {}, {}
    for t in range(25, 33):
        L, H = [None] * 8, [None] * 8
        for slot, (h, desc, byte) in enumerate(_page_records(m, t)):
            if slot in (3, 7) or desc == 0 or not 0 <= byte < 8:
                continue                            # balance editors / empty slots
            kind = h >> 8
            if kind == 0x5E and (h & 0xFF) == 0x3F:
                continue                            # waveform editor: no plain range
            if kind == 0x5E:
                L[byte], H[byte] = 0, m.ds16(desc)
            elif kind in (0x1E, 0x1F, 0x2E):
                L[byte], H[byte] = _s16(m.ds16(desc + 2)), m.ds16(desc)
            elif kind == 0xCE:
                if _string(m, m.ds16(desc)) != damp_short:     # same 'HD' short name
                    raise ValueError('not a Korg M1 Rev 19 OS image: damp editor of type %d' % t)
                L[byte], H[byte] = 0, damp_max
        if t == 27 and H[4] is not None:
            # Port semantics: E/R time = byte + 10 (FX:0CBF add ax,0Ah).
            add = m.imm8(0x0CBF, '05', 'E/R time offset')
            L[4], H[4] = L[4] + add, H[4] + add
        lo[t], hi[t] = L, H
    return lo, hi


# ---------------------------------------------------------------------------
# Main entry
# ---------------------------------------------------------------------------
def fx_tables(os_image):
    m = _Img(os_image)
    # Rev 19 signature: effect compiler in F2C3 addressing the Rev 19 data segment.
    m.fx(0x0A64, '81ff7dc6', 'edit buffer compare (C67D)')
    tpl = _templates(m)
    G = m.imm16(0x0990, 'b8', 'MDE factor') / 512.0     # 0x1EF/512 = 0.966797
    out = {}

    # --- 1. Reverbs 0-5 ----------------------------------------------------
    rt = _reverb_tables(m)
    base = [m.ds16s(0x8576 + 6 * t, 3) for t in range(6)]   # DS:8576 (FX:0B0F): AP, ER L, ER R taps
    ers = [m.ds16s(0x85A2 + 6 * t, 3) for t in range(6)]    # DS:85A2 (FX:0AA3): ER level scales, Q9
    m.fx(0x0B0F, '81c57685', 'pre-delay base table ref DS:8576')
    m.fx(0x0AA3, '81c5a285', 'ER level table ref DS:85A2')
    rev = {}
    for t in range(6):
        T = tpl(t)
        comb = range(4 * t, 4 * t + 4)                      # FX:0A5F: comb index = type*4 + k
        rev[t] = dict(
            # allpass gains: the first coefficient word of each AP (H3/ctl08/ctl09 words)
            g=[_coef(T[k]) for k in (11, 16, 37, 49)],
            # comb tap gains L0,R0,L1,R1,...: coefficient of the word after each tap word
            comb_out=[_coef(T[k]) for k in (32, 44, 34, 46, 35, 47, 36, 48)],
            # allpass delays: read address - write address of each AP region
            ap_d=[_dist(T, 14, 13), _dist(T, 18, 17), _dist(T, 39, 36), _dist(T, 51, 48)],
            # comb delays: next region start - this comb's read address
            comb_d=[_dist(T, 23, 19), _dist(T, 26, 22), _dist(T, 27, 25), _dist(T, 30, 28)],
            tap_l=[_dist(T, 23, 31), _dist(T, 24, 33), _dist(T, 29, 34), _dist(T, 32, 35)],
            tap_r=[_dist(T, 23, 43), _dist(T, 26, 45), _dist(T, 29, 46), _dist(T, 30, 47)],
            g_in=1.0,                                       # structural: no gain word on the AP input
            fb=[[_comb_feedback(rt['wf'][c], rt['sign'][c], rt['pow'], r, rt['k1ba1'])
                 for r in range(98)] for c in comb],
            damp=[[_comb_damp(rt['wd'][c], h, rt['k1ef']) for h in range(100)] for c in comb],
            base_ap=base[t][0], base_erl=base[t][1], base_err=base[t][2],
            base_erc=0,                                     # T.w40/w52 get the pre-delay only (FX:0B33/0B3E)
            # (ER L+R, ER L, ER R) = DS:85A2 words (2, 0, 1) per FX:0AAE..0AF4
            er_scale=[ers[t][2] / 512.0, ers[t][0] / 512.0, ers[t][1] / 512.0],
        )
    out['rev'] = rev

    # --- 2. Delay damping (types 9/10 and every combo delay) ----------------
    # FX:05EE: b = -(0x1EF*(0xE1-hd)/0xE1), a = hd*0x1EF/0x12C, both Q9.
    c_e1 = m.imm16(0x05F7, 'b9', 'damp b range')
    c_1ef = m.imm16(0x05FC, 'b8', 'damp b scale')
    c_e1b = m.imm16(0x0601, 'b9', 'damp b divisor')
    c_1ef2 = m.imm16(0x0641, 'b9', 'damp a scale')
    c_12c = m.imm16(0x0646, 'b9', 'damp a divisor')
    out['dly_damp_a'] = [(h * c_1ef2 // c_12c) / 512.0 for h in range(100)]
    out['dly_damp_b'] = [-(c_1ef * (c_e1 - h) // c_e1b) / 512.0 for h in range(100)]

    # --- 3. LFO ---------------------------------------------------------------
    out['wt'] = None
    out['wt2'] = None
    # FX:0671: speed byte -> centi-Hz -> phase increment (*100/0x12A).
    m.fx(0x0671, 'ac32e43d6300', 'LFO speed patcher')
    seg1 = m.imm16(0x0674, '3d', 'speed segment 1')
    mul2 = m.imm16(0x068B, 'b9', 'speed step 2')
    off2 = m.imm16(0x0690, '05', 'speed base 2')
    seg2 = m.imm16(0x0683, '3d', 'speed segment 2')
    mul3 = m.imm16(0x0696, 'b9', 'speed step 3')
    off3 = m.imm16(0x069E, '05', 'speed base 3')
    num = m.imm16(0x06A1, 'b9', 'speed numerator')
    den = m.imm16(0x06A6, 'b9', 'speed denominator')
    inc = []
    for v in range(217):
        if v <= seg1:
            a = (v + 1) * 3
        elif v <= seg2:
            a = (v - seg1) * mul2 + off2
        else:
            a = (v - seg2) * mul3 + off3
        inc.append(a * num // den)
    out['lfo_inc'] = inc

    # --- 4. Factory parameters per type (replaces the plugin parameter defaults)
    fwd = m.lin(DS + 0x7502, 34 * 8)                    # DS:7502, 34 x 8 signed bytes
    out['fw_defaults'] = [[b - 256 if b > 127 else b for b in fwd[8 * t:8 * t + 8]] for t in range(34)]
    if any(fwd[8 * 33:8 * 34]) or fwd[0] > 97:
        raise ValueError('not a Korg M1 Rev 19 OS image: factory effect parameters at DS:7502')

    # --- 5. Early reflections 6-8 --------------------------------------------
    m.fx(0x0CCC, '8b8d4688', 'ER tap table ref DS:8846')
    t_fw = m.ds16s(0x8846, 24)                           # DS:8846 tap times, 1/1000 of the ER time
    k_ms = m.imm16(0x0CC2, 'b9', 'ER time scale') / m.imm16(0x0CC7, 'b9', 'ER time div')   # 3125/10
    k_t = m.imm16(0x0CD2, 'b9', 'ER tap divisor')                                        # 10000
    ap_c = [m.imm16(0x0D05, 'b9', 'ER AP0 slope'), m.imm16(0x0D26, 'b9', 'ER AP1 slope'),
            m.imm16(0x0D47, 'b9', 'ER AP2 slope'), m.imm16(0x0D70, 'b8', 'ER AP3 slope')]
    k_ap = m.imm16(0x0D0A, 'b9', 'ER AP divisor')                                        # 8000
    ms = FS / 1000.0
    # tap delay = (time * T + pre_delay_ms) * 31.25 + 1 samples; tap 0 = pre-delay only (FX:0CAB)
    out['er_T'] = [0.0] + [v * k_ms / k_t / ms for v in t_fw]
    out['er_A'] = [c * k_ms / k_ap for c in ap_c]                 # AP delay = time * A + 1 samples
    # AP ring size: longest AP at the largest E/R time (edit maximum of type 6 byte 0,
    # handler 2EF1, plus the +10 of FX:0CBF), rounded up to a power of two.
    t_max = [m.ds16(d) for h, d, b in _page_records(m, 6)[:1]][0] + m.imm8(0x0CBF, '05', 'E/R time offset')
    max_ap = max(out['er_A']) * t_max + 2
    ma = (1 << math.ceil(math.log2(max_ap))) - 1                    # power-of-two ring for the APs
    er = {}
    for t in (6, 7, 8):
        T = tpl(t)
        er[t] = dict(
            # AP gain = first AP coefficient x MDE factor (T.w37/41/45/50, each followed by -G)
            g=[_coef(T[k]) * G for k in (37, 41, 45, 50)],
            # tap 0 (pre-delay) gain in w11, taps 1..24 in w13..w36
            tg=[_coef(T[11])] + [_coef(T[12 + k]) for k in range(1, 25)],
            m0=0x7FFF,                                      # 15-bit MDE ring (patchers keep bit 15)
            ma=[ma] * 4)
    out['er'] = er

    # --- 6. Tremolo shape, exciter blend -------------------------------------
    out['trem_shape_k'] = m.imm16(0x1160, 'b9', 'tremolo shape') / m.imm16(0x1165, 'b9', 'shape div') / 512.0
    # FX:057B: blend*0x1FF/100 written as +b and -b (FX:0E72); the port uses b = blend*k with k < 0.
    out['exc_k'] = -m.imm16(0x057F, 'b9', 'exciter scale') / m.imm16(0x0584, 'b9', 'exciter div') / 512.0

    # --- 7. Rotary speaker ----------------------------------------------------
    T = tpl(24)
    slow_k = m.imm16(0x0F4C, 'b9', 'horn slow slope')
    slow_0 = m.imm16(0x0F51, '05', 'horn slow base')
    fast_k = m.imm16(0x0F5F, 'b9', 'horn fast slope')
    fast_0 = m.imm16(0x0F64, '05', 'horn fast base')
    div = m.imm16(0x0F68, 'b9', 'horn div')
    tick = m.u8(0xE05C5) | m.u8(0xE05C9) << 8            # TCU counter 2 reload (E05C0: out 0Ch)
    m.expect(0xE05C0, 'b0b0e60eb0', 'tick timer setup')
    tick_samples = tick / 2e6 * FS                        # 2600 clocks at 2 MHz = 1.3 ms = 40.625 samples
    horn_ticks = m.imm8(0x0FB0, '24', 'horn step mask') + 1     # step when (count & 3) == 0
    drum_ticks = m.imm8(0x0FAC, '3c', 'drum step period')       # count wraps after 30
    out['rotary'] = dict(
        c20=_coef(T[3]), c24=_coef(T[11]), c28=_coef(T[6]), c2c=None,
        c30=_coef(T[10]), c38=_coef(T[7]), c40=_coef(T[4]), c44=_coef(T[12]),
        # horn buffer: write at T24.w4 (0003); plain reads become visible one sample later (+1)
        h_fb_tap=_dist(T, 4, 6) + 1, h_w=0, h_fix=_dist(T, 4, 11) + 1,
        h_base=_dist(T, 4, 18),                           # LFO-modulated tap (w18) relative to the write
        d_w=0, d_base=_dist(T, 12, 27),                   # drum: write w12, modulated tap w27
        micL=[_dist(T, 39, 41), _dist(T, 39, 42), _dist(T, 39, 43), _dist(T, 32, 38), _dist(T, 32, 40)],
        micR=[_dist(T, 39, 49), _dist(T, 39, 50), _dist(T, 39, 51), _dist(T, 32, 46), _dist(T, 32, 48)],
        g120=_coef(T[42]), g124=_coef(T[43]), g130=_coef(T[39]), g134=_coef(T[41]),
        accel=[round(horn_ticks * tick_samples), round(drum_ticks * tick_samples)],
        slow=[(slow_k * s + slow_0) // div for s in range(-10, 11)],
        fast=[(fast_k * s + fast_0) // div for s in range(-10, 11)],
        depth_k=None)

    # --- 8. Symphonic ensemble ------------------------------------------------
    out['sym'] = dict(mix2=None, baseL=None, baseR=None, off=None,
                      inc=[m.imm16(0x1290, 'b8', 'sym LFO1 inc'), m.imm16(0x1296, 'b8', 'sym LFO2 inc')])

    # --- 9. Delay/Hall, Delay/Room reverb halves -----------------------------
    m.fx(0x0BD0, '81c59a85', 'combo base table ref DS:859A')
    cb = m.ds16s(0x859A, 4)                               # DS:859A: (AP tap, ER tap B) per type
    cidx = {25: m.imm16(0x0C3B, 'bb', 'Delay/Hall comb index'),
            26: m.imm16(0x0C66, 'bb', 'Delay/Room comb index')}
    crev = {}
    for t in (25, 26):
        T = tpl(t)
        comb = range(cidx[t], cidx[t] + 4)
        crev[t] = dict(
            g=[_coef(T[10]), _coef(T[36]), _coef(T[15])],   # AP0 (input), AP1 (output), AP2 (pre-comb)
            ger=[_coef(T[40]), _coef(T[41])],                # ER taps w39 (pre-delay) and w40 (+base_b)
            cout=[_coef(T[k]) for k in (31, 33, 34, 35)],
            ap_d=[_dist(T, 13, 12), _dist(T, 38, 37), _dist(T, 17, 16)],
            comb_d=[_dist(T, 22, 18), _dist(T, 23, 21), _dist(T, 28, 24), _dist(T, 29, 27)],
            comb_tap=[_dist(T, 22, 30), _dist(T, 23, 32), _dist(T, 26, 33), _dist(T, 31, 34)],
            eq=None,
            g_in=1.0,
            fb=[[_comb_feedback(rt['wf'][c], rt['sign'][c], rt['pow'], r, rt['k1ba1'])
                 for r in range(98)] for c in comb],
            damp=[[_comb_damp(rt['wd'][c], h, rt['k1ef']) for h in range(100)] for c in comb],
            base_ap=cb[2 * (t - 25)], base_b=cb[2 * (t - 25) + 1],
            base_c=0)                                       # T.w39 gets the pre-delay only (FX:0BF4)
    out['crev'] = crev

    # --- 10. Delay/E.Ref ER half ----------------------------------------------
    T = tpl(27)
    g3 = _coef(T[38]) * G
    out['cer'] = dict(
        # Firmware Delay/E.Ref has three APs (FX:0DAF -> 0C8D: slopes D4h, 6Eh, 2Ah).
        # Index 3 (the port's third stage) is the firmware's third AP.
        g=[_coef(T[30]) * G, _coef(T[34]) * G, g3, g3],
        tg=[_coef(T[4])] + [_coef(T[5 + k]) for k in range(1, 25)],
        T=list(out['er_T']),
        A=out['er_A'][:3] + [out['er_A'][2]],
        g_in=_coef(T[2]) * _coef(T[3]))                     # two x0.5 input words

    # --- 11. Combo parameter ranges -------------------------------------------
    out['combo_lo'], out['combo_hi'] = _combo_ranges(m)
    return out


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 3:
        sys.exit('usage: m1_fx_tables.py <rev19-IC32.bin> <rev19-IC23.bin>')
    t = fx_tables(load_rev19(sys.argv[1], sys.argv[2]))
    for k, v in t.items():
        print(k, 'None' if v is None else type(v).__name__)
    print('unresolved:', ', '.join(UNRESOLVED))
