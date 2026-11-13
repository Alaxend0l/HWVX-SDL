//! A level built into its own world pack (tools/xdl_level_pack.py, stage 21.4 option 1): the
//! base world's extracted project with the level's records added to its arrays and tables and
//! its patches written into the records, rebuilt by [`crate::graph::Builder`] and packed by
//! hwvx-x into a world archive the port loads for the level's track in place of the base's.
//! Made on the player's machine from their own data: the archive is game data and never goes
//! into a mod.

use crate::error::{Error, Kind, Result};
use crate::graph::{self, Builder};
use crate::level::{self, Compiler, Env, Mode, dget, py_float, py_int, read_jsonc, truthy};
use crate::py;
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

fn lerr(m: impl Into<String>) -> Error {
    Error::level(m)
}

fn pyerr(kind: &str, m: impl std::fmt::Display) -> Error {
    Error::uncaught(format!("{kind}: {m}"))
}

fn s(v: &Value) -> String {
    py::str_of(v)
}

fn f64v(x: f64) -> Value {
    serde_json::Number::from_f64(x).map(Value::Number).unwrap_or(Value::Null)
}

fn items_of(v: Option<&Value>) -> Result<Vec<(String, Value)>> {
    match v {
        Some(x) if truthy(x) => match x {
            Value::Object(m) => Ok(m.iter().map(|(k, v)| (k.clone(), v.clone())).collect()),
            _ => Err(pyerr("AttributeError", format!("'{}' object has no attribute 'items'", py::type_name(x)))),
        },
        _ => Ok(vec![]),
    }
}

struct WorldBuild<'c> {
    b: Builder<'c>,
    pre: String,
    layout: Vec<Value>,
    file_of: IndexMap<String, String>,
    new_stems: usize,
}

impl<'c> WorldBuild<'c> {
    fn new(ctx: &'c graph::Ctx, base_project: &Path) -> Result<WorldBuild<'c>> {
        let b = Builder::new(ctx, base_project)?;
        let pre = format!("base://{}/world_0/", b.archive);
        let layout: Vec<Value> = b.pack["layout"].as_array().cloned().unwrap_or_default();
        let file_of = layout.iter().map(|e| (s(&e["id"]), s(&e["file"]))).collect();
        Ok(WorldBuild { b, pre, layout, file_of, new_stems: 0 })
    }

    fn doc(&mut self, stem: &str) -> Result<&mut Value> {
        self.b.doc(stem, ".jsonc")
    }

    /// the document of a node id
    fn doc_of(&mut self, nid: &str) -> Result<&mut Value> {
        let stem = self.file_of.get(nid).cloned().ok_or_else(|| pyerr("KeyError", py::str_repr(nid)))?;
        self.doc(&stem)
    }

    /// (doc stem, path in it) of a base record by its path: the longest node id the layout has,
    /// then items/<n> and fields.
    fn node_doc(&mut self, rel: &str) -> Result<Option<(String, String)>> {
        let segs: Vec<&str> = rel.split('/').collect();
        let mut k = segs.len();
        while k > 1 {
            let nid = format!("{}{}", self.pre, segs[..k].join("/"));
            if let Some(stem) = self.file_of.get(&nid).cloned() {
                let d = self.doc(&stem)?;
                let did = d.get("id").cloned();
                let skip = did.as_ref().is_some_and(|x| !x.is_null() && x.as_str() != Some(nid.as_str()))
                    && d.get("extends").is_none()
                    && !d.get("children").and_then(Value::as_object).is_some_and(|c| c.contains_key(&nid));
                if !skip {
                    let rest = segs[k..].join("/");
                    if d.get("items").is_some_and(Value::is_array) {
                        let p = if rest.is_empty() { "items".to_string() } else { format!("items/{rest}") };
                        return Ok(Some((stem, p)));
                    }
                    return Ok(Some((stem, rest)));
                }
            }
            k -= 1;
        }
        Ok(None)
    }

    fn add_raw(&mut self, nid: &str, data: &[u8], links: &[(u32, Value)]) -> String {
        let stem = format!("level/n{}", self.new_stems);
        self.new_stems += 1;
        self.b.docs.insert(format!("{stem}.jsonc"), raw_doc(nid, data, links));
        self.layout.push(json!({"id": nid, "file": stem, "align": 4}));
        self.file_of.insert(nid.into(), stem.clone());
        stem
    }
}

fn raw_doc(nid: &str, data: &[u8], links: &[(u32, Value)]) -> Value {
    let mut l = Map::new();
    for (o, v) in links {
        l.insert(format!("0x{o:x}"), v.clone());
    }
    json!({"id": nid, "size": data.len(), "bytes": graph::hexstr(data), "links": l})
}

/// The JSON at path (a/b/0/c) in doc, mutable.
fn lpath<'a>(doc: &'a mut Value, path: &str) -> Option<&'a mut Value> {
    let mut cur = doc;
    if path.is_empty() {
        return Some(cur);
    }
    for seg in path.split('/') {
        cur = match cur {
            Value::Array(a) if !seg.is_empty() && seg.bytes().all(|c| c.is_ascii_digit()) => {
                let i: usize = seg.parse().ok()?;
                a.get_mut(i)?
            }
            Value::Object(m) => m.get_mut(seg)?,
            _ => return None,
        };
    }
    Some(cur)
}

