#!/usr/bin/env python3
"""Reusable components for .xdl source projects (README §13, components.md; step 4a).

    xdl_components.py factor PROJECT_ROOT [--lib DIR] [--kinds A,B] [--min-uses 2] [--near N]
    xdl_components.py inline PACK_PROJECT_DIR            instances back to plain files
    xdl_components.py roundtrip SURVEY_ROOT [--rel gc] [--archive HW_Cars] [--out DIR] [--near N]
    xdl_components.py stats LIBRARY_DIR

A **component** is an object subgraph kept once, in a library beside the pack projects, and
used by any number of packs. A pack that uses one holds an **instance**: a small file with the
component's uid and the fields this use changes. The compiler (xdl_graph.Builder) copies the
component into every pack that uses it, so compiled packs don't change (README §13).

* **Subgraph.** The nodes of a pack project (xdl_graph.py) whose canonical id runs through one
  root node: the root and everything named below it, the folder xdl_graph writes for it.
  Inside a component the nodes are named from the root: `$` is the root, `$/p10` the node its
  slot 0x10 names, `$/p10/o8` a place inside that node.
* **Library.** `<library>/index.json` maps `uid://<16 hex>` to a file;
  `<library>/<Kind>/<name>.jsonc` holds `uid`, `kind`, `release` and `nodes` (local name ->
  record, the same records xdl_graph writes: `size`, `bytes` or `bin`, `links`, `textures`,
  `sounds`, `refs`, `shaders`; a schematised node has `schema`, `doc`, `base`). A link is `$...`
  (inside the component), a `base://` id of a share pack, or `"@"`: a link into the using pack,
  which every instance must bind. A component may instead `extends` another one with
  `overrides`.
* **Instance.** `{"id": <the root's id in the pack>, "component": "uid://...", "overrides": {..}}`
  at the root node's file. `overrides` is keyed by local node name. For a record: `links`,
  `textures`, `sounds`, `refs`, `shaders` (slot -> value, null removes the slot), `patch`
  (offset -> hex bytes written over the component's), or `bytes`/`size` (replace them all).
  For a schematised node: `doc` (JSON Merge Patch, RFC 7396, over the component's JSON) and
  `base` (as for a record). A local name the component doesn't have adds a node; null removes
  one.
* **factor** finds the subgraphs that occur more than once in a project tree (one release),
  moves each to the library and leaves instances. With `--near N` a subgraph that differs from
  a component in at most N values becomes an instance with overrides (or, when it occurs more
  than once itself, a component that extends the other). The packs still build byte for byte:
  the layout in pack.jsonc keeps every node's id and place.

API (for the build tools): `Library.locate(pack_dir, hint)`, `Library.nodes(uid)`,
`expand(library, instance_doc) -> {node id: record}`, `factor(project_root, ...)`,
`inline(pack_dir)`. docs-pending/components.md has the format in full.
"""

import argparse
import collections
import copy
import hashlib
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_graph as GR  # noqa: E402
import xdl_struct as XS  # noqa: E402

FORMAT = 1
# walker labels whose subgraphs become components (components.md: the "yes" and "maybe" kinds,
# and their parts for when the whole doesn't repeat). Placement, mission logic, car bodies and
# keyframe tracks are left out.
KINDS = (
    "EventSeq", "ParticleEmitter", "Particle", "Projectile", "Blast", "Tracer", "AttachData",
    "EmitterData", "LineDef", "RadarBlip", "LightDef", "SlaveTrigger[]",
    "SoundSeq", "Sound", "CarASoundSet*[]",
    "IMHeader", "CarAHud", "Font",
    "ThingRecord", "DestructibleInfo", "AICarScript", "AIStateInfo", "Shader",
    "CarAStuntInfo", "CarAAccel", "CarAGear[]", "CameraSubMode[]", "CarAStartSlave[]",
)
LINKKEYS = ("links", "textures", "sounds", "refs")
SLOTKEYS = LINKKEYS + ("shaders",)
STEP = re.compile(r"^(p[0-9a-f]+)(?:_r(\d+))?$")
BIND = "@"


class CompError(GR.GraphError):
    pass


# ------------------------------------------------------------------------------------------
# files

def load(p):
    """A project file. The files the tools write have comments only as leading lines."""
    t = Path(p).read_text()
    i = 0
    while t.startswith("//", i):
        i = t.index("\n", i) + 1
    try:
        return json.loads(t[i:])
    except ValueError:
        return XS.jsonc_loads(t)


def dump(p, doc, header=()):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("".join(f"// {h}\n" for h in header) + XS.dump_value(doc, 0) + "\n")


# ------------------------------------------------------------------------------------------
# ids: canonical paths as (step, count) lists

def rle(rest):
    """'CarAInfo/0/p10_r3' -> [('CarAInfo', 0), ('0', 0), ('p10', 3)]; count 0: not a p-step."""
    out = []
    for s in rest.split("/"):
        m = STEP.match(s)
        out.append((m.group(1), int(m.group(2) or 1)) if m else (s, 0))
    return out


