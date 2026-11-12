#!/usr/bin/env python3
"""JSON Schemas (draft 2020-12) for the .xdl source format, generated from the C structs.

    python3 tools/gen_schemas.py                 check every struct's layout, write schemas/
    python3 tools/gen_schemas.py --check         layout checks only, with a per-struct report
    python3 tools/gen_schemas.py --fields PATH   docs/fields/README.md (found by walking up
                                                 from this file when not given)

The headers in src/game/include (plus port/gx.h for GXColor) are parsed by a small parser for
their conventions: typedef'd structs and unions, PTR32(T)/FPTR32 handle fields, fixed and
multi-dimensional arrays, nested and anonymous structs/unions, __attribute__((packed/aligned)),
`/* 0xNN ... */` offset comments and _Static_assert offsetof/sizeof checks (also through the
headers' assert macros). Every struct's computed layout is checked against its own offset
comments and asserts; a mismatch fails the run.

Schemas are written for the pack-data structs of docs/fields/README.md (SCHEMA_STRUCTS), one
file each, with the structs they embed in $defs. The undecoded-fields rule (docs/xdl-format
README §10.4) comes from that table's read columns:

* read by some release (yes or ?) but unnamed (confidence `-`): a typed field `unk_0xNN`;
* read by no release (no / no / no): left out of the schema and listed in `x-hidden`; the
  compiler takes those bytes from the `extends` base (zero without one).

Header comments may carry `@xdl:` markers for what the C type doesn't say (see parse_marker):
`link=T` (a u32 holding a pointer), `texture` / `geometry` (a relocated asset slot),
`units=...`, `range=LO..HI`, `hint=...`. docs/xdl-format/schemas.md in the superproject has
the type mapping.
"""

import argparse
import ast
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "src" / "game" / "include"
EXTRA_HEADERS = [ROOT / "src" / "port" / "gx.h"]
OUT = ROOT / "schemas"
OBSERVED = ROOT / "tools" / "schema_observed.json"

FORMAT_VERSION = 1
JSON_SCHEMA = "https://json-schema.org/draft/2020-12/schema"

# The pack-data structs of docs/fields/README.md, in its order.
SCHEMA_STRUCTS = [
    "CarAInfo", "CarAEngine", "CarAAccel", "CarACollider", "CarAStuntInfo", "CarAHud",
    "ProjectileInfo", "SmartProjParams", "CreatureInfo", "ItemRecord", "WeaponInfo",
    "WorldGameData", "DestructibleInfo", "IfaceObjectDef",
    "Trigger",
    # world packs (World.h)
    "World", "TileCar", "TilePath", "WorldQuad", "QuadContents", "QuadInstance", "QuadFxPoint",
    "QuadItem", "TileSpeaker", "Teleporter", "TileLight", "TileCreature", "WorldObjective",
    "WorldVolume", "WorldVariable", "WorldTimer",
]

# name: (size, signed, float)
PRIMS = {
    "u8": (1, False, False), "s8": (1, True, False), "char": (1, True, False),
    "u16": (2, False, False), "s16": (2, True, False),
    "u32": (4, False, False), "s32": (4, True, False), "BOOL": (4, True, False),
    "int": (4, True, False), "long": (4, True, False), "short": (2, True, False),
    "u64": (8, False, False), "s64": (8, True, False),
    "f32": (4, True, True), "f64": (8, True, True), "float": (4, True, True),
    "double": (8, True, True),
    "vu8": (1, False, False), "vu16": (2, False, False), "vu32": (4, False, False),
    "vs32": (4, True, False), "vf32": (4, True, True),
    "size_t": (4, False, False), "uintptr_t": (4, False, False),
}
UNSIGNED_OF = {"char": "u8", "short": "u16", "int": "u32", "long": "u32"}

# Link grammar: docs/xdl-format/pairing.md §1 (base://) and README §10.2 (uid://). An object
# reached through the object graph is addressed by a field path below its root object
# (base://HW_Cars/car_5/CarAInfo/0/engine), like pairing.md's .../geometry/12/level/0/group/3.
LINK_PATTERN = (r"^(base://[A-Za-z0-9_]+/[A-Za-z0-9_]+/[A-Za-z0-9_]+/(?:[a-z0-9]+:)?[0-9]+"
                r"|uid://[0-9a-z]{1,13})(/[A-Za-z0-9_]+)*$")
ASSET_PATTERN = (r"^(base://[A-Za-z0-9_]+/[A-Za-z0-9_]+/{kind}/(?:[a-z0-9]+:)?[0-9]+"
                 r"|uid://[0-9a-z]{{1,13}})$")


class LayoutError(Exception):
    pass


# ------------------------------------------------------------------------------------------
# tokens

TOKEN = re.compile(r"""
    (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<string>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<name>[A-Za-z_]\w*)
  | (?P<number>0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][+-]?\d+)?[fF]?|\d+[uUlL]*)
  | (?P<op><<|>>|==|!=|<=|>=|&&|\|\||->|\#\#|[^\s])
""", re.S | re.X)


class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind, self.text, self.line = kind, text, line

    def __repr__(self):
        return f"{self.text!r}@{self.line}"


def tokenize(text):
    """Tokens of the source with preprocessor lines taken out ([(line, directive)])."""
    out, directives = [], []
    lines = text.split("\n")
    i = 0
    keep = []
    while i < len(lines):
        ln = lines[i]
        if ln.lstrip().startswith("#"):
            start = i
            d = ln
            while d.endswith("\\") and i + 1 < len(lines):
                i += 1
                d = d[:-1] + " " + lines[i]
            directives.append((start + 1, d.strip()))
            keep.extend([""] * (i - start + 1))
        else:
            keep.append(ln)
        i += 1
    src = "\n".join(keep)
    line, pos = 1, 0
    for m in TOKEN.finditer(src):
        line += src.count("\n", pos, m.start())
        pos = m.start()
        kind = m.lastgroup
        out.append(Tok(kind, m.group(), line))
    return out, directives


# ------------------------------------------------------------------------------------------
# types

class Type:
    """kind: prim | struct | union | array | ptr | fptr | enum | opaque"""

    def __init__(self, kind, name=None, size=None, align=None, **kw):
        self.kind, self.name, self.size, self.align = kind, name, size, align
        self.fields = kw.get("fields")          # struct/union: [Field]
        self.elem = kw.get("elem")              # array
        self.count = kw.get("count")            # array
        self.target = kw.get("target")          # ptr: target type text
        self.header = kw.get("header")
        self.line = kw.get("line")
        self.packed = kw.get("packed", False)
        self.min_align = kw.get("min_align", 1)
        self.bitfields = kw.get("bitfields", False)
        self.laid_out = False

    def ctype(self):
        if self.kind == "array":
            dims, t = [], self
            while t.kind == "array":
                dims.append(t.count)
                t = t.elem
            return t.ctype() + "".join(f"[{d}]" for d in dims)
        if self.kind == "ptr":
            return f"PTR32({self.target})"
        if self.kind == "fptr":
            return "FPTR32"
        return self.name or f"<anonymous {self.kind}>"


