#!/usr/bin/env python3
"""Parity of tools/xdlrs (the Rust port) with tools/xdl_struct.py and tools/xdl_tac.py.

    python3 tools/xdlrs_parity.py [--survey DIR] [--out DIR] [--only a,b] [--quick]

Runs both on the same input and compares bytes and text exactly:

  schemas    every schema's fields, offsets, C types and sizes
  jsonc      jsonc_loads (json.dumps of the result, or the error) and dump_value, over the level
             and mod samples, the schemas, the round trip's files and made-up edge cases
  f32        f32_text over random and special f32 bit patterns
  roundtrip  `roundtrip --parts -v --out` on every car of gc, pc, ps2: stdout and every file
  extract    `extract` of each car part (stdout)
  build      `build -o` of each extracted object, unchanged and with edits (good and bad):
             exit code, stdout, stderr, bytes, relocations
  observe    `observe` (the written JSON)
  validate   MiniValidator messages over the samples and edited objects
  tac        `xdl_tac.py SURVEY` (every retail parameter list decoded and encoded)
  tacenc     encode_trigger of the level samples' triggers (and edits), both byte orders
  tacext     xdl_tac.Extract over xdl_graph's Extractor of world packs (host state dumped)

The Python side runs with xdl_struct's builtin validator (MiniValidator), which is what the
port has; with the jsonschema package installed the Python tools would use that instead.
SURVEY: HWVX-SDL/build-survey (`hwvx-x extract` trees per release). OUT: build/xdlrs-parity.
Needs `cargo build --release` in tools/xdlrs.
"""

import argparse
import contextlib
import io
import json
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

import xdl_struct as XS  # noqa: E402
import xdl_tac as XT  # noqa: E402

XS.validator = lambda sch: (XS.MiniValidator(sch), "builtin")
BIN = HERE / "xdlrs" / "target" / "release" / "xdlrs"
RESULTS = []


def rs(*args, stdin=None):
    p = subprocess.run([str(BIN), *map(str, args), "--repo", str(ROOT)], capture_output=True, text=True, input=stdin)
    return p.returncode, p.stdout, p.stderr


def report(name, total, bad, notes=()):
    RESULTS.append((name, total, bad))
    print(f"{'ok  ' if not bad else 'FAIL'} {name}: {total - len(bad)}/{total} identical")
    for b in bad[:8]:
        print(f"       {b}")
    for n in notes:
        print(f"       note: {n}")


def py_main(argv):
    """xdl_struct.main() in-process: (exit code, stdout, stderr); an exception the Python
    doesn't catch comes back as ('EXC', type name, message)."""
    out, err = io.StringIO(), io.StringIO()
    old = sys.argv
    sys.argv = ["xdl_struct.py", *map(str, argv)]
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = XS.main()
    except Exception as e:  # noqa: BLE001
        return "EXC", type(e).__name__, str(e)
    finally:
        sys.argv = old
    return code, out.getvalue(), err.getvalue()


def py_build_novalidate(file, pack, o):
    """cmd_build without the schema check: the encoder's own errors."""
    out, err = io.StringIO(), io.StringIO()
    old = XS.validator
    XS.validator = lambda sch: (XS.MiniValidator({}), "none")
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            sys.argv = ["xdl_struct.py", "build", str(file), str(pack), "-o", str(o)]
            code = XS.main()
    except Exception as e:  # noqa: BLE001
        return "EXC", type(e).__name__, str(e)
    finally:
        XS.validator = old
    return code, out.getvalue(), err.getvalue()


def kind_of(e):
    return {"XdlError": "Xdl", "TacError": "Tac", "JSONDecodeError": "Json", "error": "Struct",
            "GraphError": "Graph"}.get(type(e).__name__, "Uncaught")


# ------------------------------------------------------------------------------------------