fn xyz(p: &Value) -> Result<[f64; 3]> {
    let get = |i: usize| -> Result<f64> {
        let x = match p {
            Value::Array(a) => a.get(i).ok_or_else(|| pyerr("IndexError", "list index out of range"))?,
            _ => return Err(pyerr("TypeError", format!("'{}' object is not subscriptable", py::type_name(p)))),
        };
        py_float(x)
    };
    Ok([get(0)?, get(1)?, get(2)?])
}

/// A level.jsonc volume as a WorldVolume item of the pack's array.
fn volume_item(c: &mut Compiler, v: &Value, archive: &str, wh: &str) -> Result<Value> {
    let c0 = v.get("center").cloned().unwrap_or(json!([0, 0, 0]));
    let cxyz = match &c0 {
        Value::Array(_) => xyz(&c0)?,
        Value::Object(m) => {
            let g = |k: &str| m.get(k).ok_or_else(|| pyerr("KeyError", py::str_repr(k))).and_then(py_float);
            [g("x")?, g("y")?, g("z")?]
        }
        _ => return Err(pyerr("TypeError", format!("'{}' object is not subscriptable", py::type_name(&c0)))),
    };
    let r = v.get("radius").filter(|r| !r.is_null());
    let rf = match r {
        Some(r) => Some(py_float(r)?),
        None => None,
    };
    let rsq = match v.get("radiusSq") {
        Some(x) => py_float(x)?,
        None => rf.map(|r| r * r).unwrap_or(0.0),
    };
    let mut it = Map::new();
    it.insert("editorId".into(), json!(0));
    it.insert("enabled".into(), v.get("enabled").cloned().unwrap_or(json!(1)));
    it.insert("shape".into(), v.get("shape").cloned().unwrap_or(json!("sphere")));
    it.insert("type".into(), v.get("type").cloned().unwrap_or(json!("airbox")));
    it.insert("center".into(), json!({"x": f64v(cxyz[0]), "y": f64v(cxyz[1]), "z": f64v(cxyz[2]), "w": 0.0}));
    it.insert("radius".into(), f64v(rf.unwrap_or(0.0)));
    it.insert("radiusSq".into(), f64v(rsq));
    it.insert("bsp".into(), Value::Null);
    it.insert("data".into(), Value::Null);
    if let Some(e) = v.get("editorId").filter(|e| level::is_int(e)) {
        it.insert("editorId".into(), e.clone());
    }
    for k in ["bsp", "data"] {
        if let Some(x) = v.get(k).and_then(Value::as_str) {
            let l = c.resolve(x, &format!("{wh}/{k}"), archive)?;
            it.insert(k.into(), l);
        }
    }
    Ok(Value::Object(it))
}

/// re.fullmatch's groups (owned)
fn re_full(pat: &str, s: &str) -> Option<Vec<String>> {
    let r = Regex::new(&format!("^(?:{pat})$")).ok()?;
    r.captures(s).map(|c| c.iter().map(|m| m.map(|m| m.as_str().to_string()).unwrap_or_default()).collect())
}

