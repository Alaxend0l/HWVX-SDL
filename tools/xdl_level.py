#!/usr/bin/env python3
"""Levels of one's own over a base world: source projects, compiled for the port.

    xdl_level.py compile PROJECT [-o LEVEL.json] [--base BASE_PROJECT]    check and compile
    xdl_level.py check PROJECT [--base BASE_PROJECT]                       check only
    xdl_level.py base WORLD_ARCHIVE --data DATA [--cache DIR]               the base world, extracted
    xdl_level.py mod PROJECT OUT_MOD_DIR [--base ...]                       a mod folder with the level
    xdl_level.py testdrive PROJECT SCRATCH [--car N] [--skip-intro]        the editor's test drive

A level is a folder (docs-pending/mission-editor.md):

    level.jsonc         what it is: id, name, mode, the base level it extends (its world,
                        loading screen, win and lose sequences), the world, which base triggers
                        stay, its own volumes, timers, variables and strings, field patches
    triggers/NAME.jsonc its own triggers, as tools/xdl_graph.py writes a world's
                        (schemas/Trigger.schema.json: conditions, actions, parameter lists)

Links: `base://<archive>/world_0/<path>` names a record of the base world by its structural
path (as the world's own trigger files do: World/0/volumes/19, World/0/tileCars/0,
trigger/15/actions/4/data/p0); `level://volumes/NAME`, `level://timers/NAME` (the timer's
table entry, which scripts name), `level://timers/NAME/to` (the timer), `level://variables/...`,
`level://strings/NAME`, `level://triggers/NAME` the level's own.

Compiling gives the level file the port reads (src/game/Port_Level.c): each record of the
level's own as bytes in the port's byte order with its links listed, base links as pointer
walks (["p", offset] follow, ["a", n] add) from the schemas' offsets, which the releases share.
It holds no game data: only the level's own records and paths into the player's world.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_struct as XS  # noqa: E402
import xdl_tac as XT  # noqa: E402

SCHEMAS = HERE.parent / "schemas"
E = "<"                     # the port's byte order (little-endian hosts)
FORMAT = "hwvx-level"

# the track families of the world selects (docs/xdl-format §10.5.1), GameCube numbering
MODES = {
    "joyride": (4, range(0, 5)),
    "mission": (0, range(5, 19)),
    "adventure": (0, range(5, 19)),
    "battle": (3, range(19, 27)),
    "drag": (2, range(29, 35)),
    "challenge": (1, range(39, 57)),
}
# TriggerStats[].max (data_gen.c): active triggers of a type at once
TRIGGER_MAX = [0x28, 0x14, 0xA, 0x14, 0x14, 0xA, 0x28, 0xA, 0xA, 0xA, 0x3C, 0xA, 0xA, 0xA, 0x5, 0xA, 0xA, 0xA,
               0xA, 0x5, 0xA, 0xA, 0x5, 0x5, 0x5, 0xA, 0xA, 0x28]
KINDS = ("volumes", "timers", "variables", "strings")


class LevelError(Exception):
    pass


def read_jsonc(p):
    try:
        return XS.jsonc_loads(Path(p).read_text())
    except (ValueError, OSError) as ex:
        raise LevelError(f"{p}: {ex}")


_schemas = {}


def schema(name):
    if name not in _schemas:
        _schemas[name] = json.loads((SCHEMAS / f"{name}.schema.json").read_text())
    return _schemas[name]


def struct_of(name):
    """(properties, size) of a struct the schemas describe (Trigger's records too)."""
    name = name.replace("struct ", "").strip()
    if name in ("Trigger",):
        s = schema("Trigger")
    elif name == "TriggerAction":
        s = schema("Trigger")["$defs"]["TriggerAction"]
    elif (SCHEMAS / f"{name}.schema.json").exists():
        s = schema(name)
    else:
        return None, None
    return s["properties"], int(s["x-size"], 16)


def link_of(p):
    if "x-link" in p:
        return p["x-link"]
    for alt in p.get("anyOf", []):
        if "x-link" in alt:
            return alt["x-link"]
    return None


# ------------------------------------------------------------------------------------------
# base links: structural paths as pointer walks

def base_steps(path, where):
    """{"root", "index", "steps"} for a path in a world pack (World/0/..., trigger/N/...)."""
    segs = path.split("/")
    if len(segs) >= 2 and segs[0] == "World" and segs[1] == "0":
        root, index, cur, rest = "world", 0, "World", segs[2:]
    elif len(segs) >= 2 and segs[0] == "trigger" and segs[1].isdigit():
        root, index, cur, rest = "trigger", int(segs[1]), "Trigger", segs[2:]
    else:
        raise LevelError(f"{where}: {path}: a base link starts at World/0 or trigger/N")
    steps = []
    item, item_size = None, None        # after a link to an array: its element
    for seg in rest:
        m = re.fullmatch(r"([po])([0-9a-fA-F]+)", seg)
        if seg.isdigit():
            if item_size is None:
                raise LevelError(f"{where}: {path}: {seg}: not an array here")
            steps.append(["a", int(seg) * item_size])
            cur, item, item_size = item, None, None
        elif seg == "to":
            steps.append(["p", 0])
            m2 = re.fullmatch(r"PTR32\((.+)\)", cur or "")
            cur, item, item_size = (m2.group(1) if m2 else None), None, None
        elif m:
            steps.append(["p" if m.group(1) == "p" else "a", int(m.group(2), 16)])
            cur, item, item_size = None, None, None
        else:
            props, _ = struct_of(cur) if cur else (None, None)
            if props is None or seg not in props or "x-offset" not in props[seg]:
                raise LevelError(f"{where}: {path}: {seg}: not a field of {cur or 'this record'}")
            p = props[seg]
            off = int(p["x-offset"], 16)
            ln = link_of(p)
            if ln is not None:
                steps.append(["p", off])
                if "item-size" in ln:
                    cur, item, item_size = None, ln.get("item", ln["target"]), ln["item-size"]
                else:
                    cur, item, item_size = ln["target"], None, None
            elif "$ref" in p:
                steps.append(["a", off])
                cur = p["$ref"].rsplit("/", 1)[-1]
                item = item_size = None
            else:
                raise LevelError(f"{where}: {path}: {seg} is a number, not a record")
    # fold additions
    out = []
    for s in steps:
        if s[0] == "a" and out and out[-1][0] == "a":
            out[-1][1] += s[1]
        elif not (s[0] == "a" and s[1] == 0):
            out.append(list(s))
    return {"root": root, "index": index, "steps": out}


def field_target(path, field, where):
    """(link, offset, prop) of a number field of a base record named by path: `field` is a
    field of its struct, or field/member of an embedded one (pos/y)."""
    link = base_steps(path, where)
    rec = record_struct(path, where)
    props, _ = struct_of(rec) if rec else (None, None)
    if props is None:
        raise LevelError(f"{where}: {path}: no schema for what it names")
    parts = field.split("/")
    if parts[0] not in props or "x-offset" not in props[parts[0]]:
        raise LevelError(f"{where}: {field}: not a field of {rec}")
    p = props[parts[0]]
    off = int(p["x-offset"], 16)
    if len(parts) == 2 and "$ref" in p:
        ref = p["$ref"].rsplit("/", 1)[-1]
        sub = schema(rec)["$defs"][ref]["properties"]
        if parts[1] not in sub:
            raise LevelError(f"{where}: {field}: not a field of {ref}")
        p = sub[parts[1]]
        off += int(p["x-offset"], 16)
    elif len(parts) != 1 or "$ref" in p:
        raise LevelError(f"{where}: {field}: a number field (or field/member of an embedded struct)")
    if link_of(p) is not None:
        raise LevelError(f"{where}: {field}: a link (patches set numbers)")
    return link, off, p


def record_struct(path, where):
    """The struct a base path names (None for raw nodes)."""
    segs = path.split("/")
    cur = "World" if segs[0] == "World" else "Trigger"
    item = None
    for seg in segs[2:]:
        if seg.isdigit():
            cur, item = item, None
        elif seg == "to":
            m2 = re.fullmatch(r"PTR32\((.+)\)", cur or "")
            cur = m2.group(1) if m2 else None
        elif re.fullmatch(r"[po][0-9a-fA-F]+", seg):
            cur = None
        else:
            props, _ = struct_of(cur) if cur else (None, None)
            if props is None or seg not in props:
                return None
            ln = link_of(props[seg])
            if ln is not None and "item-size" in ln:
                cur, item = None, ln.get("item", ln["target"])
            elif ln is not None:
                cur = ln["target"].replace("struct ", "")
            elif "$ref" in props[seg]:
                cur = props[seg]["$ref"].rsplit("/", 1)[-1]
    return cur


# ------------------------------------------------------------------------------------------
# records of the level's own

def scalar(T, p, v, where):
    """A number field's bytes: floats for f32, names for x-enum, lists for arrays."""
    base, dims = XS.parse_ctype(p["x-c-type"])
    code, size, flt = XS.PRIM[base]
    if dims:
        n = 1
        for d in dims:
            n *= d
        if not isinstance(v, list) or len(v) != n:
            raise LevelError(f"{where}: {n} values")
        return b"".join(scalar(T, {"x-c-type": base}, x, f"{where}/{i}") for i, x in enumerate(v))
    if flt:
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise LevelError(f"{where}: a number")
        return struct.pack(E + code, float(v))
    try:
        return XT.encode_scalar(T, p, v, E, where)
    except XT.TacError as ex:
        raise LevelError(str(ex))


class Compiler:
    def __init__(self, proj, base=None, skip_intro=False):
        self.proj = Path(proj)
        self.skip_intro = skip_intro
        self.T = XT.Tac()
        self.parts = []           # {"id", "kind", "hex", "links": [[off, LINK]]}
        self.part_ids = set()
        self.patches = []
        self.errors = []
        self.base = BaseWorld(base) if base else None
        self.trigger_types = {}

    def err(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def add_part(self, pid, kind, data, links):
        self.parts.append({"id": pid, "kind": kind, "hex": bytes(data).hex(), "links": dict(links or {})})
        self.part_ids.add(pid)

    def encode_record(self, sname, obj, where, pid):
        """bytes and {offset: link string} of one record of struct sname from its JSON."""
        props, size = struct_of(sname)
        buf = bytearray(size)
        links = {}
        for k, v in obj.items():
            if k in ("$schema", "format_version", "extends", "name", "description"):
                continue
            if k not in props or "x-offset" not in props[k]:
                raise LevelError(f"{where}/{k}: not a field of {sname}")
            p = props[k]
            off = int(p["x-offset"], 16)
            if link_of(p) is not None:
                if isinstance(v, dict) and k == "mode":
                    sub = f"{pid}/mode"
                    raw = bytes(XT.encode_scalar(self.T, {"x-c-type": "u8"}, v.get("mode", 0), E, f"{where}/mode") +
                                XT.encode_scalar(self.T, {"x-c-type": "u8"}, v.get("type", 0), E, f"{where}/mode"))
                    self.add_part(sub, "data", raw + bytes(2), [])
                    links[off] = sub
                elif v is not None:
                    if not isinstance(v, str):
                        raise LevelError(f"{where}/{k}: a link or null")
                    links[off] = v
            elif "$ref" in p:
                ref = p["$ref"].rsplit("/", 1)[-1]
                vp = schema(sname)["$defs"][ref]["properties"] if ref in schema(sname).get("$defs", {}) else None
                if vp is None:
                    raise LevelError(f"{where}/{k}: an embedded {ref} isn't supported")
                if isinstance(v, list):
                    v = dict(zip(("x", "y", "z", "w"), v))
                for kk, vv in v.items():
                    if kk not in vp:
                        raise LevelError(f"{where}/{k}/{kk}: not a field of {ref}")
                    o = off + int(vp[kk]["x-offset"], 16)
                    b = scalar(self.T, vp[kk], vv, f"{where}/{k}/{kk}")
                    buf[o:o + len(b)] = b
            else:
                b = scalar(self.T, p, v, f"{where}/{k}")
                buf[off:off + len(b)] = b
        return buf, links

    # -- the kinds ----------------------------------------------------------------------------

    def volume(self, name, v, where):
        props, _ = struct_of("WorldVolume")
        for k in v:
            if k not in props or "x-offset" not in props[k]:
                raise LevelError(f"{where}/{k}: not a field of WorldVolume")
        v = dict(v)
        v.setdefault("enabled", 1)
        v.setdefault("shape", "sphere")
        v.setdefault("type", "airbox")
        if "center" in v and isinstance(v["center"], list) and len(v["center"]) == 3:
            v["center"] = v["center"] + [0.0]
        r = v.get("radius")
        if r is not None and "radiusSq" not in v:
            v["radiusSq"] = float(r) * float(r)    # the test is distance squared < radiusSq
        if v.get("shape") == "sphere" and not v.get("radiusSq"):
            raise LevelError(f"{where}: a sphere needs a radius")
        pid = f"level://volumes/{name}"
        buf, links = self.encode_record("WorldVolume", v, where, pid)
        self.add_part(pid, "data", buf, links)

    def timer(self, name, v, where):
        v = dict(v)
        v.setdefault("active", 1)
        v.setdefault("mode", {"mode": 0x17, "type": 7})     # shown, stopped (as retail timers start)
        pid = f"level://timers/{name}/to"
        buf, links = self.encode_record("WorldTimer", v, where, pid)
        self.add_part(pid, "timer", buf, links)

    def variable(self, name, v, where):
        if isinstance(v, (int, float)):
            v = {"value": v}
        pid = f"level://variables/{name}/to"
        buf, links = self.encode_record("WorldVariable", dict(v), where, pid)
        self.add_part(pid, "variable", buf, links)

    def string(self, name, v, where):
        paras = v if isinstance(v, list) else [v]
        if not paras or not all(isinstance(x, str) for x in paras):
            raise LevelError(f"{where}: a text or a list of paragraphs")
        table = bytearray(4 * (len(paras) + 1))
        links = {}
        for i, text in enumerate(paras):
            raw = text.encode("latin-1", errors="replace")
            if len(raw) > 255:
                raise LevelError(f"{where}: paragraph {i} is over 255 characters")
            tid = f"level://strings/{name}/{i}"
            self.add_part(tid, "data", raw + b"\0" * (4 - len(raw) % 4), [])
            links[4 * i] = tid
        self.add_part(f"level://strings/{name}", "data", table, links)

    def trigger(self, name, doc, where):
        tid = f"level://triggers/{name}"
        doc = dict(doc)
        for k in ("$schema", "format_version", "id"):
            doc.pop(k, None)
        for lst in ("actions", "elseActions"):
            for a in doc.get(lst) or []:
                if isinstance(a, dict):
                    if a.get("count") == "always":
                        a["count"] = 0xFFFFFFFF
                    a.setdefault("count", 1)
                    if "delayInit" in a:
                        a.setdefault("delay", a["delayInit"])
        try:
            parts = XT.encode_trigger(self.T, doc, tid, E, where)
        except XT.TacError as ex:
            raise LevelError(str(ex))
        t = doc.get("type")
        self.trigger_types[t] = self.trigger_types.get(t, 0) + 1
        for i, (pid, data, links) in enumerate(parts):
            self.add_part(pid, "trigger" if i == 0 else "data", data, dict(links))

    # -- links --------------------------------------------------------------------------------

    def resolve(self, s, where, archive):
        if s in self.part_ids:
            return {"part": s}
        m = re.fullmatch(r"(level://[a-z]+/[^/]+(?:/to)?)(?:/o([0-9a-fA-F]+))?", s)
        if m and s.startswith("level://"):
            base, add = m.group(1), int(m.group(2), 16) if m.group(2) else 0
            kind = base.split("/")[2]
            if kind in ("timers", "variables") and not base.endswith("/to"):
                if base + "/to" not in self.part_ids:
                    raise LevelError(f"{where}: {s}: the level has no such {kind[:-1]}")
                return {"slot": base + "/to"}
            if base not in self.part_ids:
                raise LevelError(f"{where}: {s}: the level has nothing of that name")
            return {"part": base, "add": add}
        m = re.fullmatch(r"base://([A-Za-z0-9_]+)/world_0/(.+)", s)
        if m:
            if m.group(1).lower() != archive.lower():
                raise LevelError(f"{where}: {s}: not the level's world ({archive})")
            if self.base is not None:
                why = self.base.check(m.group(2))
                if why:
                    raise LevelError(f"{where}: {s}: {why}")
            return base_steps(m.group(2), where)
        raise LevelError(f"{where}: {s}: a link is base://{archive}/world_0/..., level://... or a part")

    # -- the whole --------------------------------------------------------------------------------

    def run(self):
        lv_path = self.proj / "level.jsonc"
        lv = read_jsonc(lv_path)
        where = "level.jsonc"
        for k in ("id", "name", "mode", "extends", "world"):
            if not isinstance(lv.get(k), str):
                self.err(where, f"{k}: missing (a string)")
        if self.errors:
            return None
        if lv.get("format_version") != 1:
            self.err(where, "format_version: 1")
        mode = lv["mode"]
        m = re.fullmatch(r"base://mcp/track/(\d+)", lv["extends"])
        if mode not in MODES:
            self.err(where, f"mode: one of {', '.join(MODES)}")
        elif not m or int(m.group(1)) not in MODES[mode][1]:
            self.err(where, f"extends: base://mcp/track/N with N a {mode} level's "
                            f"({MODES[mode][1].start}..{MODES[mode][1].stop - 1})")
        if not re.fullmatch(r"uid://[0-9a-z]{1,13}", lv["id"]):
            self.err(where, "id: uid:// and 1 to 13 of 0-9 a-z")
        world = lv["world"].replace("/", "\\")
        archive = world.rsplit("\\", 1)[-1].lower()
        keep = lv.get("baseTriggers", "all")
        if not (keep in ("all", "none") or (isinstance(keep, list) and all(isinstance(x, int) for x in keep))):
            self.err(where, "baseTriggers: \"all\", \"none\" or a list of trigger numbers")
            keep = "none"
        if self.base is not None and self.base.archive.lower() != archive:
            self.err(where, f"world: {world} isn't the base given ({self.base.archive})")
        # own records
        for kind, fn in (("volumes", self.volume), ("timers", self.timer), ("variables", self.variable),
                         ("strings", self.string)):
            sec = lv.get(kind) or {}
            if not isinstance(sec, dict):
                self.err(f"{where}/{kind}", "an object of named records")
                continue
            for name, v in sec.items():
                if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                    self.err(f"{where}/{kind}/{name}", "names are letters, digits, _ and -")
                    continue
                try:
                    fn(name, v, f"{where}/{kind}/{name}")
                except (LevelError, XT.TacError) as ex:
                    self.errors.append(str(ex))
        # triggers
        tdir = self.proj / "triggers"
        names = lv.get("triggers") or sorted(p.stem for p in tdir.glob("*.jsonc"))
        docs = []
        for name in names:
            f = tdir / f"{name}.jsonc"
            if not f.exists():
                self.err(where, f"triggers: {name}: no triggers/{name}.jsonc")
                continue
            try:
                docs.append((name, read_jsonc(f)))
            except LevelError as ex:
                self.errors.append(str(ex))
        if self.skip_intro:
            docs = skip_intro(docs)
        for name, d in docs:
            try:
                self.trigger(name, d, f"triggers/{name}.jsonc")
            except LevelError as ex:
                self.errors.append(str(ex))
        # links
        for part in self.parts:
            out = []
            for off, s in sorted(part["links"].items()):
                try:
                    out.append([int(off), self.resolve(s, f"{part['id']} +0x{int(off):x}", archive)])
                except LevelError as ex:
                    self.errors.append(str(ex))
            part["links"] = out
        # patches: {base path: {field: value}}
        for path, fields in (lv.get("patches") or {}).items():
            pw = f"{where}/patches/{path}"
            mm = re.fullmatch(r"base://([A-Za-z0-9_]+)/world_0/(.+)", path)
            if not mm or mm.group(1).lower() != archive:
                self.err(pw, f"patches name base://{archive}/world_0/... records")
                continue
            if not isinstance(fields, dict):
                self.err(pw, "an object of fields")
                continue
            for field, v in fields.items():
                try:
                    if self.base is not None:
                        why = self.base.check(mm.group(2))
                        if why:
                            raise LevelError(f"{pw}: {why}")
                    link, off, p = field_target(mm.group(2), field, pw)
                    b = scalar(self.T, p, v, f"{pw}/{field}")
                    self.patches.append({"target": link, "offset": off, "hex": b.hex(), "field": field,
                                         "path": mm.group(2)})
                except (LevelError, XT.TacError) as ex:
                    self.errors.append(str(ex))
        # limits
        kept = self.base.trigger_types(keep) if self.base is not None else {}
        enum = self.T.sch["properties"]["type"]
        names_of = {v: int(k) for k, v in (enum.get("x-enum") or {}).items()}
        for t in set(self.trigger_types) | set(kept):
            n = self.trigger_types.get(t, 0) + kept.get(t, 0)
            idx = names_of.get(t, t if isinstance(t, int) else None)
            if idx is not None and idx < len(TRIGGER_MAX) and n > TRIGGER_MAX[idx]:
                self.err("triggers", f"{n} {t} triggers: the game keeps {TRIGGER_MAX[idx]} at once")
        if self.errors:
            return None
        return {
            "format": FORMAT, "format_version": 1,
            "id": lv["id"], "name": lv["name"], "mode": mode, "extends": lv["extends"], "world": world,
            "baseTriggers": keep,
            "parts": self.parts,
            "patches": self.patches,
        }


class BaseWorld:
    """The base world as tools/xdl_graph.py extracts it (for checking links)."""

    def __init__(self, folder):
        self.dir = Path(folder)
        pj = read_jsonc(self.dir / "pack.jsonc")
        self.archive = pj["archive"]
        pre = f"base://{self.archive}/world_0/"
        self.ids = {n["id"][len(pre):] for n in pj["layout"]}
        self.counts = {}

    def count(self, node):
        if node not in self.counts:
            f = self.dir / f"{node}.jsonc"
            n = None
            if f.exists():
                d = read_jsonc(f)
                if isinstance(d.get("items"), list):
                    n = len(d["items"])
            self.counts[node] = n
        return self.counts[node]

    def check(self, path):
        """None if the path names something of the world, else why not."""
        if path in self.ids:
            return None
        segs = path.split("/")
        for i in range(len(segs) - 1, 0, -1):
            pre = "/".join(segs[:i])
            if pre in self.ids:
                rest = segs[i:]
                if rest and rest[0].isdigit():
                    n = self.count(pre)
                    if n is not None and int(rest[0]) >= n:
                        return f"{pre} has {n} entries"
                    return None
                return None
        return "the world has no such record"

    def trigger_types(self, keep):
        out = {}
        files = sorted(self.dir.glob("trigger/*.jsonc"), key=lambda p: int(p.stem) if p.stem.isdigit() else 0)
        for f in files:
            if not f.stem.isdigit():
                continue
            i = int(f.stem)
            if keep == "all" or (isinstance(keep, list) and i in keep):
                t = read_jsonc(f).get("type")
                out[t] = out.get(t, 0) + 1
        return out


# ------------------------------------------------------------------------------------------

def skip_intro(docs):
    """The test drive's quick start: the intro screen a level_start trigger plays is left out,
    the interface_end trigger waiting for it runs at the start instead, and the start's actions
    run without their delays (the camera, the countdown, control and the clock at once)."""
    import copy
    docs = [(n, copy.deepcopy(d)) for n, d in docs]
    waits = {}
    for n, d in docs:
        if d.get("type") == "interface_end" and isinstance(d.get("param"), dict):
            waits[d["param"].get("interface")] = n
    played = set()
    for n, d in docs:
        if d.get("type") != "level_start":
            continue
        keep = []
        for a in d.get("actions") or []:
            itf = (a.get("data") or {}).get("interface") if isinstance(a, dict) else None
            if isinstance(a, dict) and a.get("type") == "interface_play" and itf in waits:
                played.add(waits[itf])
                continue
            keep.append(a)
        d["actions"] = keep
    for n, d in docs:
        if n in played:
            d["type"] = "level_start"
            d.pop("param", None)
            d.pop("paramWords", None)
        if d.get("type") == "level_start":
            for a in d.get("actions") or []:
                if isinstance(a, dict):
                    a.pop("delayInit", None)
                    a.pop("delay", None)
    # a trigger left with no actions is dropped
    return [(n, d) for n, d in docs if d.get("actions") or d.get("type") != "level_start"]


TOOL_FILES = ("xdl_level.py", "xdl_tac.py", "xdl_struct.py")
SCHEMA_FILES = ("Trigger", "WorldVolume", "WorldTimer", "WorldVariable", "World", "TileCar", "WorldQuad",
                "QuadContents", "WorldGameData")


def source_hash(proj, options=""):
    """A hash of what a compile reads: the project's files, the tools and schemas, the options."""
    h = hashlib.sha256(options.encode())
    proj = Path(proj)
    files = [proj / "level.jsonc"] + sorted((proj / "triggers").glob("*.jsonc"))
    files += [HERE / f for f in TOOL_FILES] + [SCHEMAS / f"{n}.schema.json" for n in SCHEMA_FILES]
    for f in files:
        h.update(str(f.name).encode() + b"\0")
        try:
            h.update(f.read_bytes())
        except OSError:
            h.update(b"-")
        h.update(b"\0")
    return h.hexdigest()


def base_identity(base):
    """What the build depends on of the base: its extracted project's folder name carries the
    archive's hash (base_project); "" without one."""
    return Path(base).parent.name if base else ""


def compile_cached(proj, out, base=None, skip=False):
    """(doc, errors, cached): the level file at out, compiled again only when a source changed
    (the key: the project's files, the tools, the options, the base's identity). The test
    drive's products go through here; rebuilt packs (stage 21.4) are keyed the same way."""
    key = source_hash(proj, f"skip_intro={int(skip)};base={base_identity(base)}")
    try:
        old = json.loads(Path(out).read_text())
        if old.get("sourceHash") == key:
            return old, [], True
    except (OSError, ValueError):
        pass
    doc, errors = compile_project(proj, base, skip)
    if doc is not None:
        doc["sourceHash"] = key
        write_json(out, doc)
    return doc, errors, False


def error_records(errors):
    """Errors as {file, path, message}: where in the project each one is."""
    out = []
    for e in errors:
        m = re.match(r"((?:level\.jsonc|triggers/[A-Za-z0-9_-]+\.jsonc))(/[^:]*)?: (.*)", e, re.S)
        if m:
            out.append({"file": m.group(1), "path": (m.group(2) or "").lstrip("/"), "message": m.group(3)})
            continue
        m = re.match(r"level://(triggers|volumes|timers|variables|strings)/([A-Za-z0-9_-]+)(/[^ :]*)?(?: \+0x[0-9a-f]+)?: (.*)",
                     e, re.S)
        if m:
            kind, name, rest, msg = m.groups()
            if kind == "triggers":
                out.append({"file": f"triggers/{name}.jsonc", "path": (rest or "").lstrip("/"), "message": msg})
            else:
                out.append({"file": "level.jsonc", "path": f"{kind}/{name}" + (rest or ""), "message": msg})
            continue
        out.append({"file": "", "path": "", "message": e})
    return out


def compile_project(proj, base=None, skip=False):
    c = Compiler(proj, base, skip)
    try:
        doc = c.run()
    except LevelError as ex:
        c.errors.append(str(ex))
        doc = None
    return doc, c.errors


def write_json(path, doc):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    tmp = Path(str(path) + ".part")
    tmp.write_text(json.dumps(doc, indent=1) + "\n")
    os.replace(tmp, path)


def cache_dir(a):
    if getattr(a, "cache", None):
        return Path(a.cache)
    root = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(root) / "hwvx-sdl" / "levels"


def find_hwvx_x():
    for d in [HERE] + list(HERE.parents):
        p = d / "target" / "release" / "hwvx-x"
        if p.exists():
            return p
    return shutil.which("hwvx-x")


def base_project(world, data, cache):
    """The base world's extracted project, made once into the cache (game data: local only)."""
    world = world.replace("\\", "/")
    name = world.rsplit("/", 1)[-1].lower()
    src = None
    for p in Path(data).rglob("*"):
        if p.is_file() and p.stem.lower() == name and p.suffix.lower() in (".xgc", ".xpc", ".xps"):
            src = p
            break
    if src is None:
        raise LevelError(f"{world}: no world archive {name} under {data}")
    h = hashlib.sha256(src.read_bytes()).hexdigest()[:16]
    out = Path(cache) / f"{name}-{h}"
    proj = out / "project"
    if (proj / "pack.jsonc").exists():
        return proj
    exe = find_hwvx_x()
    if exe is None:
        raise LevelError("hwvx-x not found (target/release/hwvx-x)")
    ext = out / "extract"
    if ext.exists():
        shutil.rmtree(ext)
    r = subprocess.run([str(exe), "extract", str(src), str(ext)], capture_output=True, text=True)
    if r.returncode:
        raise LevelError("hwvx-x extract: " + r.stderr.strip()[-300:])
    packs = [m.parent for m in ext.rglob("pack.json") if "world" in m.parent.name]
    if not packs:
        raise LevelError(f"{src}: no world pack in it")
    import xdl_graph as GR
    if proj.exists():
        shutil.rmtree(proj)
    GR.Extractor(GR.Source(packs[0]), proj, XS.Schemas()).run()
    return proj


def report(errors):
    for e in errors:
        print("error: " + e, file=sys.stderr)
    return 1 if errors else 0


def base_arg(a):
    if a.base:
        return a.base
    if a.data:
        lv = read_jsonc(Path(a.project) / "level.jsonc")
        return base_project(lv["world"], a.data, cache_dir(a))
    return None


def cmd_compile(a):
    import time
    t0 = time.perf_counter()
    out = a.out or str(Path(a.project) / "build" / "level.json")
    doc, errors, cached = compile_cached(a.project, out, base_arg(a), a.skip_intro)
    ms = round((time.perf_counter() - t0) * 1000, 1)
    if a.json:
        print(json.dumps({"ok": not errors, "out": out, "cached": cached, "ms": ms,
                          "errors": error_records(errors)}))
        return 1 if errors else 0
    if errors:
        return report(errors)
    print(f"{out}: {len(doc['parts'])} parts, {len(doc['patches'])} patches"
          f"{' (up to date)' if cached else ''}, {ms} ms")
    return 0


START_MODE = {"challenge": "challenge", "mission": "mission", "adventure": "mission", "battle": "battle",
              "drag": "drag", "joyride": "joyride"}


def cmd_testdrive(a):
    """The editor's test drive: the project compiled (if it changed) into SCRATCH/mods/editor-test,
    a mod whose track is the level, and what to start the game with (JSON on stdout)."""
    import time
    t0 = time.perf_counter()
    proj, scratch = Path(a.project), Path(a.scratch)
    mod_dir = scratch / "mods" / "editor-test"
    res = {"ok": False, "errors": []}
    try:
        lv = read_jsonc(proj / "level.jsonc")
        for k in ("id", "name", "mode", "extends", "world"):
            if not isinstance(lv.get(k), str):
                raise LevelError(f"level.jsonc/{k}: missing (a string)")
        doc, errors, cached = compile_cached(proj, mod_dir / "level.json", base_arg(a), a.skip_intro)
    except LevelError as ex:
        doc, errors, cached = None, [str(ex)], False
    res["errors"] = error_records(errors)
    if doc is not None and not errors:
        track = {"extends": lv["extends"], "name": lv["name"], "unlocked": True, "world": doc["world"],
                 "level": "level.json"}
        mod = {"$schema": "../../schemas/Mod.schema.json", "format_version": 1, "id": "uid://editortest",
               "name": "Editor test drive", "version": "1.0.0", "description": f"{lv['name']}, on test",
               "tracks": {lv["id"]: track}}
        text = json.dumps(mod, indent=2) + "\n"
        mj = mod_dir / "mod.jsonc"
        if not mj.exists() or mj.read_text() != text:
            mj.write_text(text)
        mode = START_MODE.get(lv["mode"], lv["mode"])
        start = f"race:mode={mode},world={lv['id']}"
        if a.car is not None:
            start += f",car={a.car}"
        # what the build made: the level file now; later also the level's own packs
        products = [{"kind": "level", "path": str(mod_dir / "level.json"), "cached": cached}]
        res.update(ok=True, cached=all(p["cached"] for p in products), start=start, mods=str(scratch / "mods"),
                   level=str(mod_dir / "level.json"), products=products)
    res["ms"] = round((time.perf_counter() - t0) * 1000, 1)
    print(json.dumps(res))
    return 0 if res["ok"] else 1


def cmd_check(a):
    doc, errors = compile_project(a.project, base_arg(a))
    if not errors:
        print(f"{a.project}: ok")
    return report(errors)


def cmd_base(a):
    print(base_project(a.world, a.data, cache_dir(a)))
    return 0


def make_mod(proj, out, doc, extra=None):
    """A mod folder: mod.jsonc with the level's track, the level file, the project's sources."""
    proj, out = Path(proj), Path(out)
    lv = read_jsonc(proj / "level.jsonc")
    out.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^a-z0-9]+", "-", lv["id"][len("uid://"):])
    write_json(out / "level" / f"{name}.json", doc)
    src = out / "level" / f"{name}-src"
    if src.exists():
        shutil.rmtree(src)
    src.mkdir(parents=True)
    shutil.copy2(proj / "level.jsonc", src / "level.jsonc")
    if (proj / "triggers").is_dir():
        shutil.copytree(proj / "triggers", src / "triggers")
    track = {"extends": lv["extends"], "name": lv["name"], "unlocked": bool(lv.get("unlocked", True)),
             "world": doc["world"], "level": f"level/{name}.json"}
    if lv.get("thumbnail"):
        track["thumbnail"] = lv["thumbnail"]
    mod = {
        "$schema": "../../schemas/Mod.schema.json", "format_version": 1,
        "id": lv.get("modId", "uid://" + hashlib.sha1(lv["id"].encode()).hexdigest()[:13]),
        "name": lv["name"], "version": lv.get("version", "1.0.0"), "author": lv.get("author", ""),
        "description": lv.get("description", f"A {lv['mode']} level: {lv['name']}"),
        "tracks": {lv["id"]: track},
    }
    if extra:
        mod.update(extra)
    (out / "mod.jsonc").write_text("// made by tools/xdl_level.py mod\n" + json.dumps(mod, indent=2) + "\n")
    return out


def cmd_mod(a):
    doc, errors = compile_project(a.project, base_arg(a))
    if errors:
        return report(errors)
    out = make_mod(a.project, a.out, doc)
    print(f"{out}: mod.jsonc and the level")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("compile", "check", "mod", "testdrive"):
        p = sub.add_parser(name)
        p.add_argument("project")
        if name == "mod":
            p.add_argument("out")
        if name == "testdrive":
            p.add_argument("scratch", help="the editor's scratch folder (its mods/, never the player's)")
            p.add_argument("--car", type=int)
        if name in ("compile", "testdrive"):
            p.add_argument("--skip-intro", action="store_true", help="the quick start (test drives)")
        if name == "compile":
            p.add_argument("-o", "--out")
            p.add_argument("--json", action="store_true", help="the result and errors as one JSON line")
        p.add_argument("--base", help="the base world's extracted project (xdl_level.py base)")
        p.add_argument("--data", help="the game's data folder: the base world is extracted from it (cached)")
        p.add_argument("--cache")
    p = sub.add_parser("base")
    p.add_argument("world")
    p.add_argument("--data", required=True)
    p.add_argument("--cache")
    a = ap.parse_args()
    try:
        return {"compile": cmd_compile, "check": cmd_check, "base": cmd_base, "mod": cmd_mod,
                "testdrive": cmd_testdrive}[a.cmd](a)
    except LevelError as ex:
        return report([str(ex)])


if __name__ == "__main__":
    sys.exit(main())