def t_schemas(a):
    code, out, err = rs("probe", "schemas")
    got = json.loads(out)
    S = XS.Schemas()
    names = [(t, v[0]) for t, v in S.top.items()] + [(f"$defs/{k}", S.defs[k]) for k in sorted(S.defs)]
    want = {}
    for n, sch in names:
        fs = []
        for k, p in (XS.fields(sch) if "properties" in sch else []):
            ct = p["x-c-type"]
            base, dims = XS.parse_ctype(ct)
            try:
                sz = S.size_of(ct)
            except XS.XdlError as e:
                sz = str(e)
            fs.append([k, int(p["x-offset"], 16), ct, base, dims, sz])
        want[n] = {"size": int(sch["x-size"], 16) if "x-size" in sch else None, "fields": fs}
    bad = [n for n in want if want[n] != got.get(n)] + [n for n in got if n not in want]
    if list(want) != list(got):
        bad.append("order of the schemas differs")
    report("schemas", len(want), bad)


EDGE = {
    "comments.jsonc": '// a\n/* b */ {"a": 1, /* c */ "b": [1, 2, ], // d\n "c": "//x/*y*/", }\n',
    "strings.jsonc": '{"q": "a\\"b//c", "u": "\\u00e9\\ud83d\\ude00", "e": "x\\\\", "t": "\\t"}',
    "nums.jsonc": '[-0, -0.0, 0, 1E5, 1e-7, 1.0, 1.5e300, 12345678901234567890, -12, 0.1, 7400.0, 1e16]',
    "unterminated_str.jsonc": '{"a": "x}\n',
    "unterminated_com.jsonc": '{"a": 1} /* x',
    "bad.jsonc": '{"a": }',
    "empty.jsonc": '',
    "ws.jsonc": '   \n  // only a comment\n',
    "dup.jsonc": '{"a": 1, "b": 2, "a": 3}',
    "nested_commas.jsonc": '{"a": [[1,], {"b": 2,},],}',
    "comment_in_value.jsonc": '{"a": 1/**/2}',
    "slash_end.jsonc": '{"a": 1}/',
    "escape_end.jsonc": '{"a": "x\\',
    "unicode.jsonc": '{"é": "ü", "k": " "}',
    "lead_comments.jsonc": '// one\n  // two\n{"a": 1}',
    "nan.jsonc": '[NaN]',
}


def t_jsonc(a, extra):
    edge = a.out / "jsonc-edge"
    edge.mkdir(parents=True, exist_ok=True)
    for n, t in EDGE.items():
        (edge / n).write_text(t)
    files = sorted(edge.glob("*.jsonc"))
    for d in (HERE / "level-samples", HERE / "mod-samples", ROOT / "schemas"):
        files += sorted(p for p in d.rglob("*") if p.suffix in (".jsonc", ".json"))
    files += extra
    want = []
    for p in files:
        try:
            want.append("OK " + json.dumps(XS.jsonc_loads(p.read_text())))
        except Exception as e:  # noqa: BLE001
            want.append(f"ERR {kind_of(e)} {e}")
    got = []
    for i in range(0, len(files), 200):
        code, out, err = rs("probe", "jsonc", *files[i:i + 200])
        got += out.splitlines()
    bad, notes = [], []
    for p, w, g in zip(files, want, got):
        if w == g:
            continue
        # bad JSON: same failure, the parser's own message differs
        if w.startswith("ERR Json") and g.startswith("ERR Json"):
            continue
        if p.name == "nan.jsonc" and w.startswith("OK") and g.startswith("ERR Json"):
            notes.append("NaN/Infinity: Python's json accepts them, the port doesn't (no JSON number)")
            continue
        bad.append(f"{p}: python {w[:120]!r} rust {g[:120]!r}")
    report("jsonc loads", len(files), bad, notes)
    # dump_value
    bad = []
    n = 0
    for p, w, g in zip(files, want, got):
        if not (w.startswith("OK") and g.startswith("OK")):
            continue
        n += 1
        code, out, err = rs("probe", "dump", p)
        exp = XS.dump_value(XS.jsonc_loads(p.read_text()), 0) + "\n"
        if out != exp:
            bad.append(f"{p}: first difference at char {next((i for i, (x, y) in enumerate(zip(out, exp)) if x != y), min(len(out), len(exp)))}")
    report("jsonc dump_value", n, bad)


