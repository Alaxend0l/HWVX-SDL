#!/usr/bin/env python3
"""Mod packages (.hwmod) with the no-game-data check (docs/xdl-format README §8, §10.3).

    xdl_mod.py index DATA_DIR... [-o INDEX.npz] [--archives HW_Cars,HW_IPack] [--no-geometry]
    xdl_mod.py check MOD_DIR --index INDEX.npz                what pack would refuse, without packing
    xdl_mod.py pack MOD_DIR -o OUT.hwmod --index INDEX.npz [--strip-unchanged] [--allow-partial-index]
    xdl_mod.py info OUT.hwmod | MOD_DIR
    xdl_mod.py install OUT.hwmod MODS_DIR                     unpack into MODS_DIR/<name> (the port's
                                                              Port_ModsInstall does the same)

A mod is a folder with `mod.jsonc` (schemas/Mod.schema.json) and the modder's own sources. It
never holds game data: it names base data by id (base://...) and says what to change. The port
applies it over the player's own copy (src/game/Port_Mods.c).

`index` reads the modder's own install (one or more releases' folders) and writes hashes of
everything in it: no game data, but made locally and not meant to be shipped. `pack` refuses when
anything in the mod matches it, naming the base asset to reference instead:

  whole files        any file of the install, a pack's song (.mid)
  raw runs           every 32-byte block of the archives (256 of other files: movies, streams)
                     against every window of every mod file, and of every hex / base64 / data-URI
                     string in its JSON: catches extracted .bin files and raw records. Blocks of
                     few distinct bytes or plain progressions aren't indexed.
  decoded sound      the same over each sound's and bank sample's PCM16 (a WAV of a GameCube
                     or PS2 sound has other bytes than the pack)
  textures           a PNG whose pixels equal a base texture level's (with or without alpha), or
                     that looks like one (a 256-bit gradient hash of the mean of R, G and B, which
                     a hue rotation keeps): an edited game texture is still the game's, and is
                     made with a recipe instead
  geometry           a glTF mesh whose vertex positions are a base geometry's
  patches            a field patch that restates the base's value (a copied object record);
                     --strip-unchanged drops those fields in the package instead of refusing
  file kinds         only kinds the check can read: .json .jsonc .png .wav .gltf .glb .bin .mid
                     .md .txt

A package is a zip, entries stored (not compressed) in sorted order with fixed dates: the same
mod packs to the same bytes, and the port reads it without an inflater. `check.json` in it
records what was checked against which index, each file's hash, and the fingerprints of the base
assets the mod refers to on the releases the index (and the pairing table, when found) knows.
"""

import argparse
import base64
import binascii
import hashlib
import io
import json
import multiprocessing
import re
import struct
import sys
import time
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xdl_gltf as XG  # noqa: E402
import xdl_media as XM  # noqa: E402
import xdl_struct as XS  # noqa: E402

TOOL = "tools/xdl_mod.py"
TOOL_VERSION = 1
FORMAT_VERSION = 1
ROOT = Path(__file__).resolve().parents[1]

ARCHIVE_EXT = (".xgc", ".xpc", ".xps")
ALLOWED_EXT = (".json", ".jsonc", ".png", ".wav", ".gltf", ".glb", ".bin", ".mid", ".md", ".txt")
FINE, COARSE = 32, 256           # block sizes: archives, other files
MIN_DISTINCT = {FINE: 12, COARSE: 24}
UID_RE = re.compile(r"^uid://[0-9a-z]{1,13}$")
TEX_RE = re.compile(r"^base://([A-Za-z0-9_]+)/([a-z_]+_[0-9]+)/texture/((?:[a-z0-9]+:)?[0-9]+)$")
OBJ_RE = re.compile(r"^base://([A-Za-z0-9_]+)/([a-z_]+_[0-9]+)/([A-Za-z0-9_]+/[0-9]+(?:/[A-Za-z0-9_]+)*)$")
CAR_RE = re.compile(r"^base://HW_Cars/car_([0-9]+)$")
ITEM_RE = re.compile(r"^base://mcp/item/([0-9]+)$")
TRACK_RE = re.compile(r"^base://mcp/track/([0-9]+)$")
RELEASES = ("gc", "pc", "ps2")
ROOT_KIND = {"car": "CarAInfo", "item": "ItemRecord"}
# what the selects' entries extend, and the pack that holds their data
KINDS = {"cars": (CAR_RE, "HW_Cars", "car_{}", "base://HW_Cars/car_<n>"),
         "items": (ITEM_RE, "HW_Items", "item_{}", "base://mcp/item/<n>")}
OPS = {"hue_shift": {"degrees"}, "tint": {"color"}, "saturation": {"amount"},
       "brightness": {"amount"}, "matrix": {"rows"}}
DH_W, DH_H = 17, 16              # the gradient hash's grid: 16 x 16 comparisons
DH_NEAR = 40                     # bits of 256 that may differ and still "look like"
DH_MIN_STD = 6.0                 # flatter images have no gradients worth comparing
VTX_MIN, VTX_SHARE = 24, 0.2     # a mesh is a base one's from this many shared positions and share


class ModError(Exception):
    pass


def h64(b):
    return int.from_bytes(hashlib.blake2b(bytes(b), digest_size=8).digest(), "little")


# ------------------------------------------------------------------------------------------
# block hashes: a polynomial over the bytes mod 2^64, so every window of a mod file can be
# hashed at once and looked up among the base's aligned blocks

Q = 0x9E3779B97F4A7C15
QINV = pow(Q, -1, 1 << 64)


def _powers(q, n):
    p = np.full(max(n, 1), q, np.uint64)
    p[0] = 1
    return np.cumprod(p, dtype=np.uint64)


_PW = {}


def _trivial(a, min_distinct):
    """Rows of few distinct bytes, or arithmetic progressions of 1, 2 or 4-byte values."""
    s = np.sort(a, axis=1)
    out = ((s[:, 1:] != s[:, :-1]).sum(axis=1) + 1) < min_distinct
    a = np.ascontiguousarray(a)
    for dt in ("u1", ">u2", "<u2", ">u4", "<u4"):
        v = a.view(dt).astype(np.int64)
        out |= (np.diff(v, 2, axis=1) == 0).all(axis=1)
    return out


def block_hashes(buf, B):
    """Hashes of the aligned B-byte blocks of buf worth indexing."""
    n = len(buf) // B
    if n == 0:
        return np.empty(0, np.uint64)
    pw = _PW.setdefault(B, _powers(Q, B))
    a = np.frombuffer(buf, np.uint8, n * B).reshape(n, B)
    out = []
    step = (1 << 22) // B
    for i in range(0, n, step):
        c = a[i:i + step]
        h = (c.astype(np.uint64) * pw).sum(axis=1, dtype=np.uint64)
        out.append(h[~_trivial(c, MIN_DISTINCT[B])])
    return np.concatenate(out)


