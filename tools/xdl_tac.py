#!/usr/bin/env python3
"""TAC triggers (Trigger.h) as JSON: the records and their tagged-union parameter lists.

Driven by schemas/Trigger.schema.json (tools/gen_schemas.py from Trigger.h): the Trigger and
TriggerAction records by their properties, `x-enum` / `x-flags` fields by name, and each
parameter list by the layout its kind picks (`x-variants`, from the TAC_PARAMS_* macros).

A list is decoded from words; every item names what the game reads (see Trigger.h):

    {"volumes": ["base://ch1_l2/world_0/World/0/volumes/12"], "cars": [...]}

Decoding is strict: a number on a pointer slot, a link on a plain word, a count past the node,
an unknown operand type all fail, and the caller keeps the bytes raw instead. Bytes after the
list (inside its node) are kept as `"tail"` (hex). An encoded list gives the same bytes back.

Used by xdl_graph.py (world packs: one file per trigger, its conditions, actions and every
parameter list inline). `python3 tools/xdl_tac.py SURVEY_ROOT` decodes and re-encodes every
retail trigger and reports what didn't come back.
"""

import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import xdl_struct as XS  # noqa: E402

SCHEMA = HERE.parent / "schemas" / "Trigger.schema.json"


class TacError(Exception):
    pass


def f32_of(e, w):
    return struct.unpack(e + "f", struct.pack(e + "I", w))[0]


class Tac:
    """The trigger schema, digested."""

    def __init__(self, path=SCHEMA):
        self.path = Path(path)
        self.sch = json.loads(self.path.read_text())
        self.structs = {"Trigger": self.sch, "TriggerAction": self.sch["$defs"]["TriggerAction"]}
        self.variants = self.sch["x-variants"]
        self.records = self.sch.get("x-records", {})
        self.tokens = {int(k): v for k, v in self.sch["x-tokens"].items()}
        self.token_val = {v: k for k, v in self.tokens.items()}
        self.operands = {int(k): v for k, v in self.sch["x-operands"].items()}
        self.operand_val = {v: k for k, v in self.operands.items()}
        self.size = {k: int(s["x-size"], 16) for k, s in self.structs.items()}

    def fields(self, sname):
        return [(k, p) for k, p in self.structs[sname]["properties"].items() if "x-offset" in p]

    def layout(self, enum, kind):
        """Items of the parameter list of `kind` (a name or a number) of `enum`."""
        v = self.variants[enum]
        if isinstance(kind, str):
            return v.get(kind)
        return None

    # -- names ---------------------------------------------------------------------------
    @staticmethod
    def name_of(p, v):
        names = p.get("x-enum")
        if names is not None:
            return names.get(str(v), v)
        return v

    @staticmethod
    def value_of(p, v, path):
        names = p.get("x-enum")
        if isinstance(v, str):
            for k, n in (names or {}).items():
                if n == v:
                    return int(k)
            raise TacError(f"{path}: unknown name {v!r}")
        if not isinstance(v, int) or isinstance(v, bool):
            raise TacError(f"{path}: a name or an integer")
        return v

    def token(self, v):
        return self.tokens.get(v, v)

    def token_value(self, v, path):
        if isinstance(v, str):
            if v not in self.token_val:
                raise TacError(f"{path}: unknown keyword {v!r} (Trigger.h TacToken)")
            return self.token_val[v]
        if not isinstance(v, int) or isinstance(v, bool):
            raise TacError(f"{path}: a keyword or an integer")
        return v


# ------------------------------------------------------------------------------------------
# reading

class Reader:
    """Words of a pack: u32(o), slot(o) -> link text or None (a pointer slot), is_slot(o),
    and `e` (the byte order). xdl_graph supplies one over its Source and link names."""

    def __init__(self, blob, e, slots):
        self.blob, self.e, self.slots = blob, e, slots      # slots: {offset: link text}

    def u32(self, o):
        return struct.unpack_from(self.e + "I", self.blob, o)[0]


