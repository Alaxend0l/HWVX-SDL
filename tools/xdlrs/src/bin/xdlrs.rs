//! xdlrs: the crate's commands, mirroring the Python tools' (same output for the same input).
//!
//!   xdlrs struct extract PACK_DIR [--struct CarAInfo] [--root 0] [--path engine] [-o car.jsonc]
//!   xdlrs struct build FILE.jsonc PACK_DIR [-o out.bin]
//!   xdlrs struct roundtrip SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--struct CarAInfo] [--parts] [--out DIR] [-v]
//!   xdlrs struct observe SURVEY_ROOT [--rel gc,pc] [--archive HW_Cars] [--struct CarAInfo] [--out FILE]
//!   xdlrs tac survey SURVEY_ROOT
//!   xdlrs level compile|check|base|mod|testdrive|pack ...   as xdl_level.py (pack: xdl_level_pack.py build)
//!   xdlrs graph extract PACK_DIR PROJECT_DIR [--force]          as xdl_graph.py extract (no glTF)
//!   xdlrs graph build PROJECT_DIR OUT_DIR [--check PACK_DIR]    as xdl_graph.py build
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
//!   xdlrs mix ui-check MANIFEST --set gc|ps2 [--base gc] [--list]
//!                                        a mixed build's interface textures (manifest "ui" lines) by
//!                                        release; exit 1 when one breaks the set rule (Port_Mix.c)
//!   xdlrs audio level DUMP.f32 [--window S] [--from S]
//!                                        an HWVX_AUDIODUMP's loudness per window and band (audiolevel.rs)
//!   xdlrs audio level DUMP.f32 --lr [--window S] [--from S]
//!                                        its left and right RMS per window (stereo.rs)
//!   xdlrs audio pan                      the releases' pan laws: speaker dB per source position (stereo.rs)
//!   xdlrs prompts inventory DATA... [--seed KIND:REL:FILE/PACK:INDEX]... [--png DIR] [--threshold D] [--all]
//!                                        button atlases and pad pictures, the interface objects that
//!                                        show them, their cells (prompts.rs; docs-pending/prompts-inventory.md)
//!   xdlrs prompts sheet PACK_PNG256 OUT.png  the prompt pack's glyphs in several looks (prompts_art.rs)
//!   xdlrs prompts build SRC OUT.bin      assets/prompts/src into prompts.bin, the table the game embeds
//!   xdlrs prompts check SRC BIN          BIN is SRC's build and the PNGs are README.md's (sha256)
//!   xdlrs prompts montage OUT.png COLS PNG...  screenshots in a grid, 320 px wide each
//!   xdlrs cars stats DIR... [--dump OUT.json] [--md OUT]
//!                                        per release folder (GC files/, PC, PS2, the PS2 prototype):
//!                                        every car's stat bars and physics fields, the car select's
//!                                        bars (roles 70-73), labels and value->length curve (cars.rs)
//!   xdlrs mix pair LEFT.bmp RIGHT.bmp OUT.png [--half]
//!                                        two frame dumps side by side (tools/mix_pair_shots.sh)
//!   xdlrs shaders gen [--tools DIR] [--fresh]
//!                                        src/port/gx_shaders.h from src/port/shaders/ and
//!                                        tools/tevkeys.txt; unchanged shaders keep their entries (shaders.rs)
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
        if ["--parts", "-v", "--verbose", "--force", "--json", "--skip-intro", "--memory", "--keep", "--check", "--lr"].contains(&x.as_str()) {
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
        "bsp" => {
            // stdin: lines "lo0 lo1 lo2 hi0 hi1 hi2" -> the box's BSPN0 bytes (hex), little-endian
            let mut text = String::new();
            std::io::stdin().read_to_string(&mut text).map_err(|e| Error::io(e.to_string()))?;
            for ln in text.lines() {
                let v: Vec<f64> = ln.split_whitespace().filter_map(|x| x.parse().ok()).collect();
                match xdlrs::bsp::box_bsp([v[0], v[1], v[2]], [v[3], v[4], v[5]], false) {
                    Ok(b) => println!("{}", xdlrs::graph::hexstr(&b)),
                    Err(e) => println!("ERR {e}"),
                }
            }
        }
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

// ------------------------------------------------------------------------------------------
// graph

fn graph_ctx(a: &Args) -> Result<xdlrs::graph::Ctx> {
    let c = ctx(a)?;
    xdlrs::graph::Ctx::new(&c.root)
}

fn cmd_graph_extract(a: &Args) -> Result<i32> {
    let g = graph_ctx(a)?;
    let src = xdlrs::graph::Source::open(Path::new(a.pos(0)?), &g)?;
    let (arch, name) = (src.archive.clone(), src.name.clone());
    let mut ex = xdlrs::graph::Extractor::new(&g, src, Path::new(a.pos(1)?), a.flag("--force"));
    let s = ex.run()?;
    println!("{arch}/{name}: {} nodes ({} typed, {} geometry, {} raw) in {} files", s.nodes, s.typed, s.geometry, s.raw, s.files);
    for m in &s.log {
        println!("  {m}");
    }
    Ok(0)
}

fn cmd_graph_build(a: &Args) -> Result<i32> {
    let g = graph_ctx(a)?;
    let mut b = xdlrs::graph::Builder::new(&g, Path::new(a.pos(0)?))?;
    let out = PathBuf::from(a.pos(1)?);
    b.build(&out)?;
    if let Some(c) = a.opts.get("check") {
        let d = xdlrs::graph::compare(Path::new(c), &out);
        for (f, w) in &d {
            println!("DIFF {f}: {w}");
        }
        println!("{}", if d.is_empty() { "identical".to_string() } else { format!("{} differences", d.len()) });
        return Ok(if d.is_empty() { 0 } else { 1 });
    }
    Ok(0)
}

// ------------------------------------------------------------------------------------------
// level (tools/xdl_level.py's commands, same output)

fn level_env(a: &Args) -> Result<xdlrs::level::Env> {
    let c = ctx(a)?;
    let mut env = xdlrs::level::Env::new(&c.root)?;
    if let Some(x) = a.opts.get("hwvx-x") {
        env.hwvx_x = Some(PathBuf::from(x));
    }
    Ok(env)
}

fn base_opts(a: &Args) -> xdlrs::level::BaseOpts {
    xdlrs::level::BaseOpts {
        base: a.opts.get("base").map(PathBuf::from),
        data: a.opts.get("data").map(PathBuf::from),
        cache: a.opts.get("cache").map(PathBuf::from),
    }
}

fn report(errors: &[String]) -> i32 {
    for e in errors {
        eprintln!("error: {e}");
    }
    if errors.is_empty() { 0 } else { 1 }
}

fn records_json(r: &[xdlrs::level::Record]) -> Value {
    Value::Array(r.iter().map(|x| x.to_json()).collect())
}

fn level_main(cmd: &str, a: &Args) -> ExitCode {
    use xdlrs::level as lv;
    let r = (|| -> Result<i32> {
        let env = level_env(a)?;
        let base = base_opts(a);
        let sbox = a.opts.get("spawn-box").map(PathBuf::from);
        match cmd {
            "compile" => {
                let proj = PathBuf::from(a.pos(0)?);
                let c = lv::compile(&env, &proj, a.opts.get("o").or(a.opts.get("out")).map(Path::new), &base, a.flag("--skip-intro"), sbox.as_deref())?;
                if a.flag("--json") {
                    let o = json!({"ok": c.ok, "out": c.out.display().to_string(), "cached": c.cached, "ms": c.ms,
                                   "errors": records_json(&c.errors), "warnings": records_json(&c.warnings)});
                    println!("{}", py::dumps(&o));
                    return Ok(if c.ok { 0 } else { 1 });
                }
                for w in &c.warning_texts {
                    eprintln!("warning: {w}");
                }
                if !c.ok {
                    return Ok(report(&c.error_texts));
                }
                let d = c.doc.unwrap_or(Value::Null);
                println!(
                    "{}: {} parts, {} patches{}, {} ms",
                    c.out.display(),
                    d["parts"].as_array().map(Vec::len).unwrap_or(0),
                    d["patches"].as_array().map(Vec::len).unwrap_or(0),
                    if c.cached { " (up to date)" } else { "" },
                    py::float_repr(c.ms)
                );
                Ok(0)
            }
            "check" => {
                let proj = PathBuf::from(a.pos(0)?);
                let (e, w) = lv::check(&env, &proj, &base, sbox.as_deref())?;
                if a.flag("--json") {
                    let o = json!({"ok": e.is_empty(), "errors": records_json(&lv::error_records(&e)), "warnings": records_json(&lv::error_records(&w))});
                    println!("{}", py::dumps(&o));
                    return Ok(if e.is_empty() { 0 } else { 1 });
                }
                for x in &w {
                    eprintln!("warning: {x}");
                }
                if e.is_empty() {
                    println!("{}: ok", a.pos(0)?);
                }
                Ok(report(&e))
            }
            "base" => {
                let data = a.opts.get("data").ok_or_else(|| Error::uncaught("--data is required"))?;
                let p = lv::base_project(&env, a.pos(0)?, Path::new(data), &lv::cache_dir(a.opts.get("cache").map(Path::new)))?;
                println!("{}", p.display());
                Ok(0)
            }
            "mod" => {
                let proj = PathBuf::from(a.pos(0)?);
                match lv::share(&env, &proj, Path::new(a.pos(1)?), a.opts.get("xmod").map(Path::new), &base, a.opts.get("index").map(Path::new))? {
                    Ok(line) => {
                        println!("{line}");
                        Ok(0)
                    }
                    Err(e) => Ok(report(&e)),
                }
            }
            "testdrive" => {
                let o = lv::DriveOpts {
                    car: a.opts.get("car").map(|c| c.parse().unwrap_or(0)),
                    skip_intro: a.flag("--skip-intro"),
                    memory: a.flag("--memory"),
                    base,
                };
                let res = lv::testdrive(&env, Path::new(a.pos(0)?), Path::new(a.pos(1)?), &o)?;
                println!("{}", py::dumps(&res));
                Ok(if res["ok"] == true { 0 } else { 1 })
            }
            "pack" => {
                let data = a.opts.get("data").ok_or_else(|| Error::uncaught("--data is required"))?;
                let r = xdlrs::level_pack::build_cached(
                    &env,
                    Path::new(a.pos(0)?),
                    Path::new(data),
                    &lv::cache_dir(a.opts.get("cache").map(Path::new)),
                    Path::new(a.pos(1)?),
                    a.flag("--skip-intro"),
                );
                match r {
                    Ok((out, cached, _)) => {
                        println!("{}{}", out.display(), if cached { " (up to date)" } else { "" });
                        Ok(0)
                    }
                    Err(e) if e.kind == Kind::Level => Ok(report(&e.msg.lines().map(str::to_string).collect::<Vec<_>>())),
                    Err(e) => Err(e),
                }
            }
            _ => Err(Error::uncaught(format!("unknown level command {cmd}"))),
        }
    })();
    match r {
        Ok(c) => ExitCode::from(c as u8),
        Err(e) => {
            eprintln!("error: {}", xdlrs::level::top_message(&e));
            ExitCode::from(1)
        }
    }
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
    if argv.first().map(String::as_str) == Some("mix") {
        return mix_main(&argv[1..]);
    }
    if argv.first().map(String::as_str) == Some("prompts") {
        return prompts_main(&argv[1..]);
    }
    if argv.first().map(String::as_str) == Some("audio") {
        return audio_main(&argv[1..]);
    }
    if argv.first().map(String::as_str) == Some("fonts") {
        return fonts_main(&argv[1..]);
    }
    if argv.first().map(String::as_str) == Some("cars") {
        return cars_main(&argv[1..]);
    }
    if argv.first().map(String::as_str) == Some("shaders") {
        return shaders_main(&argv[1..]);
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
        (Some("level"), Some("gen-fields")) => {
            let root = xdlrs::repo_root(std::env::current_dir().ok().as_deref()).ok_or_else(|| Error::io("no schemas/ found"));
            root.and_then(|r| xdlrs::level_fields::generate(&r, a.flag("--check")))
        }
        (Some("level"), Some("refuse-test")) => xdlrs::level_refuse::main(&shifted(2).pos),
        (Some("level"), Some(c)) => {
            let c = c.to_string();
            return level_main(&c, &shifted(2));
        }
        (Some("graph"), Some("extract")) => cmd_graph_extract(&shifted(2)),
        (Some("graph"), Some("build")) => cmd_graph_build(&shifted(2)),
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


// ---- mix: mixed builds' checks --------------------------------------------------------

fn mix_main(argv: &[String]) -> ExitCode {
    let mut pos = vec![];
    let mut opts: HashMap<String, String> = HashMap::new();
    let mut flags = BTreeSet::new();
    let mut i = 0;
    while i < argv.len() {
        let x = &argv[i];
        if x == "--list" || x == "--half" {
            flags.insert(x.clone());
        } else if x.starts_with("--") && i + 1 < argv.len() {
            opts.insert(x[2..].to_string(), argv[i + 1].clone());
            i += 1;
        } else {
            pos.push(x.clone());
        }
        i += 1;
    }
    let r = match pos.first().map(String::as_str) {
        Some("ui-check") if pos.len() == 2 => mix_ui_check(&pos[1], &opts, flags.contains("--list")),
        Some("pair") if pos.len() == 4 => mix_pair(&pos[1], &pos[2], &pos[3], flags.contains("--half")).map(|_| 0),
        _ => {
            eprintln!("usage: xdlrs mix ui-check MANIFEST --set gc|ps2 [--base gc] [--list]");
            eprintln!("       xdlrs mix pair LEFT.bmp RIGHT.bmp OUT.png [--half]");
            return ExitCode::from(2);
        }
    };
    match r {
        Ok(c) => ExitCode::from(c),
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/* Counts the interface textures (manifest lines with "ui": "art" | "font") by the
 * release they came from and checks the set rule: art from the set's release or,
 * where it has no copy, the base's own (a fallback); font pages from the PC, else
 * the set's, else the base's. */
fn mix_ui_check(manifest: &str, opts: &HashMap<String, String>, list: bool) -> std::result::Result<u8, String> {
    let set = opts.get("set").map(String::as_str).ok_or("--set gc|ps2")?;
    if set != "gc" && set != "ps2" {
        return Err("--set gc|ps2".into());
    }
    let base = opts.get("base").map(String::as_str).unwrap_or("gc");
    let text = std::fs::read_to_string(manifest).map_err(|e| format!("{manifest}: {e}"))?;
    let mut counts: BTreeMap<(String, String), u32> = BTreeMap::new();
    let mut fallbacks: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut bad = vec![];
    for line in text.lines().filter(|l| !l.trim().is_empty()) {
        let e: Value = serde_json::from_str(line).map_err(|e| format!("{manifest}: {e}"))?;
        let (Some(ui), Some(from), Some(id)) = (e["ui"].as_str(), e["from"].as_str(), e["id"].as_str()) else {
            continue;
        };
        *counts.entry((ui.to_string(), from.to_string())).or_default() += 1;
        let parts: Vec<&str> = id.split('/').collect();
        let pack = if parts.len() > 3 { format!("{}/{}", parts[2], parts[3]) } else { id.to_string() };
        let idx = id.rsplit('/').next().unwrap_or("").to_string();
        if ui == "art" {
            if from == set {
                continue;
            }
            if from == base {
                fallbacks.entry(pack).or_default().push(idx);
            } else {
                bad.push(format!("{id} {from} {ui}"));
            }
        } else if from != "pc" && from != set && from != base {
            bad.push(format!("{id} {from} {ui}"));
        } else if from != "pc" {
            fallbacks.entry(pack + " (font)").or_default().push(idx);
        }
    }
    for ui in ["art", "font"] {
        let row: Vec<String> = ["gc", "pc", "ps2"]
            .iter()
            .filter_map(|r| counts.get(&(ui.to_string(), r.to_string())).map(|n| format!("{r} {n}")))
            .collect();
        println!("{:<5} {}", ui, if row.is_empty() { "none".to_string() } else { row.join(", ") });
    }
    let n: usize = fallbacks.values().map(Vec::len).sum();
    println!("not from the set (the base's own, fonts not from the pc): {} in {} packs", n, fallbacks.len());
    if list || set != base {
        for (pack, idx) in &fallbacks {
            let more = if idx.len() > 24 { " ..." } else { "" };
            println!("  {:<40} {}: {}{}", pack, idx.len(), idx[..idx.len().min(24)].join(" "), more);
        }
    }
    for b in bad.iter().take(40) {
        println!("BAD {b}");
    }
    if !bad.is_empty() {
        println!("{} interface textures break the set rule", bad.len());
        return Ok(1);
    }
    println!("ok: no interface art from another release");
    Ok(0)
}

/* An uncompressed 24- or 32-bit BMP (the port's frame dumps) as RGB rows, top first. */
fn read_bmp(path: &str) -> std::result::Result<(u32, u32, Vec<u8>), String> {
    let d = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    let rd32 = |o: usize| -> u32 { u32::from_le_bytes([d[o], d[o + 1], d[o + 2], d[o + 3]]) };
    if d.len() < 54 || &d[0..2] != b"BM" || rd32(30) != 0 {
        return Err(format!("{path}: not an uncompressed BMP"));
    }
    let off = rd32(10) as usize;
    let w = rd32(18);
    let hs = rd32(22) as i32;
    let bpp = u16::from_le_bytes([d[28], d[29]]) as usize / 8;
    let h = hs.unsigned_abs();
    if bpp != 3 && bpp != 4 {
        return Err(format!("{path}: {} bits per pixel", bpp * 8));
    }
    let stride = (w as usize * bpp + 3) & !3;
    if off + stride * h as usize > d.len() {
        return Err(format!("{path}: short"));
    }
    let mut out = vec![0u8; w as usize * h as usize * 3];
    for y in 0..h as usize {
        let src = if hs > 0 { h as usize - 1 - y } else { y };
        for x in 0..w as usize {
            let p = off + src * stride + x * bpp;
            let o = (y * w as usize + x) * 3;
            out[o] = d[p + 2];
            out[o + 1] = d[p + 1];
            out[o + 2] = d[p];
        }
    }
    Ok((w, h, out))
}

/* LEFT and RIGHT side by side as OUT.png; --half: at half the size (2x2 boxes). */
fn mix_pair(left: &str, right: &str, out: &str, half: bool) -> std::result::Result<(), String> {
    let (lw, lh, l) = read_bmp(left)?;
    let (rw, rh, r) = read_bmp(right)?;
    if lw != rw || lh != rh {
        return Err(format!("{left} is {lw}x{lh}, {right} {rw}x{rh}"));
    }
    let (w, h) = (lw as usize * 2, lh as usize);
    let mut img = vec![0u8; w * h * 3];
    for y in 0..h {
        img[y * w * 3..y * w * 3 + lw as usize * 3].copy_from_slice(&l[y * lw as usize * 3..(y + 1) * lw as usize * 3]);
        img[y * w * 3 + lw as usize * 3..(y + 1) * w * 3].copy_from_slice(&r[y * lw as usize * 3..(y + 1) * lw as usize * 3]);
    }
    let (ow, oh, pix) = if half {
        let (ow, oh) = (w / 2, h / 2);
        let mut o = vec![0u8; ow * oh * 3];
        for y in 0..oh {
            for x in 0..ow {
                for c in 0..3 {
                    let s: u32 = [(0, 0), (1, 0), (0, 1), (1, 1)]
                        .iter()
                        .map(|(dx, dy)| img[((y * 2 + dy) * w + x * 2 + dx) * 3 + c] as u32)
                        .sum();
                    o[(y * ow + x) * 3 + c] = ((s + 2) / 4) as u8;
                }
            }
        }
        (ow, oh, o)
    } else {
        (w, h, img)
    };
    let f = std::fs::File::create(out).map_err(|e| format!("{out}: {e}"))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), ow as u32, oh as u32);
    enc.set_color(png::ColorType::Rgb);
    enc.set_depth(png::BitDepth::Eight);
    let mut wr = enc.write_header().map_err(|e| format!("{out}: {e}"))?;
    wr.write_image_data(&pix).map_err(|e| format!("{out}: {e}"))?;
    Ok(())
}

/// `xdlrs fonts list [--gc DIR] [--pc DIR] [--ps2 DIR] [--dump DIR] [--report FILE.json] [--threshold D]`
/// (xdlrs::fonts): every font page of the releases' archives.
fn fonts_main(argv: &[String]) -> ExitCode {
    let a = parse_args(argv);
    if a.pos.first().map(String::as_str) != Some("list") {
        eprintln!("usage: xdlrs fonts list [--gc DIR] [--pc DIR] [--ps2 DIR] [--dump DIR] [--report FILE.json] [--threshold 12]");
        return ExitCode::from(2);
    }
    let dirs: Vec<(String, PathBuf)> =
        ["gc", "pc", "ps2"].iter().filter_map(|r| a.opts.get(*r).map(|d| (r.to_string(), PathBuf::from(d)))).collect();
    if dirs.is_empty() {
        eprintln!("fonts list: give at least one of --gc, --pc, --ps2");
        return ExitCode::from(2);
    }
    let th = a.opts.get("threshold").and_then(|t| t.parse().ok()).unwrap_or(12.0);
    match xdlrs::fonts::list(&dirs, a.opts.get("dump").map(Path::new), a.opts.get("report").map(Path::new), th) {
        Ok(s) => {
            print!("{s}");
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/// `xdlrs audio level DUMP [--window S] [--from S]` (xdlrs::audiolevel).
fn audio_main(argv: &[String]) -> ExitCode {
    let a = parse_args(argv);
    if a.pos.first().map(String::as_str) == Some("pan") {
        print!("{}", xdlrs::stereo::pan_table());
        return ExitCode::SUCCESS;
    }
    if a.pos.first().map(String::as_str) != Some("level") || a.pos.len() < 2 {
        eprintln!("usage: xdlrs audio level DUMP.f32 [--window S] [--from S] [--lr]\n       xdlrs audio pan");
        return ExitCode::from(2);
    }
    let num = |k: &str, d: f64| a.opts.get(k).and_then(|v| v.parse().ok()).unwrap_or(d);
    let s = match xdlrs::audiolevel::read_dump(Path::new(&a.pos[1])) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("error: {}: {e}", a.pos[1]);
            return ExitCode::from(1);
        }
    };
    if a.flags.iter().any(|f| f == "--lr") {
        println!("    from    left   right    R-L   (dBFS RMS)");
        for (t, l, r) in xdlrs::stereo::lr_levels(&s, xdlrs::audiolevel::RATE, num("window", 5.0), num("from", 0.0)) {
            println!("{t:7.1}s {l:7.1} {r:7.1} {:6.1}", r - l);
        }
        return ExitCode::SUCCESS;
    }
    println!("    from     rms   <250Hz  250-4k   >4kHz   (dBFS)");
    for (t, total, b) in xdlrs::audiolevel::levels(&s, num("window", 5.0), num("from", 0.0)) {
        println!("{t:7.1}s {total:7.1} {:8.1} {:7.1} {:7.1}", b[0], b[1], b[2]);
    }
    ExitCode::SUCCESS
}


// ---- prompts: the button prompts' inventory ----------------------------------------------

/// The default seeds: each release's button atlas (the PC's: keycaps), the GameCube's and the PS2's pad
/// picture (HW_IPack; docs-pending/prompts-inventory.md). The PC's controller page has none.
const PROMPT_SEEDS: [&str; 6] = [
    "atlas:gc:hw_ipack/interface_3:24",
    "pad:gc:hw_ipack/interface_6:124",
    "atlas:pc:hw_ipack/interface_3:24",
    "atlas:ps2:hw_ipack/interface_3:24",
    "pad:ps2:hw_ipack/interface_6:125",
    "pad-overlay:ps2:hw_ipack/interface_6:126",
];

fn prompts_main(argv: &[String]) -> ExitCode {
    match argv.first().map(String::as_str) {
        Some("sheet" | "build" | "check" | "montage") => return prompts_art_main(argv),
        _ => {}
    }
    let mut dirs = vec![];
    let mut seeds = vec![];
    let mut png = None;
    let mut threshold = 24.0;
    let mut all = false;
    let mut i = 1;
    let usage = || {
        eprintln!("usage: xdlrs prompts inventory DATA... [--seed KIND:REL:FILE/PACK:INDEX]... [--png DIR] [--threshold D] [--all]");
        ExitCode::from(2)
    };
    if argv.first().map(String::as_str) != Some("inventory") {
        return usage();
    }
    while i < argv.len() {
        let x = argv[i].as_str();
        let next = argv.get(i + 1).cloned();
        match x {
            "--all" => all = true,
            "--seed" | "--png" | "--threshold" => {
                let Some(v) = next else { return usage() };
                match x {
                    "--seed" => seeds.push(v),
                    "--png" => png = Some(PathBuf::from(v)),
                    _ => threshold = v.parse().unwrap_or(threshold),
                }
                i += 1;
            }
            _ => dirs.push(PathBuf::from(x)),
        }
        i += 1;
    }
    if dirs.is_empty() {
        return usage();
    }
    if seeds.is_empty() {
        seeds = PROMPT_SEEDS.iter().map(|s| s.to_string()).collect();
    }
    let mut sv = vec![];
    for s in &seeds {
        let mut p = s.splitn(3, ':');
        match (p.next(), p.next(), p.next()) {
            (Some(k), Some(r), Some(key)) => sv.push((k.to_string(), r.to_string(), key.to_string())),
            _ => return usage(),
        }
    }
    let opt = xdlrs::prompts::Options { dirs, seeds: sv, png, threshold, all };
    match xdlrs::prompts::inventory(&opt) {
        Ok(s) => {
            print!("{s}");
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/* xdlrs cars stats DIR... [--dump OUT.json] [--md OUT]: the stat bars and the physics behind them
 * (src/cars.rs). Markdown tables to stdout (or --md), everything as JSON with --dump. */
fn cars_main(argv: &[String]) -> ExitCode {
    let a = parse_args(argv);
    if a.pos.first().map(String::as_str) != Some("stats") || a.pos.len() < 2 {
        eprintln!("usage: xdlrs cars stats DIR... [--dump OUT.json] [--md OUT] [--repo DIR]");
        return ExitCode::from(2);
    }
    let run = || -> std::result::Result<(), String> {
        let c = ctx(&a).map_err(|e| e.msg)?;
        let mut rels = vec![];
        for d in &a.pos[1..] {
            rels.push(xdlrs::cars::release(&c.s, Path::new(d))?);
        }
        if let Some(j) = a.opts.get("dump") {
            std::fs::write(j, serde_json::to_string_pretty(&rels).unwrap()).map_err(|e| format!("{j}: {e}"))?;
        }
        let md = xdlrs::cars::report(&rels);
        match a.opts.get("md") {
            Some(m) => std::fs::write(m, md).map_err(|e| format!("{m}: {e}"))?,
            None => print!("{md}"),
        }
        Ok(())
    };
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/// `xdlrs shaders gen [--tools DIR] [--fresh] [--repo DIR]` (shaders.rs).
fn shaders_main(argv: &[String]) -> ExitCode {
    let mut fresh = false;
    let mut tools = None;
    let mut repo = None;
    let mut ok = argv.first().map(String::as_str) == Some("gen");
    let mut i = 1;
    while ok && i < argv.len() {
        match argv[i].as_str() {
            "--fresh" => fresh = true,
            "--tools" if i + 1 < argv.len() => {
                tools = Some(PathBuf::from(&argv[i + 1]));
                i += 1;
            }
            "--repo" if i + 1 < argv.len() => {
                repo = Some(PathBuf::from(&argv[i + 1]));
                i += 1;
            }
            _ => ok = false,
        }
        i += 1;
    }
    if !ok {
        eprintln!("usage: xdlrs shaders gen [--tools DIR] [--fresh] [--repo DIR]");
        return ExitCode::from(2);
    }
    let Some(root) = repo.or_else(|| xdlrs::repo_root(std::env::current_dir().ok().as_deref())) else {
        eprintln!("error: no schemas/ found (--repo)");
        return ExitCode::from(1);
    };
    match xdlrs::shaders::generate(&xdlrs::shaders::Opts { root, tools, fresh }) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: shaders gen: {e}");
            ExitCode::from(1)
        }
    }
}

/// `xdlrs prompts sheet|build|check` (prompts_art.rs).
fn prompts_art_main(argv: &[String]) -> ExitCode {
    use xdlrs::prompts_art as pa;
    let (Some(a), Some(b)) = (argv.get(1), argv.get(2)) else {
        eprintln!("usage: xdlrs prompts sheet PACK_PNG256 OUT.png | build SRC OUT.bin | check SRC BIN");
        return ExitCode::from(2);
    };
    let (a, b) = (Path::new(a), Path::new(b));
    let r = match argv[0].as_str() {
        "sheet" => pa::sheet(a, b).map(|_| vec![]),
        "montage" => {
            let files: Vec<PathBuf> = argv[3..].iter().map(PathBuf::from).collect();
            pa::montage(a, b.to_str().and_then(|s| s.parse().ok()).unwrap_or(4), &files).map(|_| vec![])
        }
        "build" => pa::build(a).and_then(|v| std::fs::write(b, &v).map_err(|e| format!("{}: {e}", b.display())).map(|_| vec![])),
        _ => pa::check(a, b),
    };
    match r {
        Ok(bad) if bad.is_empty() => ExitCode::SUCCESS,
        Ok(bad) => {
            for l in bad {
                eprintln!("{l}");
            }
            ExitCode::from(1)
        }
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}