def window_hashes(buf, B):
    """The hash of buf[i:i+B] for every i."""
    n = len(buf)
    if n < B:
        return np.empty(0, np.uint64)
    b = np.frombuffer(buf, np.uint8).astype(np.uint64)
    P = np.concatenate((np.zeros(1, np.uint64), np.cumsum(b * _powers(Q, n), dtype=np.uint64)))
    return (P[B:] - P[:-B]) * _powers(QINV, n - B + 1)


# ------------------------------------------------------------------------------------------
# the index

def dhash(rgba):
    """256-bit gradient hash of the mean of R, G, B (4 uint64), or None for a flat image."""
    from PIL import Image
    h, w = rgba.shape[:2]
    if w < 16 or h < 16:
        return None
    g = rgba[:, :, :3].astype(np.float32).mean(axis=2)
    im = Image.fromarray(g).resize((DH_W, DH_H), Image.BOX)
    s = np.asarray(im)
    if s.std() < DH_MIN_STD:
        return None
    bits = (s[:, 1:] > s[:, :-1]).astype(np.uint8).reshape(-1)
    return np.packbits(bits).view("<u8").copy()


def pixel_hashes(rgba):
    h, w = rgba.shape[:2]
    head = struct.pack("<2I", w, h)
    rgb = np.ascontiguousarray(rgba[:, :, :3])
    return h64(head + rgba.tobytes()), h64(head + rgb.tobytes())


def vertex_hashes(le_f32x3):
    """One 64-bit hash per position (n x 3 little-endian f32, -0 as 0)."""
    v = np.ascontiguousarray(np.asarray(le_f32x3, "<f4").reshape(-1, 3) + np.float32(0))
    u = v.view("<u4").astype(np.uint64)
    h = u[:, 0] * np.uint64(0x9E3779B97F4A7C15) + (u[:, 1] << np.uint64(21)) * np.uint64(0xC2B2AE3D27D4EB4F)
    h ^= (u[:, 2] + np.uint64(0x165667B19E3779F9)) * np.uint64(0xFF51AFD7ED558CCD)
    h ^= h >> np.uint64(29)
    return h * np.uint64(0xC4CEB9FE1A85EC53)


def _positions(blob, off):
    """A geometry's vertex positions (little-endian f32 x 3), from the vertex arrays where the
    layout gives them whole (GameCube, PC), else from the decoded triangles (PS2)."""
    rel = blob.release
    out = []
    if rel in ("gc", "pc"):
        n, lods = blob.u16(off + 2), blob.target(off + 8)
        if lods is None:
            return out
        for li in range(n):
            L = lods + 0x18 * li
            grp, vb, ng = blob.target(L + 8), blob.target(L + 0xC), blob.u16(L + 2)
            if grp is None or vb is None or blob.u16(grp + 2) & XG.F_SHADOW:
                continue
            if rel == "gc":
                cnt = blob.u32(vb) // 12
                raw = blob.d[vb + 0x10:vb + 0x10 + 12 * cnt]
                if len(raw) == 12 * cnt and cnt:
                    out.append(np.frombuffer(raw, ">f4").astype("<f4").reshape(-1, 3))
            else:
                pp = blob.target(vb)
                top = 0
                for k in range(ng):
                    G = grp + 0x20 * k
                    cnt, ix = blob.u32(G + 0x10), blob.target(G + 0x1C)
                    if ix is None or ix + 6 * cnt > len(blob.d) or cnt == 0:
                        continue
                    top = max(top, int(np.frombuffer(blob.d, "<u2", 3 * cnt, ix).max()) + 1)
                if pp is not None and top and pp + 16 * top <= len(blob.d):
                    out.append(np.frombuffer(blob.d, "<f4", 4 * top, pp).reshape(-1, 4)[:, :3])
        return out
    g = XG.decode(blob, off)
    for lv in g["levels"]:
        for gr in lv["groups"]:
            if gr.get("verts"):
                out.append(np.frombuffer(b"".join(v[0] for v in gr["verts"]), "<f4").reshape(-1, 3))
    return out


class Schemas:
    """The pack structs' fields (schemas/*.schema.json): what a patch may set."""

    def __init__(self):
        self.s = {}
        for p in sorted((ROOT / "schemas").glob("*.schema.json")):
            d = json.loads(p.read_text())
            if "x-c-struct" in d:
                self.s[d["title"]] = d

    def counts(self, name):
        """{count field: [the link fields it counts]}: num*/count* fields, and those a link's
        description names as its array's length ("[numSlaves]"). A patch never sets them: the game
        would walk past the array."""
        props = self.s[name]["properties"]
        out = {k: [] for k in props if re.match(r"^(num|count)[A-Z0-9_]", k)}
        for k, p in props.items():
            if not (p.get("x-link") or p.get("items", {}).get("x-link")):
                continue
            m = re.search(r"\[([A-Za-z_]\w*)\]", p.get("description") or "")
            c = m.group(1) if m and m.group(1) in props else "num" + k[:1].upper() + k[1:]
            if c in props:
                out.setdefault(c, []).append(k)
        return out

    def count_of(self, name, link):
        """The count field of the array a link field points to, or None (one object)."""
        for c, links in self.counts(name).items():
            if link in links:
                return c
        return None

    def fields(self, name):
        """field -> (offset, prim, count or 0, link target struct or None). Array counts have prim
        None, as links and nested structs do: not patchable."""
        out = {}
        counts = self.counts(name)
        for k, p in self.s[name]["properties"].items():
            if "x-offset" not in p:
                continue
            off, ct = int(p["x-offset"], 16), p["x-c-type"]
            link = p.get("x-link") or p.get("items", {}).get("x-link")
            m = re.match(r"^(\w+)(?:\[(\d+)\])?$", ct)
            if k in counts:
                out[k] = (off, None, 0, None)
            elif link:
                if link.get("kind") == "object" and link.get("target") in self.s and "[" not in ct:
                    out[k] = (off, None, 0, link["target"])
                else:
                    out[k] = (off, None, 0, None)
            elif m and m.group(1) in XS.PRIM:
                out[k] = (off, m.group(1), int(m.group(2) or 0), None)
            else:
                out[k] = (off, None, 0, None)       # nested structs: not patchable yet
        return out