def decode_params(T, r, off, end, items, path):
    """(json object, length) of a parameter list at off (bytes up to `end` belong to it)."""
    o = off
    out = {}

    def need(name):
        if o + 4 > end:
            raise TacError(f"{path}/{name}: past the end of the list's node")

    def plain(name):
        nonlocal o
        need(name)
        if o in r.slots:
            raise TacError(f"{path}/{name}: a number on a pointer slot")
        w = r.u32(o)
        o += 4
        return w

    def link(name):
        nonlocal o
        need(name)
        if o in r.slots:
            v = r.slots[o]
        else:
            if r.u32(o) != 0:
                raise TacError(f"{path}/{name}: a link slot holds 0x{r.u32(o):X} without relocation")
            v = None
        o += 4
        return v

    def any_(name):
        nonlocal o
        need(name)
        if o in r.slots:
            return link(name)
        return plain(name)

    def items_of(its, sub):
        obj = {}
        for it in its:
            k, name = it[0], it[1]
            if k == "u32":
                obj[name] = plain(name)
            elif k == "s32":
                w = plain(name)
                obj[name] = w - (1 << 32) if w & 0x80000000 else w
            elif k == "f32":
                v = f32_of(r.e, plain(name))
                obj[name] = XS.Num(v, XS.f32_text(v))
            elif k == "token":
                obj[name] = T.token(plain(name))
            elif k == "any":
                obj[name] = any_(name)
            elif k == "link":
                obj[name] = link(name)
            elif k == "list":
                n = plain(name + "/count")
                if o + 4 * n > end:
                    raise TacError(f"{sub}/{name}: count {n} runs past the node")
                obj[name] = [link(f"{name}/{i}") for i in range(n)]
            elif k == "scratch":
                obj[name] = [plain(f"{name}/{i}") for i in range(len(obj[it[2]]))]
            elif k == "operand":
                t = plain(name + "/type")
                kind = T.operands.get(t)
                if kind == "constant":
                    v = f32_of(r.e, plain(name))
                    obj[name] = {"const": XS.Num(v, XS.f32_text(v))}
                elif kind == "variable":
                    obj[name] = {"variable": link(name)}
                elif kind == "code_variable":
                    var = plain(name + "/code")
                    obj[name] = {"code": var, "arg": any_(name + "/arg")}
                else:
                    raise TacError(f"{sub}/{name}: operand type {t}")
            elif k == "codevar":
                var = plain(name)
                if var < 100:
                    n = plain(name + "/count")
                    if o + 4 * n > end:
                        raise TacError(f"{sub}/{name}: count {n} runs past the node")
                    obj[name] = {"var": var, "cars": [link(f"{name}/cars/{i}") for i in range(n)]}
                else:
                    obj[name] = {"var": var, "arg": any_(name + "/arg")}
            elif k == "string":
                b = r.blob[o:end]
                z = b.find(b"\0")
                if z < 0:
                    raise TacError(f"{sub}/{name}: no end to the string in its node")
                ln = (z + 4) & ~3
                if any(x in r.slots for x in range(o, o + ln, 4)):
                    raise TacError(f"{sub}/{name}: a string over a pointer slot")
                pad = r.blob[o + z + 1:o + ln]
                if any(pad) or o + ln > end:
                    raise TacError(f"{sub}/{name}: string padding not zero")
                try:
                    obj[name] = b[:z].decode("ascii")
                except UnicodeDecodeError:
                    raise TacError(f"{sub}/{name}: not ASCII")
                o_set(o + ln)
            elif k == "table":
                n = plain(name + "/count")
                rec = T.records[it[2]]
                if o + 4 * len(rec) * n > end:
                    raise TacError(f"{sub}/{name}: {n} records run past the node")
                obj[name] = [items_of(rec, f"{sub}/{name}/{i}") for i in range(n)]
            elif k == "words":
                ws = []
                while o + 4 <= end:
                    ws.append(any_(name))
                obj[name] = ws
            else:
                raise TacError(f"{sub}: item {k}")
        return obj

    def o_set(v):
        nonlocal o
        o = v

    out = items_of(items, path)
    tail = r.blob[o:end]
    if tail:
        if any(x in r.slots for x in range(o - o % 4, end, 4) if x >= o):
            # links in what follows: the tail as words
            if (end - o) % 4 or o % 4:
                raise TacError(f"{path}: a pointer slot in an unaligned tail")
            out["tail"] = []
            while o < end:
                out["tail"].append(any_("tail"))
        else:
            out["tail"] = tail.hex()
    return out, end - off


