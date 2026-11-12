//! The index (xdl_mod.py index): hashes of everything in the modder's own install, and the
//! lookups the check makes against it. Stored as numpy's .npz, the same arrays the Python
//! writes, so either side reads the other's index.

use super::blocks::{COARSE, FINE, block_hashes};
use super::geom::{self, Blob};
use super::hash::{blake2b, h64, hex};
use super::image::{Rgba, dhash, pixel_hashes};
use super::media::{self, Archive, Ids, RawPack, slice};
use super::structs::Structs;
use super::zip;
use super::{ModError, TOOL, TOOL_VERSION, FORMAT_VERSION, audio};
use crate::py;
use serde_json::{Map, Value, json};
use std::collections::{BTreeMap, HashMap, HashSet, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

pub const TABLES: [&str; 7] = ["b32", "b256", "whole", "px", "rgb", "snd", "vtx"];
pub const ARCHIVE_EXT: [&str; 3] = [".xgc", ".xpc", ".xps"];

fn root_kind(type_name: &str) -> Option<&'static str> {
    match type_name {
        "car" => Some("CarAInfo"),
        "item" => Some("ItemRecord"),
        _ => None,
    }
}

/// What one file adds to the index.
#[derive(Default)]
pub struct Collect {
    pub sources: Vec<String>,
    pub t: HashMap<&'static str, Vec<(Vec<u64>, u32)>>,
    pub dh: Vec<([u8; 32], u32)>,
    pub sfp: Vec<(Vec<u16>, Vec<u32>, u32)>,
    pub obj: Vec<u64>,
    pub objects: Vec<(String, String, String)>,
    pub tex: Vec<(String, String, Value)>,
    pub files: Vec<Value>,
    pub notes: Vec<String>,
}

impl Collect {
    fn src(&mut self, name: String) -> u32 {
        self.sources.push(name);
        (self.sources.len() - 1) as u32
    }
    fn add(&mut self, table: &'static str, hashes: Vec<u64>, src: u32) {
        if !hashes.is_empty() {
            self.t.entry(table).or_default().push((hashes, src));
        }
    }
}

/// struct.unpack + struct.pack "<": a field's value bytes as the Python hashes them
fn field_bytes(d: &[u8], o: usize, prim: &str, cnt: usize, big: bool) -> Result<Vec<u8>, String> {
    let p = crate::schema::prim(prim).unwrap();
    let n = p.size * cnt;
    if o + n > d.len() {
        return Err(format!("unpack_from requires a buffer of at least {} bytes", o + n));
    }
    let mut out = Vec::with_capacity(n);
    for k in 0..cnt {
        let mut b = d[o + k * p.size..o + (k + 1) * p.size].to_vec();
        if big {
            b.reverse();
        }
        if p.code == 'f' {
            let x = f32::from_le_bytes(b[..4].try_into().unwrap());
            b = ((x as f64) as f32).to_le_bytes().to_vec();
        }
        out.extend_from_slice(&b);
    }
    Ok(out)
}

pub fn field_key(obj_id: &str, field: &str, raw: &[u8]) -> u64 {
    let mut b = format!("{}|{field}|", obj_id.to_lowercase()).into_bytes();
    b.extend_from_slice(raw);
    h64(&b)
}