/// The level's world archive at out_archive. Err (Kind::Level) with every problem.
pub fn build_world(env: &Env, project: &Path, base_project: &Path, extract_dir: &Path, out_archive: &Path, skip_intro: bool) -> Result<PathBuf> {
    let mut w = WorldBuild::new(&env.g, base_project)?;
    let pre = w.pre.clone();
    let big = w.b.big;
    let archive = w.b.archive.clone();
    let lv = read_jsonc(&project.join("level.jsonc"))?;
    let world_id = format!("{pre}World/0");
    let world_stem = w.file_of.get(&world_id).cloned().ok_or_else(|| pyerr("KeyError", py::str_repr(&world_id)))?;
    let table = |w: &mut WorldBuild, name: &str| -> Option<String> { w.file_of.get(&format!("{pre}World/0/{name}")).cloned() };

    let mut names: IndexMap<String, String> = IndexMap::new();
    let mut own: IndexMap<String, String> = IndexMap::new();
    let own_id = |own: &mut IndexMap<String, String>, kind: &str, n: &str| -> String {
        let key = format!("level://{kind}/{n}");
        let l = own.len();
        own.entry(key).or_insert_with(|| format!("{pre}level/{l}")).clone()
    };
    let vols = items_of(dget(&lv, "volumes")?)?;
    let vstem = table(&mut w, "volumes");
    let v0 = match &vstem {
        Some(st) => w.doc(st)?.get("items").and_then(Value::as_array).map(Vec::len).ok_or_else(|| pyerr("TypeError", "items"))?,
        None => 0,
    };
    for (i, (n, _)) in vols.iter().enumerate() {
        names.insert(format!("level://volumes/{n}"), format!("{pre}World/0/volumes/{}", v0 + i));
    }
    for kind in ["timers", "variables"] {
        let recs = items_of(dget(&lv, kind)?)?;
        let k0 = match table(&mut w, kind) {
            Some(st) => w.doc(&st)?.get("items").and_then(Value::as_array).map(Vec::len).unwrap_or(0),
            None => 0,
        };
        for (i, (n, _)) in recs.iter().enumerate() {
            names.insert(format!("level://{kind}/{n}"), format!("{pre}World/0/{kind}/{}", k0 + i));
            let o = own_id(&mut own, kind, n);
            names.insert(format!("level://{kind}/{n}/to"), o);
        }
    }
    for (n, _) in items_of(dget(&lv, "strings")?)? {
        let o = own_id(&mut own, "strings", &n);
        names.insert(format!("level://strings/{n}"), o);
    }
    let tc_stem = table(&mut w, "tileCars");
    let starts = items_of(dget(&lv, "starts")?)?;
    if !starts.is_empty() {
        let tcn = match &tc_stem {
            Some(st) => w.doc(st)?["items"].as_array().map(Vec::len).unwrap_or(0),
            None => return Err(pyerr("TypeError", "'NoneType' object is not subscriptable")),
        };
        for (i, (n, _)) in starts.iter().enumerate() {
            names.insert(format!("level://starts/{n}"), format!("{pre}World/0/tileCars/{}", tcn + i));
        }
    }
    let od_stem = table(&mut w, "objectives");
    let od_n = match &od_stem {
        Some(st) => w.doc(st)?["items"].as_array().map(Vec::len).unwrap_or(0),
        None => 0,
    };
    for (i, (n, _)) in items_of(dget(&lv, "objectives")?)?.iter().enumerate() {
        names.insert(format!("level://objectives/{n}"), format!("{pre}World/0/objectives/{}", od_n + i));
    }
    for (kind, arr) in [("items", "items"), ("fxPoints", "fxPoints")] {
        let mut added: BTreeMap<i128, usize> = BTreeMap::new();
        for (n, it) in items_of(dget(&lv, kind)?)? {
            let q = if it.is_object() { it.get("quad").cloned() } else { None };
            let Some(q) = q.filter(level::is_int) else { continue };
            let qi = py_int(&q)?;
            let aid = format!("{pre}World/0/quads/{}/contents/{arr}", s(&q));
            if !w.file_of.contains_key(&aid) {
                continue;
            }
            let k0 = w.doc_of(&aid)?.get("items").filter(|x| truthy(x)).and_then(Value::as_array).map(Vec::len).unwrap_or(0);
            let a = added.get(&qi).copied().unwrap_or(0);
            names.insert(format!("level://{kind}/{n}"), format!("{aid}/{}", k0 + a));
            added.insert(qi, a + 1);
        }
    }
    let hdr_n = w.b.pack["pack"]["header"].get("num_triggers").map(py_int).transpose()?.unwrap_or(0);
    let mut tnames: Vec<String> = match lv.get("triggers") {
        Some(t) if truthy(t) => t.as_array().map(|a| a.iter().map(s).collect()).unwrap_or_default(),
        _ => level::trigger_stems(&project.join("triggers")),
    };
    tnames.extend(level::generated_triggers(&lv, "level.jsonc/rules")?.into_iter().map(|x| x.0));
    for (i, n) in tnames.iter().enumerate() {
        names.insert(format!("level://triggers/{n}"), format!("{pre}trigger/{}", hdr_n + i as i128));
    }

    let mut c = Compiler::new(env, project, Some(base_project), skip_intro, None)?;
    c.big = big;
    c.mode = Mode::Pack(names.clone());
    let doc = c.run()?;
    if !c.errors.is_empty() || doc.is_none() {
        return Err(lerr(c.errors.join("\n")));
    }
    let doc = doc.unwrap();

    // patches: written into the records' JSON
    for (path, fields) in items_of(dget(&lv, "patches")?)? {
        let rel = path.split_once("/world_0/").map(|x| x.1.to_string()).ok_or_else(|| pyerr("IndexError", "list index out of range"))?;
        let nd = w.node_doc(&rel)?;
        let ok = match &nd {
            Some((stem, at)) => lpath(w.doc(stem)?, at).is_some_and(|x| x.is_object()),
            None => false,
        };
        if !ok {
            return Err(lerr(format!("level.jsonc/patches/{path}: no such record in the project")));
        }
        let (stem, at) = nd.unwrap();
        let fm = fields.as_object().cloned().ok_or_else(|| pyerr("AttributeError", "'list' object has no attribute 'items'"))?;
        let rec = lpath(w.doc(&stem)?, &at).unwrap().as_object_mut().unwrap();
        for (f, v) in fm {
            if let Some((a, b)) = f.split_once('/') {
                match rec.get_mut(a) {
                    Some(Value::Object(m)) => {
                        m.insert(b.into(), v);
                    }
                    _ => return Err(lerr(format!("level.jsonc/patches/{path}/{f}: {a} isn't a struct"))),
                }
            } else {
                rec.insert(f, v);
            }
        }
    }

    // volumes: into the array (a box's BSP is a part of the compiler's, below)
    for (n, v) in &vols {
        let wh = format!("level.jsonc/volumes/{n}");
        let mut it = volume_item(&mut c, v, &archive, &wh)?;
        let im = it.as_object_mut().unwrap();
        if let Some(bx) = v.get("box") {
            let lo = xyz(&bx["min"])?;
            let hi = xyz(&bx["max"])?;
            im.insert("shape".into(), json!("bsp"));
            im.insert("bsp".into(), json!(format!("{}/bsp", own_id(&mut own, "volumes", n))));
            im.insert(
                "center".into(),
                json!({"x": f64v((lo[0] + hi[0]) / 2.0), "y": f64v((lo[1] + hi[1]) / 2.0), "z": f64v((lo[2] + hi[2]) / 2.0), "w": 0.0}),
            );
            let mut sum = 0.0;
            for k in 0..3 {
                sum += (hi[k] - lo[k]) * (hi[k] - lo[k]);
            }
            im.insert("radius".into(), f64v(0.5 * sum.sqrt()));
            im.insert("radiusSq".into(), json!(0.0));
        }
        if v.get("data").is_some_and(Value::is_object) {
            im.insert("data".into(), json!(format!("{}/data", own_id(&mut own, "volumes", n))));
        }
        let shape = im["shape"].clone();
        if (shape.as_str() == Some("bsp") || py::eq(&shape, &json!(0))) && im["bsp"].is_null() {
            return Err(lerr(format!("level.jsonc/volumes/{n}: a BSP volume needs a box or a bsp link")));
        }
        let st = vstem.clone().ok_or_else(|| pyerr("TypeError", "'NoneType' object is not subscriptable"))?;
        w.doc(&st)?["items"].as_array_mut().ok_or_else(|| pyerr("TypeError", "items"))?.push(it);
    }
    if !vols.is_empty() {
        let n = w.doc(vstem.as_ref().unwrap())?["items"].as_array().unwrap().len();
        w.doc(&world_stem)?["numVolumes"] = json!(n);
    }

    // the parts the compiler made, by kind
    let mut trig_records: Vec<(String, Vec<u8>, Vec<(u32, Value)>)> = vec![];
    for p in &c.parts {
        if p.kind == "volume" {
            continue;
        }
        let segs: Vec<&str> = p.id.split('/').collect();
        if segs.len() < 4 {
            return Err(lerr(format!("{}: not a level part", p.id)));
        }
        let (kind, name, rest) = (segs[2], segs[3], segs[4..].join("/"));
        let nid = if kind == "triggers" {
            trigger_nid(&names, &p.id, name, &rest)?
        } else if (kind == "timers" || kind == "variables") && rest.starts_with("to") {
            format!("{}{}", own_id(&mut own, kind, name), &rest[2..])
        } else {
            format!("{}{}", own_id(&mut own, kind, name), if rest.is_empty() { String::new() } else { format!("/{rest}") })
        };
        let links: Vec<(u32, Value)> = p.resolved.clone();
        if p.kind == "trigger" {
            trig_records.push((nid, p.data.clone(), links));
        } else {
            w.add_raw(&nid, &p.data, &links);
        }
    }
    for kind in ["timers", "variables"] {
        let recs: Vec<String> = items_of(dget(&lv, kind)?)?.iter().map(|(n, _)| names[&format!("level://{kind}/{n}/to")].clone()).collect();
        let key = format!("num{}{}", kind[..1].to_uppercase(), &kind[1..]);
        match table(&mut w, kind) {
            None if !recs.is_empty() => {
                // the world has none: a table of entries of its own, the World pointing at it
                let nid = format!("{pre}World/0/{kind}");
                let links: Vec<(u32, Value)> = recs.iter().enumerate().map(|(i, r)| (4 * i as u32, json!(r))).collect();
                let stem = w.add_raw(&nid, &vec![0u8; 4 * recs.len()], &links);
                let anchors: Map<String, Value> = (0..recs.len()).map(|i| (i.to_string(), json!(format!("0x{:x}", 4 * i)))).collect();
                w.b.docs.get_mut(&format!("{stem}.jsonc")).unwrap()["anchors"] = Value::Object(anchors);
                let wd = w.doc(&world_stem)?;
                wd[kind] = json!(nid);
                wd[&key] = json!(recs.len());
            }
            Some(st) => {
                let d = w.doc(&st)?;
                let a = d["items"].as_array_mut().ok_or_else(|| pyerr("TypeError", "items"))?;
                a.extend(recs.iter().map(|r| json!(r)));
                let n = a.len();
                w.doc(&world_stem)?[&key] = json!(n);
            }
            None => {}
        }
    }

    // starts: tile cars, copies of the player's start, moved
    if !starts.is_empty() {
        let st = tc_stem.clone().unwrap();
        let tmpl = {
            let items = w.doc(&st)?["items"].as_array().cloned().unwrap_or_default();
            let first = items.first().cloned().ok_or_else(|| pyerr("IndexError", "list index out of range"))?;
            items.iter().find(|c| py::eq(c.get("port").unwrap_or(&Value::Null), &json!(1))).cloned().unwrap_or(first)
        };
        for (_, sd) in &starts {
            let mut it = tmpl.clone();
            let p = sd.get("pos").cloned().unwrap_or(json!([0, 0, 0]));
            let p = xyz(&p)?;
            let wv = it.get("pos").and_then(|x| x.get("w")).cloned().unwrap_or(json!(0.0));
            let im = it.as_object_mut().ok_or_else(|| pyerr("TypeError", "tile car"))?;
            im.insert("pos".into(), json!({"x": f64v(p[0]), "y": f64v(p[1]), "z": f64v(p[2]), "w": wv}));
            im.insert("rotY".into(), f64v(py_float(sd.get("rotY").unwrap_or(&json!(0.0)))?));
            im.insert("port".into(), crate::decode::int_value(py_int(sd.get("port").unwrap_or(&json!(0)))?));
            for (k, v) in sd.as_object().into_iter().flatten() {
                if !["pos", "rotY", "port"].contains(&k.as_str()) {
                    im.insert(k.clone(), v.clone());
                }
            }
            w.doc(&st)?["items"].as_array_mut().unwrap().push(it);
        }
        let n = w.doc(&st)?["items"].as_array().unwrap().len();
        w.doc(&world_stem)?["numTileCars"] = json!(n);
    }

    // items: into their quadrant's contents
    for (n, it) in items_of(dget(&lv, "items")?)? {
        let q = it.get("quad").cloned().unwrap_or(Value::Null);
        if !level::is_int(&q) {
            return Err(lerr(format!("level.jsonc/items/{n}/quad: the quadrant it is in (the editor finds it)")));
        }
        let cid = format!("{pre}World/0/quads/{}/contents", s(&q));
        if !w.file_of.contains_key(&cid) {
            return Err(lerr(format!("level.jsonc/items/{n}/quad: no quadrant {}", s(&q))));
        }
        let p = xyz(&it.get("pos").cloned().unwrap_or(json!([0, 0, 0])))?;
        let rec = json!({"pos": {"x": f64v(p[0]), "y": f64v(p[1]), "z": f64v(p[2]), "w": 0.0}, "mode": "on",
                         "id": crate::decode::int_value(py_int(it.get("id").unwrap_or(&json!(0)))?),
                         "respawn": crate::decode::int_value(py_int(it.get("respawn").unwrap_or(&json!(0)))?)});
        let iid = format!("{cid}/items");
        let has = w.doc_of(&cid)?.get("items").is_some_and(truthy) && w.file_of.contains_key(&iid);
        if !has {
            return Err(lerr(format!(
                "level.jsonc/items/{n}: quadrant {} has no items yet (a new items table: not supported yet; put it where the world already has pickups)",
                s(&q)
            )));
        }
        let idoc = w.doc_of(&iid)?;
        let arr = idoc["items"].as_array_mut().ok_or_else(|| pyerr("TypeError", "items"))?;
        let new = match arr.first() {
            Some(f) => {
                let mut m = f.as_object().cloned().unwrap_or_default();
                for (k, v) in rec.as_object().unwrap() {
                    m.insert(k.clone(), v.clone());
                }
                Value::Object(m)
            }
            None => rec,
        };
        arr.push(new);
        let k = arr.len();
        w.doc_of(&cid)?["numItems"] = json!(k);
    }

    // effect points: copies of a base one, into their quadrant
    for (n, fx) in items_of(dget(&lv, "fxPoints")?)? {
        let q = fx.get("quad").cloned().unwrap_or(Value::Null);
        let tmpl = fx.get("template").map(s).unwrap_or_default();
        let m = re_full(&format!("{}World/0/quads/(\\d+)/contents/fxPoints/(\\d+)", regex::escape(&pre)), &tmpl);
        let (Some(m), true) = (m, level::is_int(&q)) else {
            return Err(lerr(format!("level.jsonc/fxPoints/{n}: quad and template (a base effect point to copy)")));
        };
        let sid = format!("{pre}World/0/quads/{}/contents/fxPoints", &m[1]);
        let idx: usize = m[2].parse().unwrap_or(usize::MAX);
        let src_item = if w.file_of.contains_key(&sid) { w.doc_of(&sid)?.get("items").and_then(Value::as_array).and_then(|a| a.get(idx)).cloned() } else { None };
        let Some(mut it) = src_item else { return Err(lerr(format!("level.jsonc/fxPoints/{n}/template: no such effect point"))) };
        let fid = format!("{pre}World/0/quads/{}/contents/fxPoints", s(&q));
        if !w.file_of.contains_key(&fid) || !w.doc_of(&fid)?.get("items").is_some_and(Value::is_array) {
            return Err(lerr(format!(
                "level.jsonc/fxPoints/{n}/quad: quadrant {} has no effect points yet (not supported yet: put it where the world has some)",
                s(&q)
            )));
        }
        let p = xyz(&fx.get("pos").cloned().unwrap_or(json!([0, 0, 0])))?;
        let wv = it.get("pos").and_then(|x| x.get("w")).cloned().unwrap_or(json!(0.0));
        let mode = fx.get("mode").cloned().unwrap_or_else(|| it.get("mode").cloned().unwrap_or(json!("on")));
        let im = it.as_object_mut().ok_or_else(|| pyerr("TypeError", "effect point"))?;
        im.insert("pos".into(), json!({"x": f64v(p[0]), "y": f64v(p[1]), "z": f64v(p[2]), "w": wv}));
        im.insert("mode".into(), mode);
        let arr = w.doc_of(&fid)?["items"].as_array_mut().unwrap();
        arr.push(it);
        let k = arr.len();
        w.doc_of(&format!("{pre}World/0/quads/{}/contents", s(&q)))?["numFxPoints"] = json!(k);
    }

    // objectives: an entry of the World's objectives and its info (WorldObjectiveInfo)
    let obs = items_of(dget(&lv, "objectives")?)?;
    if !obs.is_empty() {
        let Some(od) = od_stem.clone() else {
            return Err(lerr("level.jsonc/objectives: the world has no objectives table to add to (only mission and challenge worlds have one)"));
        };
        for (n, ob) in &obs {
            let kind = match ob.get("kind").and_then(Value::as_str).unwrap_or("none") {
                "none" => 0u8,
                "counter" => 1,
                "timer" => 2,
                k => return Err(pyerr("KeyError", py::str_repr(k))),
            };
            let hud = (py_int(ob.get("hud").unwrap_or(&json!(0)))? & 0xFF) as u8;
            let mut info = vec![kind, if ob.get("shown").map(truthy).unwrap_or(true) { 0x17 } else { 0 }, hud];
            info.extend([0u8; 25]);
            let mut links = vec![];
            for (off, k) in [(0x0Cu32, "done"), (0x10, "value"), (0x14, "text"), (0x18, "doneText")] {
                if let Some(x) = ob.get(k).and_then(Value::as_str) {
                    links.push((off, c.resolve(x, &format!("level.jsonc/objectives/{n}/{k}"), &archive)?));
                }
            }
            let nid = format!("{}/info", own_id(&mut own, "objectives", n));
            w.add_raw(&nid, &info, &links);
            let arg = crate::decode::int_value(py_int(ob.get("arg").unwrap_or(&json!(0)))?);
            w.doc(&od)?["items"].as_array_mut().ok_or_else(|| pyerr("TypeError", "items"))?.push(json!({"arg": arg, "info": nid}));
        }
        let k = w.doc(&od)?["items"].as_array().unwrap().len();
        w.doc(&world_stem)?["numObjectives"] = json!(k);
    }

    // the trigger table: the base triggers kept, the level's, then the dropped ones
    let keep = lv.get("baseTriggers").cloned().unwrap_or(json!("all"));
    let trig_re = Regex::new(&format!("^{}trigger/\\d+$", regex::escape(&pre))).unwrap();
    let recs: Vec<usize> = w.layout.iter().enumerate().filter(|(_, e)| trig_re.is_match(&s(&e["id"]))).map(|(i, _)| i).collect();
    if !recs.is_empty() {
        let first = recs[0];
        let ents: Vec<Value> = recs.iter().map(|&i| w.layout[i].clone()).collect();
        for &i in recs.iter().rev() {
            w.layout.remove(i);
        }
        let num = |e: &Value| -> i128 { s(&e["id"]).rsplit('/').next().unwrap().parse().unwrap_or(0) };
        let kept: Vec<Value> = ents
            .iter()
            .filter(|x| keep.as_str() == Some("all") || keep.as_array().is_some_and(|a| a.iter().any(|k| py::eq(k, &crate::decode::int_value(num(x))))))
            .cloned()
            .collect();
        let dropped: Vec<Value> = ents.iter().filter(|x| !kept.contains(x)).cloned().collect();
        let mut new = vec![];
        for (nid, data, links) in &trig_records {
            let stem = format!("level/n{}", w.new_stems);
            w.new_stems += 1;
            w.b.docs.insert(format!("{stem}.jsonc"), raw_doc(nid, data, links));
            w.file_of.insert(nid.clone(), stem.clone());
            new.push(json!({"id": nid, "file": stem, "align": 4}));
        }
        let mut tbl = kept.clone();
        tbl.extend(new);
        if tbl == ents {
            for (k, e) in ents.iter().enumerate() {
                w.layout.insert(first + k, e.clone());
            }
        } else {
            for (k, e) in tbl.iter().chain(dropped.iter()).enumerate() {
                let mut e2 = e.as_object().cloned().unwrap_or_default();
                e2.shift_remove("pad");
                e2.insert("align".into(), json!(if k == 0 { 32 } else { 4 }));
                w.layout.insert(first + k, Value::Object(e2));
            }
        }
        let hdr = &mut w.b.pack["pack"]["header"];
        hdr["num_triggers"] = json!(tbl.len());
        if let Some(t0) = tbl.first() {
            hdr["trigger_offset"] = t0["id"].clone();
        }
    }
    let _ = doc;

    // out
    let stem = out_archive.file_stem().map(|x| x.to_string_lossy().into_owned()).unwrap_or_default();
    let parent = out_archive.parent().unwrap_or(Path::new("."));
    let adir = parent.join(format!("{stem}.build"));
    let mut man = crate::jsonc::parse(&crate::error::read_text(&extract_dir.join("manifest.json"))?)?;
    let entries = man["entries"].as_array().cloned().unwrap_or_default();
    let wpath = entries.iter().find(|e| e["type"].as_str() == Some("world")).map(|e| s(&e["path"])).ok_or_else(|| pyerr("StopIteration", ""))?;
    let pdir = adir.join(&stem).join(&wpath);
    if adir.exists() {
        std::fs::remove_dir_all(&adir).map_err(|e| Error::io(format!("{}: {e}", adir.display())))?;
    }
    std::fs::create_dir_all(&pdir).map_err(|e| Error::io(format!("{}: {e}", pdir.display())))?;
    for e in &entries {
        let p = s(&e["path"]);
        if p != wpath {
            let target = std::fs::canonicalize(extract_dir.join(&p)).map_err(|e| Error::io(format!("{p}: {e}")))?;
            symlink_dir(&target, &pdir.parent().unwrap().join(&p))?;
        }
    }
    w.b.pack["layout"] = Value::Array(w.layout.clone());
    match w.b.build(&pdir) {
        Ok(_) => {}
        Err(e) if e.kind == Kind::Graph => return Err(lerr(format!("the world pack: {}", e.msg))),
        Err(e) => return Err(e),
    }
    man["source_file"] = json!(out_archive.file_name().map(|x| x.to_string_lossy().into_owned()).unwrap_or_default());
    std::fs::write(pdir.parent().unwrap().join("manifest.json"), py::dumps_indent(&man, 2, false)).map_err(|e| Error::io(e.to_string()))?;
    let exe = env.find_hwvx_x().ok_or_else(|| lerr("hwvx-x not found (target/release/hwvx-x)"))?;
    let r = std::process::Command::new(&exe)
        .arg("pack")
        .arg(pdir.parent().unwrap())
        .arg(out_archive)
        .arg("-f")
        .stdin(std::process::Stdio::null())
        .output()
        .map_err(|e| lerr(format!("hwvx-x pack: {e}")))?;
    if !r.status.success() {
        let t = if r.stderr.is_empty() { &r.stdout } else { &r.stderr };
        let t = String::from_utf8_lossy(t).trim().to_string();
        let c: Vec<char> = t.chars().collect();
        return Err(lerr(format!("hwvx-x pack: {}", c[c.len().saturating_sub(400)..].iter().collect::<String>())));
    }
    Ok(out_archive.to_path_buf())
}