def encode_params(T, obj, items, e, path):
    """(bytes, {offset: link}) of a parameter list."""
    out = bytearray()
    links = {}
    if not isinstance(obj, dict):
        raise TacError(f"{path}: the parameters are an object")

    def word(v):
        out.extend(struct.pack(e + "I", v & 0xFFFFFFFF))

    def num(v, name):
        if isinstance(v, bool) or not isinstance(v, (int, float)) or (isinstance(v, float) and v != int(v)):
            raise TacError(f"{path}/{name}: an integer")
        v = int(v)
        if not -(1 << 31) <= v < (1 << 32):
            raise TacError(f"{path}/{name}: {v} out of range")
        word(v)

    def flt(v, name):
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise TacError(f"{path}/{name}: a number")
        out.extend(struct.pack(e + "f", float(v)))

    def link(v, name):
        if v is None:
            word(0)
            return
        if not isinstance(v, str):
            raise TacError(f"{path}/{name}: a link (string) or null")
        links[len(out)] = v
        word(0)

    def any_(v, name):
        if isinstance(v, str) or v is None:
            link(v, name)
        else:
            num(v, name)

    def lst(v, name):
        if not isinstance(v, list):
            raise TacError(f"{path}/{name}: a list")
        return v

    def items_to(its, o, sub):
        if not isinstance(o, dict):
            raise TacError(f"{sub}: an object")
        known = {it[1] for it in its} | ({"tail"} if sub == path else set())
        for k in o:
            if k not in known:
                raise TacError(f"{sub}/{k}: not a parameter of this kind (has: {', '.join(sorted(known))})")
        for it in its:
            k, name = it[0], it[1]
            if name not in o:
                raise TacError(f"{sub}/{name}: missing")
            v = o[name]
            if k in ("u32", "s32"):
                num(v, name)
            elif k == "f32":
                flt(v, name)
            elif k == "token":
                word(T.token_value(v, f"{sub}/{name}"))
            elif k == "any":
                any_(v, name)
            elif k == "link":
                link(v, name)
            elif k == "list":
                v = lst(v, name)
                word(len(v))
                for i, x in enumerate(v):
                    link(x, f"{name}/{i}")
            elif k == "scratch":
                v = lst(v, name)
                if len(v) != len(o[it[2]]):
                    raise TacError(f"{sub}/{name}: one word per {it[2]} ({len(o[it[2]])})")
                for i, x in enumerate(v):
                    num(x, f"{name}/{i}")
            elif k == "operand":
                if not isinstance(v, dict) or len(v) not in (1, 2):
                    raise TacError(f"{sub}/{name}: {{\"const\": x}}, {{\"variable\": link}} or {{\"code\": id, \"arg\": w}}")
                if "const" in v:
                    word(T.operand_val["constant"])
                    flt(v["const"], name)
                elif "variable" in v:
                    word(T.operand_val["variable"])
                    link(v["variable"], name)
                elif "code" in v and "arg" in v:
                    word(T.operand_val["code_variable"])
                    num(v["code"], name)
                    any_(v["arg"], name)
                else:
                    raise TacError(f"{sub}/{name}: unknown operand {sorted(v)}")
            elif k == "codevar":
                if not isinstance(v, dict) or "var" not in v:
                    raise TacError(f"{sub}/{name}: {{\"var\": id, \"cars\": [...]}} or {{\"var\": id, \"arg\": w}}")
                num(v["var"], name)
                if v["var"] < 100:
                    if set(v) != {"var", "cars"}:
                        raise TacError(f"{sub}/{name}: a code variable below 100 takes \"cars\"")
                    cars = lst(v["cars"], name)
                    word(len(cars))
                    for i, x in enumerate(cars):
                        link(x, f"{name}/cars/{i}")
                else:
                    if set(v) != {"var", "arg"}:
                        raise TacError(f"{sub}/{name}: a code variable from 100 takes \"arg\"")
                    any_(v["arg"], name)
            elif k == "string":
                if not isinstance(v, str):
                    raise TacError(f"{sub}/{name}: a string")
                b = v.encode("ascii") + b"\0"
                out.extend(b + bytes(-len(b) % 4))
            elif k == "table":
                v = lst(v, name)
                word(len(v))
                for i, x in enumerate(v):
                    items_to(T.records[it[2]], x, f"{sub}/{name}/{i}")
            elif k == "words":
                for i, x in enumerate(lst(v, name)):
                    any_(x, f"{name}/{i}")
            else:
                raise TacError(f"{sub}: item {k}")

    items_to(items, obj, path)
    if isinstance(obj.get("tail"), list):
        for i, x in enumerate(obj["tail"]):
            any_(x, f"tail/{i}")
    elif "tail" in obj:
        try:
            out.extend(bytes.fromhex(obj["tail"]))
        except (TypeError, ValueError):
            raise TacError(f"{path}/tail: hex bytes, or words")
    return bytes(out), links


