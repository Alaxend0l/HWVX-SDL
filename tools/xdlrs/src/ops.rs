//! xdl_struct's commands as functions: extract an object to JSON, build one back, the scrubbed
//! base of the round trip, the round trip of one object, the value survey (observe).

use crate::decode::{Decoder, Resolver};
use crate::encode::Encoder;
use crate::error::{self, Error, Result};
use crate::jsonc;
use crate::pack::{self, Pack, Pairing, Relocs};
use crate::py;
use crate::schema::{self, Schemas};
use crate::validate::MiniValidator;
use serde_json::{Map, Value};
use std::path::{Component, Path, PathBuf};

pub fn object_id(pack: &Pack, struct_name: &str, root: i64, path: &[&str]) -> String {
    let mut s = format!("base://{}/{}/{struct_name}/{root}", pack.archive, pack.name);
    for p in path {
        s.push('/');
        s.push_str(p);
    }
    s
}

/// (offset, struct name, id) of the root object or of the object a field path points to.
pub fn locate(s: &Schemas, pack: &Pack, root_struct: &str, root: i64, path: &[&str]) -> Result<(u32, String, String)> {
    let oid = object_id(pack, root_struct, root, path);
    if path.is_empty() {
        let rk = pack::root_kind(root_struct).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(root_struct))))?;
        let off = crate::decode::py_index(&pack.roots(rk)?, root)?
            .ok_or_else(|| Error::uncaught("the root table entry is not a pointer"))?;
        return Ok((off, root_struct.into(), oid));
    }
    let (off, t) = Resolver::new(s, pack).resolve(&oid)?;
    Ok((off, t.unwrap_or_default(), oid))
}

pub struct Extracted {
    pub doc: Value,
    pub sname: String,
    pub off: u32,
    pub oid: String,
}

/// An object as its JSON document ($schema, format_version, extends, then the fields).
pub fn extract(
    s: &Schemas,
    pack: &Pack,
    pairing: Option<&Pairing>,
    root_struct: &str,
    root: i64,
    path: &[&str],
    schema_ref: Option<&str>,
) -> Result<Extracted> {
    let (off, sname, oid) = locate(s, pack, root_struct, root, path)?;
    let sch = s.struct_(&sname).ok_or_else(|| Error::xdl(format!("no schema for {sname}")))?;
    let mut doc = Map::new();
    if let Some(r) = schema_ref {
        doc.insert("$schema".into(), Value::String(r.into()));
    }
    doc.insert("format_version".into(), Value::from(1));
    doc.insert("extends".into(), Value::String(oid.clone()));
    for (k, v) in Decoder::new(s, pack, pairing).struct_(sch, off, &oid)? {
        doc.insert(k, v);
    }
    Ok(Extracted { doc: Value::Object(doc), sname, off, oid })
}

/// The bytes and relocations of a document over its base (the `extends` object at base_off;
/// no `extends`: zeros), or over `base` when given.
pub fn build(
    s: &Schemas,
    pack: &Pack,
    pairing: Option<&Pairing>,
    doc: &Value,
    sname: &str,
    base_off: u32,
    base: Option<(Vec<u8>, Relocs)>,
) -> Result<(Vec<u8>, Relocs)> {
    let ext = doc.get("extends").filter(|e| truthy(e));
    let mut enc = Encoder::new(s, pack, pairing, sname, ext.map(|_| base_off), base)?;
    enc.encode(doc, ext.and_then(Value::as_str))
}

fn truthy(v: &Value) -> bool {
    match v {
        Value::Null => false,
        Value::Bool(b) => *b,
        Value::Number(n) => n.as_f64() != Some(0.0),
        Value::String(s) => !s.is_empty(),
        Value::Array(a) => !a.is_empty(),
        Value::Object(m) => !m.is_empty(),
    }
}

/// The base object with every field the JSON carries zeroed and its relocations dropped, so a
/// round trip proves those bytes come from the JSON. Kept: hidden fields and gaps, and the raw
/// bytes of texture slots (loader junk the writer takes from the base by design).
pub fn scrubbed_base(s: &Schemas, pack: &Pack, sname: &str, off: u32) -> Result<(Vec<u8>, Relocs)> {
    let sch = s.struct_(sname).ok_or_else(|| Error::xdl(format!("no schema for {sname}")))?;
    let size = schema::size(sch)?;
    let lo0 = (off as usize).min(pack.blob.len());
    let mut data = pack.blob[lo0..(off as usize + size).min(pack.blob.len())].to_vec();
    let mut rel = pack.relocs_in(off, off + size as u32)?;
    for (_, p) in schema::fields(sch) {
        let lo = schema::offset(p)? as usize;
        let hi = lo + s.size_of(schema::ctype(p))?;
        let lf = schema::leaf(p, schema::parse_ctype(schema::ctype(p)).1.len());
        let tex = lf.get("x-link").and_then(|x| x.get("kind")).and_then(Value::as_str) == Some("texture");
        if !tex {
            // data[lo:hi] = bytes(hi - lo) (a slice past the end grows nothing here: the
            // Python's would; objects are inside their pack)
            for b in data.iter_mut().take(hi).skip(lo) {
                *b = 0;
            }
        }
        for k in rel.kinds_mut() {
            k.retain(|&o, _| !((lo as u32) <= o && o < hi as u32));
        }
    }
    Ok((data, rel))
}

