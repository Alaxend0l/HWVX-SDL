//! xdlrs: the crate's commands, mirroring the Python tools' (same output for the same input).
//!
//!   xdlrs struct extract PACK_DIR [--struct CarAInfo] [--root 0] [--path engine] [-o car.jsonc]
//!   xdlrs struct build FILE.jsonc PACK_DIR [-o out.bin]
//!   xdlrs struct roundtrip SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--struct CarAInfo] [--parts] [--out DIR] [-v]
//!   xdlrs struct observe SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--struct CarAInfo] [--out FILE]
//!   xdlrs tac survey SURVEY_ROOT
//!
//! Probes the parity test (tools/xdlrs_parity.py) uses:
//!   xdlrs probe jsonc FILE...            loads: "OK <json.dumps>" / "ERR <kind> <message>" per file
//!   xdlrs probe dump FILE                dump_value of the file's JSON
//!   xdlrs probe f32text                  stdin: hex f32 bit patterns -> f32_text
//!   xdlrs probe schemas                  every struct's fields, offsets, C types and sizes (JSON)
//!   xdlrs probe validate TITLE FILE...   MiniValidator messages per file (JSON)
//!   xdlrs probe tac-encode FILE TID gc|pc [PATH]   encode_trigger's parts (JSON)
//!   xdlrs probe tac-extract HOST.json    Extract over a host dumped by the parity test (JSON)
//!
//!   xdlrs mod index DATA... [-o OUT.npz] [--archives A,B] [--no-geometry] [-j N]
//!   xdlrs mod check MOD --index INDEX [--strip-unchanged]
//!   xdlrs mod pack MOD -o OUT.xmod --index INDEX [--strip-unchanged] [--allow-partial-index]
//!                                        as tools/xdl_mod.py (the no-game-data check)
//!   xdlrs mod probe png|audio|fp|blobs FILE   what the check reads from a file (JSON)
//!
//! --repo DIR: the HWVX-SDL checkout (schemas/); default: found above the current folder.

use serde_json::{Map, Value, json};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use xdlrs::error::{Error, Kind, Result};
use xdlrs::pack::{self, BaseLink, Pack, Pairing};
use xdlrs::schema::{self, Schemas};
use xdlrs::tac::{self, Tac, TacHost, TacNode};
use xdlrs::validate::MiniValidator;
use xdlrs::{jsonc, ops, py};

struct Args {
    pos: Vec<String>,
    opts: HashMap<String, String>,
    flags: Vec<String>,
}

fn parse_args(a: &[String]) -> Args {
    let mut r = Args { pos: vec![], opts: HashMap::new(), flags: vec![] };
    let mut i = 0;
    while i < a.len() {
        let x = &a[i];
        if ["--parts", "-v", "--verbose"].contains(&x.as_str()) {
            r.flags.push(x.clone());
        } else if x.starts_with('-') && x.len() > 1 && i + 1 < a.len() {
            r.opts.insert(x.trim_start_matches('-').to_string(), a[i + 1].clone());
            i += 1;
        } else {
            r.pos.push(x.clone());
        }
        i += 1;
    }
    r
}

impl Args {
    fn opt(&self, k: &str, d: &str) -> String {
        self.opts.get(k).cloned().unwrap_or_else(|| d.to_string())
    }
    fn flag(&self, k: &str) -> bool {
        self.flags.iter().any(|f| f == k)
    }
    fn pos(&self, i: usize) -> Result<&str> {
        self.pos.get(i).map(String::as_str).ok_or_else(|| Error::uncaught("missing argument"))
    }
}

struct Ctx {
    root: PathBuf,
    s: Schemas,
    pairing: Pairing,
}

fn ctx(a: &Args) -> Result<Ctx> {
    let root = match a.opts.get("repo") {
        Some(r) => PathBuf::from(r),
        None => xdlrs::repo_root(std::env::current_dir().ok().as_deref()).ok_or_else(|| Error::io("no schemas/ found (--repo)"))?,
    };
    let root = root.canonicalize().map_err(|e| Error::io(e.to_string()))?;
    let s = Schemas::load(&root.join("schemas"))?;
    let pairing = Pairing::new(xdlrs::pairing_dir(&root));
    Ok(Ctx { root, s, pairing })
}

fn write(p: &Path, b: &[u8]) -> Result<()> {
    std::fs::write(p, b).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}

// ------------------------------------------------------------------------------------------
// struct