#[cfg(unix)]
fn symlink_dir(target: &Path, link: &Path) -> Result<()> {
    std::os::unix::fs::symlink(target, link).map_err(|e| Error::io(format!("{}: {e}", link.display())))
}

#[cfg(windows)]
fn symlink_dir(target: &Path, link: &Path) -> Result<()> {
    std::os::windows::fs::symlink_dir(target, link).map_err(|e| Error::io(format!("{}: {e}", link.display())))
}

pub fn release_ext(release: &str) -> &'static str {
    match release {
        "pc" => ".xpc",
        "ps2" => ".xps",
        _ => ".xgc",
    }
}

/// (archive path, cached, base name): the level's world archive in out_dir, built again only
/// when a source or the base changed.
pub fn build_cached(env: &Env, project: &Path, data: &Path, cache: &Path, out_dir: &Path, skip_intro: bool) -> Result<(PathBuf, bool, String)> {
    let lv = read_jsonc(&project.join("level.jsonc"))?;
    let world = s(dget(&lv, "world")?.ok_or_else(|| pyerr("KeyError", "'world'"))?);
    let base = level::base_project(env, &world, data, cache)?;
    let bdir = base.parent().unwrap();
    let bname = bdir.file_name().unwrap().to_string_lossy().into_owned();
    let extract = bdir.join("extract").join(bname.rsplit_once('-').map(|x| x.0).unwrap_or(&bname));
    let rel = s(&read_jsonc(&base.join("pack.jsonc"))?["release"]);
    let name = world.replace('/', "\\").rsplit('\\').next().unwrap_or("").to_lowercase();
    let out = out_dir.join(format!("{name}{}", release_ext(&rel)));
    let key = level::source_hash(
        env,
        project,
        &format!(
            "pack;skip_intro={};base={};hwvx-x={}",
            skip_intro as u8,
            level::base_identity(Some(&base)),
            level::exe_stamp(env.find_hwvx_x().as_deref())
        ),
    );
    let stamp = PathBuf::from(format!("{}.key", out.display()));
    if out.exists() && std::fs::read_to_string(&stamp).ok().as_deref() == Some(key.as_str()) {
        return Ok((out, true, name));
    }
    build_world(env, project, &base, &extract, &out, skip_intro)?;
    std::fs::write(&stamp, &key).map_err(|e| Error::io(format!("{}: {e}", stamp.display())))?;
    Ok((out, false, name))
}

/// a trigger part's record id from the packer's numbering: a name it did not number (one with a
/// '/', which splits the part id) is a level error, not a panic
fn trigger_nid(names: &IndexMap<String, String>, id: &str, name: &str, rest: &str) -> Result<String> {
    let n = names.get(&format!("level://triggers/{name}")).ok_or_else(|| lerr(format!("{id}: no trigger named {}", py::str_repr(name))))?;
    Ok(format!("{n}{}", if rest.is_empty() { String::new() } else { format!("/{rest}") }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trigger_with_slash_is_an_error() {
        let mut names = IndexMap::new();
        names.insert("level://triggers/a/b".to_string(), "x/trigger/3".to_string());
        names.insert("level://triggers/ok".to_string(), "x/trigger/4".to_string());
        assert_eq!(trigger_nid(&names, "level://triggers/ok/param", "ok", "param").unwrap(), "x/trigger/4/param");
        // the part id level://triggers/a/b splits as name "a", rest "b"
        let e = trigger_nid(&names, "level://triggers/a/b", "a", "b").unwrap_err();
        assert_eq!(e.kind, crate::error::Kind::Level);
        assert!(e.msg.contains("no trigger named 'a'"), "{}", e.msg);
    }
}
