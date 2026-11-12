#!/usr/bin/env python3
"""Pack geometry <-> glTF 2.0 (.gltf + .bin), for the .xdl source projects (step 3c).

    xdl_gltf.py export PACK_DIR OUT_DIR [--only N]     every geometry of a pack to OUT_DIR/geometry_N.gltf
    xdl_gltf.py info PACK_DIR                          what the scan finds, per geometry

PACK_DIR is one pack of an `hwvx-x extract` tree (data.bin, pointers.bin, ...). The whole-pack
extraction (object graph, sidecars, build) is tools/xdl_graph.py; this module is the geometry
part of it: find, decode, write glTF, read glTF, encode.

Source encodings (docs/port-format/README.md §2, docs/ps2-version/README.md §1):
    GameCube  header 0x2003, display lists (pos/normal-or-colour/uv index triples, u8 matrix)
    PC        header 0x0002, levels at header + 0x40, u16 index lists into shared arrays
    PS2       header 0x1005, VIF packets (strips, skinned triangle lists)

glTF layout, one file per geometry:
    node "geometry_N" (extras: header, LOD distances) with one child node per level ("lod0" ...),
    each with its own mesh; a group is a primitive (extras: the group record: flags, colour,
    blend, sort key ...), its material carries the texture link (extras.texture, a base:// id;
    no image: the PNGs come from the media step and are found by that id).
    Attributes: POSITION, NORMAL (lit / env-mapped groups), COLOR_0 (other groups; u8 RGBA in the
    GameCube's scale), TEXCOORD_0, and for skinned geometry JOINTS_0 / WEIGHTS_0 (one bone per
    vertex, weight 1.0) with a skin over 8 bone nodes (positions are bone-local, as in the pack).
    NORMAL is normalised for the validator; when the source's normals are not unit length the
    exact values are kept in _NORMAL_RAW. Shadow-volume levels are LINES primitives (their edges).
    The vertex winding (counter-clockwise), axes and uv origin are the game's.

Exactness: the extractor keeps the geometry's original bytes beside the glTF (xdl_graph.py) with
a hash of what the glTF said at extraction (content_hash). An unchanged glTF builds back to the
original bytes; an edited one is encoded afresh (encode(): GameCube display lists or PC index
lists; PS2 packets are not written yet).
"""

import argparse
import base64
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

MAGIC = {"gc": 0x2003, "pc": 0x0002, "ps2": 0x1005}
ENDIAN = {"gc": ">", "pc": "<", "ps2": "<"}
F_ALPHA = 0x81
F_LITENV = 0x16
F_SHADOW = 0x4000
WHITE = b"\xff\xff\xff\xff"


class GeomError(Exception):
    pass


def find_up(rel):
    for d in Path(__file__).resolve().parents:
        if (d / rel).exists():
            return d / rel
    return None


# ------------------------------------------------------------------------------------------
# blob access

class Blob:
    """data.bin with its pointer slots: what every geometry function reads."""

    def __init__(self, data, ptrs, release):
        self.d = data
        self.ptrs = ptrs if isinstance(ptrs, (set, frozenset)) else set(ptrs)
        self.release = release
        self.e = ENDIAN[release]

    def u16(self, o):
        return struct.unpack_from(self.e + "H", self.d, o)[0]

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.d, o)[0]

    def target(self, o):
        if o not in self.ptrs:
            return None
        t = self.u32(o)
        return t if t < len(self.d) else None

    def le4(self, o, n):
        """n 32-bit words from o, as little-endian bytes (exact for any bit pattern)."""
        b = self.d[o:o + 4 * n]
        if self.e == "<":
            return bytes(b)
        return b"".join(b[i:i + 4][::-1] for i in range(0, len(b), 4))


def scan(blob):
    """Geometry header offsets in scan order (pairing.md §1: the index of geometry/N)."""
    d, rel = blob.d, blob.release
    magic = MAGIC[rel]
    out = []
    for off in range(0, len(d) - 0x40, 4):
        if blob.u16(off) != magic or off + 8 not in blob.ptrs or off + 0xC not in blob.ptrs:
            continue
        n = blob.u16(off + 2)
        if not 1 <= n <= 8:
            continue
        lods = blob.u32(off + 8)
        if rel == "gc":
            if lods < len(d):
                out.append(off)
        elif rel == "pc":
            if lods == off + 0x40:
                out.append(off)
        else:
            if blob.target(off + 8) is not None:
                out.append(off)
    return out


def _names(blob, o):
    """NUL-terminated strings up to an empty one; (list, byte length)."""
    d, out, i = blob.d, [], o
    while i < len(d) and d[i] != 0:
        j = d.index(b"\0", i)
        out.append(d[i:j].decode("latin-1"))
        i = j + 1
    return out, i + 1 - o


def _lit(flags):
    return bool(flags & F_LITENV) and not (flags & F_ALPHA)


# ------------------------------------------------------------------------------------------
# decoding: one neutral form for every release
#
# geometry = {release, offset, magic, skinned, u6, box (0x30 bytes hex), dist: [f32 LE hex],
#             levels: [level], owned: [(lo, hi)], names: {offset: structural name}}
# level    = {rec: {u0, u4, u6, u10 (non-pointer words)}, kind: drawn|shadow, names: [str]|None,
#             skin: [8 counts]|None, groups: [group]}
# group    = {rec: {u0, flags, colour, u8, blend, sort, slot18}, texture: reloc value|None,
#             uvscroll: hex|None, verts: [(pos, nrm, col, uv, bone)], tris: [(a, b, c)]}
#            shadow groups: {rec, positions-relative edges: [(n0, v0, v1, n1, type)]}
#            (pos/nrm/uv: little-endian f32 bytes, col: 4 bytes, bone: int)