/// _objects: typed objects of a car or item pack and each scalar field's value hash.
#[allow(clippy::type_complexity)]
fn objects(pk: &RawPack, blob: &Blob, st: &Structs, base_id: &str) -> Result<(Vec<(String, String)>, Vec<u64>), String> {
    let tn = media::TYPE_NAMES.get(pk.typ as usize).copied().unwrap_or("");
    let mut objs: Vec<(String, String)> = vec![];
    let mut keys = vec![];
    let Some(root) = root_kind(tn) else { return Ok((objs, keys)) };
    if blob.d.len() < 8 {
        return Ok((objs, keys));
    }
    let n = blob.u32(0)?;
    let Some(tab) = blob.target(4)? else { return Ok((objs, keys)) };
    let mut todo = VecDeque::new();
    for i in 0..n.min(0x1000) as usize {
        if let Some(o) = blob.target(tab + 8 * i)? {
            todo.push_back((format!("{base_id}/{root}/{i}"), root.to_string(), o));
        }
    }
    let mut seen = HashSet::new();
    let mut fields_of: HashMap<String, Vec<(String, super::structs::Field)>> = HashMap::new();
    while let Some((oid, sname, off)) = todo.pop_front() {
        let size = st.size(&sname).ok_or_else(|| format!("'{sname}'"))?;
        if seen.contains(&(sname.clone(), off)) || off + size > blob.d.len() {
            continue;
        }
        seen.insert((sname.clone(), off));
        match objs.iter_mut().find(|(k, _)| *k == oid) {
            Some(e) => e.1 = sname.clone(),
            None => objs.push((oid.clone(), sname.clone())),
        }
        let fields = fields_of.entry(sname.clone()).or_insert_with(|| st.fields(&sname)).clone();
        for (f, fl) in fields {
            if let Some(t) = &fl.target {
                if let Some(to) = blob.target(off + fl.off)? {
                    todo.push_back((format!("{oid}/{f}"), t.clone(), to));
                }
            } else if let Some(p) = &fl.prim {
                let raw = field_bytes(blob.d, off + fl.off, p, fl.count.max(1), blob.big)?;
                keys.push(field_key(&oid, &f, &raw));
            }
        }
    }
    Ok((objs, keys))
}

/// index_file: what one file of the install adds.
pub fn index_file(path: &Path, name: &str, want_geom: bool, root: &Path, pairing: Option<PathBuf>) -> Collect {
    let mut c = Collect::default();
    let data = match std::fs::read(path) {
        Ok(d) => d,
        Err(e) => {
            c.notes.push(format!("{name}: {e}"));
            return c;
        }
    };
    let mut file = vec![json!(name), json!(data.len()), json!(format!("{:016x}", h64(&data)))];
    let lname = name.to_lowercase();
    if !ARCHIVE_EXT.iter().any(|e| lname.ends_with(e)) {
        c.files.push(Value::Array(file));
        let s = c.src(name.to_string());
        c.add("whole", vec![h64(&data)], s);
        c.add("b256", block_hashes(&data, COARSE), s);
        return c;
    }
    let whole = h64(&data);
    let arc = match Archive::from_bytes(data, path) {
        Ok(a) => a,
        Err(e) => {
            let data = std::fs::read(path).unwrap_or_default();
            c.files.push(Value::Array(file));
            c.notes.push(format!("{name}: {e}; indexed as a plain file"));
            let s = c.src(name.to_string());
            c.add("whole", vec![whole], s);
            c.add("b32", block_hashes(&data, FINE), s);
            return c;
        }
    };
    let rel = arc.release;
    file.push(json!(rel));
    c.files.push(Value::Array(file));
    let ids = Ids::new(pairing);
    let archive = ids.archive(&arc.name);
    let st = match Structs::load(root) {
        Ok(s) => s,
        Err(e) => {
            c.notes.push(e);
            Structs { s: HashMap::new() }
        }
    };
    let s = c.src(format!("{rel}: the archive {name}"));
    c.add("whole", vec![whole], s);
    for ent in &arc.entries {
        let raw = arc.pack_bytes(ent);
        let base_id = format!("base://{archive}/{}", ent.name);
        let ps = c.src(format!("{rel}: {base_id}"));
        c.add("whole", vec![h64(raw)], ps);
        c.add("b32", block_hashes(raw, FINE), ps);
        let pk = match RawPack::new(rel, ent.typ, raw, &ent.name) {
            Ok(p) => p,
            Err(e) => {
                c.notes.push(format!("{name} {}: {e}; raw blocks only", ent.name));
                continue;
            }
        };
        if let Err(e) = index_textures(&mut c, &pk, &ids, &archive, &ent.name) {
            c.notes.push(format!("{name} {}: textures: {e}", ent.name));
        }
        if let Err(e) = index_sounds(&mut c, &pk, &ids, &archive, &ent.name, &base_id) {
            c.notes.push(format!("{name} {}: sounds: {e}", ent.name));
        }
        let ptrs = pk.words("pointers");
        let blob = Blob::new(pk.section("data"), &ptrs, rel);
        match objects(&pk, &blob, &st, &base_id) {
            Ok((objs, keys)) => {
                for (oid, sname) in objs {
                    c.objects.push((oid, rel.to_string(), sname));
                }
                c.obj.extend(keys);
            }
            Err(e) => c.notes.push(format!("{name} {}: objects: {e}", ent.name)),
        }
        if want_geom {
            let set: std::collections::BTreeSet<u32> = ptrs.iter().copied().collect();
            for (gi, off) in crate::pack::scan_geometries(blob.d, &set, rel).into_iter().enumerate() {
                match geom::positions(&blob, off as usize) {
                    Err(e) => c.notes.push(format!("{name} {} geometry {gi}: {e}", ent.name)),
                    Ok(pos) if !pos.is_empty() => {
                        let mut h: Vec<u64> = pos.iter().map(|p| geom::vertex_hash(*p)).collect();
                        h.sort_unstable();
                        h.dedup();
                        let s = c.src(format!("{rel}: {base_id}/geometry/{gi}"));
                        c.add("vtx", h, s);
                    }
                    Ok(_) => {}
                }
            }
        }
    }
    c
}