def unrle(steps):
    return "/".join(s if n <= 1 else f"{s}_r{n}" for s, n in steps)


def join(a, b):
    """Path a followed by the steps b, repeats counted as xdl_graph.add_step counts them."""
    out = list(a)
    for s, n in b:
        if n and out and out[-1][0] == s and out[-1][1]:
            out[-1] = (s, out[-1][1] + n)
        else:
            out.append((s, n))
    return out


def relative(root, node):
    """The steps from root to node, or None when node isn't below root."""
    k = len(root)
    if len(node) < k or node[:k - 1] != root[:k - 1] or node[k - 1][0] != root[k - 1][0]:
        return None
    rn, nn = root[k - 1][1], node[k - 1][1]
    if nn == rn:
        return node[k:]
    if rn and nn > rn:
        return [(root[k - 1][0], nn - rn)] + node[k:]
    return None


def local_name(steps):
    return "$" + "".join("/" + x for x in unrle(steps).split("/") if x)


def split_local(link, names):
    """'$/p10/o8' -> ('$/p10', '/o8'): the longest node name, and the rest."""
    parts = link.split("/")
    for k in range(len(parts), 0, -1):
        head = "/".join(parts[:k])
        if head in names:
            return head, "".join("/" + x for x in parts[k:])
    raise CompError(f"{link}: no such node in the component")


# ------------------------------------------------------------------------------------------
# records

def rec_data(rec):
    if "bin" in rec:
        return Path(rec["bin"]).read_bytes()
    return GR.unhex(rec.get("bytes", []))


def _h(b):
    return hashlib.blake2b(b, digest_size=16).hexdigest()


def canon_raw(rec, shape):
    out = {k: v for k, v in rec.items() if k not in ("bytes", "bin", "align")}
    if shape:
        for k in ("textures", "sounds"):
            if k in out:
                out[k] = {s: "*" for s in out[k]}
    else:
        out["data"] = _h(rec_data(rec))
    return out


def shape_doc(v):
    if isinstance(v, dict):
        return {k: shape_doc(x) for k, x in v.items()}
    if isinstance(v, list):
        return [shape_doc(x) for x in v]
    if isinstance(v, str):
        return v if v == BIND or v.startswith("$") else "*"
    return None if v is None else 0


def canon(rec, shape=False):
    """A record without where its bytes are kept (shape: without its values either)."""
    if "schema" in rec:
        return {"schema": rec["schema"], "doc": shape_doc(rec["doc"]) if shape else rec["doc"],
                "base": canon_raw(rec["base"], shape)}
    return canon_raw(rec, shape)


def digest(nodes, shape=False):
    s = json.dumps({k: canon(r, shape) for k, r in nodes.items()}, sort_keys=True, separators=(",", ":"))
    return hashlib.blake2b(s.encode(), digest_size=16).hexdigest()


def merge_patch(doc, patch):
    """RFC 7396."""
    if not isinstance(patch, dict):
        return copy.deepcopy(patch)
    out = dict(doc) if isinstance(doc, dict) else {}
    for k, v in patch.items():
        if v is None:
            out.pop(k, None)
        else:
            out[k] = merge_patch(out.get(k), v)
    return out


def merge_over(a, b):
    """Two override sets as one, b last."""
    if not (isinstance(a, dict) and isinstance(b, dict)):
        return copy.deepcopy(b)
    out = dict(a)
    for k, v in b.items():
        out[k] = merge_over(out[k], v) if k in out else copy.deepcopy(v)
    return out


def _slot(s):
    return GR.hx(int(s, 16))


def apply_raw(rec, ov, where):
    rec = dict(rec)
    for k, v in ov.items():
        if k in SLOTKEYS:
            d = dict(rec.get(k) or {})
            for s, x in v.items():
                if x is None:
                    d.pop(_slot(s), None)
                else:
                    d[_slot(s)] = x
            if d:
                rec[k] = d
            else:
                rec.pop(k, None)
        elif k == "bytes":
            rec.pop("bin", None)
            rec["bytes"] = v
            rec["size"] = ov.get("size", len(GR.unhex(v)))
        elif k == "patch":
            b = bytearray(rec_data(rec) if "bytes" not in ov else GR.unhex(ov["bytes"]))
            for s, x in v.items():
                o, x = int(s, 16), GR.unhex(x)
                if o + len(x) > len(b):
                    raise CompError(f"{where}: patch at {s} runs past the record's {len(b)} bytes")
                b[o:o + len(x)] = x
            rec.pop("bin", None)
            rec["bytes"] = GR.hex_lines(bytes(b))
        elif k in ("size", "type", "align", "json_size"):
            rec[k] = v
        else:
            raise CompError(f"{where}: unknown override '{k}'")
    return rec


