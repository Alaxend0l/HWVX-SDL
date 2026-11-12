#!/usr/bin/env python3
"""Widescreen HUD check: every interface of every pack, by side (docs-pending/hud-scan.md).

Runs the game in viewer mode on a release's data, loads each pack that can hold interfaces
(interface, world, share, item, car, link), and asks for its interfaces (the viewer's
"interfaces" command, src/port/viewer_hudscan.c: headers found in the pack, each object's
kind, role and layout position with Port_HudLayoutX's maths). Child interfaces get their
parent placeholder's x added, as at run time. Each interface is then classified as
Port_HudShift sees it (InterfaceManager.c; kind-6 placeholders left out):

  left      all objects left of 192: an interface of no player moves left as a whole
  right     all right of 448: moves right
  centre    all within 192..448: stays
  mixed     some on a side and others not (or on the other side): with no owner it stays,
            so its side objects would sit in the middle on a wide screen

    hud_scan.py --build build --data DIR [--data DIR...] [--out build/hudscan] [--json out.json]
               [--types interface,world,...] [--only mixed]

--textures prints, per pack, the pack's own textures its interfaces show (what a mixed build
should keep from the base: Port_IfaceTextures, src/game/Port_IfaceScan.c).

Prints one line per interface (release, pack, header offset/id, class, objects as
kind/role@x,y "text"); --json writes everything. The game is force-killed after each release.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from viewer_client import Viewer, ViewerError  # noqa: E402

LEFT_X, RIGHT_X = 192.0, 448.0
TEXTURES = {}   # "release pack": the pack's own textures its interfaces show (Port_IfaceTextures)
TYPES = ["interface", "world", "share", "item", "car", "link"]


def side(x):
    return "L" if x < LEFT_X else "R" if x > RIGHT_X else "C"


def classify(objs):
    xs = [o["ax"] for o in objs if o["kind"] != 6]
    if not xs:
        return "empty"
    lo, hi = min(xs), max(xs)
    if hi < LEFT_X:
        return "left"
    if lo > RIGHT_X:
        return "right"
    if lo >= LEFT_X and hi <= RIGHT_X:
        return "centre"
    return "mixed"


def scan_release(build, data, out, types):
    v = Viewer.start(build, data, out / Path(data).name)
    rel = v.hello.get("release", "?")
    found = []
    try:
        packs = v.call("packs")["packs"]
        for p in packs:
            if p["type"] not in types:
                continue
            try:
                v.call("load", archive=p["archive"], type=p["type"], id=p["id"], timeout=120)
                r = v.call("interfaces", pack=p["pack"], timeout=120)
            except ViewerError as e:
                print(f"# {rel} {p['pack']}: {e}", file=sys.stderr)
                continue
            TEXTURES[f"{p.get('release', rel)} {p['pack']}"] = r.get("interfaceTextures", [])
            for h in r.get("interfaces", []):
                h["release"], h["pack"] = p.get("release", rel), p["pack"]
                found.append(h)
            try:
                v.call("unload", pack=p["pack"])
            except ViewerError:
                pass
    finally:
        v.close()
    return found


def owners(headers):
    """Who creates each header: "player" (a car's HUD), "none" (level scripts' InterfacePlay,
    the pause menu, interface packs' roots, event 0x1F), passed on to children and to the
    headers their events go to (with the same owner, as InterfaceManager_Create is called)."""
    by_key = {(h["release"], h["pack"], h["off"]): h for h in headers}
    own = {k: set() for k in by_key}
    work = []
    for k, h in by_key.items():
        r = set(h.get("roots", []))
        if "hud" in r:
            own[k].add("player")
        if r & {"tac", "pause", "pack"}:
            own[k].add("none")
        if own[k]:
            work.append(k)
    while work:
        k = work.pop()
        h = by_key[k]
        edges = [(o["child"], None) for o in h["objects"] if o["kind"] == 6 and o.get("child", -1) >= 0]
        edges += [(e["target"], "none" if e["type"] == 0x1F else None) for e in h.get("events", [])]
        for t, forced in edges:
            tk = (k[0], k[1], t)
            if tk not in own:
                continue
            add = {forced} if forced else own[k]
            if not add <= own[tk]:
                own[tk] |= add
                work.append(tk)
    for k, h in by_key.items():
        h["owner"] = "+".join(sorted(own[k])) or "?"


def resolve(headers):
    """Absolute layout x/y per object, a child header once per parent placeholder."""
    by_key = {(h["release"], h["pack"], h["off"]): h for h in headers}
    parents = {}
    for h in headers:
        for i, o in enumerate(h["objects"]):
            c = o.get("child", -1)
            if o["kind"] == 6 and c is not None and c >= 0:
                parents.setdefault((h["release"], h["pack"], c), []).append((h, i))

    def placeholder_xy(h, i, depth):
        o = h["objects"][i]
        ctx = contexts(h, depth + 1)
        dx, dy = ctx[0][1] if ctx else (0.0, 0.0)
        return o["x"] + dx, o["y"] + dy

    def contexts(h, depth=0):
        key = (h["release"], h["pack"], h["off"])
        ps = parents.get(key, [])
        if not ps or depth > 8:
            return [(None, (0.0, 0.0))]
        return [((ph, pi), placeholder_xy(ph, pi, depth)) for ph, pi in ps]

    owners(headers)
    rows = []
    for h in headers:
        for parent, (dx, dy) in contexts(h):
            objs = [dict(o, ax=o["x"] + dx, ay=o["y"] + dy) for o in h["objects"]]
            rows.append(dict(h, objects=objs, parent=None if parent is None else
                             {"off": parent[0]["off"], "id": parent[0]["id"], "object": parent[1]},
                             cls=classify(objs)))
    return rows


def describe(r):
    parts = []
    for i, o in enumerate(r["objects"]):
        s = f"{i}:k{o['kind']}/r{o['role']}@{o['ax']:.0f},{o['ay']:.0f}{side(o['ax']) if o['kind'] != 6 else '-'}"
        if "kxmin" in o:
            s += f"[{o['kxmin']:.0f}..{o['kxmax']:.0f}]"
        if o.get("flags"):
            s += f"!{o['flags']}"
        if o.get("text"):
            s += f" \"{o['text'][:24]}\""
        parts.append(s)
    par = f" child-of {r['parent']['off']:#x}[{r['parent']['object']}]" if r["parent"] else ""
    roots = ",".join(r.get("roots", []))
    return (f"{r['release']} {r['pack']} {r['off']:#x} id{r['id']} owner={r['owner']}"
            f"{' roots=' + roots if roots else ''} {r['cls']}{par}: " + "  ".join(parts))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--data", action="append", required=True)
    ap.add_argument("--out", default="build/hudscan")
    ap.add_argument("--json")
    ap.add_argument("--types", default=",".join(TYPES))
    ap.add_argument("--only", help="classes to print, e.g. mixed,left")
    ap.add_argument("--owner", help="owners to print, e.g. none (also matches none+player)")
    ap.add_argument("--textures", action="store_true",
                    help="print each pack's interface textures (texture/N indices) instead")
    ap.add_argument("--from-json", help="classify a saved --json instead of running the game")
    a = ap.parse_args()
    if a.from_json:
        saved = json.load(open(a.from_json))
        headers = saved["headers"]
        TEXTURES.update(saved.get("textures", {}))
    else:
        headers = []
        for d in a.data:
            headers += scan_release(a.build, d, Path(a.out).resolve(), a.types.split(","))
    rows = resolve(headers)
    if a.json:
        Path(a.json).write_text(json.dumps({"headers": headers, "rows": rows, "textures": TEXTURES}, indent=1))
    if a.textures:
        for k, v in sorted(TEXTURES.items()):
            if v:
                print(f"{k}: {' '.join(map(str, v))}")
        return
    for r in rows:
        if (a.only is None or r["cls"] in a.only.split(",")) and \
                (a.owner is None or any(x in r["owner"].split("+") for x in a.owner.split(","))):
            print(describe(r))


if __name__ == "__main__":
    main()