class Field:
    def __init__(self, name, type_, comment, line):
        self.name, self.type, self.comment, self.line = name, type_, comment, line
        self.offset = None
        self.anon = name is None               # promoted members of an anonymous struct/union
        self.bits = None


class Registry:
    def __init__(self):
        self.types = {}          # typedef name / "struct Tag" -> Type
        self.defines = {}        # NAME -> token list
        self.fmacros = {}        # NAME -> (params, body tokens)
        self.enums = {}          # enumerator -> value
        self.enum_tags = {}      # enum tag -> [(enumerator, value)] (named enums only)
        self.tac = {}            # TAC_PARAMS_* / TAC_RECORD_* item lists (Trigger.h)
        self.asserts = []        # (header, line, expr tokens)
        self.order = []          # struct/union Types in definition order
        self.errors = []         # parse problems: (header, line, message)

    # -- names
    def lookup(self, name):
        if name in self.types:
            return self.types[name]
        if name in PRIMS:
            sz, signed, flt = PRIMS[name]
            t = Type("prim", name, sz, sz)
            t.signed, t.float = signed, flt
            return t
        return None

    def resolve(self, t):
        """Follow a forward reference ("struct Foo" before its body) to the defined type."""
        seen = 0
        while t.kind == "opaque" and t.name in self.types and self.types[t.name] is not t:
            t = self.types[t.name]
            seen += 1
            if seen > 20:
                break
        return t


# ------------------------------------------------------------------------------------------
# constant expressions

class Evaluator:
    def __init__(self, reg):
        self.reg = reg

    def tokens_value(self, toks, header="?", line=0):
        text = self.expand(toks, header, line)
        return self.eval_text(text, header, line)

    def expand(self, toks, header, line, depth=0):
        """Python expression text for C expression tokens (sizeof/offsetof evaluated)."""
        out = []
        i = 0
        while i < len(toks):
            t = toks[i]
            if t.kind == "comment":
                i += 1
                continue
            if t.kind == "name" and t.text in ("sizeof", "offsetof", "__alignof__", "_Alignof"):
                j, args = self.paren_args(toks, i + 1)
                if t.text == "sizeof":
                    out.append(str(self.sizeof(args[0], header, line)))
                elif t.text == "offsetof":
                    out.append(str(self.offsetof(args[0], args[1], header, line)))
                else:
                    out.append(str(self.typeof(args[0], header, line).align))
                i = j
                continue
            if t.kind == "name" and t.text in self.reg.fmacros and i + 1 < len(toks) and toks[i + 1].text == "(":
                params, body = self.reg.fmacros[t.text]
                j, args = self.paren_args(toks, i + 1)
                out.append("(" + self.expand(subst(body, params, args), header, line, depth + 1) + ")")
                i = j
                continue
            if t.kind == "name":
                if t.text in self.reg.enums:
                    out.append(str(self.reg.enums[t.text]))
                elif t.text in self.reg.defines and depth < 20:
                    out.append("(" + self.expand(self.reg.defines[t.text], header, line, depth + 1) + ")")
                elif t.text in ("u32", "s32", "int", "unsigned", "u16", "u8"):
                    # casts such as (u32)0x10: drop the type, the parentheses stay harmless
                    out.append("")
                else:
                    raise LayoutError(f"{header}:{line}: unknown name {t.text!r} in an expression")
                i += 1
                continue
            if t.kind == "number":
                s = t.text.rstrip("uUlLfF") if not t.text.lower().startswith("0x") else t.text.rstrip("uUlL")
                out.append(s)
                i += 1
                continue
            txt = {"&&": " and ", "||": " or ", "!": " not ", "/": "//"}.get(t.text, t.text)
            out.append(txt)
            i += 1
        return " ".join(x for x in out if x != "")

    def eval_text(self, text, header, line):
        try:
            tree = ast.parse(text.replace("( )", "").strip() or "0", mode="eval")
        except SyntaxError:
            raise LayoutError(f"{header}:{line}: can't evaluate {text!r}")
        allowed = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.BoolOp, ast.Compare, ast.Constant,
                   ast.operator, ast.unaryop, ast.boolop, ast.cmpop, ast.IfExp)
        for n in ast.walk(tree):
            if not isinstance(n, allowed):
                raise LayoutError(f"{header}:{line}: can't evaluate {text!r}")
        return eval(compile(tree, "<expr>", "eval"), {"__builtins__": {}}, {})

    @staticmethod
    def paren_args(toks, i):
        """(index after ')', [arg token lists]) for toks[i] == '('."""
        assert toks[i].text == "(", toks[i]
        depth, args, cur = 0, [], []
        j = i
        while j < len(toks):
            t = toks[j]
            if t.text in "([{" and t.kind == "op":
                depth += 1
                if depth > 1:
                    cur.append(t)
            elif t.text in ")]}" and t.kind == "op":
                depth -= 1
                if depth == 0:
                    args.append(cur)
                    return j + 1, args
                cur.append(t)
            elif t.text == "," and depth == 1:
                args.append(cur)
                cur = []
            elif t.kind != "comment":
                cur.append(t)
            j += 1
        raise LayoutError("unbalanced parentheses")

    def typeof(self, toks, header, line):
        names = [t.text for t in toks if t.kind == "name" and t.text not in ("const", "volatile")]
        if names and names[0] in ("struct", "union"):
            key = names[0] + " " + names[1]
        else:
            key = names[0] if names else ""
        t = self.reg.types.get(key) or self.reg.lookup(key)
        if t is None:
            raise LayoutError(f"{header}:{line}: unknown type {key!r}")
        t = self.reg.resolve(t)
        if any(x.text == "*" for x in toks):
            return Type("ptr", size=4, align=4, target=key)
        layout(self.reg, t)
        return t

    def sizeof(self, toks, header, line):
        if len(toks) == 1 and toks[0].kind == "name" and toks[0].text not in self.reg.types \
                and toks[0].text not in PRIMS:
            raise LayoutError(f"{header}:{line}: sizeof of an expression ({toks[0].text})")
        return self.typeof(toks, header, line).size

    def offsetof(self, ttoks, ftoks, header, line):
        t = self.typeof(ttoks, header, line)
        off = 0
        i = 0
        ft = [x for x in ftoks if x.kind != "comment"]
        while i < len(ft):
            x = ft[i]
            if x.text == ".":
                i += 1
                continue
            if x.text == "[":
                j = i + 1
                depth = 1
                while depth:
                    if ft[j].text == "[":
                        depth += 1
                    elif ft[j].text == "]":
                        depth -= 1
                    j += 1
                idx = self.tokens_value(ft[i + 1:j - 1], header, line)
                if t.kind != "array":
                    raise LayoutError(f"{header}:{line}: [] on a non-array in offsetof")
                off += idx * t.elem.size
                t = self.reg.resolve(t.elem)
                i = j
                continue
            if x.kind != "name":
                raise LayoutError(f"{header}:{line}: bad offsetof designator")
            f, foff = find_member(self.reg, t, x.text)
            if f is None:
                raise LayoutError(f"{header}:{line}: no member {x.text!r} in {t.ctype()}")
            off += foff
            t = self.reg.resolve(f.type)
            i += 1
        return off


