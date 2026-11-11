#!/usr/bin/env python3
"""A pack's bulk data <-> standard files: textures PNG, sounds WAV, songs .mid with a JSONC bank.

    xdl_media.py extract ARCHIVE[:PACK] OUT       one pack (car_5, geometry_0_q12, or a TOC
                                                  index); without :PACK every pack, OUT/<pack>/
    xdl_media.py build OUT PACK_FILE [--base ARCHIVE]   the pack's bytes (a TOC entry's payload)
    xdl_media.py roundtrip ARCHIVE|FOLDER ... [--fresh] [--keep OUT]
                                                  extract + build every pack, compare bytes

ARCHIVE is a release's own file: .xgc (GameCube, big-endian), .xpc (PC) or .xps (PS2), both
little-endian. Packs are read from their raw bytes here; the port's .xdl cache is another path
(pack -> cache, in the port). docs/xdl-format/README.md §12.

What a pack's folder holds (`media.jsonc` lists it):

    media.jsonc                   the pack: base:// id, release, source archive + fingerprint
    texture/<i>/texture.jsonc     format, size, levels, header words, hashes
    texture/<i>/level0.png ...    one PNG per mip level (level0 = the full size)
    sound/<i>.wav + <i>.jsonc     PCM16 mono; rate, loop, volume, the descriptor's other fields
    song/song.mid                 a standard MIDI file (the RIFX 'MIDI' chunk's body)
    song/bank.jsonc               instruments: program, key range, envelope, sample
    song/bank/<k>.wav             the bank's samples

<i> is the asset's index in the GameCube's table (pairing.md §1), or `<release>:<i>` for an asset
the GameCube lacks.

`build` starts from the player's own copy of the pack (the base, located through media.jsonc or
--base) and writes every texture level, sample, song and bank from the files. Lossless data
(GameCube RGBA8, PC RGB/RGBA, PS2 PSMCT32 and PSMT8 as a palette PNG, PCM16 samples, songs,
bank tables) is encoded from the files every time. Lossy encodings (CMPR, DXT1/5, SPU-ADPCM)
can't be recovered from the decoded pixels or samples, so their sidecars record two hashes: of
the encoded bytes, and of what they decode to. While a file still decodes to the recorded hash,
build copies the base's encoding (after checking its hash); an edited file is encoded afresh.
`--fresh` encodes every lossy asset afresh (to test the encoders).
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_struct as XS  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SCHEMAS = ROOT / "schemas"
FORMAT_VERSION = 1
TOOL = "tools/xdl_media.py"
# gitignored output: HWVX-SDL/build-media (a worktree sits inside HWVX-SDL, one level deeper)
OUT = (ROOT.parent if ROOT.parent.name == "HWVX-SDL" else ROOT) / "build-media"


def release_dirs():
    """Where the releases' archives usually are: orig/ of the editor repo, the PC import."""
    out = {}
    for rel, sub in (("gc", "orig/gc/files"), ("ps2", "orig/ps2/XPS/ENGLISH")):
        p = XS.find_up(sub)
        if p:
            out[rel] = p
    pc = Path.home() / "Library/Application Support/HWVX-SDL/HotWheelsVelocityX/pc"
    if pc.is_dir():
        out["pc"] = pc
    return out


class MediaError(Exception):
    pass


def h16(b):
    """The pairing table's hash: BLAKE2b, 16-byte digest, hex."""
    return hashlib.blake2b(bytes(b), digest_size=16).hexdigest()


# ------------------------------------------------------------------------------------------
# archives and packs (as src/archive.rs and src/pack.rs of the editor read them)

MAGIC0, MAGIC1 = 0x020100A0, 0x040100AE
TYPE_NAMES = ["car", "interface", "item", "link", "world", "collision", "world_texture",
              "geometry", "share", "audio", "music"]
TYPE_GEOMETRY = 7
STD_HEADER = 120
QUAD_HEADER = 84


def align(v, a):
    return (v + a - 1) // a * a


def detect_release(b, path=None):
    if struct.unpack_from(">I", b, 0)[0] == MAGIC0:
        return "gc"
    if struct.unpack_from("<I", b, 0)[0] != MAGIC0:
        raise MediaError(f"{path}: not a Velocity X archive")
    version, count = struct.unpack_from("<2I", b, 8)
    if version < 183:
        return "proto"
    if count:
        first = struct.unpack_from("<I", b, 16 + 20)[0]
        if first and (16 + 24 * count + first) % 2048 == 0:
            return "ps2"
        return "pc"
    return "ps2" if str(path).lower().endswith(".xps") else "pc"


class Archive:
    """An archive's table of contents; packs are byte ranges of the file."""

    def __init__(self, path):
        self.path = Path(path)
        self.bytes = self.path.read_bytes()
        self.release = detect_release(self.bytes, self.path)
        if self.release == "proto":
            raise MediaError(f"{path}: the PS2 prototype's packs are not supported")
        self.e = ">" if self.release == "gc" else "<"
        _, _, self.version, count = struct.unpack_from(self.e + "4I", self.bytes, 0)
        self.name = self.path.stem
        base = 16 + 24 * count
        self.entries = []
        for i in range(count):
            ver, typ, pid, sub, size, off = struct.unpack_from(self.e + "6I", self.bytes, 16 + 24 * i)
            tname = TYPE_NAMES[typ] if typ < len(TYPE_NAMES) else f"type{typ}"
            name = f"{tname}_{pid}" + (f"_q{sub & 0xFFFF}" if typ == TYPE_GEOMETRY else "")
            self.entries.append({"index": i, "type": typ, "type_name": tname, "id": pid, "sub_id": sub,
                                 "version": ver, "start": base + off, "size": size, "name": name})

    def find(self, sel):
        for ent in self.entries:
            if sel in (ent["name"], str(ent["index"])):
                return ent
        raise MediaError(f"{self.path.name}: no pack {sel!r} "
                         f"({', '.join(e['name'] for e in self.entries[:8])}...)")

    def pack_bytes(self, ent):
        return self.bytes[ent["start"]:ent["start"] + ent["size"]]


STD_WORDS = ["data_size", "sound_data_size", "num_sound_pointers", "num_pointers", "num_tex_relocs",
             "num_textures", "texture_data_offset", "num_sound_relocs", "num_sounds", "sfx_offset",
             "num_palettes", "palette_offset", "num_shaders", "num_shader_relocs", "num_ref_relocs",
             "shader_offset", "num_triggers", "trigger_offset", "song_offset", "song_bank_offset"]


class RawPack:
    """One pack's bytes and where its sections are. Retail layouts only (pack.rs)."""

    def __init__(self, release, typ, b, name="?"):
        self.release, self.type, self.b, self.name = release, typ, bytes(b), name
        self.e = ">" if release == "gc" else "<"
        self.quad = typ == TYPE_GEOMETRY
        self.sec = {}           # name -> (start, length) in the pack
        self.h = {}
        if self.quad:
            w = struct.unpack_from(self.e + "11I", self.b, 0)
            pos = QUAD_HEADER
            for name_, n in (("texture_relocs", w[2] * 8), ("shader_relocs", w[7] * 8),
                             ("ref_relocs", w[10] * 8), ("pointers", w[1] * 4)):
                self.sec[name_] = (pos, n)
                pos += n
            pos += w[9]
            self.sec["data"] = (pos, w[0])
            end = pos + w[0]
        else:
            w = struct.unpack_from(self.e + "20I", self.b, 0)
            self.h = dict(zip(STD_WORDS, w))
            h = self.h
            pos = STD_HEADER
            if release == "ps2":
                pos = align(pos, 2048)
            if h["sound_data_size"]:
                self.sec["sound_pointers"] = (pos, h["num_sound_pointers"] * 4)
                pos += align(h["num_sound_pointers"] * 4, 32)      # the length is padded
                self.sec["sound_data"] = (pos, h["sound_data_size"])
                pos += h["sound_data_size"]
                if release == "ps2":
                    pos = align(pos, 2048)
            n = (h["num_tex_relocs"] + h["num_shader_relocs"] + h["num_sound_relocs"]) * 8
            self.sec["texture_relocs"] = (pos, h["num_tex_relocs"] * 8)
            self.sec["shader_relocs"] = (pos + h["num_tex_relocs"] * 8, h["num_shader_relocs"] * 8)
            self.sec["sound_relocs"] = (pos + (h["num_tex_relocs"] + h["num_shader_relocs"]) * 8,
                                        h["num_sound_relocs"] * 8)
            pos += align(n, 32)
            self.sec["data"] = (pos, h["data_size"])
            pos += h["data_size"]
            self.sec["ref_relocs"] = (pos, h["num_ref_relocs"] * 8)
            pos += h["num_ref_relocs"] * 8
            self.sec["pointers"] = (pos, h["num_pointers"] * 4)
            end = pos + h["num_pointers"] * 4
        if end != len(self.b):
            raise MediaError(f"{name}: pack layout ends at 0x{end:X}, the pack is 0x{len(self.b):X} bytes")

    def section(self, name):
        s, n = self.sec.get(name, (0, 0))
        return self.b[s:s + n]

    def words(self, name):
        d = self.section(name)
        return list(struct.unpack(self.e + f"{len(d) // 4}I", d))


# ------------------------------------------------------------------------------------------
# texture formats
#
# (release, format) -> name, block size, bytes per block, lossless. Level sizes are whole
# blocks (GameCube CMPR 8x8 tiles of four 4x4 sub-blocks; RGBA8 4x4 tiles; DXT 4x4 blocks).