def canon(prim, count, value):
    """A field value's bytes (little-endian), or a ModError saying what it should be."""
    code, size, is_float = XS.PRIM[prim]
    vals = value if count else [value]
    if count and (not isinstance(value, list) or len(value) != count):
        raise ModError(f"needs an array of {count}")
    out = b""
    for v in vals:
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise ModError("needs a number")
        try:
            if is_float:
                out += struct.pack("<" + code, v)
            else:
                if v != int(v):
                    raise ModError(f"needs an integer ({prim})")
                out += struct.pack("<" + code, int(v))
        except (struct.error, OverflowError):
            raise ModError(f"{v} doesn't fit {prim}")
    return out


def field_key(obj_id, field, raw):
    return h64(f"{obj_id.lower()}|{field}|".encode() + raw)


def _objects(pk, blob, schemas, base_id):
    """Typed objects of a car or item pack: {id: struct}, and each scalar field's value hash."""
    root = ROOT_KIND.get(XM.TYPE_NAMES[pk.type] if pk.type < len(XM.TYPE_NAMES) else "")
    objs, keys = {}, []
    if root is None or len(blob.d) < 8:
        return objs, keys
    n, tab = blob.u32(0), blob.target(4)
    if tab is None:
        return objs, keys
    todo = []
    for i in range(min(n, 0x1000)):
        o = blob.target(tab + 8 * i)
        if o is not None:
            todo.append((f"{base_id}/{root}/{i}", root, o))
    seen = set()
    while todo:
        oid, sname, off = todo.pop(0)
        size = int(schemas.s[sname]["x-size"], 16)
        if (sname, off) in seen or off + size > len(blob.d):
            continue
        seen.add((sname, off))
        objs[oid] = sname
        for f, (fo, prim, count, target) in schemas.fields(sname).items():
            if target is not None:
                t = blob.target(off + fo)
                if t is not None:
                    todo.append((f"{oid}/{f}", target, t))
            elif prim is not None:
                code, sz, _ = XS.PRIM[prim]
                cnt = count or 1
                vals = struct.unpack_from(blob.e + f"{cnt}{code}", blob.d, off + fo)
                keys.append(field_key(oid, f, struct.pack(f"<{cnt}{code}", *vals)))
    return objs, keys


class Collect:
    """What one archive or file adds to the index (picklable: plain lists and arrays)."""

    def __init__(self):
        self.sources = []
        self.t = {k: ([], []) for k in ("b32", "b256", "whole", "px", "rgb", "snd", "vtx")}
        self.dh, self.dh_src = [], []
        self.obj = []
        self.objects, self.tex, self.files = {}, {}, []
        self.notes = []

    def src(self, name):
        self.sources.append(name)
        return len(self.sources) - 1

    def add(self, table, hashes, src):
        hashes = np.atleast_1d(np.asarray(hashes, np.uint64))
        if len(hashes):
            self.t[table][0].append(hashes)
            self.t[table][1].append(np.full(len(hashes), src, np.uint32))


def index_file(job):
    path, name, want_geom = job
    c = Collect()
    data = Path(path).read_bytes()
    c.files.append([name, len(data), f"{h64(data):016x}"])
    if not name.lower().endswith(ARCHIVE_EXT):
        s = c.src(name)
        c.add("whole", h64(data), s)
        c.add("b256", block_hashes(data, COARSE), s)
        return c
    try:
        arc = XM.Archive(path)
    except (XM.MediaError, struct.error) as e:
        c.notes.append(f"{name}: {e}; indexed as a plain file")
        s = c.src(name)
        c.add("whole", h64(data), s)
        c.add("b32", block_hashes(data, FINE), s)
        return c
    rel = arc.release
    c.files[-1].append(rel)
    ids = XM.Ids()
    archive = ids.archive(arc.name)
    schemas = Schemas()
    c.add("whole", h64(data), c.src(f"{rel}: the archive {name}"))
    for ent in arc.entries:
        raw = arc.pack_bytes(ent)
        base_id = f"base://{archive}/{ent['name']}"
        ps = c.src(f"{rel}: {base_id}")
        c.add("whole", h64(raw), ps)
        c.add("b32", block_hashes(raw, FINE), ps)
        try:
            pk = XM.RawPack(rel, ent["type"], raw, ent["name"])
        except (XM.MediaError, struct.error) as e:
            c.notes.append(f"{name} {ent['name']}: {e}; raw blocks only")
            continue
        try:
            for t in XM.scan_textures(pk):
                tid = ids.asset(rel, archive, ent["name"], "texture", t["i"])
                ts = c.src(f"{rel}: {tid}")
                clut = pk.b[t["clut"]:t["clut"] + 0x400] if "clut" in t else None
                c.tex.setdefault(tid, {})[rel] = {"x": XM.texture_fingerprint(pk, t), "w": t["w"], "h": t["h"],
                                                  "i": t["i"]}
                for k, (o, n, lw, lh) in enumerate(t["levels"]):
                    img = XM.decode_level(rel, t["fmt"], pk.b[o:o + n], lw, lh, clut)
                    a, b = pixel_hashes(img)
                    c.add("px", a, ts)
                    c.add("rgb", b, ts)
                    if k == 0:
                        d = dhash(img)
                        if d is not None:
                            c.dh.append(d)
                            c.dh_src.append(ts)
        except (XM.MediaError, struct.error, ValueError, IndexError) as e:
            c.notes.append(f"{name} {ent['name']}: textures: {e}")
        try:
            samples = [(ids.asset(rel, archive, ent["name"], "sound", s["i"]), pk.b[s["ptr"]:s["ptr"] + s["len"]])
                       for s in XM.scan_sounds(pk)]
            song, bank = XM.scan_song(pk)
            if song:
                o, n = song["smf"]
                c.add("whole", h64(pk.b[o:o + n]), c.src(f"{rel}: {base_id}/song/0"))
            if bank:
                done = set()
                for en in bank["entries"]:
                    if (en["off"], en["len"]) not in done:
                        done.add((en["off"], en["len"]))
                        p = bank["data"] + en["off"]
                        samples.append((ids.asset(rel, archive, ent["name"], "bank", en["k"]), pk.b[p:p + en["len"]]))
            for sid, sraw in samples:
                pcm = XM.decode_sample(rel, sraw, sid).astype("<i2").tobytes()
                ss = c.src(f"{rel}: {sid}")
                c.add("snd", h64(pcm), ss)
                c.add("b32", block_hashes(pcm, FINE), ss)
        except (XM.MediaError, struct.error, ValueError, IndexError) as e:
            c.notes.append(f"{name} {ent['name']}: sounds: {e}")
        blob = XG.Blob(pk.section("data"), set(pk.words("pointers")), rel)
        try:
            objs, keys = _objects(pk, blob, schemas, base_id)
            for oid, sname in objs.items():
                c.objects.setdefault(oid, {})[rel] = sname
            c.obj += keys
        except (struct.error, IndexError, KeyError) as e:
            c.notes.append(f"{name} {ent['name']}: objects: {e}")
        if want_geom:
            for gi, off in enumerate(XG.scan(blob)):
                try:
                    pos = _positions(blob, off)
                except (XG.GeomError, struct.error, IndexError, ValueError, Exception) as e:  # noqa: B014
                    c.notes.append(f"{name} {ent['name']} geometry {gi}: {e}")
                    continue
                if pos:
                    h = np.unique(vertex_hashes(np.concatenate(pos)))
                    c.add("vtx", h, c.src(f"{rel}: {base_id}/geometry/{gi}"))
    return c