# ------------------------------------------------------------------------------------------
# records (Trigger, TriggerAction): their scalar fields

def decode_scalar(T, r, p, off, path):
    """A record's non-link field, by its C type: names for x-enum / x-flags."""
    base, dims = XS.parse_ctype(p["x-c-type"])
    code, size, flt = XS.PRIM[base]
    n = 1
    for d in dims:
        n *= d
    vals = []
    for i in range(n):
        o = off + i * size
        if any(x in r.slots for x in range(o - o % 4, o + size, 4) if x + 4 > o):
            raise TacError(f"{path}: a number on a pointer slot")
        vals.append(struct.unpack_from(r.e + code, r.blob, o)[0])
    if dims:
        return vals
    v = vals[0]
    if "x-flags" in p:
        names = []
        rest = v
        for bit, nm in p["x-flags"].items():
            b = int(bit)
            if v & b:
                names.append(nm)
                rest &= ~b
        if rest:
            names.append(rest)
        return names
    return T.name_of(p, v)


def encode_scalar(T, p, v, e, path):
    base, dims = XS.parse_ctype(p["x-c-type"])
    code, size, flt = XS.PRIM[base]
    if dims:
        n = 1
        for d in dims:
            n *= d
        if not isinstance(v, list) or len(v) != n:
            raise TacError(f"{path}: {n} values")
        return b"".join(encode_scalar(T, {"x-c-type": base}, x, e, f"{path}/{i}") for i, x in enumerate(v))
    if "x-flags" in p:
        if not isinstance(v, list):
            raise TacError(f"{path}: a list of flag names")
        w = 0
        for x in v:
            if isinstance(x, str):
                b = [int(k) for k, nm in p["x-flags"].items() if nm == x]
                if not b:
                    raise TacError(f"{path}: unknown flag {x!r}")
                w |= b[0]
            elif isinstance(x, int) and not isinstance(x, bool):
                w |= x
            else:
                raise TacError(f"{path}: flag names or integers")
        v = w
    else:
        v = T.value_of(p, v, path)
    try:
        return struct.pack(e + code, v)
    except struct.error as ex:
        raise TacError(f"{path}: {v}: {ex}")


# ------------------------------------------------------------------------------------------
# a trigger with its parts, as one JSON object (xdl_graph.py's trigger files)

STEPS = {"Trigger": {0x4: "param", 0xC: "conditions", 0x14: "actions", 0x18: "elseActions"},
         "TriggerAction": {0x4: "data"}}
ARRAY_OF = {"conditions": "Trigger", "actions": "TriggerAction", "elseActions": "TriggerAction"}