def subst(body, params, args):
    out = []
    for t in body:
        if t.kind == "name" and t.text in params:
            out.extend(args[params.index(t.text)])
        elif t.text == "#":
            continue
        else:
            out.append(t)
    # drop the stringified tokens' leftovers: #T "." #f become T "." f, harmless in strings
    return out


def find_member(reg, t, name):
    """(Field, offset in t) of a member, looking into anonymous members."""
    t = reg.resolve(t)
    layout(reg, t)
    for f in t.fields or []:
        if f.name == name:
            return f, f.offset
        if f.anon:
            sub, off = find_member(reg, f.type, name)
            if sub is not None:
                return sub, f.offset + off
    return None, None


# ------------------------------------------------------------------------------------------
# parser

class Parser:
    def __init__(self, reg, header, toks):
        self.reg, self.header, self.toks = reg, header, toks
        self.i = 0
        self.ev = Evaluator(reg)

    def peek(self, k=0):
        j = self.i
        n = 0
        while j < len(self.toks):
            if self.toks[j].kind != "comment":
                if n == k:
                    return self.toks[j]
                n += 1
            j += 1
        return None

    def next(self):
        while self.i < len(self.toks) and self.toks[self.i].kind == "comment":
            self.i += 1
        t = self.toks[self.i]
        self.i += 1
        return t

    def expect(self, text):
        t = self.next()
        if t.text != text:
            raise LayoutError(f"{self.header}:{t.line}: expected {text!r}, found {t.text!r}")
        return t

    def err(self, line, msg):
        self.reg.errors.append((self.header, line, msg))

    # -- top level
    def parse(self):
        while self.peek() is not None:
            start = self.i
            try:
                self.top()
            except (LayoutError, IndexError, AssertionError) as e:
                t = self.toks[start] if start < len(self.toks) else None
                self.err(t.line if t else 0, str(e))
                self.i = start
                self.skip_statement()

    def top(self):
        t = self.peek()
        if t.text == "extern" and self.peek(1) and self.peek(1).kind == "string":
            self.next(); self.next()
            if self.peek() and self.peek().text == "{":
                self.next()
            return
        if t.text == "}":
            self.next()
            return
        if t.text == ";":
            self.next()
            return
        if t.text == "typedef":
            doc = self.doc_before(self.index_of_next())
            self.next()
            self.typedef(doc)
            return
        if t.text in ("struct", "union") and self.peek(1) and (
                self.peek(1).text == "{" or (self.peek(2) and self.peek(2).text == "{")):
            ty = self.aggregate()
            if self.peek().text != ";":
                self.declarators(ty, 0)          # a variable of that type: ignore
            self.expect(";")
            return
        if t.text == "enum":
            self.enum_type()
            self.skip_statement()
            return
        if t.text == "_Static_assert":
            self.next()
            j, args = Evaluator.paren_args(self.toks, self.index_of_next())
            self.i = j
            self.reg.asserts.append((self.header, t.line, args[0]))
            if self.peek() and self.peek().text == ";":
                self.next()
            return
        if t.kind == "name" and t.text in self.reg.fmacros and self.peek(1) and self.peek(1).text == "(":
            params, body = self.reg.fmacros[t.text]
            if any(b.text == "_Static_assert" for b in body):
                self.next()
                j, args = Evaluator.paren_args(self.toks, self.index_of_next())
                self.i = j
                exp = subst(body, params, args)
                k = [x for x in exp if x.text == "_Static_assert"]
                pos = exp.index(k[0])
                _, a = Evaluator.paren_args(exp, pos + 1)
                self.reg.asserts.append((self.header, t.line, a[0]))
                if self.peek() and self.peek().text == ";":
                    self.next()
                return
        self.skip_statement()

    def index_of_next(self):
        j = self.i
        while self.toks[j].kind == "comment":
            j += 1
        return j

    def skip_statement(self):
        """Skip a declaration or a function definition."""
        depth = 0
        prev = None
        while self.peek() is not None:
            t = self.next()
            if t.text == "{":
                if depth == 0 and prev is not None and prev.text == ")":
                    # function body
                    d = 1
                    while d:
                        u = self.next()
                        if u.text == "{":
                            d += 1
                        elif u.text == "}":
                            d -= 1
                    return
                depth += 1
            elif t.text == "}":
                depth -= 1
            elif t.text == ";" and depth <= 0:
                return
            prev = t

    def enum_type(self):
        self.expect("enum")
        tag = None
        if self.peek().kind == "name":
            tag = self.next().text
        if self.peek().text != "{":
            return Type("enum", "enum", 4, 4)
        self.next()
        val = -1
        members = self.reg.enum_tags.setdefault(tag, []) if tag else []
        while self.peek().text != "}":
            name = self.next().text
            if self.peek().text == "=":
                self.next()
                ex = []
                depth = 0
                while not (depth == 0 and self.peek().text in (",", "}")):
                    u = self.next()
                    depth += u.text in "([" and u.kind == "op"
                    depth -= u.text in ")]" and u.kind == "op"
                    ex.append(u)
                try:
                    val = self.ev.tokens_value(ex, self.header, ex[0].line if ex else 0)
                except LayoutError:
                    val = val + 1
            else:
                val += 1
            self.reg.enums[name] = val
            members.append((name, val))
            if self.peek().text == ",":
                self.next()
        self.next()
        return Type("enum", "enum", 4, 4)

    def attributes(self):
        """Consume __attribute__((...)) groups: (packed, aligned)."""
        packed, aligned = False, 1
        while self.peek() is not None and self.peek().text in ("__attribute__", "__attribute"):
            self.next()
            j, args = Evaluator.paren_args(self.toks, self.index_of_next())
            self.i = j
            flat = [x for a in args for x in a]
            k = 0
            while k < len(flat):
                x = flat[k]
                if x.text in ("packed", "__packed__"):
                    packed = True
                elif x.text in ("aligned", "__aligned__"):
                    if k + 1 < len(flat) and flat[k + 1].text == "(":
                        e = k + 2
                        depth = 1
                        while depth:
                            if flat[e].text == "(":
                                depth += 1
                            elif flat[e].text == ")":
                                depth -= 1
                            e += 1
                        aligned = max(aligned, self.ev.tokens_value(flat[k + 2:e - 1], self.header, x.line))
                        k = e
                        continue
                    aligned = max(aligned, 8)
                k += 1
        return packed, aligned

    def aggregate(self):
        """struct|union [Tag] [attrs] { members } -> Type (registered under 'struct Tag')."""
        kw = self.next()
        packed, aligned = self.attributes()
        tag = None
        if self.peek().kind == "name" and self.peek().text not in ("__attribute__",):
            tag = self.next().text
        p2, a2 = self.attributes()
        packed |= p2
        aligned = max(aligned, a2)
        key = f"{kw.text} {tag}" if tag else None
        if self.peek().text != "{":
            # reference to a struct type by tag
            if key in self.reg.types:
                return self.reg.types[key]
            t = Type("opaque", key)
            self.reg.types[key] = t
            return t
        self.next()
        fields = []
        bitfields = False
        while self.peek().text != "}":
            fs, bf = self.member()
            fields.extend(fs)
            bitfields |= bf
        self.next()
        p3, a3 = self.attributes()
        t = Type(kw.text, tag, fields=fields, header=self.header, line=kw.line,
                 packed=packed or p3, min_align=max(aligned, a3), bitfields=bitfields)
        if key:
            old = self.reg.types.get(key)
            if old is not None and old.kind == "opaque":
                old.__dict__.update(t.__dict__)
                t = old
            self.reg.types[key] = t
        self.reg.order.append(t)
        return t

    def type_spec(self):
        """Base type of a declaration -> Type, or None for something not a type."""
        quals = []
        while self.peek().text in ("const", "volatile", "static", "extern", "inline", "register",
                                   "unsigned", "signed"):
            quals.append(self.next().text)
        t = self.peek()
        if t.text in ("struct", "union"):
            return self.aggregate()
        if t.text == "enum":
            return self.enum_type()
        if t.text in ("PTR32", "FPTR32"):
            self.next()
            j, args = Evaluator.paren_args(self.toks, self.index_of_next())
            self.i = j
            if t.text == "PTR32":
                target = " ".join(x.text for x in args[0])
                target = re.sub(r"\s*([()])\s*", r"\1", target)
                return Type("ptr", size=4, align=4, target=target)
            return Type("fptr", "FPTR32", size=4, align=4)
        if t.kind == "name" and t.text in self.reg.fmacros and self.peek(1) and self.peek(1).text == "(":
            # a type macro (THING_SLOT(0x100)): splice its expansion in and parse that
            start = self.index_of_next()
            params, body = self.reg.fmacros[t.text]
            j, args = Evaluator.paren_args(self.toks, start + 1)
            exp = [Tok(x.kind, x.text, t.line) for x in subst(body, params, args)]
            self.toks[start:j] = exp
            self.i = start
            return self.type_spec()
        if t.kind == "name":
            if t.text in ("unsigned", "signed"):
                self.next()
            name = self.next().text
            while self.peek().text in ("const", "volatile"):
                self.next()
            if name == "long" and self.peek().text == "long":
                self.next()
                name = "s64"
            if "unsigned" in quals:
                name = UNSIGNED_OF.get(name, name)
            ty = self.reg.lookup(name)
            if ty is None:
                if name == "void":
                    return Type("opaque", "void", 0, 1)
                if not quals or name not in ("int",):
                    t2 = Type("opaque", name)
                    t2.unknown = True
                    return t2
            return ty
        if quals and quals[-1] in ("unsigned", "signed"):
            return self.reg.lookup("u32" if quals[-1] == "unsigned" else "int")
        raise LayoutError(f"{self.header}:{t.line}: unexpected {t.text!r}")

    def declarator(self, base):
        """[*...] name [dims...] [: bits] [attrs] -> (name, Type, bits)."""
        stars = 0
        while self.peek().text in ("*", "const", "volatile", "restrict"):
            if self.next().text == "*":
                stars += 1
        if self.peek().text == "(":
            # function pointer / pointer-to-array declarator
            j, args = Evaluator.paren_args(self.toks, self.index_of_next())
            names = [x.text for x in args[0] if x.kind == "name"]
            self.i = j
            if self.peek() and self.peek().text == "(":
                j, _ = Evaluator.paren_args(self.toks, self.index_of_next())
                self.i = j
            return (names[-1] if names else None), Type("fptr", "fnptr", 4, 4), None
        name = self.next().text if self.peek().kind == "name" else None
        dims = []
        while self.peek().text == "[":
            self.next()
            ex = []
            depth = 0
            while not (depth == 0 and self.peek().text == "]"):
                u = self.next()
                if u.text == "[":
                    depth += 1
                elif u.text == "]":
                    depth -= 1
                ex.append(u)
            self.next()
            if not ex:
                dims.append(0)
                continue
            try:
                dims.append(self.ev.tokens_value(ex, self.header, ex[0].line))
            except LayoutError:
                dims.append((ex, self.header))     # evaluated at layout time (sizeof a later type)
        bits = None
        if self.peek().text == ":":
            self.next()
            ex = []
            while self.peek().text not in (",", ";"):
                ex.append(self.next())
            bits = self.ev.tokens_value(ex, self.header, ex[0].line)
        p, a = self.attributes()
        t = base
        if stars:
            t = Type("ptr", size=4, align=4, target=(base.ctype() if base else "?") + "*" * (stars - 1),
                     )
            t.raw = True
        for d in reversed(dims):
            t = Type("array", size=None, align=None, elem=t, count=d)
        if a > 1:
            t = clone_aligned(t, a)
        return name, t, bits

    def declarators(self, base, line):
        out = []
        while True:
            out.append(self.declarator(base))
            if self.peek().text == ",":
                self.next()
                continue
            return out

    def comment_after(self, line):
        """Text of the comment starting on `line` right after the current position."""
        j = self.i
        texts = []
        while j < len(self.toks) and self.toks[j].kind == "comment" and self.toks[j].line == line:
            texts.append(self.toks[j].text)
            j += 1
        return clean_comment(" ".join(texts)) if texts else ""

    def member(self):
        """One member declaration -> ([Field], had bitfields)."""
        first = self.peek()
        base = self.type_spec()
        if self.peek().text == ";":
            end = self.next()
            # anonymous struct/union member
            com = self.comment_after(end.line)
            f = Field(None, base, com, first.line)
            return [f], False
        decls = self.declarators(base, first.line)
        end = self.expect(";")
        com = self.comment_after(end.line)
        out, bf = [], False
        for k, (name, t, bits) in enumerate(decls):
            # `f32 x, y, z; /* 0x00 */`: the offset is the first declarator's
            f = Field(name, t, com if k == 0 else OFFSET_COMMENT.sub("", com).strip(), end.line)
            f.bits = bits
            bf |= bits is not None
            out.append(f)
        return out, bf

    def doc_before(self, j):
        """The comment ending on the line before toks[j], if any."""
        k = j - 1
        if k < 0 or self.toks[k].kind != "comment":
            return ""
        c = self.toks[k]
        if c.line + c.text.count("\n") != self.toks[j].line - 1:
            return ""
        return clean_comment(c.text)

    def typedef(self, doc=""):
        first = self.peek()
        base = self.type_spec()
        decls = self.declarators(base, first.line)
        self.expect(";")
        for name, t, _ in decls:
            if name is None:
                continue
            if t is None:
                continue
            if t.kind in ("struct", "union") and t.name is None:
                t.name = name
            elif t.kind in ("struct", "union") and t.name != name:
                # typedef struct Foo_ Foo: keep the tag's object, add an alias
                pass
            self.reg.types[name] = t
            if t.kind in ("struct", "union") and t.name in (None, name):
                t.name = name
                t.doc = doc