def cmd_index(a):
    t0 = time.time()
    only = {s.lower() for s in a.archives.split(",")} if a.archives else None
    jobs = []
    for d in a.data:
        d = Path(d)
        if not d.is_dir():
            raise ModError(f"{d}: not a folder")
        for p in sorted(d.rglob("*")):
            if not p.is_file() or p.name.startswith("."):
                continue
            if only is not None and (not p.name.lower().endswith(ARCHIVE_EXT) or p.stem.lower() not in only):
                continue
            jobs.append((str(p), str(p.relative_to(d)).replace("\\", "/"), not a.no_geometry))
    if not jobs:
        raise ModError("no game files found in " + ", ".join(map(str, a.data)))
    jobs.sort(key=lambda j: -Path(j[0]).stat().st_size)
    with multiprocessing.Pool(a.jobs) as pool:
        parts = pool.map(index_file, jobs, chunksize=1)
    sources, tabs = [], {}
    dh, dh_src, obj, notes = [], [], [], []
    meta = {"tool": TOOL, "tool_version": TOOL_VERSION, "format_version": FORMAT_VERSION,
            "partial": bool(only) or a.no_geometry, "geometry": not a.no_geometry, "files": [],
            "objects": {}, "textures": {}}
    for c in parts:
        base = len(sources)
        sources += c.sources
        for k, (hs, ss) in c.t.items():
            for h, s in zip(hs, ss):
                tabs.setdefault(k, ([], []))
                tabs[k][0].append(h)
                tabs[k][1].append(s + np.uint32(base))
        dh += c.dh
        dh_src += [s + base for s in c.dh_src]
        obj += c.obj
        notes += c.notes
        meta["files"] += c.files
        for k, v in c.objects.items():
            meta["objects"].setdefault(k, {}).update(v)
        for k, v in c.tex.items():
            meta["textures"].setdefault(k, {}).update(v)
    meta["files"].sort()
    meta["releases"] = sorted({f[3] for f in meta["files"] if len(f) > 3})
    meta["id"] = hashlib.blake2b(json.dumps(meta["files"]).encode(), digest_size=8).hexdigest()
    meta["sources"] = sources
    out = {}
    for k in ("b32", "b256", "whole", "px", "rgb", "snd", "vtx"):
        hs, ss = tabs.get(k, ([], []))
        h = np.concatenate(hs) if hs else np.empty(0, np.uint64)
        s = np.concatenate(ss) if ss else np.empty(0, np.uint32)
        h, first = np.unique(h, return_index=True)       # sorted; the first source of each
        out[k], out[k + "_src"] = h, s[first]
    out["dh"] = np.array(dh, np.uint64).reshape(-1, 4)
    out["dh_src"] = np.array(dh_src, np.uint32)
    out["obj"] = np.unique(np.array(obj, np.uint64))
    out["meta"] = np.frombuffer(json.dumps(meta).encode(), np.uint8)
    path = Path(a.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        np.savez(f, **out)
    for n in notes[:20]:
        print("note:", n)
    if len(notes) > 20:
        print(f"note: ... and {len(notes) - 20} more")
    print(f"{path}: {len(meta['files'])} files of {', '.join(meta['releases']) or 'no release'}"
          f"{' (PARTIAL)' if meta['partial'] else ''}: {len(out['b32'])} + {len(out['b256'])} blocks, "
          f"{len(out['px'])} texture levels, {len(out['dh'])} texture looks, {len(out['snd'])} samples, "
          f"{len(out['vtx'])} vertices, {len(meta['objects'])} objects; "
          f"{path.stat().st_size / 1e6:.1f} MB, {time.time() - t0:.0f} s")
    return 0


class Index:
    def __init__(self, path):
        try:
            z = np.load(path)
            self.a = {k: z[k] for k in z.files}
            self.meta = json.loads(self.a.pop("meta").tobytes())
        except (OSError, ValueError, KeyError) as e:
            raise ModError(f"{path}: not an index of xdl_mod.py index ({e})")
        if self.meta.get("tool_version") != TOOL_VERSION:
            raise ModError(f"{path}: made by another version of {TOOL}; run index again")
        self.sources = self.meta["sources"]
        self.dh_bytes = np.ascontiguousarray(self.a["dh"]).view(np.uint8).reshape(-1, 32)

    def find(self, table, hashes):
        """(positions in hashes that are in the table, their sources)."""
        t = self.a[table]
        hashes = np.atleast_1d(np.asarray(hashes, np.uint64))
        if len(t) == 0 or len(hashes) == 0:
            return np.empty(0, np.int64), []
        i = np.minimum(np.searchsorted(t, hashes), len(t) - 1)
        hit = np.nonzero(t[i] == hashes)[0]
        if table + "_src" not in self.a:
            return hit, []
        return hit, [self.sources[s] for s in self.a[table + "_src"][i[hit]]]

    def one(self, table, h):
        hit, src = self.find(table, [h])
        return src[0] if len(hit) else None

    def near(self, d):
        """The base texture a gradient hash is closest to: (bits differing, source)."""
        if d is None or len(self.dh_bytes) == 0:
            return None
        x = np.bitwise_xor(self.dh_bytes, d.view(np.uint8))
        bits = np.unpackbits(x, axis=1).sum(axis=1)
        i = int(bits.argmin())
        return int(bits[i]), self.sources[self.a["dh_src"][i]]


# ------------------------------------------------------------------------------------------
# the manifest

def recipe_matrix(ops):
    """A recipe's ops as one 3x4 colour matrix (rows r, g, b; columns r, g, b, 1), in order.
    The port composes the same way (Port_Mods.c)."""
    m = np.eye(4)
    for op in ops:
        k = op["op"]
        n = np.eye(4)
        if k == "hue_shift":
            a = np.deg2rad(op["degrees"])
            c, s = np.cos(a), np.sin(a)
            r = np.sqrt(1 / 3)
            n[:3, :3] = c * np.eye(3) + (1 - c) / 3 * np.ones((3, 3)) + s * r * np.array(
                [[0, -1, 1], [1, 0, -1], [-1, 1, 0]])
        elif k == "tint":
            n[:3, :3] = np.diag(op["color"])
        elif k == "saturation":
            n[:3, :3] = op["amount"] * np.eye(3) + (1 - op["amount"]) / 3 * np.ones((3, 3))
        elif k == "brightness":
            n[:3, :3] = op["amount"] * np.eye(3)
        elif k == "matrix":
            n[:3, :] = np.array(op["rows"], float)
        m = n @ m
    return m[:3]


class Manifest:
    """mod.jsonc, checked. Errors name the member."""

    def __init__(self, folder):
        self.dir = Path(folder)
        p = self.dir / "mod.jsonc"
        if not p.is_file():
            raise ModError(f"{self.dir}: no mod.jsonc")
        try:
            self.doc = XS.jsonc_loads(p.read_text())
        except (ValueError, XS.XdlError) as e:
            raise ModError(f"{p}: {e}")
        self.errors = []
        self.patches = []       # (where, base object id or None, entry (kind, uid) or None, object path, dict)
        self.recipes = []       # (where, base texture id, ops)
        self.refs = set()       # every base:// id the mod names
        self._check()

    def err(self, where, msg):
        self.errors.append(f"mod.jsonc: {where}: {msg}")

    def _str(self, d, k, where, required=False, pattern=None):
        v = d.get(k)
        if v is None:
            if required:
                self.err(where, f'"{k}" is missing')
            return None
        if not isinstance(v, str) or (pattern is not None and not pattern.match(v)):
            self.err(f"{where}.{k}", "isn't a string" if not isinstance(v, str) else f"{v!r} isn't the form expected")
            return None
        return v

    def _ops(self, ops, where):
        if not isinstance(ops, list) or not ops:
            return self.err(where, "needs a list of operations")
        for i, op in enumerate(ops):
            w = f"{where}[{i}]"
            if not isinstance(op, dict) or op.get("op") not in OPS:
                self.err(w, f'"op" is one of {", ".join(sorted(OPS))}')
                continue
            if set(op) - {"op"} != OPS[op["op"]]:
                self.err(w, f'{op["op"]} takes {", ".join(sorted(OPS[op["op"]]))}')
                continue
            num = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)  # noqa: E731
            v = op[next(iter(OPS[op["op"]]))]
            if op["op"] == "tint":
                ok = isinstance(v, list) and len(v) == 3 and all(num(x) for x in v)
            elif op["op"] == "matrix":
                ok = (isinstance(v, list) and len(v) == 3 and
                      all(isinstance(r, list) and len(r) == 4 and all(num(x) for x in r) for r in v))
            else:
                ok = num(v)
            if not ok:
                self.err(w, "numbers of the wrong shape")
        return None

    def _patch(self, patch, where, base_obj, entry, path):
        if not isinstance(patch, dict) or not patch:
            return self.err(where, "needs an object of field: value")
        self.patches.append((where, base_obj, entry, path, patch))
        return None

    def _check(self):
        d = self.doc
        if not isinstance(d, dict):
            return self.err("the file", "isn't an object")
        known = {"$schema", "format_version", "id", "name", "version", "author", "description", "releases",
                 "after", "cars", "items", "tracks", "patches", "recipes"}
        for k in d:
            if k not in known:
                self.err(k, "isn't a member of a mod (schemas/Mod.schema.json)")
        if d.get("format_version") != FORMAT_VERSION:
            self.err("format_version", f"must be {FORMAT_VERSION}")
        self.id = self._str(d, "id", "the mod", True, UID_RE)
        self.name = self._str(d, "name", "the mod", True)
        self._str(d, "version", "the mod", True)
        self._str(d, "author", "the mod")
        self._str(d, "description", "the mod")
        rel = d.get("releases", list(RELEASES))
        if not isinstance(rel, list) or not rel or any(r not in RELEASES for r in rel):
            self.err("releases", f"a list of {', '.join(RELEASES)}")
            rel = list(RELEASES)
        self.releases = rel
        after = d.get("after", [])
        if not isinstance(after, list) or any(not isinstance(x, str) or not UID_RE.match(x) for x in after):
            self.err("after", "a list of mod ids (uid://...)")
        uids = {self.id}
        for kind, (regex, archive, pack, form) in KINDS.items():
            sec = d.get(kind, {})
            if not isinstance(sec, dict):
                self.err(kind, "isn't an object")
                continue
            for uid, e in sec.items():
                w = f"{kind}.{uid}"
                if not UID_RE.match(uid):
                    self.err(w, "the key is the entry's id: uid:// and up to 13 of 0-9 a-z")
                if uid in uids:
                    self.err(w, "the id is used twice")
                uids.add(uid)
                if not isinstance(e, dict):
                    self.err(w, "isn't an object")
                    continue
                for k in e:
                    if k not in ("extends", "name", "thumbnail", "unlocked", "patch", "recipes"):
                        self.err(f"{w}.{k}", "isn't a member of an entry")
                ext = self._str(e, "extends", w, True, regex)
                if "extends" in e and ext is None:
                    self.err(f"{w}.extends", f"an entry extends a base one, {form} (data of its own isn't "
                                             "loaded by the port yet)")
                self._str(e, "name", w)
                th = self._str(e, "thumbnail", w, False, TEX_RE)
                if th:
                    self.refs.add(th)
                if "unlocked" in e and not isinstance(e["unlocked"], bool):
                    self.err(f"{w}.unlocked", "true or false")
                base_pack = None
                if ext:
                    self.refs.add(ext)
                    base_pack = f"base://{archive}/{pack.format(regex.match(ext).group(1))}"
                for path, patch in (e.get("patch") or {}).items() if isinstance(e.get("patch", {}), dict) else []:
                    if not re.match(r"^[A-Za-z0-9_]+/[0-9]+(/[A-Za-z0-9_]+)*$", path):
                        self.err(f"{w}.patch.{path}", "an object of the entry's pack: CarAInfo/0/engine")
                        continue
                    self._patch(patch, f"{w}.patch.{path}", f"{base_pack}/{path}" if base_pack else None,
                                (kind, uid), path)
                for tex, ops in (e.get("recipes") or {}).items() if isinstance(e.get("recipes", {}), dict) else []:
                    if not re.match(r"^texture/[0-9]+$", tex):
                        self.err(f"{w}.recipes.{tex}", "a texture of the entry's pack: texture/3")
                        continue
                    self._ops(ops, f"{w}.recipes.{tex}")
                    if base_pack:
                        self.recipes.append((f"{w}.recipes.{tex}", f"{base_pack}/{tex}", ops))
                for k in ("patch", "recipes"):
                    if not isinstance(e.get(k, {}), dict):
                        self.err(f"{w}.{k}", "isn't an object")
        # tracks: a level of the base track's mode, its world the base's or one the MCP names
        sec = d.get("tracks", {})
        if not isinstance(sec, dict):
            self.err("tracks", "isn't an object")
            sec = {}
        for uid, e in sec.items():
            w = f"tracks.{uid}"
            if not UID_RE.match(uid):
                self.err(w, "the key is the entry's id: uid:// and up to 13 of 0-9 a-z")
            if uid in uids:
                self.err(w, "the id is used twice")
            uids.add(uid)
            if not isinstance(e, dict):
                self.err(w, "isn't an object")
                continue
            for k in e:
                if k not in ("extends", "name", "thumbnail", "unlocked", "world"):
                    self.err(f"{w}.{k}", "isn't a member of a track (extends, name, thumbnail, unlocked, world)")
            ext = self._str(e, "extends", w, True, TRACK_RE)
            if ext:
                self.refs.add(ext)
            self._str(e, "name", w)
            self._str(e, "world", w)
            th = self._str(e, "thumbnail", w, False, TEX_RE)
            if th:
                self.refs.add(th)
            if "unlocked" in e and not isinstance(e["unlocked"], bool):
                self.err(f"{w}.unlocked", "true or false")
        for target, patch in (d.get("patches") or {}).items() if isinstance(d.get("patches", {}), dict) else []:
            m = OBJ_RE.match(target)
            if not m or TEX_RE.match(target):
                self.err(f"patches.{target}", "an object's id: base://HW_Cars/car_5/CarAInfo/0/engine")
                continue
            self.refs.add(target)
            self._patch(patch, f"patches.{target}", target, None, m.group(3))
        for target, ops in (d.get("recipes") or {}).items() if isinstance(d.get("recipes", {}), dict) else []:
            if not TEX_RE.match(target):
                self.err(f"recipes.{target}", "a texture's id: base://HW_Cars/car_5/texture/3")
                continue
            self.refs.add(target)
            self._ops(ops, f"recipes.{target}")
            self.recipes.append((f"recipes.{target}", target, ops))
        for k in ("patches", "recipes"):
            if not isinstance(d.get(k, {}), dict):
                self.err(k, "isn't an object")
        return None


