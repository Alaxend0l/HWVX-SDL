#!/usr/bin/env python3
"""tools/proto_pack.py ARCHIVE [TYPE ID [--song OUTDIR]]

TOC entries: version, type, id (a quadrant's: its world's), sub id (a
quadrant's pack id), size, offset.

Reads one of the PS2 prototype's archives (orig/ps2-beta XPS/..., archive
version 177/179) in place: its TOC (offsets counted from the end of each
entry), and a pack's header (20 words, two 0x14-byte refs) and section
sizes. --song writes the pack's song (from songOffset) and song bank (from
songBankOffset) as song.bin / bank.bin into OUTDIR, for looking at the
format; keep OUTDIR in a build folder: it is game data.
"""
import struct
import sys

NAMES = ["dataSize", "soundDataSize", "numSoundPointers", "numPointers", "numRelocs", "numTextures",
         "textureDataOffset", "numSoundRelocs", "numSounds", "sfxOffset", "palettes", "paletteOffset",
         "numShaders", "numGX", "numTexRelocs", "shaderOffset", "numTriggers", "triggerOffset", "songOffset",
         "songBankOffset"]
TYPES = ["car", "interface", "item", "link", "world", "collision", "world_texture", "geometry", "share", "audio",
         "music"]


def toc(d):
    magic, kind, ver, count = struct.unpack_from('<IIII', d, 0)
    out = []
    for i in range(count):
        v, t, pid, sub, size, off = struct.unpack_from('<IIIIII', d, 0x10 + i * 24)
        out.append(dict(version=v, type=t, id=pid, sub=sub, size=size, offset=0x10 + (i + 1) * 24 + off))
    return ver, out


def main():
    d = open(sys.argv[1], 'rb').read()
    ver, entries = toc(d)
    if len(sys.argv) < 4:
        print('version %d, %d packs' % (ver, len(entries)))
        for e in entries:
            print('  %-14s id %-4d sub %-6d (0x%x) v%d size %d at 0x%x' % (
                TYPES[e['type']] if e['type'] < len(TYPES) else e['type'], e['id'], e['sub'] & 0xFFFF, e['sub'],
                e['version'], e['size'], e['offset']))
        return
    t, i = int(sys.argv[2]), int(sys.argv[3])
    e = [x for x in entries if x['type'] == t and x['id'] == i][0]
    o = e['offset']
    h = dict(zip(NAMES, struct.unpack_from('<20I', d, o)))
    for k in NAMES:
        print('%-18s %d (0x%x)' % (k, h[k], h[k]))
    snd = o + 0x78
    base = snd + ((h['numSoundPointers'] * 4))
    print('sound pointers at 0x%x, sound data at 0x%x' % (snd, base))
    if '--song' in sys.argv:
        out = sys.argv[sys.argv.index('--song') + 1]
        sd = d[base:base + h['soundDataSize']]
        so, bo = h['songOffset'], h['songBankOffset']
        ends = sorted(x for x in (so, bo, h['soundDataSize']) if x > 0)
        song = sd[so:min([x for x in ends if x > so] or [len(sd)])]
        bank = sd[bo:min([x for x in ends if x > bo] or [len(sd)])] if bo else b''
        open(out + '/song.bin', 'wb').write(song)
        open(out + '/bank.bin', 'wb').write(bank)
        print('song %d bytes at 0x%x, bank %d bytes at 0x%x' % (len(song), so, len(bank), bo))


if __name__ == '__main__':
    main()