def decode(blob, off, texrelocs=None):
    rel = blob.release
    texrelocs = texrelocs or {}
    g = {"release": rel, "offset": off, "magic": blob.u16(off), "skinned": blob.u16(off + 4),
         "u6": blob.u16(off + 6), "box": blob.d[off + 0x10:off + 0x40].hex(), "levels": []}
    n = blob.u16(off + 2)
    lods, dist = blob.target(off + 8), blob.target(off + 0xC)
    if lods is None or dist is None:
        raise GeomError(f"0x{off:X}: levels or distances not a pointer")
    owned = [(off, off + 0x40), (lods, lods + 0x18 * n), (dist, dist + 4 * n)]
    names = {off: "", lods: "levels", dist: "distances"}
    g["dist"] = [blob.le4(dist + 4 * i, 1).hex() for i in range(n)]
    for li in range(n):
        L = lods + 0x18 * li
        names[L] = f"level/{li}"
        lv = {"groups": []}
        ng = blob.u16(L + 2)
        grp = blob.target(L + 8)
        if grp is None:
            raise GeomError(f"0x{off:X} level {li}: groups not a pointer")
        names[grp] = f"level/{li}/groups"
        owned.append((grp, grp + 0x20 * ng))
        lv["rec"] = {"u0": blob.u16(L), "u4": blob.u32(L + 4)}
        shadow = bool(blob.u16(grp + 2) & F_SHADOW) or (rel == "ps2" and blob.u16(L) == 3)
        lv["kind"] = "shadow" if shadow else "drawn"
        nm = blob.target(L + 0x14)
        if nm is not None:
            lv["names"], ln = _names(blob, nm)
            owned.append((nm, nm + ln))
            names[nm] = f"level/{li}/names"
        else:
            lv["names"] = None
            lv["rec"]["u14"] = blob.u32(L + 0x14)
        sk = blob.target(L + 0x10)
        if sk is not None:
            lv["skin"] = [blob.u32(sk + 4 * i) for i in range(8)]
            owned.append((sk, sk + 0x20))
            names[sk] = f"level/{li}/bones"
        else:
            lv["skin"] = None
            lv["rec"]["u10"] = blob.u32(L + 0x10)
        if rel == "ps2" and not shadow:
            lv["rec"]["uc"] = blob.u32(L + 0xC)
        g["levels"].append(lv)
        if shadow:
            _decode_shadow(blob, L, grp, ng, lv, owned, names, li)
        elif rel == "gc":
            _decode_gc(blob, L, grp, ng, lv, owned, names, li, g["skinned"] != 0)
        elif rel == "pc":
            _decode_pc(blob, L, grp, ng, lv, owned, names, li, g["skinned"] != 0)
        else:
            _decode_ps2(blob, L, grp, ng, lv, owned, names, li)
        for k, gr in enumerate(lv["groups"]):
            G = grp + 0x20 * k
            names[G] = f"level/{li}/group/{k}"
            gr["texture"] = texrelocs.get(G + 0x18)
    g["owned"] = owned
    g["names"] = names
    return g


def _group_rec(blob, G):
    return {"u0": blob.u16(G), "flags": blob.u16(G + 2), "colour": blob.d[G + 4:G + 8].hex(),
            "u8": blob.u32(G + 8), "blend": blob.u16(G + 0x14), "sort": blob.u16(G + 0x16),
            "slot18": blob.u32(G + 0x18)}


def _uvscroll(blob, G, owned, names, path):
    u = blob.target(G + 0xC)
    if u is None:
        return None, blob.u32(G + 0xC)
    owned.append((u, u + 0x10))
    names[u] = path + "/uvscroll"
    return blob.d[u:u + 0x10].hex(), None


def _decode_shadow(blob, L, grp, ng, lv, owned, names, li):
    pos = blob.target(L + 0xC)
    lv["rec"]["uc"] = None if pos is not None else blob.u32(L + 0xC)
    top = 0
    for k in range(ng):
        G = grp + 0x20 * k
        gr = {"rec": _group_rec(blob, G), "edges": []}
        gr["rec"]["u10"] = blob.u32(G + 0x10)
        gr["rec"]["uc"] = blob.u32(G + 0xC)
        e = blob.target(G + 0x1C)
        if blob.release == "ps2":
            # the PS2's shadow groups point to a packet of their own (not decoded yet): the
            # bytes stay a raw node of the graph, linked from the geometry
            lv["undecoded"] = True
            lv["groups"].append(gr)
            continue
        if e is not None:
            ne = blob.u32(G + 0x10)
            owned.append((e, e + 0x20 * ne))
            names[e] = f"level/{li}/group/{k}/edges"
            for i in range(ne):
                E = e + 0x20 * i
                v0, v1 = blob.u16(E + 0xC), blob.u16(E + 0xE)
                gr["edges"].append((blob.le4(E, 3), v0, v1, blob.le4(E + 0x10, 3), blob.u32(E + 0x1C)))
                top = max(top, v0 + 1, v1 + 1)
        else:
            gr["rec"]["u1c"] = blob.u32(G + 0x1C)
        lv["groups"].append(gr)
    lv["positions"] = [blob.le4(pos + 12 * i, 3) for i in range(top)] if pos is not None else []
    if pos is not None:
        owned.append((pos, pos + 12 * top))
        names[pos] = f"level/{li}/positions"