fn cmd_extract(a: &Args) -> Result<i32> {
    let c = ctx(a)?;
    let pk = Pack::open(Path::new(a.pos(0)?))?;
    let st = a.opt("struct", "CarAInfo");
    let root: i64 = a.opt("root", "0").parse().unwrap_or(0);
    let pathstr = a.opt("path", "");
    let path: Vec<&str> = pathstr.split('/').filter(|x| !x.is_empty()).collect();
    let sref_name = match path.first() {
        Some(p) => pack::PARTS.iter().find(|x| x.0 == *p).map(|x| x.1).unwrap_or(&st),
        None => &st,
    };
    let sref = c.s.path(sref_name).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(sref_name))))?.display().to_string();
    let ex = ops::extract(&c.s, &pk, Some(&c.pairing), &st, root, &path, Some(&sref))?;
    let text = jsonc::to_jsonc(&ex.doc, c.s.struct_(&ex.sname).unwrap(), &[format!("{} {} ({}), from tools/xdl_struct.py", ex.sname, ex.oid, pk.release)]);
    match a.opts.get("o") {
        Some(o) => write(Path::new(o), text.as_bytes())?,
        None => print!("{text}"),
    }
    Ok(0)
}

fn cmd_build(a: &Args) -> Result<i32> {
    let c = ctx(a)?;
    let doc = jsonc::loads(&xdlrs_read(Path::new(a.pos(0)?))?)?;
    let pk = Pack::open(Path::new(a.pos(1)?))?;
    let ext = doc.get("extends").filter(|e| !e.is_null() && e.as_str() != Some(""));
    let Some(ext) = ext else {
        return Err(Error::xdl("build needs an extends object for now (the base bytes)"));
    };
    let m = BaseLink::parse(&py::str_of(ext)).ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'groups'"))?;
    let idx: i64 = m.idx.parse().map_err(|_| Error::uncaught(format!("invalid literal for int() with base 10: {}", py::str_repr(&m.idx))))?;
    let (off, sname, _) = ops::locate(&c.s, &pk, &m.kind, idx, &m.path())?;
    let top = c.s.top(&sname).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&sname))))?;
    // --no-validate 1: straight to the encoder (the parity test's probe of its errors)
    let errs = if a.opts.contains_key("no-validate") { vec![] } else { MiniValidator::new(top).errors(&doc) };
    for e in &errs {
        eprintln!("schema: {e}");
    }
    if !errs.is_empty() {
        return Ok(1);
    }
    let (data, rel) = ops::build(&c.s, &pk, Some(&c.pairing), &doc, &sname, off, None)?;
    if let Some(o) = a.opts.get("o") {
        write(Path::new(o), &data)?;
        write(Path::new(&format!("{o}.relocs.json")), format!("{}\n", py::dumps_indent(&rel.to_json(), 1, false)).as_bytes())?;
    }
    let lo = (off as usize).min(pk.blob.len());
    let same = data.as_slice() == &pk.blob[lo..(off as usize + data.len()).min(pk.blob.len())];
    println!(
        "{sname}: 0x{:X} bytes, {} relocations; {} the base",
        data.len(),
        rel.count(),
        if same { "equal to" } else { "differs from" }
    );
    Ok(0)
}

fn xdlrs_read(p: &Path) -> Result<String> {
    std::fs::read_to_string(p).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}

fn parts_of(a: &Args) -> Vec<Vec<&'static str>> {
    let mut t = vec![vec![]];
    if a.flag("--parts") {
        t.extend(pack::PARTS.iter().map(|p| vec![p.0]));
    }
    t
}

fn cmd_roundtrip(a: &Args) -> Result<i32> {
    let c = ctx(a)?;
    let survey = PathBuf::from(a.pos(0)?);
    let out = a.opts.get("out").map(PathBuf::from);
    let st = a.opt("struct", "CarAInfo");
    let rk = pack::root_kind(&st).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&st))))?;
    let (mut total, mut fails) = (0, 0);
    let verbose = a.flag("-v") || a.flag("--verbose");
    for rel in a.opt("rel", "gc,pc").split(',') {
        for pdir in ops::survey_packs(&survey, rel, &a.opt("archive", "HW_Cars"))? {
            let pk = Pack::open(&pdir)?;
            if py::str_of(&pk.entry["type"]) != rk {
                continue;
            }
            let targets = if a.flag("--parts") { parts_of(a) } else { vec![vec![]] };
            for path in targets {
                total += 1;
                let label = format!("{rel} {}{}", pk.name, path.first().map(|p| format!(" {p}")).unwrap_or_default());
                let (ok, msg) = match ops::roundtrip_one(&c.s, &pk, &c.pairing, &st, &path, out.as_deref(), rel) {
                    Ok(x) => x,
                    Err(e) if e.kind == Kind::Xdl => (false, e.msg),
                    Err(e) => return Err(e),
                };
                if !ok {
                    fails += 1;
                    println!("FAIL {label}: {msg}");
                } else if verbose {
                    println!("ok   {label}: {msg}");
                }
            }
        }
    }
    println!("{}/{total} byte-identical round trips (bytes and relocations)", total - fails);
    Ok(if fails > 0 { 1 } else { 0 })
}