def step_of(role, count, rel):
    """The id step of a pointer slot `rel` bytes into a TAC node: a trigger record ("param",
    "actions"...) or an array of `count` records ("3/data")."""
    if role == "Trigger":
        return STEPS["Trigger"].get(rel)
    size = 0x2C if role == "conditions" else 0x18
    rec = "Trigger" if role == "conditions" else "TriggerAction"
    st = STEPS[rec].get(rel % size)
    return f"{rel // size}/{st}" if st and rel // size < count else None


def tac_shapes(u32, ptr, header, n_blob):
    """{offset: (role, count)}: the trigger records of the pack's table, and the condition and
    action arrays they point to (conditions nest: their records are walked too)."""
    out = {}

    def trig(t, depth):
        out.setdefault(t, ("Trigger", None))
        for lo, no, role in ((0xC, 0x10, "conditions"), (0x14, 0x1C, "actions"), (0x18, 0x20, "elseActions")):
            a = ptr.get(t + lo)
            n = u32(t + no)
            if a is None or n == 0 or n > 0x1000:
                continue
            size = 0x2C if role == "conditions" else 0x18
            if a + n * size > n_blob:
                continue
            out.setdefault(a, (role, n))
            if role == "conditions" and depth < 4:
                for j in range(n):
                    trig(a + j * size, depth + 1)
    n = header.get("num_triggers") or 0
    for i in range(n):
        o = header["trigger_offset"] + i * 0x2C
        if o + 0x2C <= n_blob:
            trig(o, 0)
    return out


class Extract:
    """One trigger table record and the nodes it owns, as JSON. `ex` is xdl_graph's Extractor:
    node_at, link_to, by_start, src. A part is inlined when it is a node of its own whose
    canonical parent is the part pointing to it, and decodes; otherwise it stays a link."""

    def __init__(self, T, ex):
        self.T, self.ex = T, ex
        self.parts = []                     # nodes now written in the trigger's file
        self.pads = {}                      # node start -> zero bytes after its JSON

    def reader(self, nd):
        src = self.ex.src
        slots = {o: self.ex.link_to(t, elem=True) for o, t in src.ptr.items() if nd.start <= o < nd.end}
        for o, v in src.rel["ref"].items():
            if nd.start <= o < nd.end:
                raise TacError("a geometry ref in a parameter list")
        for k in ("texture", "sound", "shader"):
            if any(nd.start <= o < nd.end for o in src.rel[k]):
                raise TacError(f"a {k} slot in a TAC node")
        return Reader(src.blob, src.e, slots)

    def own(self, parent, off):
        """The node at the pointer in slot `off`, if it's `parent`'s to inline."""
        t = self.ex.src.ptr.get(off)
        if t is None:
            return None
        nd = self.ex.by_start.get(t)
        if (nd is None or nd.parent is not parent or nd.kind != "raw" or nd.fixed
                or (nd.label in self.ex.kinds and nd.label not in ("Trigger", "Action[](TAC)"))):
            return None
        return nd

    def trigger(self, nd):
        """The JSON of a trigger table record node."""
        self.parts.append(nd)
        return self.record("Trigger", nd, nd.start, nd)

    def zero_tail(self, nd, used):
        tail = self.ex.blob[nd.start + used:nd.end]
        if any(tail):
            return False
        if any(nd.start + used <= o < nd.end for o in self.ex.src.ptr):
            return False
        if nd.end - nd.start > used:
            self.pads[nd.start] = nd.end - nd.start - used
        return True

    def record(self, sname, holder, off, node):
        """A Trigger / TriggerAction record at `off` (inside `holder`, the node whose children
        its parts are)."""
        T = self.T
        r = self.reader(node)
        out = {}
        props = T.fields(sname)
        inline_counts = set()
        for key, p in props:
            o = off + int(p["x-offset"], 16)
            if "x-params" in p:
                kind = out["type"]
                items = T.variants[p["x-params"]["enum"]].get(kind) if isinstance(kind, str) else None
                child = self.own(holder, o)
                if child is not None and items is not None:
                    try:
                        pr = self.reader(child)
                        val, _ = decode_params(T, pr, child.start, child.end, items, f"{key}")
                        if isinstance(val.get("tail"), str) and not any(bytes.fromhex(val["tail"])):
                            self.pads[child.start] = len(val.pop("tail")) // 2
                        out[key] = val
                        self.parts.append(child)
                        continue
                    except TacError:
                        pass
                out[key] = r.slots.get(o) if o in r.slots else self.null_link(o, key)
            elif "x-link" in p and p.get("x-inline"):
                child = self.own(holder, o)
                n = self.ex.src.u32(off + int(T.structs[sname]["properties"][p["x-link"]["count"]]["x-offset"], 16))
                rec = ARRAY_OF[key]
                size = T.size[rec]
                if child is not None and n and self.zero_tail(child, n * size):
                    try:
                        mark = len(self.parts)
                        self.parts.append(child)
                        out[key] = [self.record(rec, child, child.start + i * size, child) for i in range(n)]
                        inline_counts.add(p["x-link"]["count"])
                        continue
                    except TacError:
                        del self.parts[mark:]
                        self.pads.pop(child.start, None)
                out[key] = r.slots.get(o) if o in r.slots else self.null_link(o, key)
            elif "x-link" in p:
                out[key] = r.slots.get(o) if o in r.slots else self.null_link(o, key)
            else:
                out[key] = decode_scalar(T, r, p, o, key)
        for c in inline_counts:
            del out[c]
        for key, p in props:                # a pad shows only when it isn't zero
            if p.get("x-c-name", "").startswith("pad") and not any(out.get(key) or []):
                out.pop(key, None)
        return out

    def null_link(self, o, key):
        if self.ex.src.u32(o) != 0:
            raise TacError(f"{key}: 0x{self.ex.src.u32(o):X} without relocation")
        return None