def _decode_gc(blob, L, grp, ng, lv, owned, names, li, skinned):
    vb = blob.target(L + 0xC)
    if vb is None:
        raise GeomError("vertex buffer not a pointer")
    uvo, clo, nro, end = (blob.u32(vb + i) for i in (0, 4, 8, 12))
    data = vb + 0x10
    owned.append((vb, data + end))
    names[vb] = f"level/{li}/vertices"
    lv["vb"] = {"offsets": [uvo, clo, nro, end]}

    def bound(start):
        return min([o for o in (uvo, clo, nro) if o > start] + [end])
    npos, nuv = uvo // 12, (bound(uvo) - uvo) // 8
    ncl = (bound(clo) - clo) // 4 if clo else 0
    nnr = (bound(nro) - nro) // 12 if nro else 0
    d = blob.d
    for k in range(ng):
        G = grp + 0x20 * k
        gr = {"rec": _group_rec(blob, G)}
        gr["uvscroll"], gr["rec"]["uc"] = _uvscroll(blob, G, owned, names, f"level/{li}/group/{k}")
        size, dl = blob.u32(G + 0x10), blob.target(G + 0x1C)
        if dl is None:
            raise GeomError("display list not a pointer")
        owned.append((dl, dl + size))
        names[dl] = f"level/{li}/group/{k}/draw"
        flags = gr["rec"]["flags"]
        lit = _lit(flags)
        mtx = skinned and lit
        verts, index, tris, cur = [], {}, [], []
        p, stop = dl, dl + size
        while p < stop:
            cmd = d[p]
            p += 1
            if cmd == 0:
                continue
            prim = cmd & 0xF8
            if prim not in (0x90, 0x98, 0xA0):
                raise GeomError(f"display-list command 0x{cmd:02X}")
            cnt = struct.unpack_from(">H", d, p)[0]
            p += 2
            seq = []
            for _ in range(cnt):
                m = 0
                if mtx:
                    m = d[p]
                    p += 1
                A, B, C = struct.unpack_from(">HHH", d, p)
                p += 6
                if A >= npos or C >= nuv:
                    raise GeomError("index past its array")
                pos = blob.le4(data + 12 * A, 3)
                uv = blob.le4(data + uvo + 8 * C, 2)
                nrm = col = None
                if lit:
                    if nro and B < nnr:
                        nrm = blob.le4(data + nro + 12 * B, 3)
                    else:
                        nrm = bytes(12)
                else:
                    col = bytes(d[data + clo + 4 * B:data + clo + 4 * B + 4]) if clo and B < ncl else WHITE
                key = (pos, nrm, col, uv, m // 3)
                if key not in index:
                    index[key] = len(verts)
                    verts.append(key)
                seq.append(index[key])
            if prim == 0x90:
                tris += [tuple(seq[i:i + 3]) for i in range(0, len(seq) - 2, 3)]
            elif prim == 0x98:
                for i in range(len(seq) - 2):
                    a, b, c = seq[i:i + 3]
                    tris.append((b, a, c) if i % 2 else (a, b, c))
            else:
                for i in range(1, len(seq) - 1):
                    tris.append((seq[0], seq[i], seq[i + 1]))
            cur.append(prim)
        gr["verts"], gr["tris"] = verts, tris
        lv["groups"].append(gr)


def _decode_pc(blob, L, grp, ng, lv, owned, names, li, skinned):
    vb = blob.target(L + 0xC)
    if vb is None:
        raise GeomError("vertex buffer not a pointer")
    pp, cp, up, np_ = (blob.target(vb + 4 * i) for i in range(4))
    names[vb] = f"level/{li}/vertices"
    counts = lv["skin"]
    lists = []
    top = 0
    for k in range(ng):
        G = grp + 0x20 * k
        n, ix = blob.u32(G + 0x10), blob.target(G + 0x1C)
        if ix is None:
            raise GeomError("index list not a pointer")
        if blob.u16(ix) & 0x8000:
            raise GeomError("strip records (no PC group has them)")
        idx = [blob.u16(ix + 2 * i) for i in range(3 * n)]
        owned.append((ix, ix + 6 * n))
        names[ix] = f"level/{li}/group/{k}/draw"
        lists.append(idx)
        if idx:
            top = max(top, max(idx) + 1)
    owned.append((vb, vb + 0x20))
    for p, stride in ((pp, 16), (cp, 4), (up, 8), (np_, 16)):
        if p is not None:
            owned.append((p, p + stride * top))
    lv["vb"] = {"count": top, "has": [pp is not None, cp is not None, up is not None, np_ is not None],
                "pad": blob.le4(pp + 12, 1).hex() if pp is not None and top else None}
    bone_of = []
    if counts:
        for b, c in enumerate(counts):
            bone_of += [b] * c
    for k in range(ng):
        G = grp + 0x20 * k
        gr = {"rec": _group_rec(blob, G)}
        gr["uvscroll"], gr["rec"]["uc"] = _uvscroll(blob, G, owned, names, f"level/{li}/group/{k}")
        lit = _lit(gr["rec"]["flags"])
        verts, index, seq = [], {}, []
        for i in lists[k]:
            pos = blob.le4(pp + 16 * i, 3)
            uv = blob.le4(up + 8 * i, 2) if up is not None else bytes(8)
            nrm = col = None
            if lit:
                nrm = blob.le4(np_ + 16 * i, 3) if np_ is not None else bytes(12)
            else:
                col = bytes(blob.d[cp + 4 * i:cp + 4 * i + 4]) if cp is not None else WHITE
            bone = bone_of[i] if counts and i < len(bone_of) else 0
            key = (pos, nrm, col, uv, bone)
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            seq.append(index[key])
        gr["verts"] = verts
        gr["tris"] = [tuple(seq[i:i + 3]) for i in range(0, len(seq), 3)]
        lv["groups"].append(gr)


_PS2 = None


def ps2_module():
    global _PS2
    if _PS2 is None:
        d = find_up("docs/ps2-version/scripts")
        if d is None:
            raise GeomError("PS2 geometry needs docs/ps2-version/scripts/ps2_geom.py")
        sys.path.insert(0, str(d))
        import ps2_geom  # noqa: E402
        _PS2 = ps2_geom
    return _PS2


class _Ps2Shim:
    def __init__(self, blob):
        self.d, self.slots = blob.d, blob.ptrs

    def u16(self, o):
        return struct.unpack_from("<H", self.d, o)[0] if o + 2 <= len(self.d) else 0

    def u32(self, o):
        return struct.unpack_from("<I", self.d, o)[0] if o + 4 <= len(self.d) else 0

    def target(self, o):
        if o not in self.slots:
            return None
        t = self.u32(o)
        return t if t < len(self.d) else None


def _decode_ps2(blob, L, grp, ng, lv, owned, names, li):
    P = ps2_module()
    pk = _Ps2Shim(blob)
    kind = blob.u16(L)
    for k in range(ng):
        G = grp + 0x20 * k
        gr = {"rec": _group_rec(blob, G)}
        gr["rec"]["u10"] = blob.u32(G + 0x10)
        gr["rec"]["u1c"] = blob.u32(G + 0x1C)
        pkt = blob.target(G + 0xC)
        if pkt is None:
            raise GeomError("PS2 group packet not a pointer")
        names[pkt] = f"level/{li}/group/{k}/draw"
        if kind == P.LEVEL_SKINNED:
            r = pkt + 0x10
            while blob.u32(r + 4):
                sub = blob.target(r + 4)
                tag = blob.u32(sub)
                owned.append((sub, sub + 16 + (tag & 0xFFFF) * 16))
                r += 8
            owned.append((pkt, r + 8))
        else:
            tag = blob.u32(pkt + 0x10)
            owned.append((pkt, pkt + 0x20 + (tag & 0xFFFF) * 16))
        try:
            tris = P.group_triangles(pk, G, kind)
        except P.FormatError as e:
            raise GeomError(f"PS2 packet: {e}")
        tris, _ = P.orient(tris)
        lit = _lit(gr["rec"]["flags"])
        verts, index, out = [], {}, []
        for a, b, c, _s in tris:
            t = []
            for v in (a, b, c):
                pos = struct.pack("<3f", *v[0])
                uv = struct.pack("<2f", *v[1])
                nrm = struct.pack("<3f", *v[2]) if v[2] is not None else None
                col = P.colour_to_gc(v[3]) if v[3] is not None else None
                if lit and nrm is None:
                    nrm = bytes(12)
                if not lit:
                    nrm = None
                    if col is None:
                        col = WHITE
                key = (pos, nrm, col, uv, v[4])
                if key not in index:
                    index[key] = len(verts)
                    verts.append(key)
                t.append(index[key])
            out.append(tuple(t))
        gr["verts"], gr["tris"] = verts, out
        lv["groups"].append(gr)


# ------------------------------------------------------------------------------------------
# glTF writing

class _Buf:
    def __init__(self):
        self.data = bytearray()
        self.views, self.accessors = [], []

    def add(self, raw, comp, typ, count, target=None, minmax=None, normalized=False):
        while len(self.data) % 4:
            self.data.append(0)
        bv = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(raw)}
        if target:
            bv["target"] = target
        self.data += raw
        self.views.append(bv)
        acc = {"bufferView": len(self.views) - 1, "componentType": comp, "count": count, "type": typ}
        if normalized:
            acc["normalized"] = True
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1