# ------------------------------------------------------------------------------------------
# the check

class Report:
    def __init__(self):
        self.refused, self.notes = [], []

    def refuse(self, where, why, use=None):
        self.refused.append((where, why, use))

    def show(self):
        for n in self.notes:
            print("note:", n)
        for where, why, use in self.refused:
            print(f"REFUSED {where}: {why}" + (f"\n        use {use} instead" if use else ""))


def _asset(src):
    """'gc: base://...' -> the id to reference."""
    return src.split(": ", 1)[1] if ": base://" in src else None


def check_bytes(index, rep, where, data, what="holds"):
    """Whole-file and raw-run checks of one byte string."""
    src = index.one("whole", h64(data))
    if src:
        rep.refuse(where, f"is the game's own file ({src})", _asset(src))
        return True
    found = False
    for table, B in (("b32", FINE), ("b256", COARSE)):
        hit, srcs = index.find(table, window_hashes(data, B))
        if len(hit):
            count = {}
            for s in srcs:
                count[s] = count.get(s, 0) + 1
            top = sorted(count.items(), key=lambda kv: -kv[1])
            n = len(hit)
            rep.refuse(where, f"{what} game data: {n} run{'s' if n > 1 else ''} of {B} bytes or more, first at "
                              f"offset 0x{int(hit[0]):X}, from {top[0][0]}"
                              + (f" and {len(top) - 1} more" if len(top) > 1 else ""), _asset(top[0][0]))
            found = True
            break
    return found


