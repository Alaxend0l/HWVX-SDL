#!/usr/bin/env python3
"""A level built into its own world pack (stage 21.4, option 1): the base world's extracted project
(xdl_level.py base) with the level's records added to its arrays and tables and its patches
written into the records, rebuilt by tools/xdl_graph.py's Builder (arrays, counts, relocations,
the trigger table) and packed by hwvx-x into a world archive the port loads for the level's track
in place of the base's. Made on the player's machine from their own data: the archive is game
data and never goes into a mod.

    xdl_level_pack.py build PROJECT OUT.xgc --data DATA [--cache DIR] [--skip-intro]

What a level adds (level.jsonc; xdl_level.py documents the rest):
  volumes      appended to World/0/volumes (sphere or box: a BSP from tools/bsp_build.py)
  timers, variables
               records of their own, appended to the World's tables of entries
  strings      string tables
  starts       tile cars appended to World/0/tileCars (a copy of the player's start, moved)
  items        pickups appended to their quadrant's items (QuadContents)
  triggers     records in the pack's trigger table, after the base triggers it keeps (the others
               stay in the pack, out of the table)
  patches      number fields of base records, written into their JSON before the build

As a module: build_world(project, base_project, extract_dir, out_archive, skip_intro) and
build_cached(...) (keyed on the sources' hashes and the base's identity).
"""

import argparse
import copy
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_graph as GR  # noqa: E402
import xdl_level as LV  # noqa: E402
import xdl_struct as XS  # noqa: E402


class PackCompiler(LV.Compiler):
    """The level's own records encoded in the release's byte order, links named as the
    rebuilt pack's nodes (not pointer walks)."""

    def __init__(self, proj, base, skip, e, names):
        super().__init__(proj, base, skip)
        self.e = e
        self.names = names          # level:// -> the pack's node id

    def resolve(self, s, where, archive):
        if s in self.names:
            return self.names[s]
        m = re.fullmatch(r"base://([A-Za-z0-9_]+)/world_0/(.+)", s)
        if m:
            if m.group(1).lower() != archive.lower():
                raise LV.LevelError(f"{where}: {s}: not the level's world ({archive})")
            if self.base is not None:
                why = self.base.check(m.group(2))
                if why:
                    raise LV.LevelError(f"{where}: {s}: {why}")
            return s
        best = max((k for k in self.names if s.startswith(k + "/")), key=len, default=None)
        if best is not None:
            return self.names[best] + s[len(best):]
        raise LV.LevelError(f"{where}: {s}: the level has nothing of that name")


def raw_doc(nid, data, links):
    return {"id": nid, "size": len(data), "bytes": bytes(data).hex(),
            "links": {hex(off): link for off, link in links}}


def lpath(doc, path):
    """The JSON object at path (a/b/0/c) in doc, or None."""
    cur = doc
    for seg in path.split("/") if path else []:
        if isinstance(cur, list) and seg.isdigit() and int(seg) < len(cur):
            cur = cur[int(seg)]
        elif isinstance(cur, dict) and seg in cur:
            cur = cur[seg]
        else:
            return None
    return cur


class WorldBuild:
    def __init__(self, base_project):
        self.B = GR.Builder(Path(base_project), XS.Schemas())
        self.pre = f"base://{self.B.archive}/world_0/"
        self.layout = list(self.B.pack["layout"])
        self.file_of = {ent["id"]: ent["file"] for ent in self.layout}
        self.new_stems = 0

    def doc(self, stem):
        return self.B.doc(stem)

    def node_doc(self, rel):
        """(doc, path in it) of a base record by its path (World/0/tileCars/0): the longest node
        id the layout has, then items/<n> and fields."""
        segs = rel.split("/")
        for k in range(len(segs), 1, -1):
            nid = self.pre + "/".join(segs[:k])
            if nid in self.file_of:
                d = self.doc(self.file_of[nid])
                if d.get("id") not in (None, nid) and "extends" not in d and nid not in d.get("children", {}):
                    continue
                rest = segs[k:]
                if isinstance(d.get("items"), list):
                    return d, "/".join(["items"] + rest)
                return d, "/".join(rest)
        return None, None

    def add_raw(self, nid, data, links, at=None, align=4):
        stem = f"level/n{self.new_stems}"
        self.new_stems += 1
        self.B.docs[stem + ".jsonc"] = raw_doc(nid, data, links)
        ent = {"id": nid, "file": stem, "align": align}
        if at is None:
            self.layout.append(ent)
        else:
            self.layout.insert(at, ent)
        self.file_of[nid] = stem
        return stem

    def build(self, out_dir):
        self.B.pack["layout"] = self.layout
        return self.B.build(out_dir)


