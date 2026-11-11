#!/usr/bin/env python3
"""xdl_cache_diff.py A B: compare two .xdl caches (src/port/xdl.c), pack by pack.

A and B are cache folders (<dir>/<gc|pc|ps2>/<file>.xdl, searched recursively)
or two .xdl files. Packs are matched by file name and source offset; their
header, segments, fixups and song bank reference must be the same bytes.
Where the packs sit in the file doesn't matter (a cache written as the game
loads and one converted ahead of time hold them in different orders).
Packs only one side has are counted, not errors, unless --same-set.
Exit status 1 if any common pack differs.

  tools/xdl_cache_diff.py build/t/lazy build/t/converted/gc
"""
import os
import struct
import sys

MAX_SEGS = 15
SEG_NAMES = ["MAIN", "SOUND", "BANK", "SFX", "RAW_HEADER", "RAW_SOUND", "RAW_MAIN"] + ["BLOCK%d" % i for i in range(8)]


def load(path):
    """{srcOffset: (type, id, header bytes, [segment (size, role, bytes)], bank, fixups)} and the file header."""
    d = open(path, "rb").read()
    magic, version, compiler, source, ssize, stime, nsec, table, flags, settings = struct.unpack_from("<4sIIIQqIIII", d, 0)
    if magic != b"XDL1":
        raise ValueError("%s: not an .xdl cache file" % path)
    secs = [struct.unpack_from("<IIQIIHHI", d, table + i * 0x20) for i in range(nsec)]
    kind, _, off, size = secs[-1][:4]
    packs = {}
    for i in range(size // 16):
        typ, pid, src, psec = struct.unpack_from("<IIII", d, off + i * 16)
        po = secs[psec][2]
        ptype, pid2, psrc, hsize = struct.unpack_from("<IIII", d, po)
        header = d[po + 16:po + 16 + hsize]
        segs = []
        so = po + 16 + 0x80
        for k in range(MAX_SEGS):
            ssz, role, sec, _ = struct.unpack_from("<IIII", d, so + k * 16)
            if ssz == 0:
                segs.append(None)
                continue
            o = secs[sec][2]
            segs.append((ssz, role, d[o:o + secs[sec][3]]))
        bank_seg, bank_off, nfix, fsec = struct.unpack_from("<iIII", d, so + MAX_SEGS * 16)
        fo = secs[fsec][2]
        fix = sorted(struct.unpack_from("<IHH", d, fo + k * 8) for k in range(nfix))
        packs[src] = (typ, pid, header, segs, (bank_seg, bank_off), fix)
    return (version, compiler, source, ssize, stime, settings), packs


def files(root):
    if os.path.isfile(root):
        return {os.path.basename(root): root}
    out = {}
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.endswith(".xdl"):
                out[os.path.relpath(os.path.join(dirpath, n), root)] = os.path.join(dirpath, n)
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    same_set = "--same-set" in sys.argv
    if len(args) != 2:
        sys.exit(__doc__)
    a, b = files(args[0]), files(args[1])
    common = sorted(set(a) & set(b))
    bad = same = only_a = only_b = 0
    for name in common:
        ha, pa = load(a[name])
        hb, pb = load(b[name])
        if ha != hb:
            print("%s: file headers differ (version, compiler, source, size, time, settings): %s / %s" % (name, ha, hb))
            bad += 1
        only_a += len(set(pa) - set(pb))
        only_b += len(set(pb) - set(pa))
        for src in sorted(set(pa) & set(pb)):
            x, y = pa[src], pb[src]
            what = []
            if x[:2] != y[:2]:
                what.append("type/id %s / %s" % (x[:2], y[:2]))
            if x[2] != y[2]:
                what.append("header")
            for k in range(MAX_SEGS):
                if x[3][k] != y[3][k]:
                    sx, sy = x[3][k], y[3][k]
                    if sx is None or sy is None:
                        what.append("%s only in %s" % (SEG_NAMES[k], "B" if sx is None else "A"))
                    elif sx[:2] != sy[:2]:
                        what.append("%s size/role %s / %s" % (SEG_NAMES[k], sx[:2], sy[:2]))
                    else:
                        n = sum(1 for i in range(len(sx[2])) if sx[2][i] != sy[2][i])
                        first = next(i for i in range(len(sx[2])) if sx[2][i] != sy[2][i])
                        what.append("%s %d bytes differ, first at 0x%x" % (SEG_NAMES[k], n, first))
            if x[4] != y[4]:
                what.append("bank %s / %s" % (x[4], y[4]))
            if x[5] != y[5]:
                what.append("fixups (%d / %d)" % (len(x[5]), len(y[5])))
            if what:
                bad += 1
                print("%s pack at 0x%x (type %d id %d): %s" % (name, src, x[0], x[1], "; ".join(what)))
            else:
                same += 1
    fa, fb = len(set(a) - set(b)), len(set(b) - set(a))
    print("%d files in both (%d only in A, %d only in B); %d packs the same, %d differ; %d packs only in A, %d only in B"
          % (len(common), fa, fb, same, bad, only_a, only_b))
    if same_set and (fa or fb or only_a or only_b):
        sys.exit(1)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