fn index_textures(c: &mut Collect, pk: &RawPack, ids: &Ids, archive: &str, pname: &str) -> Result<(), String> {
    let rel = pk.release;
    for t in media::scan_textures(pk)? {
        let tid = ids.asset(rel, archive, pname, "texture", t.i);
        let ts = c.src(format!("{rel}: {tid}"));
        let clut = t.clut.map(|o| slice(pk.b, o, (o + 0x400) as i64));
        let fp = media::texture_fingerprint(pk, &t);
        c.tex.push((tid.clone(), rel.to_string(), json!({"x": fp, "w": t.w, "h": t.h, "i": t.i})));
        for (k, &(o, n, lw, lh)) in t.levels.iter().enumerate() {
            let px = media::decode_level(rel, t.fmt, slice(pk.b, o, (o + n) as i64), lw, lh, clut)?;
            let img = Rgba { w: lw, h: lh, px };
            let (a, b) = pixel_hashes(&img);
            c.add("px", vec![a], ts);
            c.add("rgb", vec![b], ts);
            if k == 0
                && let Some(d) = dhash(&img)
            {
                c.dh.push((d, ts));
            }
        }
    }
    Ok(())
}

fn index_sounds(c: &mut Collect, pk: &RawPack, ids: &Ids, archive: &str, pname: &str, base_id: &str) -> Result<(), String> {
    let rel = pk.release;
    let mut samples: Vec<(String, &[u8], u32)> = media::scan_sounds(pk)?
        .iter()
        .map(|s| (ids.asset(rel, archive, pname, "sound", s.i), slice(pk.b, s.ptr, (s.ptr + s.len) as i64), s.rate))
        .collect();
    let (song, bank) = media::scan_song(pk)?;
    if let Some((o, n)) = song {
        let s = c.src(format!("{rel}: {base_id}/song/0"));
        c.add("whole", vec![h64(slice(pk.b, o, o as i64 + n))], s);
    }
    if let Some((ents, data)) = bank {
        let mut done = HashSet::new();
        for en in ents {
            if done.insert((en.off, en.len)) {
                let p = data + en.off;
                samples.push((ids.asset(rel, archive, pname, "bank", en.k), slice(pk.b, p, (p + en.len) as i64), en.rate));
            }
        }
    }
    for (sid, sraw, srate) in samples {
        let pcm16 = media::decode_sample(rel, sraw, &sid)?;
        let pcm: Vec<u8> = pcm16.iter().flat_map(|v| v.to_le_bytes()).collect();
        let ss = c.src(format!("{rel}: {sid}"));
        c.add("snd", vec![h64(&pcm)], ss);
        c.add("b32", block_hashes(&pcm, FINE), ss);
        let (k, f) = audio::sound_fp(&pcm16, srate);
        if !k.is_empty() {
            c.sfp.push((k, f, ss));
        }
    }
    Ok(())
}

