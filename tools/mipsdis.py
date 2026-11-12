#!/usr/bin/env python3
"""tools/mipsdis.py ELF FUNC [FUNC ...] | ELF --find SUBSTR | ELF --addr HEX [COUNT]

A small MIPS (R5900 subset) disassembler for reading the PS2 prototype's
unstripped executable (orig/ps2-beta/SLUS_001.00, its .symtab names 2,081
functions), with no toolchain: macOS's objdump has no MIPS target. Reads
only; output is for notes (docs-pending/prototype-worlds.md), not matching.

Calls and data addresses are named from the symbol table; lui/addiu and
lui/lw pairs are folded into the address they build. Unknown encodings show
as .word.
"""
import struct
import sys

R = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
     's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


def load(path):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x2E)
    secs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
    segs = []
    phoff, = struct.unpack_from('<I', d, 0x1C)
    phentsize, phnum = struct.unpack_from('<HH', d, 0x2A)
    for i in range(phnum):
        t, off, va, pa, fs, ms, fl, al = struct.unpack_from('<IIIIIIII', d, phoff + i * phentsize)
        if t == 1:
            segs.append((va, off, fs))
    syms = {}
    funcs = {}
    for s in secs:
        if s[1] == 2:   # SYMTAB
            strs = secs[s[6]]
            for k in range(s[5] // 16):
                nm, val, size, info, oth, shn = struct.unpack_from('<IIIBBH', d, s[4] + k * 16)
                name = d[strs[4] + nm:d.index(b'\0', strs[4] + nm)].decode('latin1')
                if name and val and not name.startswith('.'):
                    syms.setdefault(val, name)
                    if info & 0xF == 2:
                        funcs[name] = (val, size)
    return d, segs, syms, funcs


def cstr(a):
    d, segs = STR['d'], STR['segs']
    for v, off, fs in segs:
        if v <= a < v + fs:
            o = off + a - v
            e = d.find(b'\0', o, o + 128)
            if e > o + 1 and all(32 <= c < 127 for c in d[o:e]) and not STR['syms'].get(a, '@').isidentifier():
                return d[o:e].decode()
    return None


def word(d, segs, va):
    for v, off, fs in segs:
        if v <= va < v + fs:
            return struct.unpack_from('<I', d, off + va - v)[0]
    return None


STR = {}


def name(syms, a):
    st = STR.get('d') and cstr(a)
    if st:
        return repr(st)
    if a in syms:
        return syms[a]
    best = None
    for k in syms:
        if k <= a and a - k < 0x4000 and (best is None or k > best):
            best = k
    return '%s+0x%x' % (syms[best], a - best) if best is not None else '0x%x' % a


def dis(w, pc, syms, hi):
    op, rs, rt, rd, sa, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm = w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    r = lambda i: R[i]
    if w == 0:
        return 'nop'
    if op == 0:
        rt3 = {0: 'sll', 2: 'srl', 3: 'sra', 0x38: 'dsll', 0x3A: 'dsrl', 0x3B: 'dsra', 0x3C: 'dsll32', 0x3E: 'dsrl32', 0x3F: 'dsra32'}
        if fn in rt3:
            return '%s %s, %s, %d' % (rt3[fn], r(rd), r(rt), sa)
        if fn == 8:
            return 'jr %s' % r(rs)
        if fn == 9:
            return 'jalr %s' % r(rs)
        if fn in (0x10, 0x12):
            return '%s %s' % ('mfhi' if fn == 0x10 else 'mflo', r(rd))
        three = {4: 'sllv', 6: 'srlv', 7: 'srav', 0x0A: 'movz', 0x0B: 'movn', 0x20: 'add', 0x21: 'addu', 0x22: 'sub', 0x23: 'subu', 0x24: 'and',
                 0x25: 'or', 0x26: 'xor', 0x27: 'nor', 0x2A: 'slt', 0x2B: 'sltu', 0x2D: 'daddu', 0x2F: 'dsubu', 0x14: 'dsllv', 0x16: 'dsrlv'}
        if fn in three:
            if fn == 0x25 and rt == 0:
                return 'move %s, %s' % (r(rd), r(rs))
            if fn == 0x2D and rt == 0:
                return 'move %s, %s' % (r(rd), r(rs))
            return '%s %s, %s, %s' % (three[fn], r(rd), r(rs), r(rt))
        two = {0x18: 'mult', 0x19: 'multu', 0x1A: 'div', 0x1B: 'divu'}
        if fn in two:
            return '%s %s, %s%s' % (two[fn], r(rs), r(rt), (', ' + r(rd)) if rd else '')
        if fn == 0xF:
            return 'sync'
        return '.word 0x%08x' % w
    if op == 1:
        n = {0: 'bltz', 1: 'bgez', 2: 'bltzl', 3: 'bgezl', 0x10: 'bltzal', 0x11: 'bgezal'}.get(rt)
        if n:
            return '%s %s, 0x%x' % (n, r(rs), pc + 4 + simm * 4)
        return '.word 0x%08x' % w
    if op in (2, 3):
        t = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        return '%s %s' % ('j' if op == 2 else 'jal', name(syms, t))
    br = {4: 'beq', 5: 'bne', 0x14: 'beql', 0x15: 'bnel'}
    if op in br:
        t = pc + 4 + simm * 4
        if op == 4 and rs == 0 and rt == 0:
            return 'b 0x%x' % t
        return '%s %s, %s, 0x%x' % (br[op], r(rs), r(rt), t)
    if op in (6, 7, 0x16, 0x17):
        return '%s %s, 0x%x' % ({6: 'blez', 7: 'bgtz', 0x16: 'blezl', 0x17: 'bgtzl'}[op], r(rs), pc + 4 + simm * 4)
    if op == 0xF:
        hi[rt] = imm << 16
        return 'lui %s, 0x%x' % (r(rt), imm)
    ari = {8: 'addi', 9: 'addiu', 0xA: 'slti', 0xB: 'sltiu', 0xC: 'andi', 0xD: 'ori', 0xE: 'xori', 0x18: 'daddi', 0x19: 'daddiu'}
    if op in ari:
        note = ''
        if op in (9, 0x19) and rs in hi:
            a = (hi[rs] + simm) & 0xFFFFFFFF
            note = '   # %s' % name(syms, a) if a >= 0x100000 else '   # 0x%x' % a
        if op == 0xD and rs in hi:
            note = '   # 0x%x' % (hi[rs] | imm)
        if op in (0xC, 0xD, 0xE):
            return '%s %s, %s, 0x%x%s' % (ari[op], r(rt), r(rs), imm, note)
        return '%s %s, %s, %d%s' % (ari[op], r(rt), r(rs), simm, note)
    mem = {0x20: 'lb', 0x21: 'lh', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x27: 'lwu', 0x28: 'sb', 0x29: 'sh', 0x2B: 'sw',
           0x37: 'ld', 0x3F: 'sd', 0x1E: 'lq', 0x1F: 'sq', 0x31: 'lwc1', 0x39: 'swc1', 0x22: 'lwl', 0x26: 'lwr', 0x2A: 'swl', 0x2E: 'swr',
           0x1A: 'ldl', 0x1B: 'ldr', 0x2C: 'sdl', 0x2D: 'sdr', 0x2F: 'cache', 0x33: 'pref'}
    if op in mem:
        dst = ('$f%d' % rt) if op in (0x31, 0x39) else r(rt)
        note = ''
        if rs in hi:
            note = '   # %s' % name(syms, (hi[rs] + simm) & 0xFFFFFFFF)
        return '%s %s, %d(%s)%s' % (mem[op], dst, simm, r(rs), note)
    if op == 0x11:   # COP1
        fmt = rs
        if fmt == 0:
            return 'mfc1 %s, $f%d' % (r(rt), rd)
        if fmt == 4:
            return 'mtc1 %s, $f%d' % (r(rt), rd)
        if fmt == 2:
            return 'cfc1 %s, $%d' % (r(rt), rd)
        if fmt == 6:
            return 'ctc1 %s, $%d' % (r(rt), rd)
        if fmt == 8:
            return '%s 0x%x' % (['bc1f', 'bc1t', 'bc1fl', 'bc1tl'][rt & 3], pc + 4 + simm * 4)
        fs, fd, ft = rd, sa, rt
        if fmt == 0x10:
            n = {0: 'add.s', 1: 'sub.s', 2: 'mul.s', 3: 'div.s', 4: 'sqrt.s', 5: 'abs.s', 6: 'mov.s', 7: 'neg.s',
                 0x18: 'adda.s', 0x1C: 'madd.s', 0x1D: 'msub.s', 0x24: 'cvt.w.s', 0x28: 'max.s', 0x29: 'min.s',
                 0x30: 'c.f.s', 0x32: 'c.eq.s', 0x34: 'c.lt.s', 0x36: 'c.le.s', 0x16: 'rsqrt.s', 0x1A: 'mula.s'}.get(fn)
            if n:
                return '%s $f%d, $f%d, $f%d' % (n, fd, fs, ft)
        if fmt == 0x14 and fn == 0x20:
            return 'cvt.s.w $f%d, $f%d' % (fd, fs)
        return '.word 0x%08x  # cop1' % w
    if op == 0x10:
        return '%s %s, $%d   # cop0' % ('mfc0' if rs == 0 else 'mtc0', r(rt), rd)
    if op == 0x1C:
        return '.word 0x%08x  # mmi' % w
    if op == 0x12:
        return '.word 0x%08x  # cop2 (vu0)' % w
    return '.word 0x%08x' % w


def disfunc(d, segs, syms, start, size):
    hi = {}
    clear = False
    for pc in range(start, start + size, 4):
        w = word(d, segs, pc)
        if w is None:
            break
        lab = syms.get(pc)
        if lab and pc != start:
            print('%s:' % lab)
        print('  %08x  %s' % (pc, dis(w, pc, syms, hi)))
        op = w >> 26
        if clear:
            hi = {k: v for k, v in hi.items() if 16 <= k < 24}   # callee-saved survive a call
            clear = False
        if op in (2, 3) or (op == 0 and (w & 63) in (8, 9)):
            clear = True
        elif op == 0:
            hi.pop((w >> 11) & 31, None)
        elif op not in (0xF, 0x28, 0x29, 0x2B, 0x3F, 0x1F, 0x39, 0x2A, 0x2E, 0x2C, 0x2D) and op >= 8:
            hi.pop((w >> 16) & 31, None)


def main():
    d, segs, syms, funcs = load(sys.argv[1])
    STR.update(d=d, segs=segs, syms=syms)
    if sys.argv[2] == '--find':
        for n, (a, s) in sorted(funcs.items(), key=lambda x: x[1]):
            if sys.argv[3].lower() in n.lower():
                print('%08x %5x %s' % (a, s, n))
        return
    if sys.argv[2] == '--addr':
        a = int(sys.argv[3], 16)
        disfunc(d, segs, syms, a, 4 * int(sys.argv[4] if len(sys.argv) > 4 else 64))
        return
    for f in sys.argv[2:]:
        a, s = funcs[f]
        print('%s:  (0x%x, %d bytes)' % (f, a, s))
        disfunc(d, segs, syms, a, s)


if __name__ == '__main__':
    main()