def apply(nodes, overrides, where=""):
    """The component's nodes with an override set applied (a new dict; records are not shared)."""
    out = dict(nodes)
    for loc, ov in (overrides or {}).items():
        w = f"{where}{loc}"
        if ov is None:
            if out.pop(loc, None) is None:
                raise CompError(f"{w}: no such node to remove")
        elif loc not in out:
            if "size" not in ov and "schema" not in ov:
                raise CompError(f"{w}: not a node of the component (a new node needs a whole record)")
            out[loc] = copy.deepcopy(ov)
        elif "schema" in out[loc]:
            rec = dict(out[loc])
            for k, v in ov.items():
                if k == "doc":
                    rec["doc"] = merge_patch(rec["doc"], v)
                elif k == "base":
                    rec["base"] = apply_raw(rec["base"], v, w)
                elif k == "align":
                    rec[k] = v
                else:
                    raise CompError(f"{w}: a {rec['schema']} takes 'doc' and 'base' overrides, not '{k}'")
            out[loc] = rec
        else:
            out[loc] = apply_raw(out[loc], ov, w)
    return out


def map_doc(v, f):
    if isinstance(v, dict):
        return {k: map_doc(x, f) for k, x in v.items()}
    if isinstance(v, list):
        return [map_doc(x, f) for x in v]
    return f(v) if isinstance(v, str) else v


def map_raw(rec, f):
    out = dict(rec)
    for k in LINKKEYS:
        if k in rec:
            out[k] = {s: f(x) for s, x in rec[k].items()}
    return out


def map_links(rec, f):
    if "schema" in rec:
        return {**rec, "doc": map_doc(rec["doc"], f), "base": map_raw(rec["base"], f)}
    return map_raw(rec, f)


# ------------------------------------------------------------------------------------------
# the library