def _f32s(b):
    return struct.unpack("<%df" % (len(b) // 4), b)


def _unit(n):
    x, y, z = _f32s(n)
    ln = math.sqrt(x * x + y * y + z * z)
    if not (ln > 0) or math.isinf(ln):
        return struct.pack("<3f", 0.0, 1.0, 0.0), False
    u = struct.pack("<3f", x / ln, y / ln, z / ln)
    return u, abs(ln - 1.0) < 5e-4


def to_gltf(g, name, bin_name, tex_link=None, extras=None):
    """(gltf dict, bin bytes). tex_link(reloc value) -> base:// id (or None)."""
    buf = _Buf()
    nodes, meshes, materials, matkey = [], [], [], {}
    skinned = g["skinned"] != 0
    root = {"name": name, "children": []}
    nodes.append(root)
    joints = []
    if skinned:
        for b in range(8):
            nodes.append({"name": f"bone_{b}"})
            joints.append(len(nodes) - 1)
            root["children"].append(len(nodes) - 1)
    for li, lv in enumerate(g["levels"]):
        prims = []
        for k, gr in enumerate(lv["groups"]):
            if lv["kind"] == "shadow":
                if not lv.get("undecoded"):
                    prims.append(_shadow_prim(buf, lv, gr))
                continue
            prims.append(_drawn_prim(buf, gr, skinned, tex_link, materials, matkey))
        if not prims:
            continue
        mesh = {"name": f"{name}_lod{li}", "primitives": prims}
        meshes.append(mesh)
        node = {"name": f"lod{li}", "mesh": len(meshes) - 1,
                "extras": {"level": li, "distance": _f32s(bytes.fromhex(g["dist"][li]))[0]}}
        if skinned and lv["kind"] == "drawn":
            node["skin"] = 0
        nodes.append(node)
        root["children"].append(len(nodes) - 1)
    gl = {"asset": {"version": "2.0", "generator": "HWVX-SDL tools/xdl_gltf.py"},
          "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes}
    if meshes:
        gl["meshes"] = meshes
    if materials:
        gl["materials"] = materials
    if skinned:
        gl["skins"] = [{"name": "bones", "joints": joints}]
    gl["buffers"] = [{"uri": bin_name, "byteLength": len(buf.data)}]
    if buf.views:
        gl["bufferViews"] = buf.views
        gl["accessors"] = buf.accessors
    root["extras"] = geometry_extras(g, extras)
    if not root["children"]:
        del root["children"]              # (a PS2 geometry of one undecoded shadow level)
    while len(buf.data) % 4:
        buf.data.append(0)
    gl["buffers"][0]["byteLength"] = len(buf.data)
    if not buf.views:
        del gl["buffers"]
    return gl, bytes(buf.data)


def geometry_extras(g, extras=None):
    lv_ex = []
    for lv in g["levels"]:
        e = {"kind": lv["kind"], "record": lv["rec"]}
        if lv.get("undecoded"):
            e["undecoded"] = True
        if lv["names"] is not None:
            e["textureNames"] = lv["names"]
        if lv["skin"] is not None:
            e["boneCounts"] = lv["skin"]
        if "vb" in lv:
            e["vertexBuffer"] = lv["vb"]
        lv_ex.append(e)
    x = {"xdl": "geometry", "release": g["release"], "magic": g["magic"], "skinned": g["skinned"],
         "u6": g["u6"], "bounds": g["box"], "lodDistances": g["dist"], "levels": lv_ex}
    if extras:
        x.update(extras)
    return x


def _drawn_prim(buf, gr, skinned, tex_link, materials, matkey):
    verts, tris = gr["verts"], gr["tris"]
    lit = verts and verts[0][1] is not None
    n = len(verts)
    pos = b"".join(v[0] for v in verts)
    fl = _f32s(pos)
    mm = None
    if n:
        mm = ([min(fl[i::3]) for i in range(3)], [max(fl[i::3]) for i in range(3)])
    attrs = {"POSITION": buf.add(pos, 5126, "VEC3", n, 34962, mm)}
    if lit:
        units, exact = [], True
        for v in verts:
            u, ok = _unit(v[1])
            units.append(u)
            exact = exact and ok and u == v[1]
        attrs["NORMAL"] = buf.add(b"".join(units), 5126, "VEC3", n, 34962)
        if not exact:
            attrs["_NORMAL_RAW"] = buf.add(b"".join(v[1] for v in verts), 5126, "VEC3", n, 34962)
    else:
        attrs["COLOR_0"] = buf.add(b"".join(v[2] for v in verts), 5121, "VEC4", n, 34962, normalized=True)
    attrs["TEXCOORD_0"] = buf.add(b"".join(v[3] for v in verts), 5126, "VEC2", n, 34962)
    if skinned:
        attrs["JOINTS_0"] = buf.add(b"".join(bytes((v[4], 0, 0, 0)) for v in verts), 5121, "VEC4", n, 34962)
        attrs["WEIGHTS_0"] = buf.add(struct.pack("<4f", 1, 0, 0, 0) * n, 5126, "VEC4", n, 34962)
    flat = [i for t in tris for i in t]
    big = n > 0xFFFF
    ib = struct.pack(("<%dI" if big else "<%dH") % len(flat), *flat)
    prim = {"attributes": attrs, "mode": 4}
    if flat:
        prim["indices"] = buf.add(ib, 5125 if big else 5123, "SCALAR", len(flat), 34963)
    ex = {"group": dict(gr["rec"])}
    if gr.get("uvscroll") is not None:
        ex["uvScroll"] = gr["uvscroll"]
    prim["extras"] = ex
    tex = tex_link(gr["texture"]) if tex_link and gr.get("texture") is not None else None
    key = (tex, gr["rec"]["flags"] & 0xFF, gr["rec"]["blend"])
    if key not in matkey:
        m = {"name": f"mat{len(materials)}", "pbrMetallicRoughness": {"metallicFactor": 0.0},
             "doubleSided": False, "extras": {"texture": tex}}
        if gr["rec"]["flags"] & F_ALPHA:
            m["alphaMode"] = "BLEND"
        materials.append(m)
        matkey[key] = len(materials) - 1
    prim["material"] = matkey[key]
    if gr.get("texture") is not None and tex is None:
        ex["textureReloc"] = gr["texture"]
    return prim


def _shadow_prim(buf, lv, gr):
    pos = lv["positions"]
    n = len(pos)
    raw = b"".join(pos)
    fl = _f32s(raw)
    mm = ([min(fl[i::3]) for i in range(3)], [max(fl[i::3]) for i in range(3)]) if n else None
    prim = {"attributes": {"POSITION": buf.add(raw, 5126, "VEC3", n, 34962, mm) if n else None}, "mode": 1}
    if not n:
        prim["attributes"] = {}
    idx = [i for e in gr["edges"] for i in (e[1], e[2])]
    if idx:
        prim["indices"] = buf.add(struct.pack("<%dH" % len(idx), *idx), 5123, "SCALAR", len(idx), 34963)
    prim["extras"] = {"group": dict(gr["rec"]), "shadow": True,
                      "edgeNormals": [[e[0].hex(), e[3].hex(), e[4]] for e in gr["edges"]]}
    return prim


def dumps(gl):
    return json.dumps(gl, indent=1) + "\n"


def content_hash(gl, bin_bytes):
    """What an unchanged glTF hashes to: its JSON without asset/buffers uri, and the buffer."""
    x = {k: v for k, v in gl.items() if k not in ("asset", "buffers")}
    h = hashlib.blake2b(digest_size=16)
    h.update(json.dumps(x, sort_keys=True, separators=(",", ":")).encode())
    h.update(bin_bytes)
    return h.hexdigest()


def write(gl, bin_bytes, path):
    path = Path(path)
    path.write_text(dumps(gl))
    if bin_bytes:
        (path.parent / gl["buffers"][0]["uri"]).write_bytes(bin_bytes)


# ------------------------------------------------------------------------------------------
# glTF reading (an edited file back to the neutral form)

_COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
_NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    path = Path(path)
    gl = json.loads(path.read_text())
    bins = []
    for b in gl.get("buffers", []):
        uri = b.get("uri", "")
        if uri.startswith("data:"):
            bins.append(base64.b64decode(uri.split(",", 1)[1]))
        else:
            bins.append((path.parent / uri).read_bytes())
    return gl, (bins[0] if bins else b"")


def _acc_raw(gl, bins, i):
    """(list of tuples of python values, raw element bytes list) of accessor i."""
    a = gl["accessors"][i]
    code, size = _COMP[a["componentType"]]
    nc = _NCOMP[a["type"]]
    bv = gl["bufferViews"][a["bufferView"]]
    data = bins if isinstance(bins, (bytes, bytearray)) else bins[bv["buffer"]]
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", size * nc)
    out, raw = [], []
    for k in range(a["count"]):
        o = base + k * stride
        raw.append(bytes(data[o:o + size * nc]))
        out.append(struct.unpack_from("<%d%s" % (nc, code), data, o))
    return out, raw


def from_gltf(gl, bin_bytes):
    """The neutral form (decode()'s, without owned/names) of a glTF this module wrote, possibly
    edited: levels and groups from the meshes, records from the extras."""
    root = gl["nodes"][gl["scenes"][gl.get("scene", 0)]["nodes"][0]]
    x = root.get("extras", {})
    if x.get("xdl") != "geometry":
        raise GeomError("not an xdl geometry glTF (root node extras.xdl)")
    g = {"release": x["release"], "magic": x["magic"], "skinned": x["skinned"], "u6": x["u6"],
         "box": x["bounds"], "dist": list(x["lodDistances"]), "levels": []}
    by_level = {}
    for ci in root.get("children", []):
        nd = gl["nodes"][ci]
        if "mesh" in nd:
            by_level[nd["extras"]["level"]] = (nd, gl["meshes"][nd["mesh"]])
    for li, le in enumerate(x["levels"]):
        lv = {"kind": le["kind"], "rec": le["record"], "names": le.get("textureNames"),
              "undecoded": le.get("undecoded", False),
              "skin": le.get("boneCounts"), "groups": []}
        if "vertexBuffer" in le:
            lv["vb"] = le["vertexBuffer"]
        g["levels"].append(lv)
        if li not in by_level:
            continue
        nd, mesh = by_level[li]
        if "distance" in nd.get("extras", {}):
            g["dist"][li] = struct.pack("<f", nd["extras"]["distance"]).hex()
        for pk, pr in enumerate(mesh["primitives"]):
            ex = pr.get("extras", {})
            if "group" not in ex:
                raise GeomError(f"level {li} primitive {pk}: no extras.group (the group record). The "
                                "exporter dropped the per-primitive extras (Blender does): keep the "
                                "primitives' extras from the extracted .gltf, or edit with a tool that "
                                "preserves them")
            gr = {"rec": ex["group"]}
            if lv["kind"] == "shadow":
                if "POSITION" in pr["attributes"]:
                    _, raw = _acc_raw(gl, bin_bytes, pr["attributes"]["POSITION"])
                    lv["positions"] = raw
                else:
                    lv["positions"] = []
                idx = [v[0] for v in _acc_raw(gl, bin_bytes, pr["indices"])[0]] if "indices" in pr else []
                en = ex.get("edgeNormals", [])
                gr["edges"] = [(bytes.fromhex(en[i][0]), idx[2 * i], idx[2 * i + 1], bytes.fromhex(en[i][1]), en[i][2])
                               for i in range(len(idx) // 2)]
                lv["groups"].append(gr)
                continue
            at = pr["attributes"]
            _, pos = _acc_raw(gl, bin_bytes, at["POSITION"])
            n = len(pos)
            _, uv = _acc_raw(gl, bin_bytes, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else (None, [bytes(8)] * n)
            nrm = col = [None] * n
            if "_NORMAL_RAW" in at:
                _, nrm = _acc_raw(gl, bin_bytes, at["_NORMAL_RAW"])
            elif "NORMAL" in at:
                _, nrm = _acc_raw(gl, bin_bytes, at["NORMAL"])
            if "COLOR_0" in at:
                a = gl["accessors"][at["COLOR_0"]]
                vals, raw = _acc_raw(gl, bin_bytes, at["COLOR_0"])
                if a["componentType"] == 5121 and a["type"] == "VEC4":
                    col = raw
                else:
                    # normalised integers (u8 /255, u16 /65535) or floats; a VEC3's alpha is 1
                    scale = {5121: 255.0, 5123: 65535.0}.get(a["componentType"], 1.0)
                    col = [bytes(min(255, max(0, round(c * 255)))
                                 for c in ([x / scale for x in v] + [1.0])[:4]) for v in vals]
            bones = [0] * n
            if "JOINTS_0" in at:
                bones = [v[0] for v in _acc_raw(gl, bin_bytes, at["JOINTS_0"])[0]]
            gr["verts"] = [(pos[i], nrm[i], col[i], uv[i], bones[i]) for i in range(n)]
            if "indices" in pr:
                flat = [v[0] for v in _acc_raw(gl, bin_bytes, pr["indices"])[0]]
            else:
                flat = list(range(n))
            gr["tris"] = [tuple(flat[i:i + 3]) for i in range(0, len(flat) - 2, 3)]
            gr["uvscroll"] = ex.get("uvScroll")
            mat = gl["materials"][pr["material"]] if "material" in pr else {}
            gr["texture_link"] = mat.get("extras", {}).get("texture")
            gr["texture_reloc"] = ex.get("textureReloc")
            lv["groups"].append(gr)
    return g


# ------------------------------------------------------------------------------------------
# encoding (an edited geometry to the release's layout)

class Out:
    def __init__(self, e):
        self.e = e
        self.b = bytearray()
        self.ptrs = {}            # slot -> target (node-relative)
        self.tex = {}             # slot -> link or reloc value
        self.names = {}           # structural name -> offset

    def align(self, a):
        while len(self.b) % a:
            self.b.append(0)

    def here(self, name=None):
        if name is not None:
            self.names[name] = len(self.b)
        return len(self.b)

    def put(self, fmt, *v):
        self.b += struct.pack(self.e + fmt, *v)

    def raw(self, b):
        self.b += b

    def at(self, o, fmt, *v):
        struct.pack_into(self.e + fmt, self.b, o, *v)

    def ptr(self, slot, target):
        self.at(slot, "I", target)
        self.ptrs[slot] = target


def _f32_words(le_bytes, e):
    if e == "<":
        return le_bytes
    return b"".join(le_bytes[i:i + 4][::-1] for i in range(0, len(le_bytes), 4))


def _bounds(g, e):
    """Header 0x10..0x3F from LOD 0's positions (beta-cars geometry.md §1.1)."""
    pts = []
    for lv in g["levels"][:1]:
        for gr in lv["groups"]:
            pts += [_f32s(v[0]) for v in gr.get("verts", [])]
        pts += [_f32s(p) for p in lv.get("positions", [])]
    if not pts:
        return bytes.fromhex(g["box"])
    mn = [min(p[i] for p in pts) for i in range(3)]
    mx = [max(p[i] for p in pts) for i in range(3)]
    r1 = max(max(abs(v) for v in mn), max(abs(v) for v in mx))
    r2 = math.sqrt(sum((mx[i] - mn[i]) ** 2 for i in range(3))) / 2
    c = [(mn[i] + mx[i]) / 2 for i in range(3)]
    return struct.pack(e + "12f", *mn, r1, *mx, r2, *c, 0.0)


def encode(g, release, edited_bounds=True):
    """(bytes, pointer slots {slot: target}, texture slots {slot: group}, names {name: offset})
    for the neutral form g, in release's layout. Node-relative offsets; textures are left for
    the caller (the group's texture link) to relocate."""
    if release == "ps2":
        raise GeomError("encoding edited PS2 geometry (VIF packets) is not supported yet; "
                        "unchanged PS2 geometry keeps its original bytes")
    if any(lv.get("undecoded") for lv in g["levels"]):
        raise GeomError("a level of this geometry is not decoded (PS2 shadow volume)")
    e = ENDIAN[release]
    o = Out(e)
    n = len(g["levels"])
    o.here("")
    o.put("HHHH", MAGIC[release], n, g["skinned"], g["u6"])
    o.put("II", 0, 0)
    box = _bounds(g, e) if edited_bounds else bytes.fromhex(g["box"])
    if not edited_bounds and e == ">":
        box = _f32_words(box, e)
    o.raw(box)
    lods = o.here("levels")
    o.ptr(8, lods)
    o.raw(bytes(0x18 * n))
    o.align(16)
    dist = o.here("distances")
    o.ptr(0xC, dist)
    for dh in g["dist"]:
        o.raw(_f32_words(bytes.fromhex(dh), e))
    o.align(16)
    texslots = {}
    later = []
    for li, lv in enumerate(g["levels"]):
        L = lods + 0x18 * li
        rec = lv["rec"]
        ng = len(lv["groups"])
        o.at(L, "HH", rec.get("u0", 0), ng)
        o.at(L + 4, "I", rec.get("u4", 0))
        o.align(16)
        grp = o.here(f"level/{li}/groups")
        o.ptr(L + 8, grp)
        o.raw(bytes(0x20 * ng))
        for k, gr in enumerate(lv["groups"]):
            G = grp + 0x20 * k
            o.names[f"level/{li}/group/{k}"] = G
            r = gr["rec"]
            o.at(G, "HH", r.get("u0", 0), r["flags"])
            o.b[G + 4:G + 8] = bytes.fromhex(r["colour"])
            o.at(G + 8, "I", r.get("u8", 0))
            o.at(G + 0x14, "HH", r["blend"], r["sort"])
            has_tex = gr.get("texture_link") is not None or gr.get("texture_reloc") is not None
            # a relocated slot's stored value is tool junk the loader overwrites: write 0
            o.at(G + 0x18, "I", 0 if has_tex else r.get("slot18", 0xFFFF))
            if has_tex:
                texslots[G + 0x18] = (li, k)
            if gr.get("uvscroll"):
                o.align(16)
                u = o.here(f"level/{li}/group/{k}/uvscroll")
                o.ptr(G + 0xC, u)
                o.raw(bytes.fromhex(gr["uvscroll"]))
            else:
                o.at(G + 0xC, "I", r.get("uc") or 0)
        if lv["skin"] is not None or (g["skinned"] and lv["kind"] == "drawn"):
            o.align(16)
            sk = o.here(f"level/{li}/bones")
            o.ptr(L + 0x10, sk)
            o.raw(bytes(0x20))
            lv["_skin_at"] = sk
        else:
            o.at(L + 0x10, "I", rec.get("u10", 0))
        if lv["kind"] == "shadow":
            o.align(32)
            p = o.here(f"level/{li}/positions")
            o.ptr(L + 0xC, p)
            for q in lv["positions"]:
                o.raw(_f32_words(q, e))
            for k, gr in enumerate(lv["groups"]):
                later.append(("edges", li, k, grp + 0x20 * k, gr))
        elif release == "gc":
            _enc_gc_level(o, g, lv, li, L, grp, later)
        else:
            _enc_pc_level(o, g, lv, li, L, grp, later)
        if lv["names"] is not None:
            nm = o.here(f"level/{li}/names")
            o.ptr(L + 0x14, nm)
            for s in lv["names"]:
                o.raw(s.encode("latin-1") + b"\0")
            o.raw(b"\0")
            o.align(4)
        else:
            o.at(L + 0x14, "I", rec.get("u14", 0))
    for what, li, k, G, data in later:
        if what == "edges":
            o.align(32)
            ed = o.here(f"level/{li}/group/{k}/edges")
            o.ptr(G + 0x1C, ed)
            o.at(G + 0x10, "I", len(data["edges"]))
            for n0, v0, v1, n1, ty in data["edges"]:
                o.raw(_f32_words(n0, e))
                o.put("HH", v0, v1)
                o.raw(_f32_words(n1, e))
                o.put("I", ty)
        elif what == "dl":
            o.align(32)
            dl = o.here(f"level/{li}/group/{k}/draw")
            o.ptr(G + 0x1C, dl)
            o.raw(data)
            o.at(G + 0x10, "I", len(data))
        elif what == "ix":
            o.align(4)
            ix = o.here(f"level/{li}/group/{k}/draw")
            o.ptr(G + 0x1C, ix)
            o.raw(data)
    o.align(32)
    return bytes(o.b), o.ptrs, texslots, o.names


def _level_vertices(g, lv):
    """Level vertex list (pos, nrm, col, uv, bone) across groups, skinned ones sorted by bone;
    per group the triangles in level indices."""
    verts, index, lists = [], {}, []
    for gr in lv["groups"]:
        lst = []
        for t in gr["tris"]:
            for i in t:
                key = gr["verts"][i]
                if key not in index:
                    index[key] = len(verts)
                    verts.append(key)
                lst.append(index[key])
        lists.append(lst)
    if g["skinned"]:
        order = sorted(range(len(verts)), key=lambda i: verts[i][4])
        remap = {old: new for new, old in enumerate(order)}
        verts = [verts[i] for i in order]
        lists = [[remap[i] for i in lst] for lst in lists]
    return verts, lists


def _enc_gc_level(o, g, lv, li, L, grp, later):
    e = o.e
    verts, lists = _level_vertices(g, lv)
    arrays = {k: ([], {}) for k in ("pos", "uv", "col", "nrm")}

    def idx(kind, val):
        lst, ix = arrays[kind]
        if val not in ix:
            ix[val] = len(lst)
            lst.append(val)
        return ix[val]
    # positions in skinned levels must stay sorted by bone: index them first, in order
    for v in verts:
        idx("pos", (v[0], v[4]) if g["skinned"] else v[0])
    dls = []
    for k, gr in enumerate(lv["groups"]):
        flags = gr["rec"]["flags"]
        lit = _lit(flags)
        mtx = bool(g["skinned"]) and lit
        body = bytearray()
        for vi in lists[k]:
            v = verts[vi]
            if mtx:
                body.append(3 * v[4])
            A = idx("pos", (v[0], v[4]) if g["skinned"] else v[0])
            B = idx("nrm", v[1] if v[1] is not None else bytes(12)) if lit else idx("col", v[2] if v[2] is not None else WHITE)
            C = idx("uv", v[3])
            body += struct.pack(">HHH", A, B, C)
        n = len(lists[k])
        dl = bytes((0x90,)) + struct.pack(">H", n) + bytes(body) if n else b""
        dl += bytes(-len(dl) % 32)
        dls.append(dl)
    pos = [p[0] if g["skinned"] else p for p in arrays["pos"][0]]
    if g["skinned"]:
        counts = [0] * 8
        for p in arrays["pos"][0]:
            counts[p[1]] += 1
        o.b[lv["_skin_at"]:lv["_skin_at"] + 0x20] = struct.pack(">8I", *counts)
    o.align(16)
    vb = o.here(f"level/{li}/vertices")
    o.ptr(L + 0xC, vb)
    blob = bytearray()
    for p in pos:
        blob += _f32_words(p, e)
    uvo = len(blob)
    for u in arrays["uv"][0]:
        blob += _f32_words(u, e)
    clo = len(blob) if arrays["col"][0] else 0
    for c in arrays["col"][0]:
        blob += c
    nro = len(blob) if arrays["nrm"][0] else 0
    for nn in arrays["nrm"][0]:
        blob += _f32_words(nn, e)
    o.put("4I", uvo, clo, nro, len(blob))
    o.raw(bytes(blob))
    for k, dl in enumerate(dls):
        later.append(("dl", li, k, grp + 0x20 * k, dl))


def _enc_pc_level(o, g, lv, li, L, grp, later):
    e = o.e
    verts, lists = _level_vertices(g, lv)
    if len(verts) > 0x7FFF:
        raise GeomError(f"level {li}: {len(verts)} vertices (at most 0x7FFF)")
    # the original's arrays, plus colour / normals when the vertices need them (a missing
    # array reads as white / zero: _decode_pc)
    need = [True, any(v[2] is not None and v[2] != WHITE for v in verts), True,
            any(v[1] is not None and v[1] != bytes(12) for v in verts)]
    has = (lv.get("vb") or {}).get("has") or [True, any(v[2] is not None for v in verts), True,
                                                any(v[1] is not None for v in verts)]
    has = [a or b for a, b in zip(has, need)]
    if g["skinned"]:
        counts = [0] * 8
        for v in verts:
            counts[v[4]] += 1
        o.b[lv["_skin_at"]:lv["_skin_at"] + 0x20] = struct.pack("<8I", *counts)
    o.align(16)
    vb = o.here(f"level/{li}/vertices")
    o.ptr(L + 0xC, vb)
    o.raw(bytes(0x20))
    pad = bytes.fromhex((lv.get("vb") or {}).get("pad") or "00000000")
    cols = [(v[2] if v[2] is not None else WHITE) for v in verts]
    nrms = [(v[1] if v[1] is not None else bytes(12)) for v in verts]
    for slot, on, data, stride in ((0, has[0], [v[0] + pad for v in verts], 16), (4, has[1], cols, 4),
                                    (8, has[2], [v[3] for v in verts], 8), (0xC, has[3], [n + pad for n in nrms], 16)):
        if not on:
            continue
        o.align(16)
        a = o.here()
        o.ptr(vb + slot, a)
        for x in data:
            o.raw(x)
    for k, lst in enumerate(lists):
        later.append(("ix", li, k, grp + 0x20 * k, struct.pack("<%dH" % len(lst), *lst)))
        o.at(grp + 0x20 * k + 0x10, "I", len(lst) // 3)


# ------------------------------------------------------------------------------------------
# commands

def _pack_blob(folder):
    folder = Path(folder)
    man = json.loads((folder.parent / "manifest.json").read_text())
    rel = man["platform"]
    e = ENDIAN[rel]
    raw = (folder / "pointers.bin").read_bytes() if (folder / "pointers.bin").exists() else b""
    ptrs = set(struct.unpack(e + "%dI" % (len(raw) // 4), raw))
    tr = folder / "texture_relocs.bin"
    tex = {}
    if tr.exists():
        b = tr.read_bytes()
        tex = dict(struct.unpack_from(e + "2I", b, i) for i in range(0, len(b), 8))
    return Blob((folder / "data.bin").read_bytes(), ptrs, rel), tex


def cmd_info(a):
    blob, tex = _pack_blob(a.pack)
    for i, off in enumerate(scan(blob)):
        g = decode(blob, off, tex)
        lv = ", ".join(f"{l['kind']} {len(l['groups'])}g {sum(len(x.get('tris', x.get('edges', []))) for x in l['groups'])}t"
                       for l in g["levels"])
        print(f"geometry/{i} @0x{off:X} skinned {g['skinned']}: {lv}")
    return 0


def cmd_export(a):
    blob, tex = _pack_blob(a.pack)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for i, off in enumerate(scan(blob)):
        if a.only is not None and i != a.only:
            continue
        g = decode(blob, off, tex)
        gl, b = to_gltf(g, f"geometry_{i}", f"geometry_{i}.bin", lambda v: f"reloc:0x{v:X}")
        write(gl, b, out / f"geometry_{i}.gltf")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("pack")
    p = sub.add_parser("export")
    p.add_argument("pack")
    p.add_argument("out")
    p.add_argument("--only", type=int)
    a = ap.parse_args()
    try:
        return {"info": cmd_info, "export": cmd_export}[a.cmd](a)
    except GeomError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
