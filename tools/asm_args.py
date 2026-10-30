#!/usr/bin/env python3
"""Guess a function's parameters from the decomp's assembly.

Reports the argument registers (r3-r10, f1-f8) that the function reads before
writing them, scanning the function in address order (branches ignored, calls
clobber the volatile registers). An approximation, since arguments are usually
copied to saved registers or stored in the prologue. Good enough to settle "how many arguments does this
take" disputes between the decomp's call sites and its definitions.

  tools/asm_args.py NAME...          # needs HWVX-GC-Decomp built (build/HOTWHE/asm)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASM = ROOT / "HWVX-GC-Decomp" / "build" / "HOTWHE" / "asm"
INSN = re.compile(r"^/\* [0-9A-F]+ [0-9A-F]+\s+(?:[0-9A-F]{2} ){4}\*/\s+(\S+)\s*(.*)$")
GPR_ARGS = [f"r{i}" for i in range(3, 11)]
FPR_ARGS = [f"f{i}" for i in range(1, 9)]
# instructions whose first operand is a destination
NO_DEST = re.compile(r"^(st|cmp|b|mt|dcb|icb|sync|isync|crxor|crclr|psq_st|tw)")


def find(name):
    pat = re.compile(r"^\.fn " + re.escape(name) + r",")
    for f in sorted(ASM.rglob("*.s")):
        lines = f.read_text(errors="replace").split("\n")
        for i, l in enumerate(lines):
            if pat.match(l):
                out = []
                for l2 in lines[i + 1:]:
                    if l2.startswith(".endfn"):
                        break
                    out.append(l2)
                return f, out
    return None, None


def regs(s):
    return re.findall(r"\b([rf]\d+)\b", s)


def analyze(lines):
    written, used = set(), []
    passed = None
    for l in lines:
        m = INSN.match(l.strip())
        if not m:
            continue
        op, args = m.group(1), m.group(2).split("#")[0]
        ops = [a.strip() for a in args.split(",")] if args.strip() else []
        if op in ("bl", "bctrl", "blrl"):
            # a call: argument registers still unwritten may be this
            # function's own arguments passed straight on; the call then
            # clobbers the volatile registers
            if passed is None:
                passed = [r for r in GPR_ARGS + FPR_ARGS if r not in written]
            written |= set(GPR_ARGS + FPR_ARGS)
            continue
        dest = None if NO_DEST.match(op) else (ops[0] if ops else None)
        srcs = ops[1:] if dest else ops
        # stores: first operand is a source; update forms write the base
        for s in srcs + ([dest] if dest and op.startswith(("stwu", "stfdu")) else []):
            for r in regs(s):
                if r not in written and r in GPR_ARGS + FPR_ARGS and r not in used:
                    used.append(r)
        if dest:
            for r in regs(dest):
                written.add(r)
    return used, passed or []


def main():
    for name in sys.argv[1:]:
        f, lines = find(name)
        if f is None:
            print(f"{name}: not found")
            continue
        used, passed = analyze(lines)
        g = [r for r in GPR_ARGS if r in used]
        fp = [r for r in FPR_ARGS if r in used]
        # only the low registers are plausible pass-throughs
        maybe = [r for r in ("r3", "r4", "r5", "f1") if r in passed and r not in used]
        extra = f"   (unset before the first call, maybe passed on: {' '.join(maybe)})" if maybe else ""
        print(f"{name}: {' '.join(g + fp) or '(no args read)'}   [{f.name}]{extra}")


if __name__ == "__main__":
    main()