def clone_aligned(t, a):
    c = Type(t.kind, t.name, t.size, t.align, fields=t.fields, elem=t.elem, count=t.count,
             target=t.target, header=t.header, line=t.line, packed=t.packed,
             min_align=max(t.min_align, a), bitfields=t.bitfields)
    for k in ("signed", "float"):
        if hasattr(t, k):
            setattr(c, k, getattr(t, k))
    c.forced_align = a
    return c


def clean_comment(text):
    text = text.strip()
    if text.startswith("//"):
        text = text[2:]
    text = re.sub(r"/\*+|\*+/", " ", text)
    text = re.sub(r"\n\s*\*", "\n", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


# ------------------------------------------------------------------------------------------
# layout

def layout(reg, t):
    """Fill size/align/offsets (GameCube layout: 4-byte pointers, natural alignment)."""
    if t is None:
        raise LayoutError("missing type")
    t0 = t
    t = reg.resolve(t)
    if t.kind == "opaque":
        if t.name == "void":
            return t
        raise LayoutError(f"incomplete type {t.name}")
    if t.kind in ("prim", "ptr", "fptr", "enum"):
        return t
    if t.kind == "array":
        if isinstance(t.count, tuple):
            ex, hdr = t.count
            t.count = Evaluator(reg).tokens_value(ex, hdr, ex[0].line)
        e = layout(reg, t.elem)
        t.elem = e if t.elem.kind == "opaque" else t.elem
        t.size = e.size * t.count
        t.align = max(e.align, getattr(t, "forced_align", 1))
        return t
    if t.laid_out:
        return t
    if getattr(t, "_busy", False):
        raise LayoutError(f"{t.ctype()} contains itself")
    t._busy = True
    try:
        if t.bitfields:
            raise LayoutError(f"{t.ctype()}: bitfields are not supported")
        off = 0
        align = t.min_align
        size = 0
        for f in t.fields:
            ft = reg.resolve(f.type)
            if ft is not f.type and f.type.kind == "opaque":
                f.type = ft
            layout(reg, ft)
            if ft.size is None:
                raise LayoutError(f"{t.ctype()}.{f.name}: unknown size")
            fa = 1 if t.packed else ft.align
            if t.kind == "struct":
                off = (off + fa - 1) // fa * fa
                f.offset = off
                off += ft.size
                size = off
            else:
                f.offset = 0
                size = max(size, ft.size)
            align = max(align, fa)
        t.align = align
        t.size = (size + align - 1) // align * align
        t.laid_out = True
    finally:
        t._busy = False
    if t0 is not t:
        t0.size, t0.align = t.size, t.align
    return t


def leaf_fields(reg, t, base=0, prefix=""):
    """[(absolute offset, Field, path)] of t's members, anonymous members flattened."""
    out = []
    t = reg.resolve(t)
    for f in t.fields or []:
        if f.anon:
            out.append((base + f.offset, f, prefix + "<anon>"))
            out.extend(leaf_fields(reg, f.type, base + f.offset, prefix))
        else:
            out.append((base + f.offset, f, prefix + f.name))
    return out


OFFSET_COMMENT = re.compile(r"^(0x[0-9A-Fa-f]+)\b")


def check_struct(reg, t):
    """Offset-comment mismatches of a struct: ["name: comment 0x.. computed 0x.."]."""
    problems = []

    def walk(ty, base, top):
        for f in ty.fields:
            abs_off = base + f.offset
            m = OFFSET_COMMENT.match(f.comment or "")
            if m:
                want = int(m.group(1), 16)
                # anonymous members' comments use the enclosing named struct's offsets; a
                # named inline struct member counts from its own start
                if want != abs_off:
                    problems.append(f"{f.name or '<anon>'}: comment says {m.group(1)}, "
                                    f"computed 0x{abs_off:X} (line {f.line})")
            ft = reg.resolve(f.type)
            if f.anon and ft.kind in ("struct", "union"):
                walk(ft, abs_off, top)
            elif ft.kind in ("struct", "union") and ft.name is None:
                walk(ft, 0, False)

    walk(t, 0, True)
    return problems


# ------------------------------------------------------------------------------------------
# loading

def load(headers):
    reg = Registry()
    parsed = []
    for h in headers:
        toks, directives = tokenize(h.read_text())
        parsed.append((h, toks))
        for line, d in directives:
            m = re.match(r"#\s*define\s+([A-Za-z_]\w*)(\(([^)]*)\))?\s*(.*)$", d)
            if not m:
                continue
            name, fn, params, body = m.group(1), m.group(2), m.group(3), m.group(4)
            btoks, _ = tokenize(body)
            btoks = [x for x in btoks if x.kind != "comment"]
            if fn is not None:
                reg.fmacros[name] = ([p.strip() for p in params.split(",") if p.strip()], btoks)
            elif btoks:
                reg.defines[name] = btoks
    for h, toks in parsed:
        Parser(reg, h.name, toks).parse()
        reg.tac.update(tac_macros(h.read_text()))
    return reg


TAC_MACRO = re.compile(r"^#define\s+(TAC_(?:PARAMS|RECORD)_\w+)[ \t]*(.*)$", re.M)
TAC_ITEM = re.compile(r"(\w+)\(([^)]*)\)")
TAC_KINDS = {"U32": "u32", "S32": "s32", "F32": "f32", "TOKEN": "token", "ANY": "any", "LINK": "link",
             "LIST": "list", "OPERAND": "operand", "CODEVAR": "codevar", "SCRATCH": "scratch",
             "STRING": "string", "TABLE": "table", "WORDS": "words"}


def tac_macros(text):
    """{macro: [[kind, name, arg?], ...]} of a header's parameter-list macros (Trigger.h)."""
    out = {}
    for m in TAC_MACRO.finditer(text):
        items = []
        for k, args in TAC_ITEM.findall(m.group(2)):
            if k not in TAC_KINDS:
                raise LayoutError(f"{m.group(1)}: unknown item {k}")
            items.append([TAC_KINDS[k]] + [a.strip() for a in args.split(",")])
        out[m.group(1)] = items
    return out


def headers():
    hs = sorted(INCLUDE.glob("*.h"))
    hs = [h for h in hs if h.name != "game_protos.h"]
    return EXTRA_HEADERS + hs


def check_all(reg, verbose=False):
    """Lay out every struct, check offset comments and asserts.

    Returns (report rows [(name, header, status, details)], failed assert/comment count)."""
    rows = []
    seen = set()
    bad = 0
    for t in reg.order:
        if t.name is None or id(t) in seen:
            continue
        seen.add(id(t))
        try:
            layout(reg, t)
            probs = check_struct(reg, t)
            status = "ok" if not probs else "mismatch"
            bad += bool(probs)
            rows.append((t.name, t.header, status, probs))
        except LayoutError as e:
            rows.append((t.name, t.header, "skipped", [str(e)]))
    ev = Evaluator(reg)
    assert_fail = []
    assert_skip = []
    n_ok = 0
    for header, line, expr in reg.asserts:
        try:
            ok = ev.tokens_value(expr, header, line)
        except LayoutError as e:
            assert_skip.append(f"{header}:{line}: {e}")
            continue
        if not ok:
            txt = " ".join(x.text for x in expr)
            # say what was computed
            assert_fail.append(f"{header}:{line}: {txt} is false (computed: {ev.expand(expr, header, line)})")
        else:
            n_ok += 1
    return rows, assert_fail, assert_skip, n_ok, bad


# ------------------------------------------------------------------------------------------
# docs/fields/README.md: per-struct read and confidence columns

def find_fields_doc():
    for d in Path(__file__).resolve().parents:
        p = d / "docs" / "fields" / "README.md"
        if p.exists():
            return p
    return None


def load_fields_doc(path):
    """{struct: {offset: {name, type, confidence, gc, ps2, pc, notes}}}"""
    out, cur = {}, None
    for ln in path.read_text().splitlines():
        m = re.match(r"^### (\w+)\s*$", ln)
        if m:
            cur = out.setdefault(m.group(1), {})
            continue
        if cur is None or not ln.startswith("| 0x"):
            continue
        cells = [c.strip() for c in ln.strip().strip("|").split("|")]
        if len(cells) < 8:
            continue
        off = int(cells[0], 16)
        row = {"name": cells[1].strip("`"), "type": cells[2], "confidence": cells[3],
               "gc": cells[4], "ps2": cells[5], "pc": cells[6], "notes": "|".join(cells[7:])}
        cur.setdefault(off, row)          # first row wins (a doubled offset is a union)
    return out


# ------------------------------------------------------------------------------------------
# @xdl markers

MARKER = re.compile(r"@xdl:\s*(.*)$")


def parse_marker(comment):
    """(description without marker and offset, {key: value}).

    `@xdl: key[=value]; key[=value]...` runs to the end of the comment. Keys:
      link=T     a u32 that holds a pointer (handle) to a T: a link, not a number
      texture    a texture slot (the pack's texture relocations): a texture link
      geometry   a geometry slot (local ref relocation): a geometry link
      units=U    x-units
      hint=TEXT  x-hint
      enum=TAG   an integer named by `enum TAG`'s enumerators (x-enum; JSON: the name)
      flags=TAG  a bit set named by `enum TAG`'s enumerators (x-flags; JSON: a list of names)
      count=F    a pointer to F elements (F: a sibling field); x-link.count
      inline     the target is written inside this object's file (TAC triggers)
      params=F   the parameter list whose layout the sibling enum field F picks (Trigger.h's
                 TAC_PARAMS_* macros): a tagged union, x-params
    """
    text = comment or ""
    marks = {}
    m = MARKER.search(text)
    if m:
        for part in m.group(1).split(";"):
            part = part.strip()
            if not part:
                continue
            k, _, v = part.partition("=")
            marks[k.strip()] = v.strip() if v else True
        text = text[:m.start()]
    text = OFFSET_COMMENT.sub("", text.strip()).strip()
    return text, marks


# ------------------------------------------------------------------------------------------
# schemas

INT_RANGE = {1: 8, 2: 16, 4: 32, 8: 64}


def enum_names(reg, tag):
    """{value: json name} of `enum TAG`: the enumerators without their common prefix,
    lower case; a NUM_* / *_COUNT terminator is left out."""
    mem = [(n, v) for n, v in reg.enum_tags.get(tag, []) if not re.search(r"(^|_)NUM_|_COUNT$", n)]
    if not mem:
        raise LayoutError(f"no enum {tag}")
    names = [n for n, _ in mem]
    pre = names[0]
    for n in names[1:]:
        while not n.startswith(pre):
            pre = pre[:-1]
    pre = pre[:pre.rfind("_") + 1] if "_" in pre else ""
    out = {}
    for n, v in mem:
        out.setdefault(v, n[len(pre):].lower())
    return out


def named_int_schema(names, base):
    return {"anyOf": [{"enum": list(dict.fromkeys(names.values()))}, base]}


def link_schema(kind, target=None):
    pat = LINK_PATTERN if kind == "object" else ASSET_PATTERN.format(kind=kind)
    x = {"kind": kind}
    if target:
        x["target"] = target
    return {"type": ["string", "null"], "pattern": pat, "x-link": x}


def prim_schema(t):
    if t.float:
        return {"type": "number"}
    bits = INT_RANGE[t.size] if t.name != "char" else 8
    if t.signed:
        return {"type": "integer", "minimum": -(1 << (bits - 1)), "maximum": (1 << (bits - 1)) - 1}
    return {"type": "integer", "minimum": 0, "maximum": (1 << bits) - 1}


class Emitter:
    def __init__(self, reg, fields_doc, observed):
        self.reg, self.doc, self.obs = reg, fields_doc, observed
        self.warnings = []

    def type_schema(self, t, marks, defs):
        t = self.reg.resolve(t)
        layout(self.reg, t)
        if t.kind == "array":
            item = self.type_schema(t.elem, marks, defs)
            return {"type": "array", "items": item, "minItems": t.count, "maxItems": t.count}
        if "link" in marks and t.kind == "prim" and t.size == 4:
            return link_schema("object", None if marks["link"] is True else marks["link"])
        if "texture" in marks:
            return link_schema("texture")
        if "geometry" in marks:
            return link_schema("geometry")
        if t.kind == "prim" and "enum" in marks:
            names = enum_names(self.reg, marks["enum"])
            return dict(named_int_schema(names, prim_schema(t)), **{
                "x-enum": {str(v): n for v, n in sorted(names.items())}})
        if t.kind == "prim" and "flags" in marks:
            names = enum_names(self.reg, marks["flags"])
            return {"type": "array", "items": named_int_schema(names, prim_schema(t)), "uniqueItems": True,
                    "x-flags": {str(v): n for v, n in sorted(names.items())}}
        if t.kind == "ptr" and "params" in marks:
            return {"anyOf": [link_schema("object", t.target), {"type": "object"}],
                    "x-params": {"tag": marks["params"]}}
        if t.kind == "ptr" and "count" in marks:
            sch = link_schema("object", t.target)
            sch["x-link"]["count"] = marks["count"]
            et = self.reg.types.get(t.target)
            if et is not None:
                et = self.reg.resolve(et)
                layout(self.reg, et)
                sch["x-link"]["item"] = et.name if et.kind in ("struct", "union") else et.ctype()
                sch["x-link"]["item-size"] = et.size
            if "inline" in marks:
                tt = self.reg.types.get(t.target)
                item = self.type_schema(tt, {}, defs) if tt is not None else {"type": "object"}
                sch = {"anyOf": [sch, {"type": "array", "items": item}], "x-link": sch["x-link"],
                       "x-inline": True}
            return sch
        if t.kind == "prim":
            return prim_schema(t)
        if t.kind == "enum":
            return {"type": "integer", "minimum": -(1 << 31), "maximum": (1 << 31) - 1}
        if t.kind == "ptr":
            return link_schema("object", t.target)
        if t.kind == "fptr":
            return {"type": "integer", "minimum": 0, "maximum": 0xFFFFFFFF,
                    "description": "function pointer slot (runtime only)"}
        if t.kind in ("struct", "union"):
            if t.name:
                if t.name not in defs:
                    defs[t.name] = None
                    defs[t.name] = self.object_schema(t, defs, top=False)
                return {"$ref": f"#/$defs/{t.name}"}
            return self.object_schema(t, defs, top=False)
        raise LayoutError(f"no JSON type for {t.ctype()}")

    def members(self, t, base=0):
        """[(offset, Field, alternatives)]: anonymous members flattened, a union's first
        member stands for the slot (the others listed as alternatives)."""
        out = []
        t = self.reg.resolve(t)
        if t.kind == "union":
            first = t.fields[0]
            alts = [f.name for f in t.fields[1:] if f.name]
            if first.anon:
                sub = self.members(first.type, base)
                if sub:
                    o, f, a = sub[0]
                    sub[0] = (o, f, a + alts)
                return sub
            return [(base, first, alts)]
        for f in t.fields:
            if f.anon:
                out.extend(self.members(f.type, base + f.offset))
            else:
                out.append((base + f.offset, f, []))
        return out

    def object_schema(self, t, defs, top):
        name = t.name
        rows = self.doc.get(name, {}) if name else {}
        obs = self.obs.get(name, {}) if name else {}
        props, hidden, required = {}, [], []
        for off, f, alts in self.members(t):
            desc, marks = parse_marker(f.comment)
            row = rows.get(off)
            ft = self.reg.resolve(f.type)
            layout(self.reg, ft)
            reads = None
            if row:
                reads = {k: row[k] for k in ("gc", "ps2", "pc")}
                if all(v == "no" for v in reads.values()):
                    hidden.append({"offset": f"0x{off:02X}", "size": ft.size, "c-name": f.name,
                                   "note": desc})
                    continue
            elif rows:
                self.warnings.append(f"{name}.{f.name} (0x{off:X}) is not in docs/fields")
            unnamed = (row and row["confidence"] == "-") or (
                not row and re.match(r"^(pad|unk)[0-9A-Fa-f_]*$", f.name or ""))
            key = f"unk_0x{off:02X}" if unnamed else f.name
            sch = self.type_schema(ft, marks, defs)
            if unnamed and row and row["type"] in PRIMS and ft.kind != "prim":
                # typed by how the code loads it (docs/fields)
                pt = self.reg.lookup(row["type"])
                if pt.size == ft.size:
                    sch = prim_schema(pt)
            p = {}
            if desc:
                p["description"] = desc
            p.update(sch)
            p["x-offset"] = f"0x{off:02X}"
            p["x-c-type"] = ft.ctype() if not ("link" in marks or "texture" in marks or "geometry" in marks) \
                else ft.ctype()
            if key != f.name:
                p["x-c-name"] = f.name
            if alts:
                p["x-union-alternatives"] = alts
            if row:
                p["x-confidence"] = row["confidence"]
                p["x-read"] = reads
            for k in ("units", "hint"):
                if k in marks:
                    p[f"x-{k}"] = marks[k]
            if key in obs:
                p["x-observed"] = obs[key]
            props[key] = p
            required.append(key)
        sch = {}
        if top:
            sch["$schema"] = JSON_SCHEMA
            sch["title"] = name
        else:
            sch["title"] = name or "(anonymous)"
        doc = getattr(t, "doc", "")
        if doc:
            sch["description"] = doc
        sch["type"] = "object"
        sch["x-c-struct"] = name
        sch["x-header"] = t.header
        sch["x-size"] = f"0x{t.size:X}"
        if hidden:
            sch["x-hidden"] = hidden
        if top:
            head = {
                "$schema": {"type": "string", "description": "path or URL of this schema"},
                "format_version": {"const": FORMAT_VERSION},
                "extends": dict(link_schema("object", name), type="string",
                                description="the object this one overrides (README §10.3); "
                                            "fields left out, and the hidden ones, come from it"),
            }
            props = {**head, **props}
            sch["properties"] = props
            sch["additionalProperties"] = False
            sch["required"] = ["format_version"]
            # with `extends` every field is optional; without, every field is needed
            sch["if"] = {"required": ["extends"]}
            sch["then"] = True
            sch["else"] = {"required": required}
            real = {k: v for k, v in defs.items() if v is not None}
            if real:
                sch["$defs"] = real
        else:
            sch["properties"] = props
            sch["additionalProperties"] = False
        return sch

    def schema(self, name):
        t = self.reg.types.get(name)
        if t is None:
            raise LayoutError(f"no struct {name}")
        layout(self.reg, t)
        defs = {}
        sch = self.object_schema(t, defs, top=True)
        self.add_variants(sch)
        return sch

    # -- tagged unions: the TAC parameter lists ------------------------------------------
    def add_variants(self, sch):
        """For each `params=F` field (in the struct or its $defs): x-variants, the layout of
        each kind of F's enum from the TAC_PARAMS_* macros, with a JSON schema per kind
        ($defs/<Enum>_<kind>) and an if/then per kind picking it."""
        objs = [sch] + list(sch.get("$defs", {}).values())
        variants, records = {}, {}
        for o in objs:
            for key, p in o.get("properties", {}).items():
                if "x-params" not in p:
                    continue
                tagp = o["properties"][p["x-params"]["tag"]]
                tag = re.search(r"enum=(\w+)", self._marks_of(o, p["x-params"]["tag"]))
                tag = tag.group(1) if tag else None
                if tag is None:
                    raise LayoutError(f"{key}: params= names a field without enum=")
                p["x-params"]["enum"] = tag
                if tag not in variants:
                    variants[tag] = self.variant_layouts(tag, records)
                rules = []
                for kind, items in variants[tag].items():
                    dname = f"{tag}_{kind}"
                    sch.setdefault("$defs", {})[dname] = self.variant_schema(items, dname)
                    rules.append({"if": {"properties": {p["x-params"]["tag"]: {"const": kind}},
                                         "required": [p["x-params"]["tag"]]},
                                  "then": {"properties": {key: {"anyOf": [
                                      {"$ref": f"#/$defs/{dname}"}, {"type": ["string", "null"]}]}}}})
                o["allOf"] = rules
                del tagp  # (the tag field keeps its x-enum)
        if variants:
            # the trigger file's form (xdl_graph.py): one file per trigger record, its own id, its
            # inline lists without their counts, raw children; pads only when not zero
            inline = [p["x-link"]["count"] for p in sch["properties"].values() if p.get("x-inline")]
            sch["properties"] = {"id": dict(link_schema("object", sch["title"]), type="string",
                                            description="the record's own id (the trigger table entry)"),
                                 "children": {"type": "object", "description": "raw nodes kept in this file"},
                                 **sch["properties"]}
            req = [k for k in sch["else"]["required"] if k not in inline
                   and not sch["properties"][k].get("x-c-name", "").startswith("pad")]
            sch["else"] = {"required": req}
            sch["x-variants"] = variants
            if records:
                sch["x-records"] = records
            sch["x-tokens"] = {str(v): n for v, n in sorted(enum_names(self.reg, "TacToken").items())}
            sch["x-operands"] = {str(v): n for v, n in sorted(enum_names(self.reg, "TacOperand").items())}

    def _marks_of(self, obj_schema, key):
        t = self.reg.types.get(obj_schema.get("x-c-struct") or "")
        for off, f, alts in self.members(t):
            if f.name == key:
                return f.comment or ""
        return ""

    def variant_layouts(self, tag, records):
        out = {}
        for n, v in self.reg.enum_tags[tag]:
            if re.search(r"(^|_)NUM_|_COUNT$", n):
                continue
            macro = "TAC_PARAMS_" + n
            if macro not in self.reg.tac:
                raise LayoutError(f"{tag}: no {macro} (Trigger.h)")
            items = self.reg.tac[macro]
            for it in items:
                if it[0] == "table":
                    rm = "TAC_RECORD_" + it[2]
                    if rm not in self.reg.tac:
                        raise LayoutError(f"{macro}: no {rm}")
                    records[it[2]] = self.reg.tac[rm]
            out[enum_names(self.reg, tag)[v]] = items
        return out

    def variant_schema(self, items, title):
        link = link_schema("object")
        word = {"anyOf": [{"type": "integer"}, link]}
        token = named_int_schema(enum_names(self.reg, "TacToken"), {"type": "integer"})
        props = {}
        for it in items:
            k, name = it[0], it[1]
            if k in ("u32", "s32"):
                p = {"type": "integer"}
            elif k == "f32":
                p = {"type": "number"}
            elif k == "token":
                p = token
            elif k == "any":
                p = word
            elif k == "link":
                p = link_schema("object", None if it[2] == "void" else it[2])
            elif k == "list":
                p = {"type": "array", "items": link_schema("object", None if it[2] == "void" else it[2])}
            elif k == "operand":
                p = {"type": "object", "x-tac": "operand",
                     "description": "{\"const\": f32} | {\"variable\": WorldVariableSlot link} | "
                                    "{\"code\": id, \"arg\": word}"}
            elif k == "codevar":
                p = {"type": "object", "x-tac": "codevar",
                     "description": "{\"var\": id < 100, \"cars\": [TileCar links]} | {\"var\": id, \"arg\": word}"}
            elif k == "scratch":
                p = {"type": "array", "items": {"type": "integer"}, "description": f"run-time words, one per {it[2]}"}
            elif k == "string":
                p = {"type": "string"}
            elif k == "table":
                p = {"type": "array", "items": {"type": "object"}, "x-record": it[2]}
            elif k == "words":
                p = {"type": "array", "items": word, "description": "not decoded"}
            else:
                raise LayoutError(f"{title}: item {k}")
            props[name] = dict(p, **{"x-item": k})
        props["tail"] = {"anyOf": [{"type": "string"}, {"type": "array", "items": word}],
                         "description": "what follows the list in its node, kept as it was: hex, or "
                                        "words when it holds links"}
        return {"title": title, "type": "object", "properties": props, "additionalProperties": False,
                "required": [it[1] for it in items]}


def write_schemas(reg, fields_doc, out, observed):
    em = Emitter(reg, fields_doc, observed)
    out.mkdir(parents=True, exist_ok=True)
    written = []
    for name in SCHEMA_STRUCTS:
        sch = em.schema(name)
        p = out / f"{name}.schema.json"
        p.write_text(json.dumps(sch, indent=2) + "\n")
        written.append((name, p, sch))
    return written, em.warnings


# ------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="layout checks only, with a report")
    ap.add_argument("--fields", type=Path, help="docs/fields/README.md")
    ap.add_argument("--out", type=Path, default=OUT)
    a = ap.parse_args()
    reg = load(headers())
    rows, afail, askip, aok, bad = check_all(reg)
    if a.check:
        for name, header, status, det in sorted(rows, key=lambda r: (r[1], r[0])):
            print(f"{header:16} {name:28} {status}")
            for d in det:
                print(f"    {d}")
    for h, line, msg in reg.errors:
        print(f"parse: {h}:{line}: {msg}", file=sys.stderr)
    n = len(rows)
    print(f"{n} structs: {sum(r[2] == 'ok' for r in rows)} ok, {bad} with offset-comment "
          f"mismatches, {sum(r[2] == 'skipped' for r in rows)} not laid out; asserts: {aok} hold, "
          f"{len(afail)} fail, {len(askip)} not evaluated", file=sys.stderr)
    for x in afail:
        print("assert FAILED: " + x, file=sys.stderr)
    for x in askip:
        print("assert skipped: " + x, file=sys.stderr)
    if bad or afail:
        for name, header, status, det in rows:
            if status == "mismatch":
                for d in det:
                    print(f"layout MISMATCH: {header}: {name}.{d}", file=sys.stderr)
        sys.exit("layout checks failed")
    if a.check:
        return
    fp = a.fields or find_fields_doc()
    if fp is None:
        sys.exit("docs/fields/README.md not found: pass --fields")
    fields_doc = load_fields_doc(fp)
    observed = json.loads(OBSERVED.read_text()) if OBSERVED.exists() else {}
    written, warns = write_schemas(reg, fields_doc, a.out, observed)
    for w in warns:
        print("warning: " + w, file=sys.stderr)
    for name, p, sch in written:
        n = len(sch["properties"]) - 3
        print(f"{p.relative_to(ROOT) if p.is_relative_to(ROOT) else p}: {n} fields, "
              f"{len(sch.get('x-hidden', []))} hidden, {len(sch.get('$defs', {}))} defs", file=sys.stderr)


if __name__ == "__main__":
    main()