def t_f32(a):
    rnd = random.Random(1)
    pats = [0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF, 0xFF7FFFFF, 0x00000001, 0x007FFFFF,
            0x3F800000, 0x3E99999A, 0x4B000000, 0x4B7FFFFF, 0x5A0E1BCA, 0x2EDBE6FF]
    pats += [rnd.getrandbits(32) for _ in range(a.n_f32)]
    # values with short decimal forms
    for _ in range(a.n_f32 // 4):
        v = round(rnd.uniform(-1e5, 1e5), rnd.randint(0, 6))
        pats.append(struct.unpack("<I", struct.pack("<f", v))[0])
    want = []
    for b in pats:
        v = struct.unpack("<f", struct.pack("<I", b))[0]
        try:
            want.append(XS.f32_text(v))
        except Exception as e:  # noqa: BLE001
            want.append(f"ERR {kind_of(e)} {e}")
    code, out, err = rs("probe", "f32text", stdin="".join(f"{b:08x}\n" for b in pats))
    got = out.splitlines()
    bad = [f"{b:08x}: python {w!r} rust {g!r}" for b, w, g in zip(pats, want, got) if w != g]
    if len(got) != len(pats):
        bad.append(f"{len(got)} lines for {len(pats)}")
    notes = []
    if any(w.startswith("ERR Uncaught") for w in want):
        notes.append("f32 max: the Python's f32_text raises OverflowError (packing 3.403e+38); the port the same error")
    report("f32_text", len(pats), bad, notes)


def survey_rels(a):
    return [r for r in ("gc", "pc", "ps2") if (a.survey / r).is_dir()]


def t_roundtrip(a):
    rels = ",".join(survey_rels(a))
    po, ro = a.out / "rt-py", a.out / "rt-rs"
    for d in (po, ro):
        shutil.rmtree(d, ignore_errors=True)
    w = py_main(["roundtrip", a.survey, "--rel", rels, "--parts", "-v", "--out", po])
    g = rs("struct", "roundtrip", a.survey, "--rel", rels, "--parts", "-v", "--out", ro)
    bad = []
    if w != g:
        bad.append(f"output differs:\n{w[1][-400:]}\n--- rust\n{g[1][-400:]}{g[2][-400:]}")
    pf = sorted(p.relative_to(po) for p in po.rglob("*.jsonc"))
    rf = sorted(p.relative_to(ro) for p in ro.rglob("*.jsonc"))
    if pf != rf:
        bad.append(f"file lists differ: {len(pf)} vs {len(rf)}")
    for f in pf:
        if (ro / f).exists() and (po / f).read_bytes() != (ro / f).read_bytes():
            bad.append(f"{f} differs")
    report("struct roundtrip (stdout, files)", len(pf) + 1, bad, [w[1].strip().splitlines()[-1]] if w[0] != "EXC" else [])
    return [po / f for f in pf]


def t_extract(a, files):
    bad, n = [], 0
    for f in files[:: max(1, len(files) // a.n_extract)]:
        rel, pname, fname = f.parts[-3:]
        pack = pack_dir(a, rel, pname)
        path = [] if fname == "car.jsonc" else ["--path", fname[:-6]]
        n += 1
        w = py_main(["extract", pack, *path])
        g = rs("struct", "extract", pack, *path)
        if w != g:
            bad.append(f"{rel} {pname} {fname}: {str(w)[:200]} | {str(g)[:200]}")
    report("struct extract", n, bad)


def pack_dir(a, rel, pname):
    for p in XS.survey_packs(a.survey, rel, "HW_Cars"):
        if XS.Pack(p).name == pname:
            return p
    raise SystemExit(f"no pack {rel} {pname}")


def mutations(doc, S, sname, rnd, P):
    """(label, doc) edits of an extracted object: good and bad."""
    sch = S.struct(sname)
    flds = XS.fields(sch)
    out = [("unchanged", doc)]

    def edit(label, f):
        d = json.loads(json.dumps(doc))
        try:
            f(d)
        except (KeyError, IndexError, TypeError, ValueError):
            return
        out.append((label, d))

    nums = [(k, p) for k, p in flds if "x-link" not in p and "$ref" not in p and isinstance(doc.get(k), (int, float)) and not isinstance(doc.get(k), bool)]
    arrs = [(k, p) for k, p in flds if isinstance(doc.get(k), list) and doc[k] and all(isinstance(x, (int, float)) for x in doc[k])]
    enums = [(k, p) for k, p in flds if "x-enum" in p]
    flags = [(k, p) for k, p in flds if "x-flags" in p]
    links = [(k, p) for k, p in flds if "x-link" in p]
    texs = [(k, p) for k, p in links if p["x-link"]["kind"] == "texture"]
    objs = [(k, p) for k, p in links if p["x-link"]["kind"] == "object"]
    if nums:
        k, p = rnd.choice(nums)
        base = XS.parse_ctype(p["x-c-type"])[0]
        flt = XS.PRIM.get(base, ("", 0, False))[2]
        edit("number+1", lambda d: d.__setitem__(k, d[k] + (0.25 if flt else 1)))
        edit("number huge", lambda d: d.__setitem__(k, 1 << 40))
        edit("number negative", lambda d: d.__setitem__(k, -5))
        edit("number 1.5", lambda d: d.__setitem__(k, 1.5))
        edit("number 2.0", lambda d: d.__setitem__(k, 2.0))
        edit("number as string", lambda d: d.__setitem__(k, "12"))
        edit("number null", lambda d: d.__setitem__(k, None))
        edit("number true", lambda d: d.__setitem__(k, True))
        edit("number 3.4e38", lambda d: d.__setitem__(k, 3.4e38))
        edit("number 1e39", lambda d: d.__setitem__(k, 1e39))
        edit("number removed", lambda d: d.pop(k))
    if arrs:
        k, p = rnd.choice(arrs)
        edit("array short", lambda d: d.__setitem__(k, d[k][:-1]))
        edit("array item 0.1", lambda d: d[k].__setitem__(0, 0.1))
    if enums:
        k, p = rnd.choice(enums)
        names = list(p["x-enum"].values())
        edit("enum other", lambda d: d.__setitem__(k, rnd.choice(names)))
        edit("enum unknown", lambda d: d.__setitem__(k, "no_such_name"))
        edit("enum number", lambda d: d.__setitem__(k, 3))
    if flags:
        k, p = rnd.choice(flags)
        names = list(p["x-flags"].values())
        edit("flags all", lambda d: d.__setitem__(k, names))
        edit("flags unknown", lambda d: d.__setitem__(k, ["nope"]))
        edit("flags number", lambda d: d.__setitem__(k, [names[0], 64]))
        edit("flags float", lambda d: d.__setitem__(k, [1.5]))
    if texs:
        k, p = rnd.choice(texs)
        cur = doc.get(k)
        edit("texture null", lambda d: d.__setitem__(k, None))
        if isinstance(cur, str):
            m = XS.BASE_RE.match(cur)
            if m:
                arch, pk, kind, idx, rest = m.groups()
                edit("texture other", lambda d: d.__setitem__(k, f"base://{arch}/{pk}/texture/{int(idx.split(':')[-1]) + 1}"))
                edit("texture share", lambda d: d.__setitem__(k, "base://HW_Cars/share_2/texture/3"))
                edit("texture elsewhere", lambda d: d.__setitem__(k, "base://ch1_l2/world_0/texture/3"))
                edit("texture not base", lambda d: d.__setitem__(k, "xyz"))
        edit("texture number", lambda d: d.__setitem__(k, 5))
    if objs:
        k, p = rnd.choice(objs)
        cur = doc.get(k)
        edit("object null", lambda d: d.__setitem__(k, None))
        edit("object uid", lambda d: d.__setitem__(k, "uid://abc"))
        edit("object foreign", lambda d: d.__setitem__(k, "base://HW_Cars/car_9/CarAInfo/0/engine"))
        edit("object root", lambda d: d.__setitem__(k, d["extends"].split("/CarAInfo/")[0] + "/CarAInfo/0"))
        edit("object engine", lambda d: d.__setitem__(k, d["extends"].split("/CarAInfo/")[0] + "/CarAInfo/0/engine"))
        edit("object bad field", lambda d: d.__setitem__(k, d["extends"].split("/CarAInfo/")[0] + "/CarAInfo/0/nosuch"))
        edit("object index", lambda d: d.__setitem__(k, d["extends"].split("/CarAInfo/")[0] + "/CarAInfo/0/wheels/1"))
        edit("object deep", lambda d: d.__setitem__(k, d["extends"].split("/CarAInfo/")[0] + "/CarAInfo/0/engine/torqueCurve/2"))
        if isinstance(cur, str):
            edit("object same", lambda d: d.__setitem__(k, cur))
    edit("unknown field", lambda d: d.__setitem__("nosuch", 1))
    edit("no extends", lambda d: d.pop("extends"))
    edit("extends elsewhere", lambda d: d.__setitem__("extends", "base://HW_Cars/car_1/CarAInfo/0"))
    edit("format 2", lambda d: d.__setitem__("format_version", 2))
    return out


def t_build(a, files):
    S = XS.Schemas()
    P = XS.Pairing(XS.find_up("docs/xdl-format/pairing"))
    rnd = random.Random(7)
    work = a.out / "build"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    bad, n, both_raise = [], 0, {}
    stats = {}
    vdocs = []
    for f in files[:: max(1, len(files) // a.n_build)]:
        rel, pname, fname = f.parts[-3:]
        pack = pack_dir(a, rel, pname)
        doc = XS.jsonc_loads(f.read_text())
        doc.pop("$schema", None)
        m = XS.BASE_RE.match(doc["extends"])
        rest = [x for x in m.group(5).split("/") if x]
        sname = XS.PARTS[rest[0]] if rest else "CarAInfo"
        for label, d in mutations(doc, S, sname, rnd, P):
            n += 1
            src = work / f"{n}.jsonc"
            src.write_text(json.dumps(d, indent=1))
            vdocs.append((sname, src))
            ob_p, ob_r = work / f"{n}.py.bin", work / f"{n}.rs.bin"
            w = py_main(["build", src, pack, "-o", ob_p])
            g = rs("struct", "build", src, pack, "-o", ob_r)
            what = f"{rel} {pname} {fname} [{label}]"
            n += 1
            wn = py_build_novalidate(src, pack, work / f"{n}.pyn.bin")
            gn = rs("struct", "build", src, pack, "-o", work / f"{n}.rsn.bin", "--no-validate", "1")
            if wn[0] == "EXC":
                k = f"{wn[1]}: {wn[2][:60]} (no validation)"
                both_raise[k] = both_raise.get(k, 0) + 1
                if gn[0] == 0:
                    bad.append(f"{what} (no validation): python raised {wn[1]}, rust succeeded")
            elif wn != gn:
                bad.append(f"{what} (no validation): python {wn} | rust {gn}")
            elif wn[0] == 0 and (work / f"{n}.pyn.bin").read_bytes() != (work / f"{n}.rsn.bin").read_bytes():
                bad.append(f"{what} (no validation): bytes differ")
            stats[(w[0] == 0, wn[0] == 0)] = stats.get((w[0] == 0, wn[0] == 0), 0) + 1
            if w[0] == "EXC":
                # the Python stops with a traceback; the port must fail too
                both_raise[f"{w[1]}: {w[2][:60]}"] = both_raise.get(f"{w[1]}: {w[2][:60]}", 0) + 1
                if g[0] == 0:
                    bad.append(f"{what}: python raised {w[1]}, rust succeeded")
                continue
            if w != g:
                bad.append(f"{what}: python {w} | rust {g}")
                continue
            if w[0] == 0 and ob_p.exists():
                if ob_p.read_bytes() != ob_r.read_bytes():
                    bad.append(f"{what}: bytes differ")
                rp = json.loads(Path(str(ob_p) + ".relocs.json").read_text())
                rr = json.loads(Path(str(ob_r) + ".relocs.json").read_text())
                if rp != rr:
                    bad.append(f"{what}: relocations differ")
    notes = [f"python uncaught {k} (x{v}); the port fails there too" for k, v in sorted(both_raise.items())]
    notes.append("built / refused, checked then unchecked: " + ", ".join(
        f"{'built' if a_ else 'refused'}+{'built' if b_ else 'refused'} x{v}" for (a_, b_), v in sorted(stats.items())))
    report("struct build (edits)", n, bad, notes)
    return vdocs


def t_validate(a, vdocs):
    S = XS.Schemas()
    items = list(vdocs)
    for d in sorted((HERE / "level-samples").glob("*/triggers/*.jsonc")):
        items.append(("Trigger", d))
    for d in sorted((HERE / "level-samples").glob("*/level.jsonc")):
        items.append(("Level", d))
    for d in sorted((HERE / "mod-samples").rglob("mod.jsonc")):
        items.append(("Mod", d))
    bad = []
    by = {}
    for t, p in items:
        by.setdefault(t, []).append(p)
    n = 0
    for t, ps in by.items():
        if t not in S.top:
            continue
        v = XS.MiniValidator(S.top[t][0])
        for i in range(0, len(ps), 200):
            chunk = ps[i:i + 200]
            code, out, err = rs("probe", "validate", t, *chunk)
            for p, g in zip(chunk, out.splitlines()):
                n += 1
                try:
                    w = json.dumps(v.iter_errors(XS.jsonc_loads(p.read_text())))
                except Exception as e:  # noqa: BLE001
                    w = f"ERR {kind_of(e)} {e}"
                if w != g:
                    bad.append(f"{t} {p}: python {w[:150]} | rust {g[:150]}")
    report("validate (MiniValidator)", n, bad)


def t_observe(a):
    rels = ",".join(survey_rels(a))
    po, ro = a.out / "observed-py.json", a.out / "observed-rs.json"
    for p in (po, ro):
        shutil.copy(HERE / "schema_observed.json", p)
    XS.OBSERVED = po
    w = py_main(["observe", a.survey, "--rel", rels])
    g = rs("struct", "observe", a.survey, "--rel", rels, "--out", ro)
    bad = []
    if w[0] != g[0] or w[1].split(":")[1:] != g[1].split(":")[1:]:
        bad.append(f"output: {w} | {g}")
    if po.read_bytes() != ro.read_bytes():
        bad.append("the written JSON differs")
    report("struct observe", 1, bad)


def t_tac(a):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        st, fl = XT.survey(str(a.survey))
        for k, v in sorted(st.items()):
            print(f"{v:6}  {k}")
        print(f"\n{sum(st.values())} lists, {sum(fl.values())} not decoded:")
        for k, v in fl.most_common():
            print(f"{v:6}  {k}")
    g = rs("tac", "survey", a.survey)
    bad = [] if out.getvalue() == g[1] else ["survey output differs"]
    report("tac survey (decode + encode of every retail list)", sum(st.values()), bad)


def tac_docs(rnd):
    """(label, doc) of the level samples' triggers, as the level tool prepares them, and edits."""
    out = []
    for p in sorted((HERE / "level-samples").glob("*/triggers/*.jsonc")):
        raw = XS.jsonc_loads(p.read_text())
        out.append((f"{p} raw", raw))
        d = json.loads(json.dumps(raw))
        for k in ("$schema", "format_version", "id"):
            d.pop(k, None)
        for lst in ("actions", "elseActions"):
            for x in d.get(lst) or []:
                if isinstance(x, dict):
                    if x.get("count") == "always":
                        x["count"] = 0xFFFFFFFF
                    x.setdefault("count", 1)
                    if "delayInit" in x:
                        x.setdefault("delay", x["delayInit"])
        out.append((f"{p} prepared", d))

        def edit(label, f, base=d):
            e = json.loads(json.dumps(base))
            try:
                f(e)
            except (KeyError, IndexError, TypeError, AttributeError):
                return
            out.append((f"{p} {label}", e))

        edit("type unknown", lambda e: e.__setitem__("type", "nope"))
        edit("type number", lambda e: e.__setitem__("type", 3))
        edit("no type", lambda e: e.pop("type"))
        edit("param list", lambda e: e.__setitem__("param", ["x"]))
        edit("param null", lambda e: e.__setitem__("param", None))
        edit("param extra", lambda e: e["param"].__setitem__("extra", 1))
        edit("param tail hex", lambda e: e["param"].__setitem__("tail", "0000 0001"))
        edit("param tail bad", lambda e: e["param"].__setitem__("tail", "xyz"))
        edit("param tail words", lambda e: e["param"].__setitem__("tail", [1, "level://x", None]))
        edit("actions empty", lambda e: e.__setitem__("actions", []))
        edit("actions link", lambda e: e.__setitem__("actions", "level://a"))
        edit("unknown field", lambda e: e.__setitem__("nosuch", 1))
        edit("mode name", lambda e: e.__setitem__("mode", "nope"))
        edit("state flags", lambda e: e.__setitem__("state", ["nope"]))
        edit("state ints", lambda e: e.__setitem__("state", [1, 4]))
        edit("unk bytes", lambda e: e.__setitem__("unk_0x29", [1, 2, 300]))
        edit("unk short", lambda e: e.__setitem__("unk_0x29", [1, 2]))
        for lst in ("actions", "conditions"):
            for i, x in enumerate(d.get(lst) or []):
                if not isinstance(x, dict):
                    continue
                key = "data" if lst == "actions" else "param"
                prm = x.get(key)
                if isinstance(prm, dict):
                    for pk, pv in prm.items():
                        if isinstance(pv, (int, float)) and not isinstance(pv, bool):
                            edit(f"{lst}{i}.{pk}=1.5", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, 1.5))
                            edit(f"{lst}{i}.{pk}=-1", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, -1))
                            edit(f"{lst}{i}.{pk}=2^32", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, 1 << 32))
                            edit(f"{lst}{i}.{pk}=str", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, "x"))
                        elif isinstance(pv, list):
                            edit(f"{lst}{i}.{pk}=num", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, 5))
                            edit(f"{lst}{i}.{pk}+=7", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key][pk].append(7))
                        elif isinstance(pv, dict):
                            edit(f"{lst}{i}.{pk}={{}}", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, {}))
                            edit(f"{lst}{i}.{pk}.x", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key][pk].__setitem__("x", 1))
                            edit(f"{lst}{i}.{pk}.kind", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key][pk].__setitem__("kind", "nope"))
                        elif isinstance(pv, str):
                            edit(f"{lst}{i}.{pk}=nope", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, "nope"))
                            edit(f"{lst}{i}.{pk}=7", lambda e, lst=lst, i=i, key=key, pk=pk: e[lst][i][key].__setitem__(pk, 7))
                edit(f"{lst}{i}.type", lambda e, lst=lst, i=i: e[lst][i].__setitem__("type", "nope"))
                edit(f"{lst}{i}.count", lambda e, lst=lst, i=i: e[lst][i].__setitem__("count", -1))
    return out