def encode_trigger(T, doc, tid, e, path):
    """[(node id, bytes, {offset: link})] of a trigger file: the record first, then each inlined
    part in the order the extract finds them (the record, its parameters, its condition array and
    each condition's parts, its actions and each action's data, its else-actions)."""
    parts = []

    def record(sname, obj, nid, at, buf, links, holder_id, prefix, sub):
        if not isinstance(obj, dict):
            raise TacError(f"{sub}: an object")
        props = T.fields(sname)
        known = {k for k, _ in props} | ({"$schema", "format_version", "id", "children"} if sub == path else set())
        for k in obj:
            if k not in known:
                raise TacError(f"{sub}/{k}: not a field of {sname}")
        kind = obj.get("type")
        later = []
        counts = {}
        for key, p in props:
            if "x-link" in p and p.get("x-inline") and isinstance(obj.get(key), list):
                counts[p["x-link"]["count"]] = len(obj[key])
        for key, p in props:
            o = at + int(p["x-offset"], 16)
            if key in counts:
                v = counts[key]
                buf[o:o + 4] = struct.pack(e + "I", v)
                continue
            if key not in obj:
                if p.get("x-c-name", "").startswith("pad"):
                    continue                # zero
                raise TacError(f"{sub}/{key}: missing")
            v = obj[key]
            step = f"{prefix}{STEPS[sname][int(p['x-offset'], 16)]}" if int(p["x-offset"], 16) in STEPS[sname] else None
            if "x-params" in p:
                if isinstance(v, dict):
                    if not isinstance(kind, str) or kind not in T.variants[p["x-params"]["enum"]]:
                        raise TacError(f"{sub}/type: {kind!r} has no parameter layout (use its name)")
                    cid = f"{holder_id}/{step}"
                    links[o] = cid
                    later.append(("params", cid, v, T.variants[p["x-params"]["enum"]][kind], f"{sub}/{key}"))
                else:
                    put_link(v, o, links, f"{sub}/{key}")
            elif "x-link" in p and p.get("x-inline") and isinstance(v, list):
                if not v:
                    raise TacError(f"{sub}/{key}: an empty list (use null)")
                cid = f"{holder_id}/{step}"
                links[o] = cid
                later.append(("array", cid, v, ARRAY_OF[key], f"{sub}/{key}"))
            elif "x-link" in p:
                put_link(v, o, links, f"{sub}/{key}")
            else:
                b = encode_scalar(T, p, v, e, f"{sub}/{key}")
                buf[o:o + len(b)] = b
        return later

    def put_link(v, o, links, sub):
        if v is None:
            return
        if not isinstance(v, str):
            raise TacError(f"{sub}: a link or null (or the list itself)")
        links[o] = v

    def expand(later):
        for what, cid, v, x, sub in later:
            if what == "params":
                b, ln = encode_params(T, v, x, e, sub)
                parts.append((cid, b, ln))
            else:
                size = T.size[x]
                buf = bytearray(size * len(v))
                links = {}
                parts.append((cid, buf, links))
                more = []
                for i, item in enumerate(v):
                    more += record(x, item, cid, i * size, buf, links, cid, f"{i}/", f"{sub}/{i}")
                expand(more)

    buf = bytearray(T.size["Trigger"])
    links = {}
    parts.append((tid, buf, links))
    expand(record("Trigger", doc, tid, 0, buf, links, tid, "", path))
    return [(i, bytes(b), ln) for i, b, ln in parts]