HEX_RE = re.compile(r"^(?:[0-9a-fA-F]{2}[\s]*){32,}$")
B64_RE = re.compile(r"^[A-Za-z0-9+/\s]{44,}={0,2}$")


def json_blobs(node, path=""):
    """(path, bytes) of every string in a JSON tree that holds bytes: hex, base64, a data URI."""
    if isinstance(node, dict):
        for k, v in node.items():
            yield from json_blobs(v, f"{path}.{k}" if path else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from json_blobs(v, f"{path}[{i}]")
    elif isinstance(node, str):
        s = node
        if s.startswith("data:") and ";base64," in s[:200]:
            s = s.split(";base64,", 1)[1]
            try:
                yield path, base64.b64decode(s)
            except (binascii.Error, ValueError):
                pass
        elif HEX_RE.match(s):
            yield path, bytes.fromhex("".join(s.split()))
        elif B64_RE.match(s):
            try:
                yield path, base64.b64decode(s, validate=False)
            except (binascii.Error, ValueError):
                pass


def gltf_positions(path, data, folder):
    """Every POSITION accessor's f32 x 3 of a .gltf / .glb (buffers beside it or embedded)."""
    bins = {}
    if path.suffix.lower() == ".glb":
        if data[:4] != b"glTF" or len(data) < 20:
            raise ModError("not a GLB file")
        n = struct.unpack_from("<I", data, 12)[0]
        gl = json.loads(data[20:20 + n])
        if len(data) > 28 + n:
            m = struct.unpack_from("<I", data, 20 + n)[0]
            bins[0] = data[28 + n:28 + n + m]
    else:
        gl = json.loads(data)
    for i, b in enumerate(gl.get("buffers", [])):
        uri = b.get("uri")
        if uri is None:
            continue
        if uri.startswith("data:"):
            bins[i] = base64.b64decode(uri.split(",", 1)[1])
        else:
            p = (path.parent / uri).resolve()
            if folder.resolve() not in p.parents or not p.is_file():
                raise ModError(f"buffer {uri!r} isn't a file of the mod")
            bins[i] = p.read_bytes()
    out = []
    for mesh in gl.get("meshes", []):
        for pr in mesh.get("primitives", []):
            ai = pr.get("attributes", {}).get("POSITION")
            if ai is None:
                continue
            a = gl["accessors"][ai]
            if a.get("componentType") != 5126 or a.get("type") != "VEC3" or "bufferView" not in a:
                continue
            bv = gl["bufferViews"][a["bufferView"]]
            buf = bins.get(bv.get("buffer", 0), b"")
            o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            stride = bv.get("byteStride", 12)
            raw = b"".join(buf[o + k * stride:o + k * stride + 12] for k in range(a["count"])) if stride != 12 \
                else buf[o:o + 12 * a["count"]]
            if len(raw) == 12 * a["count"]:
                out.append(np.frombuffer(raw, "<f4").reshape(-1, 3))
    return out


def check_mod(folder, index, strip=False):
    """-> (Manifest, Report, files {relative path: bytes} as the package gets them)."""
    folder = Path(folder)
    man = Manifest(folder)
    rep = Report()
    for e in man.errors:
        rep.refuse("mod.jsonc", e[len("mod.jsonc: "):])
    meta = index.meta
    schemas = Schemas()
    have = set(meta["releases"])

    # what the mod refers to must be there, on a release the index has
    for where, tid, ops in man.recipes:
        if tid not in meta["textures"]:
            rep.refuse(f"mod.jsonc: {where}", f"no texture {tid} in your game data ({', '.join(sorted(have))})")
    for uid_kind in ("cars", "items", "tracks"):
        for uid, e in (man.doc.get(uid_kind) or {}).items() if isinstance(man.doc.get(uid_kind, {}), dict) else []:
            th = e.get("thumbnail") if isinstance(e, dict) else None
            if isinstance(th, str) and TEX_RE.match(th) and th not in meta["textures"]:
                rep.refuse(f"mod.jsonc: {uid_kind}.{uid}.thumbnail", f"no texture {th} in your game data")

    # patches: fields that exist, values of their type, and none that restates the base's
    stripped = 0
    doc = json.loads(json.dumps(man.doc))
    for where, base_obj, entry, path, patch in man.patches:
        if base_obj is None:
            continue
        structs = meta["objects"].get(base_obj)
        if structs is None:
            near = sorted(k for k in meta["objects"] if k.startswith(base_obj.rsplit("/", 1)[0]))[:6]
            rep.refuse(f"mod.jsonc: {where}", f"no object {base_obj} in your game data"
                       + (f" (there: {', '.join(n.rsplit('/', 1)[1] for n in near)})" if near else "")
                       + ("" if any(o.startswith(base_obj.split("/")[0] + "//" + base_obj.split("/")[2]) for o in
                                    meta["objects"]) else "; is its archive in the index?"))
            continue
        sname = next(iter(structs.values()))
        fields = schemas.fields(sname)
        for f, v in list(patch.items()):
            w = f"mod.jsonc: {where}.{f}"
            if f not in fields:
                rep.refuse(w, f"{sname} has no field {f}")
                continue
            off, prim, count, target = fields[f]
            if prim is None and f in schemas.counts(sname):
                rep.refuse(w, f"{sname}.{f} counts an array's entries: the game would read past the array")
                continue
            if prim is None:
                rep.refuse(w, f"{sname}.{f} is a link or a nested struct: a patch sets numbers")
                continue
            try:
                raw = canon(prim, count, v)
            except ModError as e:
                rep.refuse(w, str(e))
                continue
            if len(index.find("obj", [field_key(base_obj, f, raw)])[0]):
                if strip:
                    stripped += 1
                    node = doc[entry[0]][entry[1]]["patch"][path] if entry else doc["patches"][base_obj]
                    del node[f]
                else:
                    rep.refuse(w, f"restates the game's own value of {base_obj} {f} (a copied record); leave "
                                  "out what the mod doesn't change, or pack with --strip-unchanged")
    if stripped:
        rep.notes.append(f"{stripped} patch field(s) equal to the game's own left out of the package")

    # the files
    files = {}
    for p in sorted(folder.rglob("*")):
        if p.is_dir():
            continue
        rel = str(p.relative_to(folder)).replace("\\", "/")
        if any(part.startswith(".") for part in p.relative_to(folder).parts):
            rep.notes.append(f"{rel}: hidden, left out")
            continue
        if p.is_symlink():
            rep.refuse(rel, "is a link; a mod holds its own files")
            continue
        ext = p.suffix.lower()
        if ext not in ALLOWED_EXT:
            rep.refuse(rel, ("is a game archive or cache" if ext in ARCHIVE_EXT + (".xdl",) else
                             f"a {ext or 'file without extension'} can't be checked") +
                       f"; a mod holds {' '.join(ALLOWED_EXT)}")
            continue
        data = p.read_bytes()
        files[rel] = data
        if check_bytes(index, rep, rel, data):
            continue
        try:
            if ext in (".json", ".jsonc", ".gltf"):
                tree = XS.jsonc_loads(data.decode("utf-8")) if ext != ".gltf" else json.loads(data)
                for jp, blob in json_blobs(tree):
                    check_bytes(index, rep, f"{rel}: {jp}", blob)
            if ext == ".png":
                img, _ = XM.read_png(p)
                a, b = pixel_hashes(img)
                src = index.one("px", a) or index.one("rgb", b)
                if src:
                    rep.refuse(rel, f"is the game's texture ({src})", f'a recipe on "{_asset(src)}"')
                else:
                    near = index.near(dhash(img))
                    if near and near[0] <= DH_NEAR:
                        rep.refuse(rel, f"looks like the game's texture ({near[1]}; {near[0]} of 256 gradient "
                                        "bits differ): an edited game texture is still the game's",
                                   f'a recipe on "{_asset(near[1])}"')
                check_bytes(index, rep, f"{rel}: pixels", img.tobytes(), "the pixels hold")
            elif ext == ".wav":
                import wave
                with wave.open(io.BytesIO(data), "rb") as w:
                    if w.getsampwidth() != 2:
                        rep.refuse(rel, "needs 16-bit PCM to be checked")
                        continue
                    pcm = np.frombuffer(w.readframes(w.getnframes()), "<i2")
                    chans = [pcm[c::w.getnchannels()] for c in range(w.getnchannels())]
                for ci, ch in enumerate(chans):
                    raw = np.ascontiguousarray(ch).tobytes()
                    src = index.one("snd", h64(raw))
                    if src:
                        rep.refuse(rel, f"is the game's sound ({src})", _asset(src))
                    elif len(chans) > 1:
                        check_bytes(index, rep, f"{rel}: channel {ci}", raw, "the samples hold")
            if ext in (".gltf", ".glb"):
                if not index.meta["geometry"]:
                    rep.refuse(rel, "the index was made without geometry: meshes can't be checked")
                    continue
                pos = gltf_positions(p, data, folder)
                if pos:
                    h = np.unique(vertex_hashes(np.concatenate(pos)))
                    hit, srcs = index.find("vtx", h)
                    if len(hit) >= VTX_MIN and len(hit) >= VTX_SHARE * len(h):
                        count = {}
                        for s in srcs:
                            count[s] = count.get(s, 0) + 1
                        top = max(count.items(), key=lambda kv: kv[1])
                        rep.refuse(rel, f"{len(hit)} of its {len(h)} vertex positions are the game's "
                                        f"(most from {top[0]})", _asset(top[0]))
        except (ModError, XM.MediaError, XS.XdlError, ValueError, KeyError, IndexError, OSError, EOFError,
                struct.error) as e:
            rep.refuse(rel, f"can't be read to be checked: {e}")
    if stripped:
        files["mod.jsonc"] = (json.dumps(doc, indent=2) + "\n").encode()
    return man, rep, files


def fingerprints(man, index):
    """{base id: {release: hash}} of the textures the mod refers to: the index's releases, and the
    pairing table's when it is found (docs/xdl-format/pairing)."""
    out = {}
    pairing = XS.find_up("docs/xdl-format/pairing")
    ids = sorted({t for _, t, _ in man.recipes} | {r for r in man.refs if TEX_RE.match(r)})
    table = {}
    if pairing is not None:
        for arch in {TEX_RE.match(t).group(1).lower() for t in ids}:
            for p in pairing.glob("*.jsonl"):
                if p.stem.lower() == arch:
                    for ln in p.read_text().splitlines():
                        r = json.loads(ln)
                        if r.get("id") in ids:
                            table[r["id"]] = {k: r[k]["x"] for k in RELEASES if k in r and "x" in r[k]}
    for t in ids:
        fp = dict(table.get(t, {}))
        fp.update({rel: v["x"] for rel, v in index.meta["textures"].get(t, {}).items()})
        if fp:
            out[t] = fp
    return out


def write_package(path, files, check):
    names = sorted(files)
    body = dict(files)
    body["check.json"] = (json.dumps(check, indent=2, sort_keys=True) + "\n").encode()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for n in ["check.json"] + [n for n in names if n != "check.json"]:
            zi = zipfile.ZipInfo(n, (1980, 1, 1, 0, 0, 0))
            zi.external_attr = 0o644 << 16
            zi.compress_type = zipfile.ZIP_STORED
            z.writestr(zi, body[n])


def cmd_check(a, pack=False):
    index = Index(a.index)
    man, rep, files = check_mod(a.mod, index, getattr(a, "strip_unchanged", False))
    if index.meta["partial"]:
        msg = "the index doesn't cover the whole game (made with --archives or --no-geometry)"
        if pack and not a.allow_partial_index:
            rep.refuse("the index", msg + "; make a whole one, or pass --allow-partial-index (recorded)")
        else:
            rep.notes.append(msg)
    if "check.json" in files:
        rep.refuse("check.json", "is written by pack; a mod's sources don't have one")
    rep.show()
    if rep.refused:
        print(f"{a.mod}: {len(rep.refused)} problem(s): not packed" if pack else
              f"{a.mod}: {len(rep.refused)} problem(s)")
        return 1
    if not pack:
        print(f"{a.mod}: {len(files)} files, no game data found")
        return 0
    check = {"tool": TOOL, "tool_version": TOOL_VERSION, "format_version": FORMAT_VERSION,
             "mod": man.id, "result": "no game data found",
             "index": {"id": index.meta["id"], "releases": index.meta["releases"],
                       "files": len(index.meta["files"]), "partial": index.meta["partial"]},
             "files": {n: hashlib.blake2b(d, digest_size=16).hexdigest() for n, d in sorted(files.items())},
             "fingerprints": fingerprints(man, index)}
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    write_package(out, files, check)
    print(f"{out}: {man.name} ({man.id}), {len(files)} files, {out.stat().st_size} bytes, no game data found")
    return 0


def read_package(path):
    try:
        with zipfile.ZipFile(path) as z:
            return {i.filename: z.read(i) for i in z.infolist()}
    except (zipfile.BadZipFile, OSError) as e:
        raise ModError(f"{path}: not a mod package ({e})")


def cmd_info(a):
    p = Path(a.mod)
    if p.is_dir():
        man = Manifest(p)
        check = None
    else:
        files = read_package(p)
        if "mod.jsonc" not in files:
            raise ModError(f"{p}: no mod.jsonc in it")
        check = json.loads(files.get("check.json", b"null"))
        doc = XS.jsonc_loads(files["mod.jsonc"].decode())
        print(f"{p}: {len(files)} files")
        man = type("M", (), {"doc": doc, "errors": []})()
    d = man.doc
    print(f"{d.get('name')} {d.get('version')} ({d.get('id')}) by {d.get('author', '?')}")
    for uid, e in (d.get("tracks") or {}).items():
        print(f"  track {uid}: {e.get('name', '(the base name)')}, extends {e.get('extends')}"
              f"{', world ' + e['world'] if e.get('world') else ''}")
    for k in ("cars", "items"):
        for uid, e in (d.get(k) or {}).items():
            print(f"  {k[:-1]} {uid}: {e.get('name', '(the base name)')}, extends {e.get('extends')}, "
                  f"{len(e.get('patch') or {})} patched objects, {len(e.get('recipes') or {})} recipes")
    print(f"  {len(d.get('patches') or {})} patched base objects, {len(d.get('recipes') or {})} base recipes")
    if check:
        print(f"  checked by {check['tool']} v{check['tool_version']} against {check['index']['files']} files of "
              f"{', '.join(check['index']['releases'])}{' (partial index)' if check['index']['partial'] else ''}: "
              f"{check['result']}")
    elif not p.is_dir():
        print("  NOT CHECKED: no check.json")
    for e in man.errors:
        print("  error:", e)
    return 1 if man.errors else 0


def cmd_install(a):
    files = read_package(a.mod)
    if "mod.jsonc" not in files:
        raise ModError(f"{a.mod}: no mod.jsonc in it")
    doc = XS.jsonc_loads(files["mod.jsonc"].decode())
    uid = str(doc.get("id", ""))
    if not UID_RE.match(uid):
        raise ModError(f"{a.mod}: its id isn't a uid://")
    out = Path(a.mods) / uid[6:]
    for n, d in files.items():
        p = (out / n).resolve()
        if out.resolve() not in p.parents:
            raise ModError(f"{a.mod}: {n!r} leaves the mod's folder")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(d)
    print(f"{out}: {len(files)} files")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("index")
    p.add_argument("data", nargs="+")
    p.add_argument("-o", "--out", default=str(ROOT / "build" / "mod-index.npz"))
    p.add_argument("--archives", help="only these archives (a partial index: for tests)")
    p.add_argument("--no-geometry", action="store_true")
    p.add_argument("-j", "--jobs", type=int, default=4)
    for name in ("check", "pack"):
        p = sub.add_parser(name)
        p.add_argument("mod")
        p.add_argument("--index", required=True)
        p.add_argument("--strip-unchanged", action="store_true")
        if name == "pack":
            p.add_argument("-o", "--out", required=True)
            p.add_argument("--allow-partial-index", action="store_true")
    p = sub.add_parser("info")
    p.add_argument("mod")
    p = sub.add_parser("install")
    p.add_argument("mod")
    p.add_argument("mods")
    a = ap.parse_args()
    try:
        if a.cmd == "index":
            return cmd_index(a)
        if a.cmd in ("check", "pack"):
            return cmd_check(a, a.cmd == "pack")
        if a.cmd == "info":
            return cmd_info(a)
        return cmd_install(a)
    except ModError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