/// Path.rglob("*") as Python 3.9 walks it (symlinked folders not entered), sorted as paths
/// sort (by their parts): (path, parts).
pub fn rglob(d: &Path) -> Vec<(PathBuf, Vec<String>)> {
    fn walk(d: &Path, parts: &[String], out: &mut Vec<(PathBuf, Vec<String>)>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        for e in rd.flatten() {
            let n = e.file_name().to_string_lossy().into_owned();
            let mut p = parts.to_vec();
            p.push(n);
            let path = e.path();
            out.push((path.clone(), p.clone()));
            let is_link = e.file_type().map(|t| t.is_symlink()).unwrap_or(false);
            if !is_link && path.is_dir() {
                walk(&path, &p, out);
            }
        }
    }
    let mut out = vec![];
    walk(d, &[], &mut out);
    out.sort_by(|a, b| a.1.cmp(&b.1));
    out
}

pub struct IndexOptions {
    pub archives: Option<Vec<String>>,
    pub no_geometry: bool,
    pub jobs: usize,
}

/// cmd_index: index the folders, write the .npz. -> the Python's printed summary.
pub fn build_index(data: &[PathBuf], out: &Path, opt: &IndexOptions, root: &Path) -> Result<String, ModError> {
    let t0 = std::time::Instant::now();
    let only: Option<HashSet<String>> = opt.archives.as_ref().map(|a| a.iter().map(|s| s.to_lowercase()).collect());
    let mut jobs: Vec<(PathBuf, String, u64)> = vec![];
    for d in data {
        if !d.is_dir() {
            return Err(ModError(format!("{}: not a folder", super::py_path(&d.to_string_lossy()))));
        }
        for (p, parts) in rglob(d) {
            if !p.is_file() || parts.last().is_some_and(|n| n.starts_with('.')) {
                continue;
            }
            let lname = parts.last().unwrap().to_lowercase();
            if let Some(only) = &only
                && (!ARCHIVE_EXT.iter().any(|e| lname.ends_with(e)) || !only.contains(&media::stem(&p).to_lowercase()))
            {
                continue;
            }
            let size = std::fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
            jobs.push((p, parts.join("/"), size));
        }
    }
    if jobs.is_empty() {
        return Err(ModError(format!(
            "no game files found in {}",
            data.iter().map(|d| super::py_path(&d.to_string_lossy())).collect::<Vec<_>>().join(", ")
        )));
    }
    jobs.sort_by_key(|j| std::cmp::Reverse(j.2));
    let pairing = crate::pairing_dir(root);
    let next = AtomicUsize::new(0);
    let parts: Mutex<Vec<Option<Collect>>> = Mutex::new((0..jobs.len()).map(|_| None).collect());
    std::thread::scope(|s| {
        for _ in 0..opt.jobs.max(1) {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, Ordering::SeqCst);
                    if i >= jobs.len() {
                        break;
                    }
                    let (p, n, _) = &jobs[i];
                    let c = index_file(p, n, !opt.no_geometry, root, pairing.clone());
                    parts.lock().unwrap()[i] = Some(c);
                }
            });
        }
    });
    let parts: Vec<Collect> = parts.into_inner().unwrap().into_iter().map(Option::unwrap).collect();
    // merge, as cmd_index does
    let mut sources: Vec<String> = vec![];
    let mut tabs: HashMap<&str, (Vec<u64>, Vec<u32>)> = HashMap::new();
    let mut dh: Vec<[u8; 32]> = vec![];
    let mut dh_src: Vec<u32> = vec![];
    let mut sfp: Vec<(Vec<u16>, Vec<u32>, u32)> = vec![];
    let mut obj: Vec<u64> = vec![];
    let mut notes: Vec<String> = vec![];
    let mut files: Vec<Value> = vec![];
    let mut objects = Map::new();
    let mut textures = Map::new();
    for c in parts {
        let base = sources.len() as u32;
        sources.extend(c.sources);
        for k in TABLES {
            if let Some(v) = c.t.get(k) {
                let e = tabs.entry(k).or_default();
                for (h, s) in v {
                    e.0.extend_from_slice(h);
                    e.1.extend(std::iter::repeat_n(s + base, h.len()));
                }
            }
        }
        for (d, s) in c.dh {
            dh.push(d);
            dh_src.push(s + base);
        }
        sfp.extend(c.sfp.into_iter().map(|(k, f, s)| (k, f, s + base)));
        obj.extend(c.obj);
        notes.extend(c.notes);
        files.extend(c.files);
        for (oid, rel, sname) in c.objects {
            objects.entry(oid).or_insert_with(|| json!({})).as_object_mut().unwrap().insert(rel, json!(sname));
        }
        for (tid, rel, v) in c.tex {
            textures.entry(tid).or_insert_with(|| json!({})).as_object_mut().unwrap().insert(rel, v);
        }
    }
    files.sort_by(py_list_cmp);
    let mut rels: Vec<String> =
        files.iter().filter_map(|f| f.get(3).and_then(Value::as_str).map(String::from)).collect::<std::collections::BTreeSet<_>>().into_iter().collect();
    rels.sort();
    let partial = only.is_some() || opt.no_geometry;
    let id = hex(&blake2b(py::dumps(&Value::Array(files.clone())).as_bytes(), 8));
    let nfiles = files.len();
    let meta = json!({
        "tool": TOOL, "tool_version": TOOL_VERSION, "format_version": FORMAT_VERSION,
        "partial": partial, "geometry": !opt.no_geometry, "files": files,
        "objects": objects, "textures": textures, "releases": rels, "id": id, "sources": sources,
    });
    // the arrays
    let mut arrays: Vec<(String, Vec<u8>)> = vec![];
    let le64 = |v: &[u64]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>();
    let le32 = |v: &[u32]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>();
    let mut counts = HashMap::new();
    for k in TABLES {
        let (h, s) = tabs.remove(k).unwrap_or_default();
        let mut idx: Vec<u32> = (0..h.len() as u32).collect();
        idx.sort_by_key(|&i| (h[i as usize], i));
        let mut uh = Vec::with_capacity(h.len());
        let mut us = Vec::with_capacity(h.len());
        for i in idx {
            let x = h[i as usize];
            if uh.last() != Some(&x) {
                uh.push(x);
                us.push(s[i as usize]);
            }
        }
        counts.insert(k, uh.len());
        arrays.push((format!("{k}.npy"), zip::npy_bytes("<u8", &[uh.len()], &le64(&uh))));
        arrays.push((format!("{k}_src.npy"), zip::npy_bytes("<u4", &[us.len()], &le32(&us))));
    }
    let dh_flat: Vec<u8> = dh.iter().flatten().copied().collect();
    arrays.push(("dh.npy".into(), zip::npy_bytes("<u8", &[dh.len(), 4], &dh_flat)));
    arrays.push(("dh_src.npy".into(), zip::npy_bytes("<u4", &[dh_src.len()], &le32(&dh_src))));
    obj.sort_unstable();
    obj.dedup();
    arrays.push(("obj.npy".into(), zip::npy_bytes("<u8", &[obj.len()], &le64(&obj))));
    let mut nsfp = 0;
    if !sfp.is_empty() {
        let mut all: Vec<(u16, u32, u32)> = vec![];
        for (k, f, s) in &sfp {
            for (a, b) in k.iter().zip(f) {
                all.push((*a, *b, *s));
            }
        }
        all.sort_by_key(|x| x.0);
        nsfp = all.len();
        let keys: Vec<u8> = all.iter().flat_map(|x| x.0.to_le_bytes()).collect();
        let fr: Vec<u8> = all.iter().flat_map(|x| x.1.to_le_bytes()).collect();
        let sr: Vec<u8> = all.iter().flat_map(|x| x.2.to_le_bytes()).collect();
        arrays.push(("sfp.npy".into(), zip::npy_bytes("<u2", &[all.len()], &keys)));
        arrays.push(("sfp_frame.npy".into(), zip::npy_bytes("<u4", &[all.len()], &fr)));
        arrays.push(("sfp_src.npy".into(), zip::npy_bytes("<u4", &[all.len()], &sr)));
    }
    let mtext = py::dumps(&meta);
    arrays.push(("meta.npy".into(), zip::npy_bytes("|u1", &[mtext.len()], mtext.as_bytes())));
    if let Some(p) = out.parent() {
        std::fs::create_dir_all(p).map_err(|e| ModError(e.to_string()))?;
    }
    let refs: Vec<(String, &[u8])> = arrays.iter().map(|(n, b)| (n.clone(), b.as_slice())).collect();
    let z = zip::write_stored(&refs);
    std::fs::write(out, &z).map_err(|e| ModError(format!("{}: {e}", out.display())))?;
    let mut text = String::new();
    for n in notes.iter().take(20) {
        text += &format!("note: {n}\n");
    }
    if notes.len() > 20 {
        text += &format!("note: ... and {} more\n", notes.len() - 20);
    }
    let shown = super::py_path(&out.to_string_lossy());
    text += &format!(
        "{shown}: {nfiles} files of {}{}: {} + {} blocks, {} texture levels, {} texture looks, {} samples ({nsfp} sound frames), {} vertices, {} objects; {:.1} MB, {:.0} s\n",
        if rels.is_empty() { "no release".to_string() } else { rels.join(", ") },
        if partial { " (PARTIAL)" } else { "" },
        counts["b32"], counts["b256"], counts["px"], dh.len(), counts["snd"], counts["vtx"],
        meta["objects"].as_object().unwrap().len(),
        z.len() as f64 / 1e6,
        t0.elapsed().as_secs_f64()
    );
    Ok(text)
}