/// The pack folders of `archive` in a survey tree's release (its manifest's entries).
pub fn survey_packs(root: &Path, rel: &str, archive: &str) -> Result<Vec<PathBuf>> {
    let mut d = root.join(rel).join(archive);
    if !d.exists() {
        let mut cands = vec![];
        walk_manifests(&root.join(rel), &mut cands);
        cands.retain(|p: &PathBuf| p.parent().and_then(|x| x.file_name()).and_then(|n| n.to_str()).is_some_and(|n| n.to_lowercase() == archive.to_lowercase()));
        cands.sort();
        let Some(c) = cands.first() else {
            return Err(Error::xdl(format!("no {archive} under {}/{rel}", root.display())));
        };
        d = c.parent().unwrap().to_path_buf();
    }
    let man = jsonc::parse(&error::read_text(&d.join("manifest.json"))?)?;
    Ok(man["entries"].as_array().map(|es| es.iter().map(|e| d.join(py::str_of(&e["path"]))).collect()).unwrap_or_default())
}

/// Every manifest.json below `d`, sorted (Path.rglob + sorted).
pub fn walk_manifests(d: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(d) else { return };
    let mut es: Vec<PathBuf> = rd.flatten().map(|e| e.path()).collect();
    es.sort();
    for p in es {
        if p.is_dir() {
            walk_manifests(&p, out);
        } else if p.file_name().is_some_and(|n| n == "manifest.json") {
            out.push(p);
        }
    }
}

/// os.path.relpath(path, start) for absolute paths.
pub fn relpath(path: &Path, start: &Path) -> String {
    let a: Vec<Component> = path.components().collect();
    let b: Vec<Component> = start.components().collect();
    let n = a.iter().zip(&b).take_while(|(x, y)| x == y).count();
    let mut parts: Vec<String> = vec!["..".into(); b.len() - n];
    parts.extend(a[n..].iter().map(|c| c.as_os_str().to_string_lossy().into_owned()));
    if parts.is_empty() { ".".into() } else { parts.join("/") }
}

/// One object's round trip: extract to text, read the text back, validate, build over the
/// scrubbed base, compare bytes and relocations. Ok((true, "0x.. bytes, ..")) or
/// Ok((false, why)); `out`: write the .jsonc under out/rel/pack/.
pub fn roundtrip_one(
    s: &Schemas,
    pack: &Pack,
    pairing: &Pairing,
    root_struct: &str,
    path: &[&str],
    out: Option<&Path>,
    rel: &str,
) -> Result<(bool, String)> {
    let sname_guess = match path.first() {
        Some(p) => pack::PARTS.iter().find(|x| x.0 == *p).map(|x| x.1).unwrap_or(root_struct),
        None => root_struct,
    };
    let fname = format!("{}.jsonc", path.first().copied().unwrap_or("car"));
    let dest = out.map(|o| o.join(rel).join(&pack.name).join(&fname));
    let mut schema_ref = None;
    if let Some(d) = &dest {
        let dir = d.parent().unwrap();
        std::fs::create_dir_all(dir).map_err(|e| Error::io(format!("{}: {e}", dir.display())))?;
        let dir = dir.canonicalize().map_err(|e| Error::io(e.to_string()))?;
        let sp = s.path(sname_guess).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(sname_guess))))?;
        schema_ref = Some(relpath(sp, &dir));
    }
    let ex = extract(s, pack, Some(pairing), root_struct, 0, path, schema_ref.as_deref())?;
    let sch = s.struct_(&ex.sname).unwrap();
    let text = jsonc::to_jsonc(&ex.doc, sch, &[format!("{} {} ({}), from tools/xdl_struct.py", ex.sname, ex.oid, pack.release)]);
    if let Some(d) = &dest {
        std::fs::write(d, &text).map_err(|e| Error::io(format!("{}: {e}", d.display())))?;
    }
    // back from the text, as an editor would leave it
    let doc2 = jsonc::loads(&text)?;
    let top = s.top(&ex.sname).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&ex.sname))))?;
    let errs = MiniValidator::new(top).errors(&doc2);
    if !errs.is_empty() {
        return Ok((false, format!("schema (builtin): {}", errs.iter().take(3).cloned().collect::<Vec<_>>().join("; "))));
    }
    let base = scrubbed_base(s, pack, &ex.sname, ex.off)?;
    let (data, rel_out) = build(s, pack, Some(pairing), &doc2, &ex.sname, ex.off, Some(base))?;
    let size = data.len();
    let off = ex.off as usize;
    let want = &pack.blob[off.min(pack.blob.len())..(off + size).min(pack.blob.len())];
    let want_rel = pack.relocs_in(ex.off, ex.off + size as u32)?;
    if data != want {
        let diff: Vec<String> = (0..size)
            .filter(|&i| want.get(i) != Some(&data[i]))
            .take(8)
            .map(|i| format!("0x{i:x}"))
            .collect();
        return Ok((false, format!("bytes differ at {}", diff.join(", "))));
    }
    for ((k, w), (_, g)) in want_rel.kinds().iter().zip(rel_out.kinds().iter()) {
        if w != g {
            let mut d: Vec<(u32, u32)> = w.iter().filter(|(a, b)| g.get(a) != Some(b)).map(|(a, b)| (*a, *b)).collect();
            d.extend(g.iter().filter(|(a, b)| w.get(a) != Some(b)).map(|(a, b)| (*a, *b)));
            d.sort();
            let shown: Vec<String> = d.iter().take(4).map(|(a, b)| format!("({a}, {b})")).collect();
            return Ok((false, format!("{k} relocations differ: [{}]", shown.join(", "))));
        }
    }
    Ok((true, format!("0x{size:X} bytes, {} relocations, {} fields (builtin)", rel_out.count(), schema::fields(sch).len())))
}

/// Numbers of a decoded value (lists flattened, objects skipped).
pub fn observe_values(v: &Value, acc: &mut Vec<Value>) {
    match v {
        Value::Array(a) => a.iter().for_each(|x| observe_values(x, acc)),
        Value::Number(_) => acc.push(v.clone()),
        _ => {}
    }
}