def survey(root, rels=("gc", "pc", "ps2")):
    """Decode and re-encode every parameter list of every retail world pack (pointer slots as
    their target offsets): counts per kind, and what failed."""
    import collections
    import xdl_graph as XGR
    T = Tac()
    stats = collections.Counter()
    fails = collections.Counter()
    for rel in rels:
        for arch in XGR.survey_archives(root, rel):
            man = json.loads((arch / "manifest.json").read_text())
            for ent in man["entries"]:
                if ent["type"] != "world":
                    continue
                src = XGR.Source(arch / ent["path"])
                targets = sorted(set(src.ptr.values()) | {len(src.blob)})
                import bisect
                r = Reader(src.blob, src.e, {o: f"@{t:X}" for o, t in src.ptr.items()})

                def one(enum, kind_v, slot):
                    p = src.ptr.get(slot)
                    kind = T.variants[enum] and T.sch["$defs"]["TriggerAction" if enum == "ActionType" else "Trigger"]
                    names = (T.structs["TriggerAction"] if enum == "ActionType" else T.structs["Trigger"])["properties"]["type"]["x-enum"]
                    k = names.get(str(kind_v), kind_v)
                    key = f"{enum}.{k}"
                    stats[key] += 1
                    if p is None:
                        return
                    end = targets[bisect.bisect_right(targets, p)]
                    try:
                        obj, ln = decode_params(T, r, p, end, T.variants[enum][k], key)
                        b, links = encode_params(T, obj, T.variants[enum][k], src.e, key)
                        want = bytearray(src.blob[p:end])
                        for o in links:
                            want[o:o + 4] = bytes(4)
                        if b != bytes(want):
                            raise TacError("bytes differ")
                        if {p + o: v for o, v in links.items()} != {o: v for o, v in r.slots.items() if p <= o < end}:
                            raise TacError("links differ")
                    except (TacError, KeyError) as ex:
                        fails[f"{key}: {str(ex).split(': ')[-1]}"] += 1

                def trig(t, depth):
                    one("TriggerType", src.u32(t), t + 4)
                    for lo, no in ((0x14, 0x1C), (0x18, 0x20)):
                        lst = src.ptr.get(t + lo)
                        for j in range(src.u32(t + no) if lst is not None else 0):
                            one("ActionType", src.u32(lst + j * 0x18), lst + j * 0x18 + 4)
                    c = src.ptr.get(t + 0xC)
                    for j in range(src.u32(t + 0x10) if c is not None and depth < 2 else 0):
                        trig(c + j * 0x2C, depth + 1)
                h = src.header
                for i in range(h.get("num_triggers") or 0):
                    trig(h["trigger_offset"] + i * 0x2C, 0)
    return stats, fails


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else str(HERE.parents[1] / "build-survey")
    st, fl = survey(root)
    for k, v in sorted(st.items()):
        print(f"{v:6}  {k}")
    print(f"\n{sum(st.values())} lists, {sum(fl.values())} not decoded:")
    for k, v in fl.most_common():
        print(f"{v:6}  {k}")