fn cmd_observe(a: &Args) -> Result<i32> {
    let c = ctx(a)?;
    let survey = PathBuf::from(a.pos(0)?);
    let st = a.opt("struct", "CarAInfo");
    let archive = a.opt("archive", "HW_Cars");
    let rk = pack::root_kind(&st).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&st))))?;
    // sname -> key -> (values, objects {(rel, pack)}, float field)
    let mut vals: Vec<(String, Vec<(String, Vec<Value>, BTreeSet<(String, String)>, bool)>)> = vec![];
    for rel in a.opt("rel", "gc,pc").split(',') {
        for pdir in ops::survey_packs(&survey, rel, &archive)? {
            let pk = Pack::open(&pdir)?;
            if py::str_of(&pk.entry["type"]) != rk {
                continue;
            }
            let mut targets = vec![vec![]];
            targets.extend(pack::PARTS.iter().map(|p| vec![p.0]));
            for path in targets {
                let ex = ops::extract(&c.s, &pk, Some(&c.pairing), &st, 0, &path, None)?;
                let sch = c.s.struct_(&ex.sname).unwrap();
                let si = match vals.iter().position(|v| v.0 == ex.sname) {
                    Some(i) => i,
                    None => {
                        vals.push((ex.sname.clone(), vec![]));
                        vals.len() - 1
                    }
                };
                for (key, p) in schema::fields(sch) {
                    if p.get("x-link").is_some() || p.get("$ref").is_some() || p.get("items").and_then(|i| i.get("x-link")).is_some() {
                        continue;
                    }
                    let flt = schema::prim(&schema::parse_ctype(schema::ctype(p)).0).is_some_and(|x| x.float);
                    let ks = &mut vals[si].1;
                    let ki = match ks.iter().position(|k| &k.0 == key) {
                        Some(i) => i,
                        None => {
                            ks.push((key.clone(), vec![], BTreeSet::new(), flt));
                            ks.len() - 1
                        }
                    };
                    ops::observe_values(&ex.doc[key.as_str()], &mut ks[ki].1);
                    ks[ki].2.insert((rel.to_string(), pk.name.clone()));
                }
            }
        }
    }
    let path = a.opts.get("out").map(PathBuf::from).unwrap_or_else(|| c.root.join("tools/schema_observed.json"));
    let mut out: Map<String, Value> = if path.exists() { jsonc::parse(&xdlrs_read(&path)?)?.as_object().cloned().unwrap_or_default() } else { Map::new() };
    for (sname, ks) in &vals {
        let mut o = Map::new();
        for (key, vs, objs, flt) in ks {
            if vs.is_empty() {
                continue;
            }
            // the true f32 of a decoded value (its text reads back as it)
            let nums: Vec<py::Num> = vs
                .iter()
                .map(|v| {
                    let n = py::Num::of(v).unwrap();
                    if *flt { py::Num::Float((n.f64() as f32) as f64) } else { n }
                })
                .collect();
            let mut mn = nums[0];
            let mut mx = nums[0];
            for &n in &nums[1..] {
                if n.py_lt(mn) {
                    mn = n;
                }
                if mx.py_lt(n) {
                    mx = n;
                }
            }
            let fmt = |n: py::Num| -> Result<Value> {
                match n {
                    py::Num::Int(i) => Ok(Value::from(i as i64)),
                    py::Num::Float(f) => {
                        let t = py::f32_text(f, false)?;
                        Ok(serde_json::Number::from_f64(t.parse::<f64>().unwrap()).map(Value::Number).unwrap_or(Value::Null))
                    }
                }
            };
            let mut distinct: Vec<py::Num> = vec![];
            for &n in &nums {
                if !distinct.iter().any(|d| d.py_eq(n)) {
                    distinct.push(n);
                }
            }
            let packs: BTreeSet<&String> = objs.iter().map(|x| &x.1).collect();
            let rels: BTreeSet<&String> = objs.iter().map(|x| &x.0).collect();
            o.insert(
                key.clone(),
                json!({"min": fmt(mn)?, "max": fmt(mx)?, "distinct": distinct.len(), "objects": packs.len(),
                       "releases": rels.into_iter().collect::<Vec<_>>(), "archive": archive}),
            );
        }
        out.insert(sname.clone(), Value::Object(o));
    }
    let n: usize = out.values().map(|v| v.as_object().map(|m| m.len()).unwrap_or(0)).sum();
    write(&path, format!("{}\n", py::dumps_indent(&Value::Object(out), 1, true)).as_bytes())?;
    let shown = path.strip_prefix(&c.root).map(|p| p.display().to_string()).unwrap_or_else(|_| path.display().to_string());
    println!("{shown}: {n} fields");
    Ok(0)
}

// ------------------------------------------------------------------------------------------
// tac survey