/// Python's ordering of the index's file lists ([name, size, hash] or with the release)
fn py_list_cmp(a: &Value, b: &Value) -> std::cmp::Ordering {
    let (a, b) = (a.as_array().unwrap(), b.as_array().unwrap());
    for (x, y) in a.iter().zip(b) {
        let o = match (x, y) {
            (Value::String(x), Value::String(y)) => x.cmp(y),
            (Value::Number(x), Value::Number(y)) => x.as_u64().cmp(&y.as_u64()),
            _ => std::cmp::Ordering::Equal,
        };
        if o != std::cmp::Ordering::Equal {
            return o;
        }
    }
    a.len().cmp(&b.len())
}

/// A loaded index and its lookups (xdl_mod.Index).
pub struct Index {
    pub meta: Value,
    pub sources: Vec<String>,
    tabs: BTreeMap<String, (Vec<u64>, Option<Vec<u32>>)>,
    dh: Vec<[u8; 32]>,
    dh_src: Vec<u32>,
    sfp: Option<(Vec<u16>, Vec<u32>, Vec<u32>)>,
}

impl Index {
    pub fn load(path: &Path) -> Result<Index, ModError> {
        let shown = super::py_path(&path.to_string_lossy());
        let bad = |e: String| ModError(format!("{shown}: not an index of xdl_mod.py index ({e})"));
        let z = std::fs::read(path).map_err(|e| match e.kind() {
            std::io::ErrorKind::NotFound => bad(format!("[Errno 2] No such file or directory: '{shown}'")),
            _ => bad(e.to_string()),
        })?;
        let ents = zip::read_stored(&z).map_err(bad)?;
        let mut arr: BTreeMap<String, zip::Npy> = BTreeMap::new();
        for (n, (o, l)) in &ents {
            if let Some(k) = n.strip_suffix(".npy") {
                arr.insert(k.to_string(), zip::npy_parse(&z[*o..*o + *l]).map_err(bad)?);
            }
        }
        let meta_a = arr.remove("meta").ok_or_else(|| bad("'meta is not a file in the archive'".into()))?;
        let meta: Value = serde_json::from_slice(&meta_a.data).map_err(|e| bad(e.to_string()))?;
        if meta.get("tool_version") != Some(&json!(TOOL_VERSION)) {
            return Err(ModError(format!("{shown}: made by another version of {TOOL}; run index again")));
        }
        let sources: Vec<String> =
            meta["sources"].as_array().map(|a| a.iter().map(|s| s.as_str().unwrap_or("").to_string()).collect()).unwrap_or_default();
        let mut tabs = BTreeMap::new();
        for k in TABLES.iter().copied().chain(["obj"]) {
            let Some(a) = arr.get(k) else { continue };
            let h = zip::u64s(a).map_err(bad)?;
            let s = match arr.get(&format!("{k}_src")) {
                Some(s) => Some(zip::u32s(s).map_err(bad)?),
                None => None,
            };
            tabs.insert(k.to_string(), (h, s));
        }
        let dh = arr.get("dh").map(|a| a.data.chunks_exact(32).map(|c| c.try_into().unwrap()).collect()).unwrap_or_default();
        let dh_src = arr.get("dh_src").map(|a| zip::u32s(a)).transpose().map_err(bad)?.unwrap_or_default();
        let sfp = match (arr.get("sfp"), arr.get("sfp_frame"), arr.get("sfp_src")) {
            (Some(k), Some(f), Some(s)) => Some((zip::u16s(k).map_err(bad)?, zip::u32s(f).map_err(bad)?, zip::u32s(s).map_err(bad)?)),
            _ => None,
        };
        Ok(Index { meta, sources, tabs, dh, dh_src, sfp })
    }