TEX_FORMATS = {
    ("gc", 4): ("CMPR", 8, 32, False),
    ("gc", 1): ("RGBA8", 4, 64, True),
    ("pc", 4): ("DXT1", 4, 8, False),
    ("pc", 5): ("DXT5", 4, 16, False),
    ("ps2", 1): ("PSMCT32", 1, 4, True),
    ("ps2", 2): ("PSMT8", 1, 1, True),     # lossless as a palette PNG (indices + CLUT)
}
TEX_CODE = {(rel, v[0]): k for (rel, k), v in TEX_FORMATS.items()}


def level_size(rel, fmt, w, h):
    _, blk, nb, _ = TEX_FORMATS[(rel, fmt)]
    return ((w + blk - 1) // blk) * ((h + blk - 1) // blk) * nb


def level_dims(w, h, k):
    return max(1, w >> k), max(1, h >> k)


def csm1(i):
    """PS2 CLUT: logical index -> stored position (bits 3 and 4 swapped); its own inverse."""
    return (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


CSM1 = np.array([csm1(i) for i in range(256)])


def _565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int64)


def _palette(c0, c1, four_always, transparent):
    """Colour palettes [..., 4, 4] of DXT1-style blocks, as the decoders compute them."""
    a, b = _565(c0), _565(c1)
    four = np.ones(c0.shape, bool) if four_always else (c0 > c1)
    p2 = np.where(four[..., None], (2 * a + b) // 3, (a + b) // 2)
    p3 = np.where(four[..., None], (a + 2 * b) // 3, 0)
    pal = np.stack([a, b, p2, p3], -2)
    alpha = np.full(c0.shape + (4,), 255, np.int64)
    if transparent:
        alpha[..., 3] = np.where(four, 255, 0)
    return np.concatenate([pal, alpha[..., None]], -1)


def _blocks_to_image(px, bh, bw, w, h):
    """px [bh, bw, 16, 4] (texel k = y * 4 + x) -> image [h, w, 4]."""
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w].astype(np.uint8)


def _image_to_blocks(img, blk=4):
    """image [h, w, 4] -> [bh, bw, 16, 4], edges repeated to whole blocks."""
    h, w = img.shape[:2]
    H, W = (h + blk - 1) // blk * blk, (w + blk - 1) // blk * blk
    img = np.pad(img, ((0, H - h), (0, W - w), (0, 0)), mode="edge")
    return img.reshape(H // 4, 4, W // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(H // 4, W // 4, 16, 4)


def _dxt5_alpha_decode(blk):
    a0, a1 = blk[..., 0], blk[..., 1]
    bits = sum(blk[..., 2 + k] << (8 * k) for k in range(6))
    idx = np.stack([(bits >> (3 * k)) & 7 for k in range(16)], -1)
    k = np.arange(8)
    a0e, a1e = a0[..., None], a1[..., None]
    eight = np.where(k == 0, a0e, np.where(k == 1, a1e, ((8 - k) * a0e + (k - 1) * a1e) // 7))
    six = np.where(k == 0, a0e, np.where(k == 1, a1e, ((6 - k) * a0e + (k - 1) * a1e) // 5))
    six = np.where(k == 6, 0, np.where(k == 7, 255, six))
    tab = np.where((a0 > a1)[..., None], eight, six)
    return np.take_along_axis(tab, idx, -1), tab


def decode_level(rel, fmt, raw, w, h, clut=None):
    """One level's stored bytes -> RGBA uint8 [h, w, 4]."""
    name = TEX_FORMATS[(rel, fmt)][0]
    b = np.frombuffer(raw, np.uint8)
    if name in ("DXT1", "DXT5"):
        bw, bh = (w + 3) // 4, (h + 3) // 4
        blk = b.reshape(bh, bw, -1).astype(np.int64)
        alpha = None
        if name == "DXT5":
            alpha, _ = _dxt5_alpha_decode(blk)
            blk = blk[..., 8:]
        c0 = blk[..., 0] | (blk[..., 1] << 8)
        c1 = blk[..., 2] | (blk[..., 3] << 8)
        pal = _palette(c0, c1, name == "DXT5", False)
        idx = np.stack([(blk[..., 4 + y] >> (2 * x)) & 3 for y in range(4) for x in range(4)], -1)
        px = np.take_along_axis(pal, idx[..., None].repeat(4, -1), -2)
        if alpha is not None:
            px[..., 3] = alpha
        return _blocks_to_image(px, bh, bw, w, h)
    if name == "CMPR":
        tw, th = (w + 7) // 8, (h + 7) // 8
        blk = b.reshape(th, tw, 2, 2, 8).astype(np.int64)
        c0 = (blk[..., 0] << 8) | blk[..., 1]
        c1 = (blk[..., 2] << 8) | blk[..., 3]
        pal = _palette(c0, c1, False, True)
        idx = np.stack([(blk[..., 4 + y] >> (6 - 2 * x)) & 3 for y in range(4) for x in range(4)], -1)
        px = np.take_along_axis(pal, idx[..., None].repeat(4, -1), -2)  # [th, tw, 2, 2, 16, 4]
        img = px.reshape(th, tw, 2, 2, 4, 4, 4).transpose(0, 2, 4, 1, 3, 5, 6).reshape(th * 8, tw * 8, 4)
        return img[:h, :w].astype(np.uint8)
    if name == "RGBA8":
        tw, th = (w + 3) // 4, (h + 3) // 4
        blk = b.reshape(th, tw, 2, 16, 2)
        a, r = blk[:, :, 0, :, 0], blk[:, :, 0, :, 1]
        g, bb = blk[:, :, 1, :, 0], blk[:, :, 1, :, 1]
        return _blocks_to_image(np.stack([r, g, bb, a], -1), th, tw, w, h)
    if name == "PSMCT32":
        return b.reshape(h, w, 4).copy()
    if name == "PSMT8":
        return clut_rgba(clut)[b.reshape(h, w)]
    raise MediaError(f"{rel} texture format {fmt}")


def clut_rgba(clut):
    """A stored 0x400-byte CLUT -> [256, 4] RGBA by logical index."""
    return np.frombuffer(clut, np.uint8).reshape(256, 4)[CSM1]


def clut_bytes(pal):
    """[256, 4] RGBA by logical index -> the stored CLUT."""
    return np.ascontiguousarray(np.asarray(pal, np.uint8)[CSM1]).tobytes()


# ---- encoders (used for edited pixels; unchanged ones reuse the base's encoding) ----------

def _endpoints(px, weight):
    """Principal-axis endpoints of each block's colours. px [..., 16, 3] float, weight [..., 16]."""
    wsum = np.maximum(weight.sum(-1, keepdims=True), 1e-9)
    mean = (px * weight[..., None]).sum(-2) / wsum
    d = (px - mean[..., None, :]) * np.sqrt(weight)[..., None]
    cov = np.einsum("...ki,...kj->...ij", d, d)
    axis = np.ones(mean.shape)
    for _ in range(8):
        axis = np.einsum("...ij,...j->...i", cov, axis)
        n = np.linalg.norm(axis, axis=-1, keepdims=True)
        axis = np.where(n > 1e-9, axis / np.maximum(n, 1e-9), np.array([0.577, 0.577, 0.577]))
    t = np.einsum("...ki,...i->...k", px - mean[..., None, :], axis)
    big = 1e9
    tmin = np.where(weight > 0, t, big).min(-1)
    tmax = np.where(weight > 0, t, -big).max(-1)
    tmin = np.where(tmin == big, 0, tmin)
    tmax = np.where(tmax == -big, 0, tmax)
    lo = mean + tmin[..., None] * axis
    hi = mean + tmax[..., None] * axis
    return lo, hi


def _to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.int64)
    return ((c[..., 0] * 31 + 127) // 255 << 11) | ((c[..., 1] * 63 + 127) // 255 << 5) | ((c[..., 2] * 31 + 127) // 255)


def _encode_colour(blocks, transparent_mode, four_always):
    """blocks [..., 16, 4] uint8 -> (c0, c1, indices [..., 16]) for DXT1-style colour."""
    px = blocks[..., :3].astype(np.float64)
    trans = (blocks[..., 3] < 128) if transparent_mode else np.zeros(blocks.shape[:-1], bool)
    lo, hi = _endpoints(px, (~trans).astype(np.float64))
    c_hi, c_lo = _to565(hi), _to565(lo)
    has_t = trans.any(-1)
    # 4-colour mode needs c0 > c1, 3-colour (transparent texels) c0 <= c1
    c0 = np.where(has_t, np.minimum(c_hi, c_lo), np.maximum(c_hi, c_lo))
    c1 = np.where(has_t, np.maximum(c_hi, c_lo), np.minimum(c_hi, c_lo))
    pal = _palette(c0, c1, four_always, transparent_mode)[..., :3]          # [..., 4, 3]
    err = ((px[..., :, None, :] - pal[..., None, :, :]) ** 2).sum(-1)      # [..., 16, 4]
    if not four_always:
        three = (c0 <= c1)[..., None, None]
        err = np.where(three & (np.arange(4) == 3), np.inf, err)           # 3: transparent only
    idx = err.argmin(-1)
    idx = np.where(trans, 3, idx)
    return c0, c1, idx


def _encode_dxt5_alpha(a):
    """a [..., 16] -> 8 bytes per block."""
    a = a.astype(np.int64)
    blk = np.zeros(a.shape[:-1] + (8,), np.int64)
    blk[..., 0], blk[..., 1] = a.max(-1), a.min(-1)        # 8-value mode (6-value when equal)
    _, tab = _dxt5_alpha_decode(blk)
    idx = np.abs(a[..., :, None] - tab[..., None, :]).argmin(-1)
    bits = sum(idx[..., k] << (3 * k) for k in range(16))
    for k in range(6):
        blk[..., 2 + k] = (bits >> (8 * k)) & 0xFF
    return blk.astype(np.uint8)


def encode_level(rel, fmt, img, clut_pal=None):
    """RGBA [h, w, 4] -> one level's bytes (PSMT8: indices into clut_pal [256, 4])."""
    name = TEX_FORMATS[(rel, fmt)][0]
    h, w = img.shape[:2]
    if name in ("DXT1", "DXT5"):
        bl = _image_to_blocks(img)
        c0, c1, idx = _encode_colour(bl, False, name == "DXT5")
        bits = sum(idx[..., k].astype(np.int64) << (2 * k) for k in range(16))
        col = np.stack([c0 & 0xFF, c0 >> 8, c1 & 0xFF, c1 >> 8] + [(bits >> (8 * k)) & 0xFF for k in range(4)], -1)
        out = col.astype(np.uint8)
        if name == "DXT5":
            out = np.concatenate([_encode_dxt5_alpha(bl[..., 3]), out], -1)
        return out.tobytes()
    if name == "CMPR":
        th, tw = (h + 7) // 8, (w + 7) // 8
        H, W = th * 8, tw * 8
        im = np.pad(img, ((0, H - h), (0, W - w), (0, 0)), mode="edge")
        bl = _image_to_blocks(im)                                   # [2th, 2tw, 16, 4]
        c0, c1, idx = _encode_colour(bl, True, False)
        bits = sum(idx[..., k].astype(np.int64) << (30 - 2 * k) for k in range(16))
        sub = np.stack([c0 >> 8, c0 & 0xFF, c1 >> 8, c1 & 0xFF] + [(bits >> (24 - 8 * k)) & 0xFF for k in range(4)], -1)
        sub = sub.reshape(th, 2, tw, 2, 8).transpose(0, 2, 1, 3, 4)  # tile, sub-block row, column
        return sub.astype(np.uint8).tobytes()
    if name == "RGBA8":
        th, tw = (h + 3) // 4, (w + 3) // 4
        bl = _image_to_blocks(img)                                   # [th, tw, 16, 4]
        ar = np.stack([bl[..., 3], bl[..., 0]], -1).reshape(th, tw, 32)
        gb = np.stack([bl[..., 1], bl[..., 2]], -1).reshape(th, tw, 32)
        return np.concatenate([ar, gb], -1).astype(np.uint8).tobytes()
    if name == "PSMCT32":
        return np.ascontiguousarray(img, np.uint8).tobytes()
    if name == "PSMT8":
        pal = np.asarray(clut_pal, np.int64)
        flat = img.reshape(-1, 4).astype(np.int64)
        out = np.empty(len(flat), np.uint8)
        for s in range(0, len(flat), 4096):
            d = ((flat[s:s + 4096, None, :] - pal[None, :, :]) ** 2).sum(-1)
            out[s:s + 4096] = d.argmin(-1)
        return out.tobytes()
    raise MediaError(f"{rel} texture format {fmt}")


def quantize256(images):
    """Palette [256, 4] for RGBA images (a PSMT8 CLUT of its own): Pillow's octree."""
    from PIL import Image
    stack = np.concatenate([im.reshape(-1, 4) for im in images])[None]
    stack = np.ascontiguousarray(stack, np.uint8)
    q = Image.frombytes("RGBA", (stack.shape[1], 1), stack.tobytes()).quantize(256, method=Image.Quantize.FASTOCTREE)
    pal = np.array(q.getpalette("RGBA")[:1024], np.uint8).reshape(-1, 4)
    out = np.zeros((256, 4), np.uint8)
    out[:len(pal)] = pal
    return out


# ---- PNG ---------------------------------------------------------------------------------

def write_png(path, img=None, indices=None, palette=None):
    """RGBA image, or a palette PNG (indices [h, w] into palette [256, 4], alpha in tRNS)."""
    from PIL import Image
    if indices is not None:
        idx = np.ascontiguousarray(indices, np.uint8)
        im = Image.frombytes("P", (idx.shape[1], idx.shape[0]), idx.tobytes())
        im.putpalette(np.asarray(palette, np.uint8)[:, :3].tobytes(), "RGB")
        im.save(path, transparency=np.asarray(palette, np.uint8)[:, 3].tobytes(), optimize=False)
    else:
        img = np.ascontiguousarray(img, np.uint8)
        Image.frombytes("RGBA", (img.shape[1], img.shape[0]), img.tobytes()).save(path, optimize=False)


def read_png(path):
    """-> (RGBA [h, w, 4], (indices, palette [256, 4]) or None for a palette PNG)."""
    from PIL import Image
    im = Image.open(path)
    im.load()
    pal = None
    if im.mode == "P":
        rgb = np.array(im.getpalette("RGB"), np.uint8).reshape(-1, 3)
        pal = np.zeros((256, 4), np.uint8)
        pal[:len(rgb), :3] = rgb[:256]
        pal[:, 3] = 255
        t = im.info.get("transparency")
        if isinstance(t, bytes):
            pal[:len(t), 3] = np.frombuffer(t, np.uint8)[:256]
        elif isinstance(t, int):
            pal[t, 3] = 0
        idx = np.array(im, np.uint8)
        return pal[idx], (idx, pal)
    return np.array(im.convert("RGBA"), np.uint8), None


def mip_down(img):
    """2x2 box filter to the next level (for levels whose PNG is missing)."""
    h, w = img.shape[:2]
    H, W = max(1, h // 2), max(1, w // 2)
    f = img.astype(np.float64)
    if h > 1:
        f = (f[0:2 * H:2] + f[1:2 * H:2]) / 2
    if w > 1:
        f = (f[:, 0:2 * W:2] + f[:, 1:2 * W:2]) / 2
    return np.clip(np.rint(f), 0, 255).astype(np.uint8)


# ------------------------------------------------------------------------------------------
# sound: PCM16 and PS2 SPU-ADPCM

ADPCM_FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def adpcm_decode(buf):
    """SPU-ADPCM (16-byte blocks, 28 samples each) -> int16 array, every block."""
    out = np.empty(len(buf) // 16 * 28, np.int16)
    s1 = s2 = 0
    o = 0
    for p in range(0, len(buf) - 15, 16):
        shift, filt = buf[p] & 15, buf[p] >> 4
        f0, f1 = ADPCM_FILTERS[filt] if filt < 5 else (0, 0)
        for k in range(28):
            n = (buf[p + 2 + k // 2] >> (4 * (k & 1))) & 15
            if n >= 8:
                n -= 16
            s = ((n << 12) >> shift) if shift <= 12 else 0
            s += (s1 * f0 + s2 * f1 + 32) >> 6
            s = -32768 if s < -32768 else 32767 if s > 32767 else s
            out[o] = s
            o += 1
            s2, s1 = s1, s
    return out


def adpcm_encode(pcm, loop_block=None):
    """int16 samples -> SPU-ADPCM with the release's framing: a silent block 0, the samples
    (padded to whole blocks), flags as the game's data has them (one-shot: 0 ... 1; looping:
    2, loop block 6, 2 ..., last 3). loop_block counts block 0."""
    x = [int(v) for v in pcm]
    x += [0] * (-len(x) % 28)
    nblk = len(x) // 28
    out = bytearray(16 * (nblk + 1))
    loop = loop_block is not None
    out[0], out[1] = 0x0C, 2 if loop else 0
    s1 = s2 = 0
    for b in range(nblk):
        blk = x[28 * b:28 * b + 28]
        best = None
        for fi, (f0, f1) in enumerate(ADPCM_FILTERS):
            # residual against the predictor from the source (the shift choice), then
            # encoded against the decoder's own state
            p1, p2, peak = s1, s2, 0
            for v in blk:
                r = v - ((p1 * f0 + p2 * f1 + 32) >> 6)
                peak = max(peak, abs(r))
                p2, p1 = p1, v
            shift = 12
            while shift > 0 and 2 * peak > 15 << (12 - shift):
                shift -= 1
            q1, q2, err, nib = s1, s2, 0, []
            for v in blk:
                pred = (q1 * f0 + q2 * f1 + 32) >> 6
                scale = 1 << (12 - shift)
                n = (v - pred + (scale >> 1)) // scale if v >= pred else -((pred - v + (scale >> 1)) // scale)
                n = max(-8, min(7, n))
                s = max(-32768, min(32767, ((n << 12) >> shift) + pred))
                err += (s - v) ** 2
                nib.append(n & 15)
                q2, q1 = q1, s
            if best is None or err < best[0]:
                best = (err, fi, shift, nib, q1, q2)
        _, fi, shift, nib, s1, s2 = best
        p = 16 * (b + 1)
        out[p] = (fi << 4) | shift
        for k in range(14):
            out[p + 2 + k] = nib[2 * k] | (nib[2 * k + 1] << 4)
        out[p + 1] = 2 if loop else 0
    if loop:
        out[16 * loop_block + 1] = 6
        out[-15] = 3
    else:
        out[-15] = 1
    return bytes(out)


def write_wav(path, pcm, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.asarray(pcm, "<i2").tobytes())


def read_wav(path):
    """-> (int16 samples, rate). Mono PCM16 only (the game's form)."""
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 1:
            raise MediaError(f"{path}: needs mono 16-bit PCM (has {w.getnchannels()} channels, "
                             f"{8 * w.getsampwidth()} bits)")
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").copy(), w.getframerate()


# ------------------------------------------------------------------------------------------
# where a pack's media are (offsets are in the pack's bytes)

def scan_textures(pk):
    """The texture table: per texture its header and the levels the data holds.

    Stored levels: the header's count (0 = one), except GameCube RGBA8 (one level: the game's
    TexSize and the data agree; the count is kept in the header only) and the PS2's PSMCT32
    chains, whose 1x1 level is the next texture's first 4 bytes (left to that texture)."""
    if pk.quad or not pk.h.get("num_textures"):
        return []
    rel, e = pk.release, pk.e
    d0, dn = pk.sec["data"]
    stride = 0x20 if rel == "gc" else 0x10
    out = []
    for i in range(pk.h["num_textures"]):
        o = d0 + pk.h["texture_data_offset"] + stride * i
        fmt, w, h, mips = struct.unpack_from("<4H", pk.b, o)
        ptr, word = struct.unpack_from(e + "2I", pk.b, o + 8)
        if (rel, fmt) not in TEX_FORMATS:
            raise MediaError(f"{pk.name}: texture {i}: {rel} format {fmt} not supported")
        n = 1 if (rel, fmt) == ("gc", 1) else max(mips, 1)
        out.append({"i": i, "hdr": o, "fmt": fmt, "w": w, "h": h, "mips": mips, "ptr": ptr,
                    "word": word, "n": n})
    starts = sorted(d0 + t["ptr"] for t in out)
    for t in out:
        p = d0 + t["ptr"]
        nxt = next((s for s in starts if s > p), d0 + dn)
        levels = []
        for k in range(t["n"]):
            lw, lh = level_dims(t["w"], t["h"], k)
            size = level_size(rel, t["fmt"], lw, lh)
            if p + size > nxt and rel == "ps2" and k == t["n"] - 1 and k > 0:
                break
            levels.append((p, size, lw, lh))
            p += size
        t["levels"] = levels
        if rel == "ps2" and t["fmt"] == 2:
            t["clut"] = d0 + pk.h["palette_offset"] + 0x400 * t["word"]
    return out


def scan_sounds(pk):
    if not pk.h.get("num_sounds"):
        return []
    s0, _ = pk.sec["sound_data"]
    e = pk.e
    out = []
    for i in range(pk.h["num_sounds"]):
        o = s0 + pk.h["sfx_offset"] + 0x20 * i
        ptr, length, loop, rate = struct.unpack_from(e + "4I", pk.b, o)
        b10, bits = pk.b[o + 0x10], pk.b[o + 0x11]
        vol = struct.unpack_from(e + "H", pk.b, o + 0x12)[0]
        if any(pk.b[o + 0x14:o + 0x20]):
            raise MediaError(f"{pk.name}: sound {i}: descriptor bytes 0x14-0x1F aren't zero")
        out.append({"i": i, "desc": o, "ptr": s0 + ptr, "len": length, "loop": loop, "rate": rate,
                    "b10": b10, "bits": bits, "vol": vol})
    return out


def scan_song(pk):
    """(song, bank) or (None, None): the RIFX 'MIDI' chunk and the 'sndb' instrument bank."""
    if pk.quad or "sound_data" not in pk.sec:
        return None, None
    s0, sn = pk.sec["sound_data"]
    b = pk.b
    song = bank = None
    so, bo = s0 + pk.h["song_offset"], s0 + pk.h["song_bank_offset"]
    if b[so:so + 4] == b"RIFX" and b[so + 8:so + 12] == b"MIDI":
        size = struct.unpack_from(">I", b, so + 4)[0]
        song = {"off": so, "size": size, "smf": (so + 12, size - 4)}
    if pk.h["song_bank_offset"] and b[bo:bo + 4] in (b"RIFX", b"RIFF"):
        e = ">" if b[bo:bo + 4] == b"RIFX" else "<"
        size, kind, version, count, table = struct.unpack_from(e + "I4sIII", b, bo + 4)
        if kind != b"sndb" or table != 0x20 + 0x20 * count:
            raise MediaError(f"{pk.name}: bank at 0x{bo:X} isn't a sndb bank this tool reads")
        w18, w1c = struct.unpack_from(e + "2I", b, bo + 0x18)
        ents = []
        for k in range(count):
            q = bo + 0x20 + 0x20 * k
            off, length, loop, rate = struct.unpack_from(e + "4I", b, q)
            ents.append({"k": k, "entry": q, "off": off, "len": length, "loop": loop, "rate": rate,
                         "b10": b[q + 0x10], "bits": b[q + 0x11],
                         "w12": struct.unpack_from(e + "H", b, q + 0x12)[0],
                         "prog": b[q + 0x14], "b15": b[q + 0x15], "lo": b[q + 0x16], "hi": b[q + 0x17],
                         "env": list(struct.unpack_from(e + "4H", b, q + 0x18))})
        bank = {"off": bo, "e": e, "size": size, "version": version, "count": count, "table": table,
                "w18": w18, "w1c": w1c, "entries": ents, "data": bo + table}
    return song, bank


# ------------------------------------------------------------------------------------------
# structural ids (docs/xdl-format/pairing.md §1)

class Ids:
    def __init__(self):
        self.pairing = XS.Pairing(XS.find_up("docs/xdl-format/pairing"))
        self.dir = self.pairing.dir

    def archive(self, stem):
        """The archive's GameCube spelling, from the pairing table's first line."""
        if self.dir is not None:
            for p in self.dir.glob("*.jsonl"):
                if p.stem.lower() == stem.lower():
                    with p.open() as f:
                        return json.loads(f.readline()).get("archive", p.stem)
        return stem

    def asset(self, rel, archive, pack, kind, i):
        try:
            return self.pairing.id(rel, archive, pack, kind, i)
        except XS.XdlError:
            return f"base://{archive}/{pack}/{kind}/{rel}:{i}"


def id_index(link):
    """'base://a/p/texture/ps2:5' -> 'ps2_5' (a file name on every system)."""
    return link.rsplit("/", 1)[1].replace(":", "_")


# ------------------------------------------------------------------------------------------
# sidecar files

def jsonc_text(doc, header):
    return "".join(f"// {h}\n" for h in header) + XS.dump_value(doc, 0) + "\n"


def schema_ref(path, name):
    return os.path.relpath(SCHEMAS / f"{name}.schema.json", Path(path).parent)


def write_jsonc(path, name, body, header):
    doc = {"$schema": schema_ref(path, name), "format_version": FORMAT_VERSION}
    doc.update(body)
    Path(path).write_text(jsonc_text(doc, header))


def read_jsonc(path, name):
    doc = XS.jsonc_loads(Path(path).read_text())
    errs = validate(doc, name)
    if errs:
        raise MediaError(f"{path}: " + "; ".join(errs[:5]))
    return doc


def pcm_of(rel, raw):
    if rel == "gc":
        return np.frombuffer(raw, ">i2").astype(np.int16)
    return np.frombuffer(raw, "<i2").copy()


def pcm_bytes(rel, pcm):
    return np.asarray(pcm, ">i2" if rel == "gc" else "<i2").tobytes()


def adpcm_body(raw, what):
    """A PS2 sample's PCM without block 0 (always silence: shift 12, no data)."""
    if len(raw) % 16 or len(raw) < 16 or raw[0] != 0x0C or any(raw[2:16]):
        raise MediaError(f"{what}: SPU-ADPCM doesn't start with the silent block")
    return adpcm_decode(raw)[28:]


def loop_to_samples(rel, loop, what):
    if loop == 0xFFFFFFFF:
        return None
    if rel == "ps2":
        if loop % 16 or loop < 16:
            raise MediaError(f"{what}: PS2 loop 0x{loop:X} isn't a block after block 0")
        return (loop // 16 - 1) * 28
    if loop % 2:
        raise MediaError(f"{what}: odd loop offset {loop}")
    return loop // 2


def samples_to_loop(rel, n):
    if n is None:
        return 0xFFFFFFFF
    if rel == "ps2":
        return (n // 28 + 1) * 16           # the block holding sample n
    return 2 * n


def decode_sample(rel, raw, what):
    return adpcm_body(raw, what) if rel == "ps2" else pcm_of(rel, raw)


def texture_fingerprint(pk, t):
    """pairing.md §3.1 `x`: header u16s, every level the header's count names, the CLUT."""
    d = struct.pack("<4H", t["fmt"], t["w"], t["h"], t["mips"])
    p = pk.sec["data"][0] + t["ptr"]
    for k in range(max(t["mips"], 1)):
        n = level_size(pk.release, t["fmt"], *level_dims(t["w"], t["h"], k))
        d += pk.b[p:p + n]
        p += n
    if "clut" in t:
        d += pk.b[t["clut"]:t["clut"] + 0x400]
    return h16(d)


# ------------------------------------------------------------------------------------------
# extract

def extract_pack(arc, ent, out, ids=None):
    ids = ids or Ids()
    rel = arc.release
    pk = RawPack(rel, ent["type"], arc.pack_bytes(ent), ent["name"])
    archive = ids.archive(arc.name)
    pname = ent["name"]
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    tag = f"({rel}), from {TOOL}"
    media = {"pack": f"base://{archive}/{pname}", "release": rel,
             "source": {"archive": str(arc.path.resolve()), "pack": pname, "toc_index": ent["index"],
                        "bytes": len(pk.b), "blake2b": h16(pk.b)},
             "textures": [], "sounds": []}

    for t in scan_textures(pk):
        tid = ids.asset(rel, archive, pname, "texture", t["i"])
        d = out / "texture" / id_index(tid)
        d.mkdir(parents=True, exist_ok=True)
        name = TEX_FORMATS[(rel, t["fmt"])][0]
        clut = pk.b[t["clut"]:t["clut"] + 0x400] if "clut" in t else None
        levels = []
        for k, (o, n, lw, lh) in enumerate(t["levels"]):
            raw = pk.b[o:o + n]
            img = decode_level(rel, t["fmt"], raw, lw, lh, clut)
            f = f"level{k}.png"
            if name == "PSMT8":
                write_png(d / f, indices=np.frombuffer(raw, np.uint8).reshape(lh, lw), palette=clut_rgba(clut))
            else:
                write_png(d / f, img)
            levels.append({"file": f, "width": lw, "height": lh, "pixels": h16(img.tobytes()),
                           "encoded": h16(raw)})
        body = {"id": tid, "release": rel, "index": t["i"], "format": name, "width": t["w"],
                "height": t["h"], "mips": t["mips"]}
        if name == "PSMT8":
            body["clut"] = t["word"]
            body["clut_encoded"] = h16(clut)
        else:
            body["header_0x0C"] = t["word"]
        body["fingerprint"] = texture_fingerprint(pk, t)
        body["levels"] = levels
        write_jsonc(d / "texture.jsonc", "MediaTexture", body, [f"texture {tid} {tag}"])
        media["textures"].append(f"texture/{id_index(tid)}/texture.jsonc")

    for s in scan_sounds(pk):
        sid = ids.asset(rel, archive, pname, "sound", s["i"])
        what = f"{pname} sound {s['i']}"
        raw = pk.b[s["ptr"]:s["ptr"] + s["len"]]
        pcm = decode_sample(rel, raw, what)
        (out / "sound").mkdir(exist_ok=True)
        base = id_index(sid)
        write_wav(out / "sound" / f"{base}.wav", pcm, s["rate"])
        body = {"id": sid, "release": rel, "index": s["i"], "file": f"{base}.wav", "rate": s["rate"],
                "loop": loop_to_samples(rel, s["loop"], what), "volume": s["vol"], "bits": s["bits"],
                "unk_0x10": s["b10"], "samples": h16(pcm.astype("<i2").tobytes())}
        if rel == "ps2":
            body["encoded"] = h16(raw)
        write_jsonc(out / "sound" / f"{base}.jsonc", "MediaSound", body, [f"sound {sid} {tag}"])
        media["sounds"].append(f"sound/{base}.jsonc")

    song, bank = scan_song(pk)
    if song:
        (out / "song").mkdir(exist_ok=True)
        o, n = song["smf"]
        (out / "song" / "song.mid").write_bytes(pk.b[o:o + n])
        media["song"] = {"id": ids.asset(rel, archive, pname, "song", 0), "file": "song/song.mid"}
    if bank:
        (out / "song" / "bank").mkdir(parents=True, exist_ok=True)
        files, insts = {}, []
        for en in bank["entries"]:
            key = (en["off"], en["len"])
            what = f"{pname} bank instrument {en['k']}"
            if key not in files:
                raw = pk.b[bank["data"] + en["off"]:bank["data"] + en["off"] + en["len"]]
                pcm = decode_sample(rel, raw, what)
                f = f"bank/{en['k']}.wav"
                write_wav(out / "song" / f, pcm, en["rate"])
                files[key] = (f, h16(pcm.astype("<i2").tobytes()), h16(raw))
            f, ph, eh = files[key]
            inst = {"id": ids.asset(rel, archive, pname, "bank", en["k"]),
                    "program": en["prog"], "key_low": en["lo"], "key_high": en["hi"],
                    "attack": en["env"][0], "decay": en["env"][1], "sustain": en["env"][2],
                    "release": en["env"][3], "sample": f, "rate": en["rate"],
                    "loop": loop_to_samples(rel, en["loop"], what), "bits": en["bits"],
                    "unk_0x10": en["b10"], "unk_0x12": en["w12"], "unk_0x15": en["b15"], "samples": ph}
            if rel == "ps2":
                inst["encoded"] = eh
            insts.append(inst)
        body = {"release": rel, "version": bank["version"],
                "unk_0x18": bank["w18"], "unk_0x1C": bank["w1c"], "instruments": insts}
        write_jsonc(out / "song" / "bank.jsonc", "MediaBank", body, [f"instrument bank of {media['pack']} {tag}"])
        media["bank"] = "song/bank.jsonc"
    write_jsonc(out / "media.jsonc", "MediaPack", media, [f"media of {media['pack']} {tag}"])
    return media


# ------------------------------------------------------------------------------------------
# schemas of the sidecars (schemas/Media*.schema.json; `xdl_media.py schemas` writes them)

def _link(kind):
    return {"type": "string",
            "pattern": rf"^base://[A-Za-z0-9_]+/[A-Za-z0-9_]+/{kind}/(?:[a-z0-9]+:)?[0-9]+$",
            "x-link": {"kind": kind}, "description": f"this {kind}'s structural id (pairing.md §1)"}


HASH = {"type": "string", "pattern": "^[0-9a-f]{32}$"}
U8 = {"type": "integer", "minimum": 0, "maximum": 255}
U16 = {"type": "integer", "minimum": 0, "maximum": 65535}
U32 = {"type": "integer", "minimum": 0, "maximum": 4294967295}
RELEASE = {"enum": ["gc", "pc", "ps2"], "description": "the release whose encoding this is"}
LOOP = {"type": ["integer", "null"], "minimum": 0,
        "description": "loop start in samples of the WAV (null: plays once). PS2: a multiple of 28 "
                       "(an ADPCM block)"}


def _doc(title, desc, props, required):
    p = {"$schema": {"type": "string", "description": "path or URL of this schema"},
         "format_version": {"const": FORMAT_VERSION}}
    p.update(props)
    return {"$schema": "https://json-schema.org/draft/2020-12/schema", "title": title,
            "description": desc, "type": "object", "x-tool": TOOL, "properties": p,
            "additionalProperties": False, "required": ["format_version"] + required}


def _h(desc, base=HASH):
    return dict(base, description=desc)


def media_schemas():
    level = {"type": "object", "additionalProperties": False,
             "required": ["file", "width", "height", "pixels", "encoded"],
             "properties": {
                 "file": {"type": "string", "description": "the level's PNG, beside this file; when "
                          "missing, made from the level above with a 2x2 box filter"},
                 "width": U16, "height": U16,
                 "pixels": _h("BLAKE2b-128 of the RGBA8 pixels the release's data decodes to; while "
                              "the PNG still decodes to it, build reuses the base's encoding"),
                 "encoded": _h("BLAKE2b-128 of the level's stored bytes in the base")}}
    tex = _doc("MediaTexture", "One texture of a pack: its header and its levels as PNG files "
               "(xdl README §10.1). Wrap and filter are not texture data in these packs: the game "
               "sets them per draw (Gfx_SetTextureWrapMode).", {
                   "id": _link("texture"), "release": RELEASE,
                   "index": dict(U16, description="the texture's index in its release's table"),
                   "format": {"enum": sorted({v[0] for v in TEX_FORMATS.values()}),
                              "description": "GameCube CMPR/RGBA8, PC DXT1/DXT5, PS2 PSMT8 (palette, a "
                                             "palette PNG)/PSMCT32. Colours as stored: the GameCube's are "
                                             "the PC's squared (port-format §2.4)"},
                   "width": U16, "height": U16,
                   "mips": dict(U16, description="the header's level count (0 = one level). GameCube "
                                "RGBA8 stores one level whatever it says; a PS2 PSMCT32 chain's 1x1 "
                                "level is the next texture's first texel"),
                   "header_0x0C": dict(U32, description="GameCube and PC: the header's last word (0)"),
                   "clut": dict(U32, description="PS2 PSMT8: the CLUT (palette) index; textures "
                                "sharing one must have equal palettes"),
                   "clut_encoded": _h("PS2 PSMT8: BLAKE2b-128 of the stored CLUT"),
                   "fingerprint": _h("pairing.md §3.1 `x` of the base's copy"),
                   "levels": {"type": "array", "minItems": 1, "items": level}},
               ["id", "release", "index", "format", "width", "height", "mips", "levels"])
    snd = _doc("MediaSound", "One sound effect: a mono PCM16 WAV and the descriptor's fields "
               "(port-format §2.5).", {
                   "id": _link("sound"), "release": RELEASE,
                   "index": dict(U16, description="the sound's index in its release's table"),
                   "file": {"type": "string", "description": "mono 16-bit WAV beside this file"},
                   "rate": dict(U32, description="sample rate in Hz; must equal the WAV's"),
                   "loop": LOOP,
                   "volume": dict(U16, description="descriptor u16 at 0x12 (0x7FFF everywhere)"),
                   "bits": dict(U8, description="descriptor u8 at 0x11 (16)"),
                   "unk_0x10": dict(U8, description="descriptor u8 at 0x10 (0)"),
                   "samples": _h("BLAKE2b-128 of the PCM16 LE samples (pairing.md `n` for GameCube and "
                                 "PC); PS2: of the decoded ADPCM without its silent first block"),
                   "encoded": _h("PS2: BLAKE2b-128 of the SPU-ADPCM bytes, reused while the WAV "
                                 "still has the samples above")},
               ["id", "release", "index", "file", "rate", "loop", "volume", "bits", "unk_0x10", "samples"])
    inst = {"type": "object", "additionalProperties": False,
            "required": ["id", "program", "key_low", "key_high", "attack", "decay", "sustain", "release",
                         "sample", "rate", "loop", "bits", "unk_0x10", "unk_0x12", "unk_0x15", "samples"],
            "properties": {
                "id": _link("bank"),
                "program": dict(U8, description="MIDI program; 128: a drum, played for the notes "
                                "key_low..key_high on channel 10 (MIDI.c CreateMIDI)"),
                "key_low": dict(U8, description="lowest note"), "key_high": dict(U8, description="highest note"),
                "attack": dict(U16, description="envelope rate (GetEnvRate)"),
                "decay": dict(U16, description="envelope rate"),
                "sustain": dict(U16, description="sustain level"),
                "release": dict(U16, description="envelope rate"),
                "sample": {"type": "string", "description": "the WAV, relative to this file; instruments "
                           "may share one"},
                "rate": dict(U32, description="sample rate at the base note (GetNoteRate_)"),
                "loop": LOOP, "bits": dict(U8, description="16 (PCM16) or 4 (PS2 SPU-ADPCM)"),
                "unk_0x10": U8, "unk_0x12": U16, "unk_0x15": U8,
                "samples": _h("BLAKE2b-128 of the sample's PCM16 LE samples"),
                "encoded": _h("PS2: BLAKE2b-128 of the SPU-ADPCM bytes")}}
    bank = _doc("MediaBank", "A song's instrument bank ('sndb', MIDI.c MIDI_SND/MIDI_INST); the "
                "song itself is a standard MIDI file beside it.", {
                    "release": RELEASE, "version": U32,
                    "unk_0x18": dict(U32, description="header word (GameCube 0x1A01, PC 0xCCCCCCCC)"),
                    "unk_0x1C": U32,
                    "instruments": {"type": "array", "items": inst}},
                ["release", "version", "unk_0x18", "unk_0x1C", "instruments"])
    pack = _doc("MediaPack", "A pack's media as tools/xdl_media.py extracts them: the files it "
                "builds back into the base pack (xdl README §12).", {
                    "pack": {"type": "string", "pattern": "^base://[A-Za-z0-9_]+/[A-Za-z0-9_]+$",
                             "description": "the pack's structural id"},
                    "release": RELEASE,
                    "source": {"type": "object", "additionalProperties": False,
                               "required": ["archive", "pack", "toc_index", "bytes", "blake2b"],
                               "description": "the player's copy this was extracted from: build's base",
                               "properties": {"archive": {"type": "string"}, "pack": {"type": "string"},
                                              "toc_index": U32, "bytes": U32, "blake2b": HASH}},
                    "textures": {"type": "array", "items": {"type": "string"}},
                    "sounds": {"type": "array", "items": {"type": "string"}},
                    "song": {"type": "object", "additionalProperties": False, "required": ["id", "file"],
                             "properties": {"id": _link("song"), "file": {"type": "string"}}},
                    "bank": {"type": "string"}},
                ["pack", "release", "source", "textures", "sounds"])
    return {s["title"]: s for s in (pack, tex, snd, bank)}


_VALIDATORS = {}


def validate(doc, name):
    if name not in _VALIDATORS:
        p = SCHEMAS / f"{name}.schema.json"
        sch = json.loads(p.read_text()) if p.exists() else media_schemas()[name]
        _VALIDATORS[name] = XS.validator(sch)[0]
    return [f"{'/'.join(map(str, e.path)) or '(top)'}: {e.message}" if hasattr(e, "path") else str(e)
            for e in _VALIDATORS[name].iter_errors(doc)]


def cmd_schemas(a):
    bad = 0
    for name, sch in media_schemas().items():
        p = SCHEMAS / f"{name}.schema.json"
        text = json.dumps(sch, indent=1, ensure_ascii=False) + "\n"
        if a.check:
            if not p.exists() or p.read_text() != text:
                print(f"{p.relative_to(ROOT)} is out of date")
                bad += 1
        else:
            p.write_text(text)
            print(p.relative_to(ROOT))
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------
# build

def relayout(items, total, a=0x20):
    """items [(key, orig_off, orig_len, new_len)] -> ({key: new_off}, new_total). Items keep
    their offsets until one grows; from there on, everything after moves by the growth (the
    gaps kept, each start aligned to `a`). Equal lengths give equal offsets."""
    out, shift, end = {}, 0, 0
    for key, off, _, n in sorted(items, key=lambda x: x[1]):
        o = off + shift
        if o < end:
            o = align(end, a)
            shift = o - off
        out[key] = o
        end = o + n
    return out, max(total + shift, end)


class Stats:
    def __init__(self):
        self.c = {}

    def add(self, kind, how, n=1):
        self.c[(kind, how)] = self.c.get((kind, how), 0) + n


def load_base(media, base_arg=None):
    src = media["source"]
    path = Path(base_arg or src["archive"])
    arc = Archive(path)
    if arc.release != media["release"]:
        raise MediaError(f"{path} is a {arc.release} archive; these files are {media['release']}")
    ent = arc.find(src["pack"])
    b = arc.pack_bytes(ent)
    if h16(b) != src["blake2b"]:
        raise MediaError(f"{path} {src['pack']} isn't the copy these files were extracted from "
                         f"(BLAKE2b {h16(b)}, recorded {src['blake2b']})")
    return RawPack(arc.release, ent["type"], b, ent["name"])


class Builder:
    def __init__(self, folder, media, base, fresh=False, stats=None, log=None):
        self.dir, self.media, self.pk = Path(folder), media, base
        self.rel, self.e = base.release, base.e
        self.fresh, self.st = fresh, stats or Stats()
        self.log = log or (lambda m: print(m, file=sys.stderr))
        self.out = bytearray(base.b)

    def scrub(self, o, n):
        self.out[o:o + n] = bytes(n)

    def put(self, o, data):
        self.out[o:o + len(data)] = data

    # ---- textures --------------------------------------------------------------------------
    def textures(self):
        pk, rel = self.pk, self.rel
        base = {t["i"]: t for t in scan_textures(pk)}
        docs = {}
        for f in self.media["textures"]:
            p = self.dir / f
            doc = read_jsonc(p, "MediaTexture")
            if doc["release"] != rel:
                raise MediaError(f"{p}: a {doc['release']} texture in a {rel} pack")
            t = base.get(doc["index"])
            if t is None:
                raise MediaError(f"{p}: the base pack has no texture {doc['index']} (adding textures "
                                 "needs the .xdl compiler)")
            if doc["index"] in docs:
                raise MediaError(f"{p}: texture {doc['index']} listed twice")
            docs[doc["index"]] = (p, doc)
        if set(docs) != set(base):
            missing = sorted(set(base) - set(docs))
            raise MediaError(f"textures {missing} of the base pack have no files")
        for t in base.values():
            self.scrub(t["hdr"], 8)
            self.scrub(t["hdr"] + 0xC, 4)
            for o, n, _, _ in t["levels"]:
                self.scrub(o, n)
            if "clut" in t:
                self.scrub(t["clut"], 0x400)
        imgs = {}
        for i, (p, doc) in docs.items():
            t = base[i]
            code = TEX_CODE.get((rel, doc["format"]))
            if code is None:
                raise MediaError(f"{p}: {doc['format']} isn't a {rel} format")
            if (code, doc["width"], doc["height"]) != (t["fmt"], t["w"], t["h"]):
                raise MediaError(f"{p}: {doc['format']} {doc['width']}x{doc['height']}: the base's slot is "
                                 f"{TEX_FORMATS[(rel, t['fmt'])][0]} {t['w']}x{t['h']}; other sizes and "
                                 "formats need the .xdl compiler (the data blob can't grow here)")
            if len(doc["levels"]) != len(t["levels"]):
                raise MediaError(f"{p}: {len(doc['levels'])} levels, the base stores {len(t['levels'])}")
            hdr = struct.pack("<4H", code, doc["width"], doc["height"], doc["mips"])
            self.put(t["hdr"], hdr)
            word = doc.get("clut", doc.get("header_0x0C", 0))
            if code == 2 and rel == "ps2" and word != t["word"]:
                raise MediaError(f"{p}: CLUT {word}; moving a texture to another CLUT isn't supported")
            self.put(t["hdr"] + 0xC, struct.pack(self.e + "I", word))
            lv, prev = [], None
            for k, L in enumerate(doc["levels"]):
                _, _, lw, lh = t["levels"][k]
                f = p.parent / L["file"]
                if f.exists():
                    img, pal = read_png(f)
                elif prev is not None:
                    img, pal = mip_down(prev), None
                    self.st.add("texture level", "made from the level above")
                else:
                    raise MediaError(f"{f} is missing")
                if img.shape[:2] != (lh, lw):
                    raise MediaError(f"{f}: {img.shape[1]}x{img.shape[0]}, level {k} is {lw}x{lh}")
                lv.append((img, pal, L))
                prev = img
            imgs[i] = lv
        for i, (p, doc) in docs.items():
            if doc["format"] != "PSMT8":
                self.texture_levels(base[i], imgs[i], p)
        cluts = {}
        for i, (p, doc) in docs.items():
            if doc["format"] == "PSMT8":
                cluts.setdefault(base[i]["word"], []).append(i)
        for c, users in sorted(cluts.items()):
            self.psmt8(c, [base[i] for i in users], [imgs[i] for i in users], [docs[i] for i in users])

    def texture_levels(self, t, lv, p):
        rel = self.rel
        name, blk, _, lossless = TEX_FORMATS[(rel, t["fmt"])]
        for k, (img, _, L) in enumerate(lv):
            o, n, lw, lh = t["levels"][k]
            exact = lossless and lw % blk == 0 and lh % blk == 0
            if exact:
                data = encode_level(rel, t["fmt"], img)
                how = "lossless"
            else:
                data = self.reuse(o, n, img, L) if not self.fresh else None
                how = "reused"
                if data is None:
                    data = encode_level(rel, t["fmt"], img)
                    how = "encoded"
            if len(data) != n:
                raise MediaError(f"{p}: level {k} encodes to {len(data)} bytes, not {n}")
            self.put(o, data)
            self.st.add(f"texture {name}", how)

    def reuse(self, o, n, img, L):
        """The base's bytes, while the pixels are the recorded ones and the base is that copy."""
        if h16(img.tobytes()) != L["pixels"]:
            return None
        raw = self.pk.b[o:o + n]
        if h16(raw) != L["encoded"]:
            self.log(f"note: {L['file']}: unchanged pixels, but the base's encoding differs; encoding afresh")
            return None
        return raw

    def psmt8(self, c, ts, lvs, docs):
        """PS2 palette textures sharing CLUT c: the CLUT from their palette PNGs, else the
        base's (unchanged pixels), else quantised from all of them."""
        rel = self.rel
        pals = [(p, pal[1]) for (p, _), lv in zip(docs, lvs) for _, pal, _ in lv if pal is not None]
        base_clut = self.pk.b[ts[0]["clut"]:ts[0]["clut"] + 0x400]
        base_pal = clut_rgba(base_clut)
        if pals:
            pal = pals[0][1]
            for p, q in pals[1:]:
                if not np.array_equal(q, pal):
                    raise MediaError(f"{p}: its palette differs from {pals[0][0]}'s; both use CLUT {c}")
            how = "lossless"
        else:
            unchanged = all(h16(img.tobytes()) == L["pixels"] for lv in lvs for img, _, L in lv)
            if unchanged and not self.fresh and h16(base_clut) == docs[0][1].get("clut_encoded"):
                pal, how = base_pal, "reused"
            else:
                pal, how = quantize256([img for lv in lvs for img, _, _ in lv]), "encoded"
        self.put(ts[0]["clut"], clut_bytes(pal))
        self.st.add("texture PSMT8 CLUT", how)
        same_pal = np.array_equal(pal, base_pal)
        for t, lv, (p, doc) in zip(ts, lvs, docs):
            for k, (img, ip, L) in enumerate(lv):
                o, n, lw, lh = t["levels"][k]
                if ip is not None:
                    data, how = ip[0].tobytes(), "lossless"
                else:
                    data = self.reuse(o, n, img, L) if same_pal and not self.fresh else None
                    how = "reused"
                    if data is None:
                        data, how = encode_level(rel, t["fmt"], img, pal), "encoded"
                self.put(o, data)
                self.st.add("texture PSMT8", how)

    # ---- sounds, song, bank ----------------------------------------------------------------
    def sample(self, f, doc, base_raw, kind, what):
        """A WAV -> the release's bytes (PCM16 in its byte order, or SPU-ADPCM)."""
        pcm, rate = read_wav(f)
        if rate != doc["rate"]:
            raise MediaError(f"{f}: {rate} Hz, its sidecar says {doc['rate']}")
        if self.rel != "ps2":
            self.st.add(kind, "lossless")
            return pcm_bytes(self.rel, pcm)
        if (not self.fresh and h16(pcm.astype("<i2").tobytes()) == doc["samples"]
                and base_raw is not None and h16(base_raw) == doc.get("encoded")):
            self.st.add(kind, "reused")
            return base_raw
        self.st.add(kind, "encoded")
        loop = doc["loop"]
        if loop is not None and loop % 28:
            self.log(f"note: {what}: loop {loop} moved to the ADPCM block at {loop // 28 * 28}")
        return adpcm_encode(pcm, None if loop is None else loop // 28 + 1)

    def sounds(self):
        pk, rel, e = self.pk, self.rel, self.e
        base = {s["i"]: s for s in scan_sounds(pk)}
        song, bank = scan_song(pk)
        if not base and not song and not bank:
            if self.media["sounds"] or "song" in self.media or "bank" in self.media:
                raise MediaError("the base pack has no sound data")
            return
        s0, sn = pk.sec["sound_data"]
        new = {}             # key -> bytes
        docs = {}
        for f in self.media["sounds"]:
            p = self.dir / f
            doc = read_jsonc(p, "MediaSound")
            s = base.get(doc["index"])
            if s is None:
                raise MediaError(f"{p}: the base pack has no sound {doc['index']}")
            docs[doc["index"]] = doc
            new[("sfx", s["i"])] = self.sample(p.parent / doc["file"], doc, pk.b[s["ptr"]:s["ptr"] + s["len"]],
                                               "sound", f)
        if set(docs) != set(base):
            raise MediaError(f"sounds {sorted(set(base) - set(docs))} of the base pack have no files")
        if song:
            if "song" not in self.media:
                raise MediaError("the base pack has a song; media.jsonc lists none")
            smf = (self.dir / self.media["song"]["file"]).read_bytes()
            if smf[:4] != b"MThd":
                raise MediaError(f"{self.media['song']['file']}: not a standard MIDI file")
            new["song"] = b"RIFX" + struct.pack(">I", 4 + len(smf)) + b"MIDI" + smf
            self.st.add("song", "lossless")
        bdoc = None
        if bank:
            if "bank" not in self.media:
                raise MediaError("the base pack has an instrument bank; media.jsonc lists none")
            bp = self.dir / self.media["bank"]
            bdoc = read_jsonc(bp, "MediaBank")
            if len(bdoc["instruments"]) != bank["count"]:
                raise MediaError(f"{bp}: {len(bdoc['instruments'])} instruments, the base has {bank['count']} "
                                 "(other counts need the .xdl compiler)")
            files = {}
            for en, ins in zip(bank["entries"], bdoc["instruments"]):
                key = ins["sample"]
                if key not in files:
                    raw = pk.b[bank["data"] + en["off"]:bank["data"] + en["off"] + en["len"]]
                    files[key] = (self.sample(bp.parent / key, ins, raw, "bank sample", f"{bp} {key}"), en)
            # the bank's samples, laid out inside the bank
            first = {}
            for key, (data, en) in files.items():
                first.setdefault((en["off"], en["len"]), key)
            items = [(key, en["off"], en["len"], len(data)) for key, (data, en) in files.items()
                     if first[(en["off"], en["len"])] == key]
            if len(items) != len(files):
                raise MediaError(f"{bp}: instruments sharing a sample in the base name different WAVs")
            area = bank["size"] + 8 - bank["table"]
            offs, area2 = relayout(items, area)
            body = bytearray(area2)
            for key, (data, en) in files.items():
                body[offs[key]:offs[key] + len(data)] = data
            be = bank["e"]
            hdr = bytearray((b"RIFX" if be == ">" else b"RIFF") + struct.pack(
                be + "I4sIIIII", bank["table"] + area2 - 8, b"sndb", bdoc["version"], bank["count"],
                bank["table"], bdoc["unk_0x18"], bdoc["unk_0x1C"]))
            for ins in bdoc["instruments"]:
                data = files[ins["sample"]][0]
                hdr += struct.pack(be + "4I", offs[ins["sample"]], len(data),
                                   samples_to_loop(rel, ins["loop"]), ins["rate"])
                hdr += struct.pack(be + "BBH", ins["unk_0x10"], ins["bits"], ins["unk_0x12"])
                hdr += struct.pack(be + "BBBB4H", ins["program"], ins["unk_0x15"], ins["key_low"],
                                   ins["key_high"], ins["attack"], ins["decay"], ins["sustain"], ins["release"])
            new["bank"] = bytes(hdr) + bytes(body)
            self.st.add("bank", "lossless")
        # sound_data: descriptors, samples, song, bank
        items = []
        if base:
            n = 0x20 * len(base)
            items.append(("desc", pk.h["sfx_offset"], n, n))
            for s in base.values():
                items.append((("sfx", s["i"]), s["ptr"] - s0, s["len"], len(new[("sfx", s["i"])])))
        if song:
            items.append(("song", song["off"] - s0, song["size"] + 8, len(new["song"])))
        if bank:
            items.append(("bank", bank["off"] - s0, bank["size"] + 8, len(new["bank"])))
        offs, size = relayout(items, sn)
        sd = bytearray(pk.b[s0:s0 + sn]) + bytes(max(0, size - sn))
        del sd[size:]
        for _, off, n, _ in items:
            sd[off:off + n] = bytes(n)
        for key, data in new.items():
            sd[offs[key]:offs[key] + len(data)] = data
        for s in base.values():
            doc = docs[s["i"]]
            data = new[("sfx", s["i"])]
            o = offs["desc"] + 0x20 * s["i"]
            sd[o:o + 0x20] = struct.pack(e + "4IBBH", offs[("sfx", s["i"])], len(data),
                                         samples_to_loop(rel, doc["loop"]), doc["rate"], doc["unk_0x10"],
                                         doc["bits"], doc["volume"]) + bytes(12)
        if size == sn and offs.get("desc", pk.h["sfx_offset"]) == pk.h["sfx_offset"]:
            self.put(s0, sd)
            return
        self.st.add("sound_data", "laid out again")
        self.join_sound_data(bytes(sd), offs, song, bank)

    def join_sound_data(self, sd, offs, song, bank):
        """The pack with a sound_data of another size (pack.rs's standard layout)."""
        pk, e = self.pk, self.e
        h = dict(pk.h)
        old_sfx = h["sfx_offset"]
        h["sound_data_size"] = len(sd)
        if "desc" in offs:
            h["sfx_offset"] = offs["desc"]
        if song:
            h["song_offset"] = offs["song"]
        if bank:
            h["song_bank_offset"] = offs["bank"]
        cur = bytes(self.out)
        sp = [p - old_sfx + h["sfx_offset"] for p in pk.words("sound_pointers")]
        head = struct.pack(e + "20I", *(h[k] for k in STD_WORDS)) + cur[80:STD_HEADER]
        if self.rel == "ps2":
            head += bytes(align(len(head), 2048) - len(head))
        head += struct.pack(e + f"{len(sp)}I", *sp)
        head += bytes(align(4 * len(sp), 32) - 4 * len(sp))
        head += sd
        if self.rel == "ps2":
            head += bytes(align(len(head), 2048) - len(head))
        self.out = bytearray(head + cur[pk.sec["texture_relocs"][0]:])

    def run(self):
        self.textures()
        self.sounds()
        return bytes(self.out)


def build_pack(folder, base_arg=None, fresh=False, stats=None, log=None):
    folder = Path(folder)
    media = read_jsonc(folder / "media.jsonc", "MediaPack")
    base = load_base(media, base_arg)
    return Builder(folder, media, base, fresh, stats, log).run()


# ------------------------------------------------------------------------------------------
# round trip

def archives_in(paths):
    out = []
    for p in map(Path, paths):
        if p.is_dir():
            out += sorted(q for q in p.rglob("*") if q.suffix.lower() in (".xgc", ".xpc", ".xps"))
        else:
            out.append(p)
    return out


def quality(rel, base, built):
    """Fresh encodes against the base's decode: texture RGB/alpha RMSE, sample SNR (dB)."""
    q = []
    tb, tn = scan_textures(base), scan_textures(built)
    for a, b in zip(tb, tn):
        name = TEX_FORMATS[(rel, a["fmt"])][0]
        for (o, n, w, h), (o2, n2, _, _) in zip(a["levels"], b["levels"]):
            ca = base.b[a["clut"]:a["clut"] + 0x400] if "clut" in a else None
            cb = built.b[b["clut"]:b["clut"] + 0x400] if "clut" in b else None
            x = decode_level(rel, a["fmt"], base.b[o:o + n], w, h, ca).astype(np.float64)
            y = decode_level(rel, b["fmt"], built.b[o2:o2 + n2], w, h, cb).astype(np.float64)
            q.append((f"texture {name}", float(np.sqrt(((x[..., :3] - y[..., :3]) ** 2).mean())),
                      float(np.sqrt(((x[..., 3] - y[..., 3]) ** 2).mean()))))
    if rel == "ps2":
        for a, b in zip(scan_sounds(base), scan_sounds(built)):
            x = adpcm_body(base.b[a["ptr"]:a["ptr"] + a["len"]], "base").astype(np.float64)
            y = adpcm_body(built.b[b["ptr"]:b["ptr"] + b["len"]], "built").astype(np.float64)[:len(x)]
            err = ((x[:len(y)] - y) ** 2).sum()
            q.append(("sound ADPCM", float(10 * np.log10(max((x ** 2).sum(), 1) / max(err, 1e-9))), 0.0))
    return q


def roundtrip_one(job):
    path, idx, out, fresh, keep = job
    import shutil
    arc = Archive(path)
    ent = arc.entries[idx]
    folder = Path(out) / arc.release / arc.name / ent["name"]
    st = Stats()
    res = {"release": arc.release, "archive": arc.name, "pack": ent["name"]}
    try:
        if folder.exists():
            shutil.rmtree(folder)
        extract_pack(arc, ent, folder)
        orig = arc.pack_bytes(ent)
        b = Builder(folder, read_jsonc(folder / "media.jsonc", "MediaPack"),
                    RawPack(arc.release, ent["type"], orig, ent["name"]), fresh, st, log=lambda m: None).run()
        res["equal"] = b == orig
        if not res["equal"]:
            pos = next((i for i, (x, y) in enumerate(zip(b, orig)) if x != y), min(len(b), len(orig)))
            res["diff"] = f"first difference at 0x{pos:X} ({len(b)} vs {len(orig)} bytes)"
        if fresh:
            res["quality"] = quality(arc.release, RawPack(arc.release, ent["type"], orig),
                                     RawPack(arc.release, ent["type"], b))
    except MediaError as ex:
        res["error"] = str(ex)
    res["stats"] = {f"{k}|{h}": n for (k, h), n in st.c.items()}
    if not keep:
        shutil.rmtree(folder, ignore_errors=True)
    return res


def cmd_roundtrip(a):
    from multiprocessing import Pool
    jobs = []
    for p in archives_in(a.paths):
        arc = Archive(p)
        for ent in arc.entries:
            if a.only and a.only.lower() not in f"{arc.name}/{ent['name']}".lower():
                continue
            jobs.append((str(p), ent["index"], a.out, a.fresh, a.keep))
    tot, by_rel, quality_ = {}, {}, {}
    bad = 0
    with Pool(a.jobs) as pool:
        for r in pool.imap_unordered(roundtrip_one, jobs, chunksize=1):
            rel = r["release"]
            pr = by_rel.setdefault(rel, [0, 0, 0])
            pr[0] += 1
            if r.get("error"):
                pr[2] += 1
                bad += 1
                print(f"{rel} {r['archive']}/{r['pack']}: error: {r['error']}")
            elif r["equal"]:
                pr[1] += 1
            else:
                if not a.fresh:
                    bad += 1
                if a.verbose or not a.fresh:
                    print(f"{rel} {r['archive']}/{r['pack']}: differs: {r['diff']}")
            for k, n in r["stats"].items():
                tot[(rel,) + tuple(k.split("|"))] = tot.get((rel,) + tuple(k.split("|")), 0) + n
            for kind, v1, v2 in r.get("quality", []):
                quality_.setdefault((rel, kind), []).append((v1, v2))
    print("\npacks: release, packs, byte-identical, errors")
    for rel, (n, eq, err) in sorted(by_rel.items()):
        print(f"  {rel:4s} {n:5d} {eq:5d} {err:3d}")
    print("\nassets: release, kind, how built, count")
    for k in sorted(tot):
        print(f"  {k[0]:4s} {k[1]:20s} {k[2]:28s} {tot[k]:6d}")
    if quality_:
        print("\nfresh encodes against the base (textures: RGB / alpha RMSE, median and p90; ADPCM: SNR dB)")
        for (rel, kind), v in sorted(quality_.items()):
            x = np.array(v)
            print(f"  {rel:4s} {kind:16s} n={len(x):6d}  median {np.median(x[:, 0]):6.2f} / {np.median(x[:, 1]):6.2f}"
                  f"  p90 {np.percentile(x[:, 0], 90):6.2f} / {np.percentile(x[:, 1], 90):6.2f}")
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------
# command line

def split_sel(s):
    p, _, sel = s.rpartition(":")
    if p and "/" not in sel and Path(p).suffix.lower() in (".xgc", ".xpc", ".xps"):
        return Path(p), sel
    return Path(s), None


def cmd_extract(a):
    path, sel = split_sel(a.pack)
    arc = Archive(path)
    ids = Ids()
    ents = [arc.find(sel)] if sel else arc.entries
    for ent in ents:
        out = Path(a.out) if sel else Path(a.out) / ent["name"]
        m = extract_pack(arc, ent, out, ids)
        print(f"{ent['name']}: {len(m['textures'])} textures, {len(m['sounds'])} sounds"
              + (", song" if "song" in m else "") + (", bank" if "bank" in m else "") + f" -> {out}")
    return 0


def cmd_build(a):
    st = Stats()
    b = build_pack(a.folder, a.base, a.fresh, st)
    Path(a.pack).write_bytes(b)
    media = read_jsonc(Path(a.folder) / "media.jsonc", "MediaPack")
    same = h16(b) == media["source"]["blake2b"]
    print(f"{a.pack}: {len(b)} bytes, {'equal to' if same else 'differs from'} the base; "
          + ", ".join(f"{k} {h} {n}" for (k, h), n in sorted(st.c.items())))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract", help="a pack's media to files")
    p.add_argument("pack", help="ARCHIVE[:PACK]")
    p.add_argument("out")
    p = sub.add_parser("build", help="the pack's bytes from the files")
    p.add_argument("folder")
    p.add_argument("pack", help="output file")
    p.add_argument("--base", help="the archive holding the base pack (default: as extracted)")
    p.add_argument("--fresh", action="store_true", help="encode lossy assets afresh")
    p = sub.add_parser("roundtrip", help="extract and build every pack, compare")
    p.add_argument("paths", nargs="+", help="archives or folders of them")
    p.add_argument("--out", default=str(OUT / "roundtrip"))
    p.add_argument("--only", help="archive/pack substring")
    p.add_argument("--fresh", action="store_true", help="encode lossy assets afresh; report quality")
    p.add_argument("--keep", action="store_true", help="keep the extracted files")
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("-v", "--verbose", action="store_true")
    p = sub.add_parser("schemas", help="write schemas/Media*.schema.json")
    p.add_argument("--check", action="store_true")
    a = ap.parse_args()
    try:
        return {"extract": cmd_extract, "build": cmd_build, "roundtrip": cmd_roundtrip,
                "schemas": cmd_schemas}[a.cmd](a)
    except (MediaError, XS.XdlError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