struct Source {
    blob: Vec<u8>,
    big: bool,
    ptr: BTreeMap<u32, u32>,
    header: Value,
}

impl Source {
    fn open(dir: &Path) -> Result<Source> {
        let man = jsonc::parse(&xdlrs_read(&dir.parent().unwrap().join("manifest.json"))?)?;
        let big = man["platform"].as_str() == Some("gc");
        let blob = std::fs::read(dir.join("data.bin")).map_err(|e| Error::io(e.to_string()))?;
        let header = jsonc::parse(&xdlrs_read(&dir.join("pack.json"))?)?["header"].clone();
        let mut ptr = BTreeMap::new();
        if let Ok(b) = std::fs::read(dir.join("pointers.bin")) {
            for ch in b.chunks_exact(4) {
                let o = if big { u32::from_be_bytes(ch.try_into().unwrap()) } else { u32::from_le_bytes(ch.try_into().unwrap()) };
                ptr.insert(o, pack::u32_at(&blob, o, big)?);
            }
        }
        Ok(Source { blob, big, ptr, header })
    }
    fn u32(&self, o: u32) -> Result<u32> {
        pack::u32_at(&self.blob, o, self.big)
    }
}

#[derive(Default)]
struct Counter(Vec<(String, usize)>);

impl Counter {
    fn add(&mut self, k: String) {
        match self.0.iter_mut().find(|x| x.0 == k) {
            Some(x) => x.1 += 1,
            None => self.0.push((k, 1)),
        }
    }
    fn total(&self) -> usize {
        self.0.iter().map(|x| x.1).sum()
    }
}

fn cmd_tac_survey(a: &Args) -> Result<i32> {
    let c = ctx(a)?;
    let t = Tac::from_schemas(&c.s)?;
    let root = PathBuf::from(a.pos(0)?);
    let mut stats = Counter::default();
    let mut fails = Counter::default();
    for rel in ["gc", "pc", "ps2"] {
        let mut mans = vec![];
        ops::walk_manifests(&root.join(rel), &mut mans);
        for m in mans {
            let arch = m.parent().unwrap();
            let man = jsonc::parse(&xdlrs_read(&m)?)?;
            for ent in man["entries"].as_array().cloned().unwrap_or_default() {
                if ent["type"].as_str() != Some("world") {
                    continue;
                }
                let src = Source::open(&arch.join(py::str_of(&ent["path"])))?;
                let mut targets: Vec<u32> = src.ptr.values().copied().collect::<BTreeSet<_>>().into_iter().collect();
                targets.push(src.blob.len() as u32);
                targets.sort();
                targets.dedup();
                let r = tac::Reader { blob: &src.blob, big: src.big, slots: src.ptr.iter().map(|(o, t)| (*o, Value::String(format!("@{t:X}")))).collect() };
                let mut one = |en: &str, kind_v: u32, slot: u32| -> Result<()> {
                    let p = src.ptr.get(&slot).copied();
                    let rec = if en == "ActionType" { "TriggerAction" } else { "Trigger" };
                    let names = &t.structs[rec]["properties"]["type"]["x-enum"];
                    let k = names.get(kind_v.to_string()).cloned().unwrap_or(Value::from(kind_v));
                    let key = format!("{en}.{}", py::str_of(&k));
                    stats.add(key.clone());
                    let Some(p) = p else { return Ok(()) };
                    let end = targets[targets.partition_point(|&x| x <= p)];
                    let res = (|| -> Result<()> {
                        let lay = k
                            .as_str()
                            .and_then(|kk| t.variants.get(en).and_then(|v| v.get(kk)))
                            .ok_or_else(|| Error::new(Kind::Uncaught, format!("KeyError: {}", py::repr(&k))))?;
                        let (obj, _) = tac::decode_params(&t, &r, p, end, lay, &key)?;
                        let (b, links) = tac::encode_params(&t, &obj, lay, src.big, &key)?;
                        let mut want = src.blob[p as usize..end as usize].to_vec();
                        for o in links.keys() {
                            let o = *o as usize;
                            want[o..o + 4].fill(0);
                        }
                        if b != want {
                            return Err(Error::tac("bytes differ"));
                        }
                        let got: BTreeMap<u32, Value> = links.iter().map(|(o, v)| (p + o, v.clone())).collect();
                        let exp: BTreeMap<u32, Value> = r.slots.iter().filter(|(o, _)| p <= **o && **o < end).map(|(o, v)| (*o, v.clone())).collect();
                        if got != exp {
                            return Err(Error::tac("links differ"));
                        }
                        Ok(())
                    })();
                    match res {
                        Ok(()) => {}
                        Err(e) if e.kind == Kind::Tac || e.msg.starts_with("KeyError: ") => {
                            let m = e.msg.strip_prefix("KeyError: ").unwrap_or(&e.msg);
                            fails.add(format!("{key}: {}", m.rsplit(": ").next().unwrap()));
                        }
                        Err(e) => return Err(e),
                    }
                    Ok(())
                };
                fn trig(src: &Source, t: u32, depth: u32, one: &mut dyn FnMut(&str, u32, u32) -> Result<()>) -> Result<()> {
                    one("TriggerType", src.u32(t)?, t + 4)?;
                    for (lo, no) in [(0x14, 0x1C), (0x18, 0x20)] {
                        if let Some(&lst) = src.ptr.get(&(t + lo)) {
                            for j in 0..src.u32(t + no)? {
                                one("ActionType", src.u32(lst + j * 0x18)?, lst + j * 0x18 + 4)?;
                            }
                        }
                    }
                    if let Some(&c) = src.ptr.get(&(t + 0xC))
                        && depth < 2
                    {
                        for j in 0..src.u32(t + 0x10)? {
                            trig(src, c + j * 0x2C, depth + 1, one)?;
                        }
                    }
                    Ok(())
                }
                let n = src.header.get("num_triggers").and_then(Value::as_u64).unwrap_or(0) as u32;
                let base = src.header.get("trigger_offset").and_then(Value::as_u64).unwrap_or(0) as u32;
                for i in 0..n {
                    trig(&src, base + i * 0x2C, 0, &mut one)?;
                }
            }
        }
    }
    let mut st = stats.0.clone();
    st.sort_by(|a, b| a.0.cmp(&b.0));
    for (k, v) in &st {
        println!("{v:6}  {k}");
    }
    println!("\n{} lists, {} not decoded:", stats.total(), fails.total());
    let mut fl = fails.0.clone();
    fl.sort_by(|a, b| b.1.cmp(&a.1));
    for (k, v) in fl {
        println!("{v:6}  {k}");
    }
    Ok(0)
}