    pub fn has_sfp(&self) -> bool {
        self.sfp.is_some()
    }

    /// find: (positions in `hashes` that are in the table, their sources)
    pub fn find(&self, table: &str, hashes: &[u64]) -> (Vec<usize>, Vec<String>) {
        let Some((t, s)) = self.tabs.get(table) else { return (vec![], vec![]) };
        let mut hit = vec![];
        let mut src = vec![];
        if t.is_empty() {
            return (hit, src);
        }
        for (i, h) in hashes.iter().enumerate() {
            if let Ok(j) = t.binary_search(h) {
                // the first of equal hashes (the table is unique, so any)
                hit.push(i);
                if let Some(s) = s {
                    src.push(self.sources[s[j] as usize].clone());
                }
            }
        }
        (hit, src)
    }

    pub fn one(&self, table: &str, h: u64) -> Option<String> {
        let (hit, src) = self.find(table, &[h]);
        if hit.is_empty() { None } else { src.into_iter().next() }
    }

    /// sounds_like: (frames lined up, of how many, source)
    pub fn sounds_like(&self, keys: &[u16], frames: &[u32]) -> Option<(i64, usize, String)> {
        let (t, tf, ts) = self.sfp.as_ref()?;
        if t.is_empty() || keys.is_empty() {
            return None;
        }
        let mut pairs: Vec<(i64, usize)> = vec![];
        for (q, &k) in keys.iter().enumerate() {
            let lo = t.partition_point(|&x| x < k);
            let hi = t.partition_point(|&x| x <= k);
            let cnt = (hi - lo).min(2000);
            for p in lo..lo + cnt {
                let src = ts[p] as i64;
                let off = tf[p] as i64 - frames[q] as i64 + (1 << 20);
                pairs.push(((src << 22) | off, q));
            }
        }
        if pairs.is_empty() {
            return None;
        }
        pairs.sort_unstable();
        pairs.dedup();
        // (key, distinct query frames)
        let mut v: Vec<(i64, i64)> = vec![];
        for (k, _) in pairs {
            match v.last_mut() {
                Some(l) if l.0 == k => l.1 += 1,
                _ => v.push((k, 1)),
            }
        }
        let mut best = (i64::MIN, 0usize);
        for i in 0..v.len() {
            let mut c2 = v[i].1;
            if i + 1 < v.len() && v[i + 1].0 == v[i].0 + 1 {
                c2 += v[i + 1].1;
            }
            if c2 > best.0 {
                best = (c2, i);
            }
        }
        Some((best.0, keys.len(), self.sources[(v[best.1].0 >> 22) as usize].clone()))
    }

    /// near: the base texture a gradient hash is closest to: (bits differing, source)
    pub fn near(&self, d: Option<[u8; 32]>) -> Option<(u32, String)> {
        let d = d?;
        if self.dh.is_empty() {
            return None;
        }
        let mut best = (u32::MAX, 0usize);
        for (i, r) in self.dh.iter().enumerate() {
            let bits: u32 = r.iter().zip(&d).map(|(a, b)| (a ^ b).count_ones()).sum();
            if bits < best.0 {
                best = (bits, i);
            }
        }
        Some((best.0, self.sources[self.dh_src[best.1] as usize].clone()))
    }
}
