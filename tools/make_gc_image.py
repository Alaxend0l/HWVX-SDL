#!/usr/bin/env python3
"""Builds a GameCube disc image from an extracted disc, to test the port's
disc image reader (src/port/disc.c) without a dump at hand.

  tools/make_gc_image.py DISC OUT.iso [--nkit]

DISC holds sys/ (boot.bin, bi2.bin, apploader.img, main.dol, fst.bin) and
files/ (what Dolphin's "Extract Entire Disc" makes). Without --nkit: a plain
image with every file at its offset in sys/fst.bin, as the disc has it (the
gaps zero, not the disc's junk, and the file sparse where the system allows).
With --nkit: the layout of an NKit image (.nkit.iso): "NKIT" at 0x200, the
junk between files dropped and the files packed after the file table, whose
offsets are rewritten to match. The output is the player's own data: never
commit it.
"""
import os
import struct
import sys


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    nkit = "--nkit" in sys.argv
    if len(args) != 2:
        sys.exit(__doc__)
    disc, out = args
    sysd, files = os.path.join(disc, "sys"), os.path.join(disc, "files")
    boot = bytearray(open(os.path.join(sysd, "boot.bin"), "rb").read())
    bi2 = open(os.path.join(sysd, "bi2.bin"), "rb").read()
    apploader = open(os.path.join(sysd, "apploader.img"), "rb").read()
    dol = open(os.path.join(sysd, "main.dol"), "rb").read()
    fst = bytearray(open(os.path.join(sysd, "fst.bin"), "rb").read())
    dol_off, fst_off = be32(boot, 0x420), be32(boot, 0x424)

    entries = be32(fst, 8)
    names = entries * 12

    def name(i):
        o = names + (be32(fst, i * 12) & 0xFFFFFF)
        return fst[o:fst.index(0, o)].decode("latin-1")

    # each file's host path: walk the table keeping the open directories
    paths, stack = {}, [(entries, files)]
    for i in range(1, entries):
        while i >= stack[-1][0]:
            stack.pop()
        if fst[i * 12]:
            stack.append((be32(fst, i * 12 + 8), os.path.join(stack[-1][1], name(i))))
        else:
            paths[i] = os.path.join(stack[-1][1], name(i))

    if nkit:
        pos = (fst_off + len(fst) + 3) & ~3
        for i in sorted(paths, key=lambda i: be32(fst, i * 12 + 4)):
            struct.pack_into(">I", fst, i * 12 + 4, pos)
            pos = (pos + be32(fst, i * 12 + 8) + 3) & ~3
        # NKit's header: magic, version, the original image's CRC and size
        boot[0x200:0x21C] = b"NKIT" + b"v01\0" + struct.pack(">II", 0, 1459978240) + bytes(12)
        struct.pack_into(">I", boot, 0x428, len(fst))
        struct.pack_into(">I", boot, 0x42C, len(fst))
    end = 0
    with open(out, "wb") as f:
        def put(off, data):
            nonlocal end
            f.seek(off)
            f.write(data)
            end = max(end, off + len(data))
        put(0, boot)
        put(0x440, bi2)
        put(0x2440, apploader)
        put(dol_off, dol)
        put(fst_off, fst)
        for i, p in paths.items():
            data = open(p, "rb").read()
            if len(data) != be32(fst, i * 12 + 8):
                sys.exit(f"{p}: {len(data)} bytes, the table says {be32(fst, i * 12 + 8)}")
            put(be32(fst, i * 12 + 4), data)
        if not nkit:
            f.truncate(max(end, 1459978240))   # a full disc's size
    print(f"{out}: {len(paths)} files, {os.path.getsize(out) / 1048576:.1f} MB{' (NKit layout)' if nkit else ''}")


main()