// ------------------------------------------------------------------------------------------
// probes

fn err_line(e: &Error) -> String {
    format!("ERR {:?} {}", e.kind, e.msg)
}

fn probe(a: &Args) -> Result<i32> {
    let what = a.pos(1)?;
    match what {
        "jsonc" => {
            for f in &a.pos[2..] {
                let line = match xdlrs_read(Path::new(f)).and_then(|t| jsonc::loads(&t)) {
                    Ok(v) => format!("OK {}", py::dumps(&v)),
                    Err(e) => err_line(&e),
                };
                println!("{line}");
            }
        }
        "dump" => {
            let v = jsonc::loads(&xdlrs_read(Path::new(a.pos(2)?))?)?;
            println!("{}", jsonc::dump_value(&v, 0, None));
        }
        "f32text" => {
            let mut s = String::new();
            std::io::stdin().read_to_string(&mut s).map_err(|e| Error::io(e.to_string()))?;
            for ln in s.lines() {
                let bits = u32::from_str_radix(ln.trim(), 16).unwrap_or(0);
                match py::f32_text(f32::from_bits(bits) as f64, false) {
                    Ok(t) => println!("{t}"),
                    Err(e) => println!("{}", err_line(&e)),
                }
            }
        }
        "schemas" => {
            let c = ctx(a)?;
            let mut out = Map::new();
            let mut names: Vec<(String, &Value)> = c.s.top.iter().map(|(t, v, _)| (t.clone(), v)).collect();
            let mut defs: Vec<(&String, &Value)> = c.s.defs.iter().collect();
            defs.sort_by(|a, b| a.0.cmp(b.0));
            names.extend(defs.into_iter().map(|(k, v)| (format!("$defs/{k}"), v)));
            for (n, sch) in names {
                let mut fs = vec![];
                for (k, p) in schema::fields(sch) {
                    let ct = schema::ctype(p);
                    let (base, dims) = schema::parse_ctype(ct);
                    let sz = match c.s.size_of(ct) {
                        Ok(x) => Value::from(x),
                        Err(e) => Value::String(e.msg),
                    };
                    let off = schema::offset(p).map(Value::from).unwrap_or(Value::Null);
                    fs.push(json!([k, off, ct, base, dims, sz]));
                }
                let size = schema::size(sch).map(Value::from).unwrap_or(Value::Null);
                out.insert(n, json!({"size": size, "fields": fs}));
            }
            println!("{}", py::dumps(&Value::Object(out)));
        }
        "validate" => {
            let c = ctx(a)?;
            let top = c.s.top(a.pos(2)?).ok_or_else(|| Error::uncaught("no such schema"))?;
            let v = MiniValidator::new(top);
            for f in &a.pos[3..] {
                let line = match xdlrs_read(Path::new(f)).and_then(|t| jsonc::loads(&t)) {
                    Ok(d) => py::dumps(&json!(v.errors(&d))),
                    Err(e) => err_line(&e),
                };
                println!("{line}");
            }
        }
        "tac-encode" => {
            let c = ctx(a)?;
            let t = Tac::from_schemas(&c.s)?;
            let doc = jsonc::loads(&xdlrs_read(Path::new(a.pos(2)?))?)?;
            let big = a.pos(4)? == "gc";
            let path = a.pos.get(5).cloned().unwrap_or_default();
            match tac::encode_trigger(&t, &doc, a.pos(3)?, big, &path) {
                Ok(parts) => {
                    let v: Vec<Value> = parts
                        .iter()
                        .map(|(id, b, l)| {
                            let lm: Map<String, Value> = l.iter().map(|(o, x)| (o.to_string(), x.clone())).collect();
                            json!([id, b.iter().map(|x| format!("{x:02x}")).collect::<String>(), lm])
                        })
                        .collect();
                    println!("{}", py::dumps(&Value::Array(v)));
                }
                Err(e) => println!("{}", err_line(&e)),
            }
        }
        "tac-extract" => {
            let c = ctx(a)?;
            let t = Tac::from_schemas(&c.s)?;
            let host = DumpHost::load(Path::new(a.pos(2)?))?;
            let mut res = vec![];
            for &h in &host.triggers {
                let mut x = tac::Extract::new(&t, &host);
                let size = t.size["Trigger"];
                let nd = host.nodes[h].clone();
                if nd.end - nd.start < size || !x.zero_tail(h, size) {
                    res.push(json!({"start": nd.start, "skip": true}));
                    continue;
                }
                match x.trigger(h) {
                    Ok(doc) => {
                        let parts: Vec<u32> = x.parts.iter().map(|&p| host.nodes[p].start).collect();
                        let pads: Map<String, Value> = x.pads.iter().map(|(a, b)| (a.to_string(), Value::from(*b))).collect();
                        res.push(json!({"start": nd.start, "doc": jsonc::dump_value(&doc, 0, None), "parts": parts, "pads": pads}));
                    }
                    Err(e) => res.push(json!({"start": nd.start, "error": format!("{:?}", e.kind), "msg": e.msg})),
                }
            }
            println!("{}", py::dumps(&Value::Array(res)));
        }
        "decode" => {
            // an object as Decoder writes it, and Encoder::symbolic of that over zeros
            let c = ctx(a)?;
            let pk = Pack::open(Path::new(a.pos(2)?))?;
            let sname = a.pos(3)?;
            let off: u32 = a.pos(4)?.parse().unwrap_or(0);
            let oid = format!("base://{}/{}/{sname}/0", pk.archive, pk.name);
            let sch = c.s.struct_(sname).ok_or_else(|| Error::uncaught("no such schema"))?;
            let dec = xdlrs::decode::Decoder::new(&c.s, &pk, Some(&c.pairing));
            let mut out = Map::new();
            match dec.struct_(sch, off, &oid) {
                Ok(doc) => {
                    let doc = Value::Object(doc);
                    out.insert("doc".into(), Value::String(jsonc::dump_value(&doc, 0, None)));
                    let size = schema::size(sch)?;
                    let sym = xdlrs::encode::Encoder::symbolic(&c.s, &pk, Some(&c.pairing), sname, vec![0; size])
                        .and_then(|mut e| {
                            let (b, _) = e.encode(&doc, None)?;
                            Ok((b, e.links.clone()))
                        });
                    match sym {
                        Ok((b, l)) => {
                            let lm: Map<String, Value> = l.iter().map(|(o, (k, v))| (o.to_string(), json!([k, v]))).collect();
                            out.insert("sym".into(), json!([b.iter().map(|x| format!("{x:02x}")).collect::<String>(), lm]));
                        }
                        Err(e) => {
                            out.insert("sym".into(), Value::String(err_line(&e)));
                        }
                    }
                }
                Err(e) => {
                    out.insert("doc".into(), Value::String(err_line(&e)));
                }
            }
            println!("{}", py::dumps(&Value::Object(out)));
        }
        _ => return Err(Error::uncaught(format!("unknown probe {what}"))),
    }
    Ok(0)
}