def volume_item(v):
    """A level.jsonc volume as a WorldVolume item of the pack's array."""
    c = v.get("center", [0, 0, 0])
    if isinstance(c, list):
        c = {"x": c[0], "y": c[1], "z": c[2]}
    r = v.get("radius")
    it = {"editorId": 0, "enabled": v.get("enabled", 1), "shape": v.get("shape", "sphere"),
          "type": v.get("type", "airbox"), "center": {"x": float(c["x"]), "y": float(c["y"]), "z": float(c["z"]), "w": 0.0},
          "radius": float(r) if r is not None else 0.0,
          "radiusSq": float(v.get("radiusSq", float(r) * float(r) if r is not None else 0.0)),
          "bsp": None, "data": None}
    return it


def box_bsp(lo, hi, e):
    """A box volume's BSP (bsp_build.py), as pack bytes in the release's byte order."""
    import bsp_build as BB
    import world_lib as WL
    tris = BB.prism([(lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2])], lo[1], hi[1])
    tree = BB.build(BB.polys_from_triangles(tris))
    return WL.bspn0_write(tree, e)


def build_world(project, base_project, extract_dir, out_archive, skip_intro=False):
    """The level's world archive at out_archive. Raises LevelError with every problem."""
    project = Path(project)
    W = WorldBuild(base_project)
    pre, e = W.pre, W.B.e
    lv = LV.read_jsonc(project / "level.jsonc")
    world = W.doc(W.file_of[pre + "World/0"])

    def table_doc(name):
        nid = pre + f"World/0/{name}"
        return W.doc(W.file_of[nid]) if nid in W.file_of else None

    names = {}
    own = {}            # level://<kind>/<name> -> pre + level/<k> (node ids end in a number)

    def own_id(kind, n):
        key = f"level://{kind}/{n}"
        if key not in own:
            own[key] = f"{pre}level/{len(own)}"
        return own[key]
    # the level's own records: where they go
    vols = list((lv.get("volumes") or {}).items())
    vdoc = table_doc("volumes")
    v0 = len(vdoc["items"]) if vdoc is not None else 0
    for i, (n, _) in enumerate(vols):
        names[f"level://volumes/{n}"] = f"{pre}World/0/volumes/{v0 + i}"
    for kind in ("timers", "variables"):
        recs = list((lv.get(kind) or {}).items())
        d = table_doc(kind)
        k0 = len(d["items"]) if d is not None else 0
        for i, (n, _) in enumerate(recs):
            names[f"level://{kind}/{n}"] = f"{pre}World/0/{kind}/{k0 + i}"
            names[f"level://{kind}/{n}/to"] = own_id(kind, n)
    for n in (lv.get("strings") or {}):
        names[f"level://strings/{n}"] = own_id("strings", n)
    # the records the pack build adds to tables: where links to them go
    tc = table_doc("tileCars")
    for i, n in enumerate(lv.get("starts") or {}):
        names[f"level://starts/{n}"] = f"{pre}World/0/tileCars/{len(tc['items']) + i}"
    od = table_doc("objectives")
    for i, n in enumerate(lv.get("objectives") or {}):
        names[f"level://objectives/{n}"] = f"{pre}World/0/objectives/{(len(od['items']) if od else 0) + i}"
    for kind, arr in (("items", "items"), ("fxPoints", "fxPoints")):
        added = {}
        for n, it in (lv.get(kind) or {}).items():
            q = it.get("quad") if isinstance(it, dict) else None
            aid = f"{pre}World/0/quads/{q}/contents/{arr}"
            if not isinstance(q, int) or aid not in W.file_of:
                continue
            k0 = len(W.doc(W.file_of[aid]).get("items") or [])
            names[f"level://{kind}/{n}"] = f"{aid}/{k0 + added.get(q, 0)}"
            added[q] = added.get(q, 0) + 1
    hdr = W.B.pack["pack"]["header"]
    nbase = int(hdr.get("num_triggers", 0))
    tnames = lv.get("triggers") or sorted(p.stem for p in (project / "triggers").glob("*.jsonc"))
    tnames = list(tnames) + [n for n, _ in LV.generated_triggers(lv, "level.jsonc/rules")]
    for i, n in enumerate(tnames):
        names[f"level://triggers/{n}"] = f"{pre}trigger/{nbase + i}"

    C = PackCompiler(project, base_project, skip_intro, e, names)
    doc = C.run()
    if C.errors or doc is None:
        raise LV.LevelError("\n".join(C.errors))

    # patches: written into the records' JSON
    for path, fields in (lv.get("patches") or {}).items():
        rel = path.split("/world_0/", 1)[1]
        d, at = W.node_doc(rel)
        rec = lpath(d, at) if d is not None else None
        if not isinstance(rec, dict):
            raise LV.LevelError(f"level.jsonc/patches/{path}: no such record in the project")
        for f, v in fields.items():
            if "/" in f:
                a, b = f.split("/", 1)
                if not isinstance(rec.get(a), dict):
                    raise LV.LevelError(f"level.jsonc/patches/{path}/{f}: {a} isn't a struct")
                rec[a][b] = v
            else:
                rec[f] = v

    # volumes: into the array (a box's BSP is a part of the compiler's, below)
    for i, (n, v) in enumerate(vols):
        it = volume_item(v)
        if "box" in v:
            lo, hi = v["box"]["min"], v["box"]["max"]
            it["shape"] = "bsp"
            it["bsp"] = own_id("volumes", n) + "/bsp"
            it["center"] = {"x": (lo[0] + hi[0]) / 2, "y": (lo[1] + hi[1]) / 2, "z": (lo[2] + hi[2]) / 2, "w": 0.0}
            it["radius"] = 0.5 * math.sqrt(sum((hi[k] - lo[k]) ** 2 for k in range(3)))
            it["radiusSq"] = 0.0
        if isinstance(v.get("data"), dict):
            it["data"] = own_id("volumes", n) + "/data"
        vdoc["items"].append(it)
    if vols:
        world["numVolumes"] = len(vdoc["items"])

    # the parts the compiler made (records, strings, triggers' lists), by kind
    parts = doc["parts"]
    trig_records = []
    for p in parts:
        pid = p["id"]
        if p["kind"] == "volume":
            continue                       # in the array above
        segs = pid.split("/")             # level:, '', kind, name, rest...
        kind, name, rest = segs[2], segs[3], "/".join(segs[4:])
        if kind == "triggers":
            nid = names[f"level://triggers/{name}"] + ("/" + rest if rest else "")
        elif kind in ("timers", "variables") and rest.startswith("to"):
            nid = own_id(kind, name) + rest[2:]
        else:
            nid = own_id(kind, name) + ("/" + rest if rest else "")
        data = bytes.fromhex(p["hex"])
        links = []
        for off, l in p["links"]:
            links.append((off, l))
        if p["kind"] == "trigger":
            trig_records.append((nid, data, links))
        else:
            W.add_raw(nid, data, links)
    for kind in ("timers", "variables"):
        d = table_doc(kind)
        recs = [names[f"level://{kind}/{n}/to"] for n in (lv.get(kind) or {})]
        key = "num" + kind[0].upper() + kind[1:]
        if d is None and recs:
            # the world has none: a table of entries of its own, the World pointing at it
            nid = f"{pre}World/0/{kind}"
            stem = W.add_raw(nid, bytes(4 * len(recs)), [(4 * i, r) for i, r in enumerate(recs)])
            W.B.docs[stem + ".jsonc"]["anchors"] = {str(i): hex(4 * i) for i in range(len(recs))}
            world[kind] = nid
            world[key] = len(recs)
        elif d is not None:
            d["items"].extend(recs)
            world[key] = len(d["items"])

    # starts: tile cars, copies of the player's start, moved
    starts = list((lv.get("starts") or {}).items())
    if starts:
        tc = table_doc("tileCars")
        tmpl = next((c for c in tc["items"] if c.get("port") == 1), tc["items"][0])
        for n, s in starts:
            it = copy.deepcopy(tmpl)
            p = s.get("pos", [0, 0, 0])
            it["pos"] = {"x": float(p[0]), "y": float(p[1]), "z": float(p[2]), "w": it["pos"].get("w", 0.0)}
            it["rotY"] = float(s.get("rotY", 0.0))
            it["port"] = int(s.get("port", 0))
            for k, v in s.items():
                if k not in ("pos", "rotY", "port"):
                    it[k] = v
            tc["items"].append(it)
        world["numTileCars"] = len(tc["items"])

    # items: into their quadrant's contents
    for n, it in (lv.get("items") or {}).items():
        q = it.get("quad")
        if not isinstance(q, int):
            raise LV.LevelError(f"level.jsonc/items/{n}/quad: the quadrant it is in (the editor finds it)")
        cid = f"{pre}World/0/quads/{q}/contents"
        if cid not in W.file_of:
            raise LV.LevelError(f"level.jsonc/items/{n}/quad: no quadrant {q}")
        cdoc = W.doc(W.file_of[cid])
        p = it.get("pos", [0, 0, 0])
        rec = {"pos": {"x": float(p[0]), "y": float(p[1]), "z": float(p[2]), "w": 0.0}, "mode": "on",
               "id": int(it.get("id", 0)), "respawn": int(it.get("respawn", 0))}
        iid = cid + "/items"
        if cdoc.get("items") and iid in W.file_of:
            idoc = W.doc(W.file_of[iid])
            idoc["items"].append(dict(copy.deepcopy(idoc["items"][0]), **rec) if idoc["items"] else rec)
            cdoc["numItems"] = len(idoc["items"])
        else:
            raise LV.LevelError(f"level.jsonc/items/{n}: quadrant {q} has no items yet (a new items table: "
                                "not supported yet; put it where the world already has pickups)")

    # effect points: copies of a base one (its emitter, corona), into their quadrant
    for n, fx in (lv.get("fxPoints") or {}).items():
        q, tmpl = fx.get("quad"), fx.get("template")
        m = re.fullmatch(re.escape(pre) + r"World/0/quads/(\d+)/contents/fxPoints/(\d+)", tmpl or "")
        if not isinstance(q, int) or not m:
            raise LV.LevelError(f"level.jsonc/fxPoints/{n}: quad and template (a base effect point to copy)")
        src = W.doc(W.file_of[f"{pre}World/0/quads/{m.group(1)}/contents/fxPoints"]) \
            if f"{pre}World/0/quads/{m.group(1)}/contents/fxPoints" in W.file_of else None
        fid = f"{pre}World/0/quads/{q}/contents/fxPoints"
        if src is None or not isinstance(src.get("items"), list) or int(m.group(2)) >= len(src["items"]):
            raise LV.LevelError(f"level.jsonc/fxPoints/{n}/template: no such effect point")
        if fid not in W.file_of or not isinstance(W.doc(W.file_of[fid]).get("items"), list):
            raise LV.LevelError(f"level.jsonc/fxPoints/{n}/quad: quadrant {q} has no effect points yet "
                                "(not supported yet: put it where the world has some)")
        it = copy.deepcopy(src["items"][int(m.group(2))])
        p = fx.get("pos", [0, 0, 0])
        it["pos"] = {"x": float(p[0]), "y": float(p[1]), "z": float(p[2]), "w": it.get("pos", {}).get("w", 0.0)}
        it["mode"] = fx.get("mode", it.get("mode", "on"))
        fdoc = W.doc(W.file_of[fid])
        fdoc["items"].append(it)
        cdoc = W.doc(W.file_of[f"{pre}World/0/quads/{q}/contents"])
        cdoc["numFxPoints"] = len(fdoc["items"])

    # objectives: an entry of the World's objectives and its info (WorldObjectiveInfo)
    obs = list((lv.get("objectives") or {}).items())
    if obs:
        od = table_doc("objectives")
        if od is None:
            raise LV.LevelError("level.jsonc/objectives: the world has no objectives table to add to "
                                "(only mission and challenge worlds have one)")
        for n, ob in obs:
            kind = {"none": 0, "counter": 1, "timer": 2}[ob.get("kind", "none")]
            info = bytes([kind, 0x17 if ob.get("shown", True) else 0, int(ob.get("hud", 0)) & 0xFF]) + bytes(9) + bytes(16)
            links = []
            for off, k in ((0x0C, "done"), (0x10, "value"), (0x14, "text"), (0x18, "doneText")):
                if isinstance(ob.get(k), str):
                    links.append((off, C.resolve(ob[k], f"level.jsonc/objectives/{n}/{k}", W.B.archive)))
            nid = own_id("objectives", n) + "/info"
            W.add_raw(nid, info, links)
            od["items"].append({"arg": int(ob.get("arg", 0)), "info": nid})
        world["numObjectives"] = len(od["items"])

    # the trigger table: the base triggers kept, the level's, then the dropped ones
    keep = lv.get("baseTriggers", "all")
    recs = [i for i, ent in enumerate(W.layout) if re.fullmatch(re.escape(pre) + r"trigger/\d+", ent["id"])]
    if recs:
        first = recs[0]
        ents = [W.layout[i] for i in recs]
        for i in reversed(recs):
            del W.layout[i]
        num = lambda ent: int(ent["id"].rsplit("/", 1)[1])
        kept = [x for x in ents if keep == "all" or (isinstance(keep, list) and num(x) in keep)]
        dropped = [x for x in ents if x not in kept]
        new = []
        for nid, data, links in trig_records:
            stem = f"level/n{W.new_stems}"
            W.new_stems += 1
            W.B.docs[stem + ".jsonc"] = raw_doc(nid, data, links)
            W.file_of[nid] = stem
            new.append({"id": nid, "file": stem, "align": 4})
        table = kept + new
        if table == ents:
            for k, ent in enumerate(ents):              # as it was
                W.layout.insert(first + k, ent)
        else:
            for k, ent in enumerate(table + dropped):
                # back to back (no padding between: the game reads them as one table)
                e2 = {key: v for key, v in ent.items() if key != "pad"}
                W.layout.insert(first + k, dict(e2, align=32 if k == 0 else 4))
        hdr["num_triggers"] = len(table)
        hdr["trigger_offset"] = table[0]["id"] if table else hdr.get("trigger_offset")

    out_archive = Path(out_archive)
    adir = out_archive.parent / (out_archive.stem + ".build")
    man = json.loads((Path(extract_dir) / "manifest.json").read_text())
    wpath = next(e["path"] for e in man["entries"] if e["type"] == "world")
    pdir = adir / out_archive.stem / wpath
    if adir.exists():
        import shutil
        shutil.rmtree(adir)
    pdir.mkdir(parents=True)
    # the archive's other packs (a battle or drag world's collision, geometry, textures), as they are
    for ent in man["entries"]:
        if ent["path"] != wpath:
            (pdir.parent / ent["path"]).symlink_to((Path(extract_dir) / ent["path"]).resolve(), target_is_directory=True)
    try:
        W.build(pdir)
    except GR.GraphError as ex:
        raise LV.LevelError(f"the world pack: {ex}")
    man["source_file"] = out_archive.name
    (pdir.parent / "manifest.json").write_text(json.dumps(man, indent=2))
    exe = LV.find_hwvx_x()
    if exe is None:
        raise LV.LevelError("hwvx-x not found (target/release/hwvx-x)")
    r = subprocess.run([str(exe), "pack", str(pdir.parent), str(out_archive), "-f"], capture_output=True, text=True)
    if r.returncode:
        raise LV.LevelError("hwvx-x pack: " + (r.stderr or r.stdout).strip()[-400:])
    return out_archive