class Library:
    """A folder of components: index.json (uid -> file) and one file per component."""

    _open = {}

    def __init__(self, d):
        self.dir = Path(d)
        f = self.dir / "index.json"
        self.index = json.loads(f.read_text()) if f.exists() else {}
        self._docs, self._nodes = {}, {}

    @classmethod
    def locate(cls, pack_dir, hint=None):
        """The library of a pack project: pack.jsonc's "components" (a path relative to the pack
        folder), or the nearest `components/index.json` above it."""
        pack_dir = Path(pack_dir).resolve()
        if hint:
            d = (pack_dir / hint).resolve()
        else:
            d = next((p / "components" for p in pack_dir.parents if (p / "components" / "index.json").exists()), None)
        if d is None or not (d / "index.json").exists():
            raise CompError(f"no component library for {pack_dir} (components/index.json)")
        if d not in cls._open:
            cls._open[d] = cls(d)
        return cls._open[d]

    def doc(self, uid):
        if uid not in self._docs:
            if uid not in self.index:
                raise CompError(f"{uid}: not in {self.dir}/index.json")
            self._docs[uid] = load(self.dir / self.index[uid])
        return self._docs[uid]

    def nodes(self, uid, _seen=()):
        """{local name: record} of a component, `extends` applied, `bin` paths absolute."""
        if uid in self._nodes:
            return self._nodes[uid]
        if uid in _seen:
            raise CompError(f"{uid}: extends itself")
        d = self.doc(uid)
        where = f"{self.index[uid]}: "
        nodes = {}
        if "extends" in d:
            nodes = self.nodes(d["extends"], _seen + (uid,))
        for loc, rec in (d.get("nodes") or {}).items():
            rec = dict(rec)
            for r in ((rec, rec.get("base")) if "schema" in rec else (rec,)):
                if r is not None and "bin" in r:
                    if r is not rec:
                        r = rec["base"] = dict(r)
                    r["bin"] = str(self.dir / r["bin"])
            nodes = {**nodes, loc: rec}
        nodes = apply(nodes, d.get("overrides"), where)
        if "$" not in nodes:
            raise CompError(f"{where}no root node '$'")
        self._nodes[uid] = nodes
        return nodes

    # -- writing (factor) ----------------------------------------------------------------
    def name_for(self, kind, h):
        return re.sub(r"[^A-Za-z0-9]+", "", kind) + "/" + h[:12]

    def add(self, kind, release, h, nodes, note=""):
        """Write a component from in-memory nodes (bin paths absolute); returns its uid."""
        uid = "uid://" + h[:16]
        if uid in self.index:
            return uid
        name = self.name_for(kind, h)
        out = {}
        for i, (loc, rec) in enumerate(sorted(nodes.items())):
            rec = copy.deepcopy(rec)
            r = rec["base"] if "schema" in rec else rec
            if "bin" in r:
                rel = f"{name}/{i}.bin"
                (self.dir / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(r["bin"], self.dir / rel)
                r["bin"] = rel
            out[loc] = rec
        dump(self.dir / (name + ".jsonc"),
             {"format_version": FORMAT, "uid": uid, "kind": kind, "release": release, "nodes": out},
             [f"component ({kind}){note}: tools/xdl_components.py"])
        self.index[uid] = name + ".jsonc"
        return uid

    def add_extends(self, kind, release, h, base_uid, overrides, note=""):
        uid = "uid://" + h[:16]
        if uid in self.index:
            return uid
        name = self.name_for(kind, h)
        dump(self.dir / (name + ".jsonc"),
             {"format_version": FORMAT, "uid": uid, "kind": kind, "release": release,
              "extends": base_uid, "overrides": overrides},
             [f"component ({kind}){note}: tools/xdl_components.py"])
        self.index[uid] = name + ".jsonc"
        return uid

    def save(self):
        self.dir.mkdir(parents=True, exist_ok=True)
        (self.dir / "index.json").write_text(json.dumps(dict(sorted(self.index.items())), indent=1) + "\n")


def expand(lib, inst):
    """{node id: record} of an instance: the component's nodes with the instance's overrides,
    named in the using pack. Records are what xdl_graph.Builder reads (`bin` absolute)."""
    uid = inst.get("component")
    if not isinstance(uid, str) or not uid.startswith("uid://"):
        raise CompError(f"'component' must be a uid:// link, not {uid!r}")
    nodes = apply(lib.nodes(uid), inst.get("overrides"))
    m = re.match(r"^(base://[^/]+/[^/]+)/(.+)$", inst.get("id", ""))
    if not m:
        raise CompError(f"an instance needs the id of its root in the pack, not {inst.get('id')!r}")
    prefix, root = m.group(1) + "/", rle(m.group(2))
    ids = {loc: prefix + unrle(join(root, rle(loc[2:]) if len(loc) > 1 else [])) for loc in nodes}
    out = {}
    for loc, rec in nodes.items():
        def fix(v, loc=loc):
            if v == BIND:
                raise CompError(f"{loc}: a link into the pack ('@') is not bound by the instance's overrides")
            if v == "$" or v.startswith("$/"):
                head, rest = split_local(v, ids)
                return ids[head] + rest
            return v
        out[ids[loc]] = map_links(rec, fix)
    return out


# ------------------------------------------------------------------------------------------
# reading a pack project

class PackProject:
    def __init__(self, d):
        self.dir = Path(d)
        self.pack = load(self.dir / "pack.jsonc")
        self.layout = self.pack["layout"]
        self.id = self.pack["id"]
        self.prefix = self.id + "/"
        self.ent = {e["id"]: e for e in self.layout}
        self._home, self._split, self._tree = {}, {}, None
        self.dirty = False

    def home(self, stem):
        h = self._home.get(stem)
        if h is None:
            for kind, ext in (("geometry", ".xgeo.jsonc"), ("typed", ".base.jsonc"), ("raw", ".jsonc")):
                p = self.dir / (stem + ext)
                if p.exists():
                    h = (kind, load(p))
                    break
            else:
                raise CompError(f"{self.dir}: no file for {stem}")
            self._home[stem] = h
        return h

    def record(self, nid):
        """A node's record (bin paths absolute), or None for a geometry or a node of an instance."""
        e = self.ent[nid]
        kind, d = self.home(e["file"])
        if "component" in d:
            return None
        if d.get("id") == nid:
            if kind == "geometry":
                return None
            body = {k: v for k, v in d.items() if k not in ("format_version", "id", "children")}
            if "bin" in body:
                body["bin"] = str(self.dir / body["bin"])
            if kind == "typed":
                doc = load(self.dir / (e["file"] + ".jsonc"))
                doc.pop("$schema", None)
                return {"schema": body.pop("schema"), "doc": doc, "base": body}
            return body
        rec = (d.get("children") or {}).get(nid)
        if rec is None:
            raise CompError(f"{nid}: not in {e['file']}")
        rec = {k: v for k, v in rec.items() if k != "id"}
        if "bin" in rec:
            rec["bin"] = str(self.dir / rec["bin"])
        return rec

    def tree(self):
        """({id: steps}, {id: [ids named directly below it]})."""
        if self._tree is None:
            n = len(self.prefix)
            r = {nid: rle(nid[n:]) for nid in self.ent}
            by = {tuple(v): k for k, v in r.items()}
            kids = collections.defaultdict(list)
            for nid, p in r.items():
                q = p
                while q:
                    s, c = q[-1]
                    q = q[:-1] + [(s, c - 1)] if c > 1 else q[:-1]
                    par = by.get(tuple(q))
                    if par is not None:
                        kids[par].append(nid)
                        break
            self._tree = (r, kids)
        return self._tree

    def split(self, link):
        """An own-pack link -> (node id, rest), or None."""
        x = self._split.get(link, 0)
        if x == 0:
            parts = link[len(self.prefix):].split("/")
            x = None
            for k in range(len(parts), 0, -1):
                head = self.prefix + "/".join(parts[:k])
                if head in self.ent:
                    x = (head, "".join("/" + p for p in parts[k:]))
                    break
            self._split[link] = x
        return x

    def candidates(self, kinds):
        """[(root id, kind)] of the nodes with a file of their own and a component kind, outermost first."""
        r, _ = self.tree()
        out = []
        typed_layout = any("type" in e for e in self.layout)
        for e in self.layout:
            if typed_layout:
                t = e.get("type")
            else:                       # a project extracted before the layout carried types
                if e["file"] != GR.file_stem(GR.id_path(e["id"])):
                    continue
                kind, d = self.home(e["file"])
                t = d.get("type") if d.get("id") == e["id"] else None
            if t in kinds:
                out.append((e["id"], t))
        out.sort(key=lambda x: (len(r[x[0]]), x[0]))
        return out

    def save(self):
        if self.dirty:
            dump(self.dir / "pack.jsonc", self.pack,
                 [f"{self.id[7:]} ({self.pack['release']}): pack header, TOC entry and the order of its nodes "
                  "(tools/xdl_graph.py; components factored by tools/xdl_components.py)"])
            self.dirty = False


class Cand:
    """One candidate subgraph of a pack: its nodes named from the root, links made relative."""

    def __init__(self, pp, root):
        self.pp, self.root = pp, root
        r, kids = pp.tree()
        ids, stack = [root], [root]
        while stack:
            for c in kids.get(stack.pop(), ()):
                ids.append(c)
                stack.append(c)
        rr = r[root]
        self.local = {}
        for nid in ids:
            l = relative(rr, r[nid])
            if l is None:
                raise CompError(f"{nid} is not below {root}")
            self.local[nid] = local_name(l)
        self.nodes = self.binds = None

    def link(self, v):
        """(the link as the component holds it, whether the instance must bind it)."""
        pp = self.pp
        if not v.startswith(pp.prefix):
            return v, False
        x = pp.split(v)
        if x is not None and x[0] in self.local:
            return self.local[x[0]] + x[1], False
        return BIND, True

    def _raw(self, rec):
        out, ov = dict(rec), {}
        for k in LINKKEYS:
            if k in rec:
                d = {}
                for s, x in rec[k].items():
                    d[s], b = self.link(x)
                    if b:
                        ov.setdefault(k, {})[s] = x
                out[k] = d
        return out, ov

    def _doc(self, v):
        """(component value, instance patch, whether a patch is needed)."""
        if isinstance(v, dict):
            c, p = {}, {}
            for k, x in v.items():
                c[k], pv, need = self._doc(x)
                if need:
                    p[k] = pv
            return c, p, bool(p)
        if isinstance(v, list):
            xs = [self._doc(x) for x in v]
            if any(x[2] for x in xs):        # merge patch replaces a list whole
                keep = map_doc(v, lambda s: s if self.link(s)[1] else self.link(s)[0])
                return [x[0] for x in xs], keep, True
            return [x[0] for x in xs], None, False
        if isinstance(v, str):
            y, b = self.link(v)
            return y, v, b
        return v, None, False

    def build(self):
        """Fills nodes ({local: record}) and binds (the overrides every instance needs: its links
        into the pack). False when the subgraph holds something a component can't."""
        nodes, binds = {}, {}
        for nid, loc in self.local.items():
            rec = self.pp.record(nid)
            if rec is None:
                return False
            if "schema" in rec:
                doc, patch, need = self._doc(rec["doc"])
                base, ov = self._raw(rec["base"])
                rec = {"schema": rec["schema"], "doc": doc, "base": base}
                b = {}
                if need:
                    b["doc"] = patch
                if ov:
                    b["base"] = ov
            else:
                rec, b = self._raw(rec)
            rec["align"] = self.pp.ent[nid]["align"]
            nodes[loc] = rec
            if b:
                binds[loc] = b
        self.nodes, self.binds = nodes, binds
        return True

    def size(self):
        return sum((r["base"] if "schema" in r else r)["size"] for r in self.nodes.values())


# ------------------------------------------------------------------------------------------
# near-duplicates: what one subgraph changes in another of the same shape

def diff_doc(a, b):
    """(merge patch turning a into b, number of values it sets), or (None, 0) when equal."""
    if isinstance(a, dict) and isinstance(b, dict):
        p, n = {}, 0
        for k, v in b.items():
            if k not in a:
                p[k], n = v, n + 1
            elif a[k] != v:
                p[k], c = diff_doc(a[k], v)
                n += c
        return (p, n) if p else (None, 0)
    if a == b:
        return None, 0
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        return b, sum(1 for x, y in zip(a, b) if x != y)
    return b, 1


def diff_raw(a, b):
    ov, n = {}, 0
    for k in SLOTKEYS:
        da, db = a.get(k) or {}, b.get(k) or {}
        d = {s: v for s, v in db.items() if da.get(s) != v}
        d.update({s: None for s in da if s not in db})
        if d:
            ov[k] = d
            n += len(d)
    x, y = rec_data(a), rec_data(b)
    if len(x) != len(y):
        return None, 0
    if x != y:
        patch, run = {}, None
        for o in range(0, len(x), 4):
            if x[o:o + 4] != y[o:o + 4]:
                n += 1
                if run is None:
                    run = o
            elif run is not None:
                patch[GR.hx(run)] = y[run:o].hex()
                run = None
        if run is not None:
            patch[GR.hx(run)] = y[run:].hex()
        ov["patch"] = patch
    return ov, n


def diff(a, b):
    """(overrides turning nodes a into nodes b, number of values changed), or (None, 0)."""
    if set(a) != set(b):
        return None, 0
    out, n = {}, 0
    for loc in a:
        ra, rb = a[loc], b[loc]
        if ("schema" in ra) != ("schema" in rb) or ra.get("schema") != rb.get("schema"):
            return None, 0
        if "schema" in ra:
            ov = {}
            p, c = diff_doc(ra["doc"], rb["doc"])
            if p is not None:
                ov["doc"] = p
            bo, c2 = diff_raw(ra["base"], rb["base"])
            if bo is None:
                return None, 0
            if bo:
                ov["base"] = bo
            c += c2
        else:
            ov, c = diff_raw(ra, rb)
            if ov is None:
                return None, 0
        if ov:
            out[loc] = ov
            n += c
    return out, n


def same(a, b):
    return set(a) == set(b) and all(canon(a[k]) == canon(b[k]) for k in a)


# ------------------------------------------------------------------------------------------
# factor

def pack_dirs(root):
    return sorted(p.parent for p in Path(root).rglob("pack.jsonc"))


def scan_pack(d, kinds):
    """[(root, kind, exact digest, shape digest, bytes, nodes)] of a pack's candidates."""
    pp = PackProject(d)
    out = []
    for root, kind in pp.candidates(kinds):
        c = Cand(pp, root)
        if not c.build():
            continue
        out.append((root, kind, digest(c.nodes), digest(c.nodes, True), c.size(), len(c.nodes)))
    return pp.pack["release"], out


def _scan(args):
    return args[0], scan_pack(*args)


def factor(project_root, lib_dir=None, kinds=KINDS, min_uses=2, near=0, jobs=1, log=print):
    """Move the repeated subgraphs of every pack project under project_root to a component
    library (default project_root/components) and leave instances. Returns the counts."""
    root = Path(project_root)
    lib = Library(lib_dir or root / "components")
    kinds = set(kinds)
    dirs = [d for d in pack_dirs(root) if lib.dir not in d.parents]
    t0 = time.time()
    # 1. every candidate's digests
    scans = {}
    if jobs > 1:
        import multiprocessing
        with multiprocessing.Pool(jobs) as pool:
            for d, res in pool.imap_unordered(_scan, [(d, kinds) for d in dirs], chunksize=4):
                scans[d] = res
    else:
        for d in dirs:
            scans[d] = scan_pack(d, kinds)
    rels = {r for r, c in scans.values() if c}
    if len(rels) > 1:
        raise CompError(f"{root}: packs of several releases ({', '.join(sorted(rels))}); factor one release at a time")
    release = next(iter(rels), None)
    var = {}                    # exact digest -> [kind, shape, uses, bytes, (dir, root)]
    for d in dirs:
        for rt, kind, ex, sh, size, n in scans[d][1]:
            v = var.setdefault(ex, [kind, sh, 0, size, (d, rt)])
            v[2] += 1
    log(f"scan: {len(dirs)} packs, {sum(len(c) for r, c in scans.values())} candidate subgraphs, "
        f"{len(var)} distinct ({time.time() - t0:.0f} s)")
    # 2. the plan: digest -> ("comp", uid) | ("near", base digest)
    plan = {ex: "comp" for ex, v in var.items() if v[2] >= min_uses}
    groups = collections.defaultdict(list)
    if near:
        for ex, v in var.items():
            groups[(v[0], v[1])].append(ex)
        groups = {k: sorted(g, key=lambda ex: (-var[ex][2], ex)) for k, g in groups.items() if len(g) > 1}
    need = set(plan) | {ex for g in groups.values() for ex in g}
    reps = collections.defaultdict(list)
    for ex in need:
        reps[var[ex][4][0]].append(ex)
    nodes = {}

    def rep_nodes(batch):
        for d in batch:
            pp = PackProject(d)
            for ex in reps[d]:
                c = Cand(pp, var[ex][4][1])
                c.build()
                nodes[ex] = c.nodes
    base_of, base_ov = {}, {}
    if groups:
        rep_nodes(sorted({var[ex][4][0] for g in groups.values() for ex in g}))
        for g in groups.values():
            b = g[0]
            words = var[b][3] // 4
            for ex in g[1:]:
                ov, n = diff(nodes[b], nodes[ex])
                if ov is None or n > near or n * 8 > words or not same(apply(nodes[b], ov), nodes[ex]):
                    continue
                base_of[ex], base_ov[ex] = b, ov
                plan.setdefault(b, "comp")
                plan.setdefault(ex, "near")
    rep_nodes(sorted({var[ex][4][0] for ex in plan if ex not in nodes}))
    uids = {}
    for ex in sorted(plan, key=lambda ex: (ex in base_of, ex)):
        kind, sh, uses, size, (d, rt) = var[ex]
        note = f", {uses} uses, first in {rt}"
        if ex not in base_of:
            uids[ex] = lib.add(kind, release, ex, nodes[ex], note)
        elif plan[ex] == "comp":
            uids[ex] = lib.add_extends(kind, release, ex, uids[base_of[ex]], base_ov[ex], note)
    nodes.clear()
    lib.save()
    # 3. instances
    tot = collections.Counter()
    per_kind = collections.Counter()
    for d in dirs:
        chosen = [(rt, kind, ex) for rt, kind, ex, sh, size, n in scans[d][1] if ex in plan]
        if not chosen:
            continue
        pp = PackProject(d)
        taken = set()
        for rt, kind, ex in chosen:
            if rt in taken:
                continue
            c = Cand(pp, rt)
            if any(n in taken for n in c.local) or not c.build():
                continue
            if ex in uids:
                uid, ov = uids[ex], {}
            else:
                uid = uids[base_of[ex]]
                ov, _ = diff(lib.nodes(uid), c.nodes)
            ov = merge_over(ov, c.binds)
            inst = {"format_version": FORMAT, "id": rt, "component": uid, "kind": kind}
            if ov:
                inst["overrides"] = ov
            got = expand(lib, inst)
            want = {nid: pp.record(nid) for nid in c.local}
            if set(got) != set(want) or any(canon({k: v for k, v in got[n].items() if k != "align"}) != canon(want[n])
                                            for n in want):
                log(f"  {rt}: the instance does not expand to the same nodes; left as it is")
                tot["refused"] += 1
                continue
            _replace(pp, c, inst)
            taken |= set(c.local)
            tot["instances"] += 1
            tot["nodes"] += len(c.local)
            tot["bytes"] += c.size()
            tot["overridden"] += bool(ov and ex not in uids)
            per_kind[kind] += 1
        if pp.dirty:
            pp.pack["components"] = os.path.relpath(lib.dir.resolve(), pp.dir.resolve())
            pp.save()
            tot["packs"] += 1
    tot["components"] = len(uids)
    tot["extending"] = sum(1 for ex in uids if ex in base_of)
    log(f"factor: {tot['components']} components ({tot['extending']} extend another), {tot['instances']} instances "
        f"({tot['overridden']} with value overrides) in {tot['packs']} packs; {tot['nodes']} nodes, "
        f"{tot['bytes']} bytes of pack data now come from the library ({time.time() - t0:.0f} s)")
    for k, v in per_kind.most_common():
        log(f"  {v:7}  {k}")
    return tot


def _replace(pp, c, inst):
    """The subgraph's files out, the instance file in, the layout pointed at it."""
    stem = pp.ent[c.root]["file"]
    files = set()
    for nid in c.local:
        e = pp.ent[nid]
        kind, d = pp.home(e["file"])
        files.add(pp.dir / (e["file"] + ".jsonc"))
        files.add(pp.dir / (e["file"] + ".base.jsonc"))
        rec = d if d.get("id") == nid else d["children"][nid]
        if "bin" in rec:
            files.add(pp.dir / rec["bin"])
    dirs = set()
    for f in files:
        if f.exists():
            f.unlink()
            dirs.add(f.parent)
    for nid in c.local:
        pp._home.pop(pp.ent[nid]["file"], None)
        pp.ent[nid]["file"] = stem
    dump(pp.dir / (stem + ".jsonc"), inst, [f"an instance of a component ({inst['kind']}): {len(c.local)} nodes "
                                            "from the library, with the fields this pack changes"])
    for d in sorted(dirs, key=lambda p: -len(p.parts)):
        while d != pp.dir and d.exists() and not any(d.iterdir()):
            d.rmdir()
            d = d.parent
    pp.dirty = True


def inline(pack_dir, lib=None, S=None):
    """Instances back to plain files (one per node). Returns the number of instances."""
    pp = PackProject(pack_dir)
    S = S or XS.Schemas()
    done = {}
    for e in pp.layout:
        stem = e["file"]
        if stem not in done:
            kind, d = pp.home(stem)
            if "component" not in d:
                continue
            lib = lib or Library.locate(pp.dir, pp.pack.get("components"))
            done[stem] = expand(lib, d)
    for stem in done:
        (pp.dir / (stem + ".jsonc")).unlink()
    for stem, recs in done.items():
        for nid, rec in recs.items():
            own = GR.file_stem(GR.id_path(nid))
            pp.ent[nid]["file"] = own
            rec = dict(rec)
            rec.pop("align", None)
            r = rec["base"] if "schema" in rec else rec
            r = dict(r)
            if "bin" in r or r["size"] > GR.INLINE_MAX:
                data = rec_data(r)
                r.pop("bytes", None)
                r["bin"] = own + (".base.bin" if "schema" in rec else ".bin")
                (pp.dir / r["bin"]).parent.mkdir(parents=True, exist_ok=True)
                (pp.dir / r["bin"]).write_bytes(data)
            if "schema" in rec:
                p = pp.dir / (own + ".jsonc")
                p.parent.mkdir(parents=True, exist_ok=True)
                sname = rec["schema"]
                doc = {"$schema": os.path.relpath(S.path(sname).resolve(), p.parent.resolve()), **rec["doc"]}
                p.write_text(XS.to_jsonc(doc, S.struct(sname), [f"{sname} {nid}, from a component"]))
                dump(pp.dir / (own + ".base.jsonc"), {"format_version": FORMAT, "id": nid, "schema": sname, **r})
            else:
                dump(pp.dir / (own + ".jsonc"), {"format_version": FORMAT, "id": nid, **r})
    if done:
        pp.dirty = True
        pp.save()
    return len(done)


# ------------------------------------------------------------------------------------------
# commands

def tree_size(d):
    n = b = 0
    for p in Path(d).rglob("*"):
        if p.is_file():
            n += 1
            b += p.stat().st_size
    return n, b


def cmd_factor(a):
    kinds = set(a.kinds.split(",")) if a.kinds else KINDS
    factor(a.project, a.lib, kinds, a.min_uses, a.near, a.jobs)
    return 0


def cmd_inline(a):
    print(f"{a.pack}: {inline(a.pack)} instances written out as plain files")
    return 0


def cmd_stats(a):
    lib = Library(a.library)
    c = collections.Counter()
    for uid in lib.index:
        d = lib.doc(uid)
        c[(d["kind"], "extends" in d)] += 1
    for (k, ext), v in sorted(c.items()):
        print(f"{v:6}  {k}{' (extends another)' if ext else ''}")
    n, b = tree_size(lib.dir)
    print(f"{len(lib.index)} components, {n} files, {b / 1e6:.1f} MB")
    return 0


def cmd_roundtrip(a):
    """extract -> factor -> build -> compare, every pack of the chosen archives."""
    S = XS.Schemas()
    only = {x.lower() for x in a.archive.split(",")} if a.archive else None
    bad = 0
    for rel in a.rel.split(","):
        out = Path(a.out) / rel
        if out.exists():
            shutil.rmtree(out)
        packs = []
        t0 = time.time()
        for arch in GR.survey_archives(a.survey, rel, only):
            man = json.loads((arch / "manifest.json").read_text())
            relpath = arch.relative_to(Path(a.survey) / rel)
            GR.write_archive(arch, out / "project" / relpath)
            for ent in man["entries"]:
                proj = out / "project" / relpath / GR.pack_name(ent)
                try:
                    GR.Extractor(GR.Source(arch / ent["path"]), proj, S).run()
                    packs.append((arch / ent["path"], proj, out / "built" / relpath / ent["path"]))
                except (GR.GraphError, XS.XdlError) as e:
                    print(f"FAIL {rel} {relpath}/{ent['path']}: extract: {e}", flush=True)
                    bad += 1
        n0, b0 = tree_size(out / "project")
        print(f"{rel}: {len(packs)} packs extracted, {n0} files, {b0 / 1e6:.1f} MB ({time.time() - t0:.0f} s)", flush=True)
        factor(out / "project", None, KINDS, a.min_uses, a.near, a.jobs)
        n1, b1 = tree_size(out / "project")
        print(f"{rel}: project now {n1} files, {b1 / 1e6:.1f} MB (was {n0}, {b0 / 1e6:.1f} MB)", flush=True)
        ok = 0
        t0 = time.time()
        for src, proj, built in packs:
            try:
                GR.Builder(proj, S).build(built)
                d = GR.compare(src, built)
            except (GR.GraphError, XS.XdlError) as e:
                d = [("error", str(e))]
            if d:
                bad += 1
                print(f"FAIL {rel} {proj}: {d[:3]}", flush=True)
            else:
                ok += 1
            shutil.rmtree(built, ignore_errors=True)
        print(f"{rel}: {ok}/{len(packs)} packs byte-identical after factoring ({time.time() - t0:.0f} s)", flush=True)
        if not a.keep:
            shutil.rmtree(out)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("factor")
    p.add_argument("project", help="a folder of pack projects of one release")
    p.add_argument("--lib", help="the component library (default PROJECT/components)")
    p.add_argument("--kinds", help="comma-separated walker labels (default: the list in this file)")
    p.add_argument("--min-uses", type=int, default=2)
    p.add_argument("--near", type=int, default=0, help="also factor subgraphs within N changed values of a component")
    p.add_argument("-j", "--jobs", type=int, default=1)
    p = sub.add_parser("inline")
    p.add_argument("pack")
    p = sub.add_parser("stats")
    p.add_argument("library")
    p = sub.add_parser("roundtrip")
    p.add_argument("survey")
    p.add_argument("--rel", default="gc")
    p.add_argument("--archive")
    p.add_argument("--out", default=str(XS.ROOT / "build" / "components"))
    p.add_argument("--min-uses", type=int, default=2)
    p.add_argument("--near", type=int, default=0)
    p.add_argument("--keep", action="store_true", help="keep the factored project (OUT/<rel>/project)")
    p.add_argument("-j", "--jobs", type=int, default=1)
    a = ap.parse_args()
    try:
        return {"factor": cmd_factor, "inline": cmd_inline, "stats": cmd_stats, "roundtrip": cmd_roundtrip}[a.cmd](a)
    except (GR.GraphError, XS.XdlError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