/// A TacHost read from the parity test's dump of xdl_graph's Extractor.
struct DumpHost {
    blob: Vec<u8>,
    big: bool,
    ptr: BTreeMap<u32, u32>,
    rel: HashMap<String, BTreeMap<u32, u32>>,
    links: HashMap<u32, std::result::Result<String, String>>,
    nodes: Vec<TacNode>,
    starts: HashMap<u32, usize>,
    kinds: BTreeSet<String>,
    triggers: Vec<usize>,
}

impl DumpHost {
    fn load(p: &Path) -> Result<DumpHost> {
        let d = jsonc::parse(&xdlrs_read(p)?)?;
        let num = |v: &Value| v.as_u64().unwrap_or(0) as u32;
        let pairs = |v: &Value| -> BTreeMap<u32, u32> { v.as_array().map(|a| a.iter().map(|x| (num(&x[0]), num(&x[1]))).collect()).unwrap_or_default() };
        let blob = std::fs::read(py::str_of(&d["blob"])).map_err(|e| Error::io(e.to_string()))?;
        let mut rel = HashMap::new();
        for k in ["ref", "texture", "sound", "shader"] {
            rel.insert(k.to_string(), pairs(&d["rel"][k]));
        }
        let mut links = HashMap::new();
        for x in d["links"].as_array().cloned().unwrap_or_default() {
            let v = if x[1].is_string() { Ok(py::str_of(&x[1])) } else { Err(py::str_of(&x[2])) };
            links.insert(num(&x[0]), v);
        }
        let nodes: Vec<TacNode> = d["nodes"]
            .as_array()
            .unwrap()
            .iter()
            .map(|n| TacNode {
                start: num(&n[0]),
                end: num(&n[1]),
                parent: n[2].as_u64().map(|x| x as usize),
                raw: n[3].as_bool().unwrap_or(false),
                fixed: n[4].as_bool().unwrap_or(false),
                label: n[5].as_str().map(str::to_string),
            })
            .collect();
        let mut starts = HashMap::new();
        for x in d["by_start"].as_array().cloned().unwrap_or_default() {
            starts.insert(num(&x[0]), x[1].as_u64().unwrap() as usize);
        }
        Ok(DumpHost {
            blob,
            big: d["big"].as_bool().unwrap_or(false),
            ptr: pairs(&d["ptr"]),
            rel,
            links,
            nodes,
            starts,
            kinds: d["kinds"].as_array().map(|a| a.iter().map(py::str_of).collect()).unwrap_or_default(),
            triggers: d["triggers"].as_array().map(|a| a.iter().map(|x| x.as_u64().unwrap() as usize).collect()).unwrap_or_default(),
        })
    }
}

