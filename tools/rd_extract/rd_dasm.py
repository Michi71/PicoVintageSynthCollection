#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""HD6301 disassembler for the MKS-20 / MK-80 program ROM.

    rd_dasm.py <mcu.cpp> <program.bin> [sweep | <addr> [count]]

`sweep` follows the interrupt vectors by recursive descent and prints only
bytes actually reached as instructions, with gaps marked. Give an address
instead for a plain linear run from there.

Get the program ROM out of a ROM set with rd_make_rom; the reference
emulator's mcu.cpp supplies the opcode table.

The opcode table is lifted from the reference emulator's own dispatch table
(Mcu::hd63701_insn), so the mnemonics and addressing modes are the ones that
emulator implements rather than a second-hand listing.

The ROM is 8 KB mirrored into 0xC000-0xFFFF; the firmware runs in the 0xE000
copy, which is where the vectors point.
"""
import re, sys

BRANCH = {'bcc','bcs','beq','bge','bgt','bhi','ble','bls','blt','bmi','bne',
          'bpl','bra','brn','bvc','bvs','bsr'}
IMM16  = {0x8c,0x8e,0x83,0xc3,0xcc,0xce}          # cpx lds subd addd ldd ldx
BITOP  = {'aim','oim','eim','tim'}                 # 6301: immediate + operand

def load_ops(path):
    s = open(path).read()
    i = s.index('hd63701_insn[0x100]')
    body = s[s.index('{', i)+1 : s.index('};', i)]
    return re.findall(r'&Mcu::(\w+)', body)

def decode(ops, rom, off, base):
    op = rom[off]
    name = ops[op]
    m = re.search(r'_(im|di|ix|ex)$', name)
    mode = m.group(1) if m else None
    mn = re.sub(r'_(im|di|ix|ex)$', '', name)
    pc = base + off

    if mn in BITOP:                       # aim #$xx,<dir> / aim #$xx,off,x
        imm, opnd = rom[off+1], rom[off+2]
        txt = f"{mn} #${imm:02x},${opnd:02x}" + (",x" if mode=='ix' else "")
        return 3, txt, None
    if mn in BRANCH:
        rel = rom[off+1]
        tgt = (pc + 2 + (rel - 256 if rel > 127 else rel)) & 0xffff
        return 2, f"{mn} ${tgt:04x}", tgt
    if mode is None:
        return 1, mn, None
    if mode == 'im':
        if op in IMM16:
            v = (rom[off+1] << 8) | rom[off+2]
            return 3, f"{mn} #${v:04x}", None
        return 2, f"{mn} #${rom[off+1]:02x}", None
    if mode == 'di':
        return 2, f"{mn} ${rom[off+1]:02x}", None
    if mode == 'ix':
        return 2, f"{mn} ${rom[off+1]:02x},x", None
    v = (rom[off+1] << 8) | rom[off+2]
    return 3, f"{mn} ${v:04x}", (v if mn in ('jmp','jsr') else None)

TERMINAL = {'rts','rti','jmp','bra','swi','wai','slp'}

def sweep(ops, rom, base, seeds):
    """Recursive descent: only bytes actually reached as instructions."""
    code, targets, todo = {}, set(), list(seeds)
    while todo:
        pc = todo.pop()
        while True:
            off = pc - base
            if not (0 <= off < len(rom)) or off in code:
                break
            n, txt, tgt = decode(ops, rom, off, base)
            code[off] = (n, txt)
            mn = txt.split()[0]
            if tgt is not None:
                targets.add(tgt)
                if mn in ('jsr','bsr') or mn.startswith('b'):
                    todo.append(tgt)
            if mn in TERMINAL:
                break
            pc += n
    return code, targets

def main():
    ops = load_ops(sys.argv[1])
    rom = open(sys.argv[2], 'rb').read()
    base = 0xE000
    if len(sys.argv) > 3 and sys.argv[3] == 'sweep':
        names = ['SCI','TOF','OCF','ICF','IRQ1','SWI','NMI','RESET']
        seeds, vec = [], {}
        for i, nm in enumerate(names):
            o = 0x1ff0 + i*2
            v = (rom[o] << 8) | rom[o+1]
            vec[v] = nm
            if 0xe000 <= v <= 0xffff: seeds.append(v)
        code, targets = sweep(ops, rom, base, seeds)
        print(f"; vectors: " + ", ".join(f"{n}=${a:04x}" for a,n in vec.items()))
        print(f"; {len(code)} instructions, {sum(n for n,_ in code.values())} of {len(rom)} bytes reached")
        prev = None
        for off in sorted(code):
            n, txt = code[off]
            a = base + off
            if prev is not None and off != prev:
                print(f"; ---- gap {prev+base:04x}..{a-1:04x} ({off-prev} bytes) ----")
            lbl = " <--" if a in targets else ""
            raw = " ".join(f"{b:02x}" for b in rom[off:off+n])
            print(f"{a:04x}  {raw:<9}  {txt}{lbl}")
            prev = off + n
        return
    start = int(sys.argv[3], 0) if len(sys.argv) > 3 else None
    count = int(sys.argv[4], 0) if len(sys.argv) > 4 else 64

    if start is None:                     # follow the reset vector
        start = (rom[0x1ffe] << 8) | rom[0x1fff]
        print(f"; reset vector -> ${start:04x}")
    off = start - base
    for _ in range(count):
        if not (0 <= off < len(rom)): break
        n, txt, tgt = decode(ops, rom, off, base)
        raw = " ".join(f"{b:02x}" for b in rom[off:off+n])
        print(f"{base+off:04x}  {raw:<9}  {txt}")
        off += n

main()