def t_tacenc(a):
    T = XT.Tac()
    rnd = random.Random(3)
    work = a.out / "tacenc"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    bad, n = [], 0
    for label, doc in tac_docs(rnd):
        for e, rel in ((">", "gc"), ("<", "pc")):
            n += 1
            src = work / f"{n}.jsonc"
            src.write_text(json.dumps(doc))
            try:
                parts = XT.encode_trigger(T, doc, "level://triggers/t", e, "w")
                w = json.dumps([[i, b.hex(), {str(o): v for o, v in sorted(ln.items())}] for i, b, ln in parts])
            except Exception as ex:  # noqa: BLE001
                w = f"ERR {kind_of(ex)} {ex}"
            g = rs("probe", "tac-encode", src, "level://triggers/t", rel, "w")[1].strip()
            if w != g and not (w.startswith("ERR Uncaught") and g.startswith("ERR")):
                bad.append(f"{label} {rel}: python {w[:160]} | rust {g[:160]}")
    report("tac encode_trigger (samples and edits)", n, bad)


def t_tacext(a):
    import xdl_graph as GR
    T = XT.Tac()
    S = XS.Schemas()
    worlds = []
    for rel in survey_rels(a):
        for m in sorted((a.survey / rel).rglob("manifest.json")):
            man = json.loads(m.read_text())
            for ent in man["entries"]:
                if ent["type"] == "world":
                    worlds.append(m.parent / ent["path"])
    rnd = random.Random(5)
    pick = worlds if a.n_worlds >= len(worlds) else rnd.sample(worlds, a.n_worlds)
    bad, n, skipped = [], 0, 0
    for pdir in pick:
        tag = f"{pdir.parent.parent.name}-{pdir.parent.name}-{pdir.name}"
        host = a.out / "tacext" / f"{tag}.json"
        host.parent.mkdir(parents=True, exist_ok=True)
        expect = []
        src = GR.Source(pdir)
        ex = GR.Extractor(src, a.out / "tacext" / f"{tag}-proj", S, force=True)
        orig = ex.compose_triggers

        def hook(ex=ex, src=src, host=host, expect=expect, orig=orig):
            idx = {id(nd): i for i, nd in enumerate(ex.nodes)}
            links = []
            for t in sorted(set(src.ptr.values())):
                try:
                    links.append([t, ex.link_to(t, elem=True)])
                except Exception as e:  # noqa: BLE001
                    links.append([t, None, str(e)])
            trig = [i for i, nd in enumerate(ex.nodes)
                    if ex.tables_at.get(nd.start, ("",))[0] == "trigger" and nd.kind == "raw"]
            blob = host.with_suffix(".bin")
            blob.write_bytes(src.blob)
            host.write_text(json.dumps({
                "blob": str(blob), "big": src.e == ">",
                "ptr": sorted(src.ptr.items()),
                "rel": {k: sorted(src.rel[k].items()) for k in ("ref", "texture", "sound", "shader")},
                "links": links,
                "nodes": [[nd.start, nd.end, idx.get(id(nd.parent)) if nd.parent is not None else None,
                           nd.kind == "raw", bool(nd.fixed), nd.label] for nd in ex.nodes],
                "by_start": [[s, idx[id(nd)]] for s, nd in ex.by_start.items()],
                "kinds": sorted(ex.kinds), "triggers": trig}))
            for i in trig:
                nd = ex.nodes[i]
                x = XT.Extract(T, ex)
                if nd.end - nd.start < T.size["Trigger"] or not x.zero_tail(nd, T.size["Trigger"]):
                    expect.append({"start": nd.start, "skip": True})
                    continue
                try:
                    doc = x.trigger(nd)
                    expect.append({"start": nd.start, "doc": XS.dump_value(doc, 0),
                                   "parts": [p.start for p in x.parts], "pads": {str(k): v for k, v in x.pads.items()}})
                except Exception as e:  # noqa: BLE001
                    expect.append({"start": nd.start, "error": kind_of(e), "msg": str(e)})
            return orig()

        ex.compose_triggers = hook
        ex.run()
        code, out, err = rs("probe", "tac-extract", host)
        got = json.loads(out) if code == 0 else []
        n += len(expect)
        if len(got) != len(expect):
            bad.append(f"{tag}: {len(expect)} triggers vs {len(got)} ({err.strip()[:200]})")
            continue
        for w, g in zip(expect, got):
            if w.get("error") == "Uncaught" and "error" in g:
                continue
            if w != g:
                bad.append(f"{tag} @0x{w['start']:X}: python {json.dumps(w)[:200]} | rust {json.dumps(g)[:200]}")
        skipped += sum(1 for w in expect if w.get("skip"))
        shutil.rmtree(a.out / "tacext" / f"{tag}-proj", ignore_errors=True)
    report(f"tac Extract ({len(pick)} world packs)", n, bad,
           [f"{skipped} trigger records not decoded by either (record size)"] if skipped else [])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--survey", default=None)
    ap.add_argument("--out", default=str(ROOT / "build" / "xdlrs-parity"))
    ap.add_argument("--only", default="")
    ap.add_argument("--quick", action="store_true", help="fewer samples")
    a = ap.parse_args()
    a.survey = Path(a.survey) if a.survey else next(
        (d for d in (ROOT / "build-survey", ROOT.parent / "build-survey") if d.is_dir()), ROOT / "build-survey")
    a.out = Path(a.out)
    a.out.mkdir(parents=True, exist_ok=True)
    a.n_f32 = 20000 if a.quick else 200000
    a.n_extract = 20 if a.quick else 120
    a.n_build = 20 if a.quick else 200
    a.n_worlds = 4 if a.quick else 30
    if not BIN.exists():
        raise SystemExit(f"{BIN}: run `cargo build --release` in tools/xdlrs first")
    only = set(a.only.split(",")) if a.only else None

    def on(n):
        return only is None or n in only

    files = []
    if on("schemas"):
        t_schemas(a)
    if on("f32"):
        t_f32(a)
    if on("roundtrip") or on("extract") or on("build") or on("jsonc") or on("validate"):
        files = t_roundtrip(a)
    if on("jsonc"):
        t_jsonc(a, files)
    if on("extract"):
        t_extract(a, files)
    vdocs = []
    if on("build") or on("validate"):
        vdocs = t_build(a, files)
    if on("validate"):
        t_validate(a, vdocs)
    if on("observe"):
        t_observe(a)
    if on("tac"):
        t_tac(a)
    if on("tacenc"):
        t_tacenc(a)
    if on("tacext"):
        t_tacext(a)
    fails = sum(1 for r in RESULTS if r[2])
    print(f"\n{len(RESULTS) - fails}/{len(RESULTS)} checks identical")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