impl TacHost for DumpHost {
    fn blob(&self) -> &[u8] {
        &self.blob
    }
    fn big(&self) -> bool {
        self.big
    }
    fn ptr(&self) -> &BTreeMap<u32, u32> {
        &self.ptr
    }
    fn rel(&self, kind: &str) -> Option<&BTreeMap<u32, u32>> {
        self.rel.get(kind)
    }
    fn link_to_elem(&self, t: u32) -> Result<String> {
        match self.links.get(&t) {
            Some(Ok(s)) => Ok(s.clone()),
            Some(Err(m)) => Err(Error::graph(m.clone())),
            None => Err(Error::graph(format!("no link for 0x{t:X} in the dump"))),
        }
    }
    fn by_start(&self, t: u32) -> Option<usize> {
        self.starts.get(&t).copied()
    }
    fn node(&self, h: usize) -> &TacNode {
        &self.nodes[h]
    }
    fn is_kind(&self, label: &str) -> bool {
        self.kinds.contains(label)
    }
}

// ------------------------------------------------------------------------------------------

/// `xdlrs mod ...`: tools/xdl_mod.py's index, check and pack (same output, same exit codes).
fn mod_main(argv: &[String]) -> ExitCode {
    use xdlrs::modcheck as mc;
    let (mut pos, mut opts, mut flags) = (vec![], HashMap::new(), vec![]);
    let mut i = 0;
    while i < argv.len() {
        let x = argv[i].as_str();
        if ["--strip-unchanged", "--allow-partial-index", "--no-geometry"].contains(&x) {
            flags.push(x.to_string());
        } else if ["-o", "--out", "--index", "--archives", "-j", "--jobs", "--repo"].contains(&x) && i + 1 < argv.len() {
            opts.insert(x.trim_start_matches('-').to_string(), argv[i + 1].clone());
            i += 1;
        } else {
            pos.push(argv[i].clone());
        }
        i += 1;
    }
    let flag = |k: &str| flags.iter().any(|f| f == k);
    let opt = |a: &str, b: &str| opts.get(a).or_else(|| opts.get(b)).cloned();
    let root = match opts.get("repo") {
        Some(r) => PathBuf::from(r),
        None => match xdlrs::repo_root(std::env::current_dir().ok().as_deref()) {
            Some(r) => r,
            None => {
                eprintln!("error: no schemas/ found (--repo)");
                return ExitCode::from(2);
            }
        },
    };
    let usage = || {
        eprintln!("usage: xdlrs mod index DATA... [-o OUT] [--archives A,B] [--no-geometry] [-j N]\n       xdlrs mod check MOD --index INDEX [--strip-unchanged]\n       xdlrs mod pack MOD -o OUT --index INDEX [--strip-unchanged] [--allow-partial-index]\n       xdlrs mod probe png|audio|fp|blobs FILE");
        ExitCode::from(2)
    };
    match pos.first().map(String::as_str) {
        Some("index") if pos.len() > 1 => {
            let o = IndexOpts { archives: opt("archives", "archives").map(|s| s.split(',').map(String::from).collect()), no_geometry: flag("--no-geometry"), jobs: opt("j", "jobs").and_then(|j| j.parse().ok()).unwrap_or(4) };
            let out = opt("o", "out").map(PathBuf::from).unwrap_or_else(|| root.join("build").join("mod-index.npz"));
            let data: Vec<PathBuf> = pos[1..].iter().map(PathBuf::from).collect();
            match mc::build_index(&data, &out, &o, &root) {
                Ok(t) => {
                    print!("{t}");
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    ExitCode::from(2)
                }
            }
        }
        Some(c @ ("check" | "pack")) if pos.len() == 2 => {
            let Some(index) = opts.get("index") else { return usage() };
            let out = if c == "pack" {
                match opt("o", "out") {
                    Some(o) => Some(PathBuf::from(o)),
                    None => return usage(),
                }
            } else {
                None
            };
            let o = mc::Options { strip_unchanged: flag("--strip-unchanged"), allow_partial_index: flag("--allow-partial-index"), root: Some(root) };
            let r = mc::check_and_pack(Path::new(&pos[1]), Path::new(index), out.as_deref(), &o);
            print!("{}", r.stdout);
            eprint!("{}", r.stderr);
            ExitCode::from(r.code as u8)
        }
        Some("probe") if pos.len() == 3 => match mod_probe(&pos[1], Path::new(&pos[2])) {
            Ok(s) => {
                print!("{s}");
                ExitCode::SUCCESS
            }
            Err(e) => {
                println!("ERR {e}");
                ExitCode::from(1)
            }
        },
        _ => usage(),
    }
}

