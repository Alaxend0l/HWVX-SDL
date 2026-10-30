#!/usr/bin/env python3
"""Inspect a BrainCore data pack (.xgc) from the disc: TOC, pack header,
relocation tables, and the root table of each pack's data blob. Helps work out
the formats the port has to byte-swap (src/game/Port_Swap.c).

  tools/xgc_inspect.py orig/files/mcp.xgc [--pack N] [--dump OFFSET [N]] [--refs OFFSET]

--refs: pointer slots aimed at OFFSET or just before it (the object holding
OFFSET), then the slots aimed at those slots' objects, a few levels up: shows
which structure a swap checker report belongs to.
"""
import struct
import sys

HDR = ["dataSize", "soundDataSize", "numSoundPointers", "numPointers", "numRelocs",
       "numTextures", "textureDataOffset", "numSoundRelocs", "numSounds", "sfxOffset",
       "palettes", "paletteOffset", "numShaders", "numGX", "numTexRelocs", "shaderOffset",
       "numTriggers", "triggerOffset", "songOffset", "songBankOffset"]


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


class Pack:
    def __init__(self, d, off):
        self.h = dict(zip(HDR, struct.unpack_from(">20I", d, off)))
        self.refs = []
        for i in range(2):
            ro = off + 80 + i * 0x14
            self.refs.append((d[ro:ro + 12].split(b"\0")[0].decode(errors="replace"), be32(d, ro + 12)))
        o = off + 80 + 0x28
        h = self.h
        if h["soundDataSize"]:
            n = h["numSoundPointers"] * 4
            pad = (n & 0x1F) and 0x20 - (n & 0x1F)
            o += n + pad + h["soundDataSize"]
        self.relocs_off = o
        rs = h["numRelocs"] * 8 + h["numGX"] * 8 + h["numSoundRelocs"] * 8
        pad = (rs & 0x1F) and 0x20 - (rs & 0x1F)
        self.base_off = o + rs + pad     # file offset of the data blob
        self.texrel_off = self.base_off + h["dataSize"]
        self.ptr_off = self.texrel_off + h["numTexRelocs"] * 8
        self.blob = d[self.base_off:self.base_off + h["dataSize"]]
        self.pointers = [be32(d, self.ptr_off + 4 * i) for i in range(h["numPointers"])]
        self.texrelocs = [(be32(d, self.texrel_off + 8 * i), be32(d, self.texrel_off + 8 * i + 4))
                          for i in range(h["numTexRelocs"])]


def refs(p, off, window=0x80, depth=3):
    """Slots pointing into [off - window, off], nearest target first, then
    the slots pointing at the objects holding those slots."""
    targets = {}
    for slot in p.pointers:
        if slot + 4 <= len(p.blob):
            targets.setdefault(be32(p.blob, slot), []).append(slot)

    def walk(o, level, seen):
        hits = sorted(((o - t, t) for t in targets if o - window <= t <= o))[:3]
        for dist, t in hits:
            for slot in targets[t][:4]:
                print("%s0x%X+0x%X <- slot 0x%X" % ("  " * level, t, dist, slot))
                if level + 1 < depth and slot not in seen:
                    walk(slot, level + 1, seen | {slot})

    print("refs to 0x%X:" % off)
    walk(off, 0, set())


def main():
    d = open(sys.argv[1], "rb").read()
    magic = struct.unpack_from(">4I", d, 0)
    print("magic %08X %08X version %d entries %d" % magic)
    n = magic[3]
    packs = []
    for i in range(n):
        e = struct.unpack_from(">6I", d, 16 + i * 24)
        off = e[5] + 0x10 + n * 24
        print("entry %d: version %d type %d id %d unk %08X %08X offset 0x%X -> file 0x%X"
              % (i, e[0], e[1], e[2], e[3], e[4], e[5], off))
        p = Pack(d, off)
        packs.append(p)
        print("   ", {k: v for k, v in p.h.items() if v})
        print("    refs", p.refs, " blob at file 0x%X" % p.base_off)
        ptrs = set(p.pointers)
        b = p.blob
        if len(b) >= 8:
            cnt, tab = be32(b, 0), be32(b, 4)
            print("    root: count %d table @0x%X (ptr: %s)" % (cnt, tab, 4 in ptrs))
            for k in range(min(cnt, 20)):
                if tab + 8 * k + 8 > len(b):
                    break
                print("      [%d] obj @0x%X id %d" % (k, be32(b, tab + 8 * k), be32(b, tab + 8 * k + 4)))
    p = packs[0]
    if "--pack" in sys.argv:
        p = packs[int(sys.argv[sys.argv.index("--pack") + 1], 0)]
    if "--refs" in sys.argv:
        refs(p, int(sys.argv[sys.argv.index("--refs") + 1], 0))
    if "--dump" in sys.argv:
        i = sys.argv.index("--dump")
        o = int(sys.argv[i + 1], 0)
        cnt = int(sys.argv[i + 2], 0) if len(sys.argv) > i + 2 else 64
        ptrs = set(p.pointers)
        for a in range(o, o + cnt * 4, 4):
            w = be32(p.blob, a)
            f = struct.unpack_from(">f", p.blob, a)[0]
            raw = p.blob[a:a + 4]
            txt = "".join(chr(c) if 32 <= c < 127 else "." for c in raw)
            print("  %06X: %08X %s %-14g %s" % (a, w, "PTR" if a in ptrs else "   ", f, txt))


if __name__ == "__main__":
    main()