def release_ext(release):
    return {"gc": ".xgc", "pc": ".xpc", "ps2": ".xps"}.get(release, ".xgc")


def build_cached(project, data, cache, out_dir, skip_intro=False):
    """(archive path, cached, base name): the level's world archive in out_dir, built again only
    when a source or the base changed."""
    lv = LV.read_jsonc(Path(project) / "level.jsonc")
    base = LV.base_project(lv["world"], data, cache)
    extract = Path(base).parent / "extract" / Path(base).parent.name.rsplit("-", 1)[0]
    rel = LV.read_jsonc(Path(base) / "pack.jsonc")["release"]
    name = lv["world"].replace("/", "\\").rsplit("\\", 1)[-1].lower()
    out = Path(out_dir) / (name + release_ext(rel))
    key = LV.source_hash(project, f"pack;skip_intro={int(skip_intro)};base={LV.base_identity(base)};"
                                  + hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    stamp = out.with_suffix(out.suffix + ".key")
    if out.exists() and stamp.exists() and stamp.read_text() == key:
        return out, True, name
    build_world(project, base, extract, out, skip_intro)
    stamp.write_text(key)
    return out, False, name


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build")
    p.add_argument("project")
    p.add_argument("out_dir")
    p.add_argument("--data", required=True)
    p.add_argument("--cache")
    p.add_argument("--skip-intro", action="store_true")
    a = ap.parse_args()
    try:
        out, cached, _ = build_cached(a.project, a.data, LV.cache_dir(a), a.out_dir, a.skip_intro)
    except LV.LevelError as ex:
        return LV.report(str(ex).splitlines())
    print(f"{out}{' (up to date)' if cached else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