type IndexOpts = xdlrs::modcheck::IndexOptions;

/// Probes the parity test uses: what the check reads from one file (JSON).
fn mod_probe(what: &str, p: &Path) -> std::result::Result<String, String> {
    use xdlrs::modcheck::{audio, blobs, hash, image};
    let data = std::fs::read(p).map_err(|e| e.to_string())?;
    let v = match what {
        "png" => {
            let img = image::read_png(&data)?;
            let (a, b) = image::pixel_hashes(&img);
            json!({"w": img.w, "h": img.h, "px": format!("{a:016x}"), "rgb": format!("{b:016x}"),
                   "dhash": image::dhash(&img).map(|d| hash::hex(&d))})
        }
        "audio" | "fp" => {
            let pcm = audio::decode_audio(p, &p.to_string_lossy())?;
            let raw: Vec<u8> = pcm.data.iter().flat_map(|x| x.to_le_bytes()).collect();
            let mono = pcm.mono();
            let mut j = json!({"channels": pcm.channels, "rate": pcm.rate, "frames": pcm.frames(), "pcm": format!("{:016x}", hash::h64(&raw))});
            if what == "fp" {
                let (k, f) = audio::sound_fp(if pcm.channels > 1 { &mono } else { &pcm.data }, pcm.rate);
                j["keys"] = json!(k);
                j["frames_kept"] = json!(f);
            }
            j
        }
        "blobs" => {
            let s = String::from_utf8_lossy(&data);
            let mut out = vec![];
            match jsonc::loads(&s) {
                Ok(tree) => blobs::json_blobs(&tree, "", &mut out),
                Err(_) => out = blobs::text_blobs(&s),
            }
            Value::Array(out.into_iter().map(|(w, b)| json!([w, hash::hex(&b)])).collect())
        }
        _ => return Err(format!("no probe {what}")),
    };
    Ok(py::dumps(&v) + "\n")
}

fn main() -> ExitCode {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    if argv.first().map(String::as_str) == Some("mod") {
        return mod_main(&argv[1..]);
    }
    let a = parse_args(&argv);
    let cmd = (a.pos.first().map(String::as_str), a.pos.get(1).map(String::as_str));
    let shifted = |n: usize| Args { pos: a.pos[n..].to_vec(), opts: a.opts.clone(), flags: a.flags.clone() };
    let r = match cmd {
        (Some("struct"), Some("extract")) => cmd_extract(&shifted(2)),
        (Some("struct"), Some("build")) => cmd_build(&shifted(2)),
        (Some("struct"), Some("roundtrip")) => cmd_roundtrip(&shifted(2)),
        (Some("struct"), Some("observe")) => cmd_observe(&shifted(2)),
        (Some("tac"), Some("survey")) => cmd_tac_survey(&shifted(2)),
        (Some("probe"), Some(_)) => probe(&a),
        _ => {
            eprintln!("usage: see the top of tools/xdlrs/src/bin/xdlrs.rs");
            return ExitCode::from(2);
        }
    };
    match r {
        Ok(c) => ExitCode::from(c as u8),
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

