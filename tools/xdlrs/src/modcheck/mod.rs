//! The mod check: tools/xdl_mod.py's index, check and pack (docs/xdl-format README §8) in Rust,
//! with the parts of xdl_media.py and xdl_gltf.py they need.
//!
//! * [`build_index`]: hashes of the modder's own install (xdl_mod.py index), written as the
//!   same .npz the Python writes (either reads the other's).
//! * [`check_and_pack`]: what `xdl_mod.py check` / `pack` decide for a mod folder, with the
//!   same refusals (place, reason, the base:// id to use instead), notes, printed text and
//!   exit code; `pack` writes the same .xmod bytes.
//!
//! Sounds decode through the port's own decoders (src/audiodec, linked by build.rs), so a mod's
//! sound is checked as the samples the game will play.

pub mod audio;
pub mod blobs;
pub mod blocks;
pub mod geom;
pub mod hash;
pub mod image;
pub mod index;
pub mod manifest;
pub mod media;
pub mod structs;
pub mod zip;

pub use index::{Index, IndexOptions, build_index};
pub use manifest::Manifest;

use blocks::{COARSE, FINE, window_hashes_chunked};
use hash::{h64, sha256_hex};
use manifest::{AUDIO_EXT, res};
use regex::Regex;
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

pub const TOOL: &str = "tools/xdl_mod.py";
pub const TOOL_VERSION: i64 = 2;
pub const FORMAT_VERSION: i64 = 1;
pub const ALLOWED_EXT: [&str; 13] =
    [".json", ".jsonc", ".png", ".wav", ".flac", ".ogg", ".mp3", ".gltf", ".glb", ".bin", ".mid", ".md", ".txt"];
const DH_NEAR: u32 = 40;
const VTX_MIN: usize = 24;
const VTX_SHARE: f64 = 0.2;
const SFP_MIN: i64 = 8;
const SFP_SHARE: f64 = 0.15;
/// 1/128 of full scale (-42 dB): a run this quiet (a fade, near-silence) is in any audio
const QUIET: i32 = 256;

/// xdl_mod.ModError: the Python prints "error: <message>" and exits 2.
#[derive(Debug, Clone)]
pub struct ModError(pub String);

impl std::fmt::Display for ModError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// str(PurePosixPath(s)): how the Python prints a path it made from a string.
pub fn py_path(s: &str) -> String {
    let root = if s.starts_with("//") && !s.starts_with("///") {
        "//"
    } else if s.starts_with('/') {
        "/"
    } else {
        ""
    };
    let parts: Vec<&str> = s.split('/').filter(|p| !p.is_empty() && *p != ".").collect();
    let out = format!("{root}{}", parts.join("/"));
    if out.is_empty() { ".".into() } else { out }
}

/// A Python pattern for re.match: `$` also matches before a final newline.
pub fn re_py(p: &str) -> Regex {
    let p = p.strip_suffix('$').map(|x| format!(r"{x}\n?\z")).unwrap_or_else(|| p.to_string());
    Regex::new(&p).unwrap()
}

/// One refusal: where, why, and the base:// id (or recipe) to use instead.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Refusal {
    pub place: String,
    pub why: String,
    pub use_instead: Option<String>,
}

#[derive(Default)]
pub struct Report {
    pub refused: Vec<Refusal>,
    pub notes: Vec<String>,
}

impl Report {
    fn refuse(&mut self, place: impl Into<String>, why: impl Into<String>, use_instead: Option<String>) {
        self.refused.push(Refusal { place: place.into(), why: why.into(), use_instead });
    }
    fn show(&self) -> String {
        let mut s = String::new();
        for n in &self.notes {
            s += &format!("note: {n}\n");
        }
        for r in &self.refused {
            s += &format!("REFUSED {}: {}", r.place, r.why);
            if let Some(u) = &r.use_instead {
                s += &format!("\n        use {u} instead");
            }
            s.push('\n');
        }
        s
    }
}

/// _asset: 'gc: base://...' -> the id to reference
fn asset(src: &str) -> Option<String> {
    if src.contains(": base://") { src.split_once(": ").map(|x| x.1.to_string()) } else { None }
}

fn py_opt(s: &Option<String>) -> String {
    s.clone().unwrap_or_else(|| "None".into())
}

/// check_bytes: whole-file and raw-run checks of one byte string. keep(offset, B): which hits count.
fn check_bytes(index: &Index, rep: &mut Report, place: &str, data: &[u8], what: &str, keep: Option<&dyn Fn(usize, usize) -> bool>) -> bool {
    if let Some(src) = index.one("whole", h64(data)) {
        let a = asset(&src);
        rep.refuse(place, format!("is the game's own file ({src})"), a);
        return true;
    }
    for (table, b) in [("b32", FINE), ("b256", COARSE)] {
        // hashed in chunks of 1M windows (8 MB), not 8 bytes per byte of the whole file at once
        let (mut hit, mut srcs) = (vec![], vec![]);
        window_hashes_chunked(data, b, 1 << 20, |start, hs| {
            let (h, s) = index.find(table, hs);
            hit.extend(h.into_iter().map(|o| o + start));
            srcs.extend(s);
        });
        if !hit.is_empty()
            && let Some(k) = keep
        {
            let ks: Vec<bool> = hit.iter().map(|&o| k(o, b)).collect();
            hit = hit.iter().zip(&ks).filter(|x| *x.1).map(|x| *x.0).collect();
            srcs = srcs.into_iter().zip(&ks).filter(|x| *x.1).map(|x| x.0).collect();
        }
        if !hit.is_empty() {
            let mut count: Vec<(String, usize)> = vec![];
            for s in srcs {
                match count.iter_mut().find(|(n, _)| *n == s) {
                    Some(e) => e.1 += 1,
                    None => count.push((s, 1)),
                }
            }
            let mut top = count;
            top.sort_by(|a, b| b.1.cmp(&a.1));
            let n = hit.len();
            let more = if top.len() > 1 { format!(" and {} more", top.len() - 1) } else { String::new() };
            rep.refuse(
                place,
                format!(
                    "{what} game data: {n} run{} of {b} bytes or more, first at offset 0x{:X}, from {}{more}",
                    if n > 1 { "s" } else { "" },
                    hit[0],
                    top[0].0
                ),
                asset(&top[0].0),
            );
            return true;
        }
    }
    false
}

/// Python's buf[lo:hi] with negative positions from the end
fn pyslice(b: &[u8], lo: i64, hi: i64) -> &[u8] {
    let n = b.len() as i64;
    let f = |x: i64| if x < 0 { (x + n).max(0) } else { x.min(n) } as usize;
    let (l, h) = (f(lo), f(hi));
    if h <= l { &b[0..0] } else { &b[l..h] }
}

fn int_of(v: Option<&Value>, d: i64) -> Result<i64, String> {
    match v {
        None => Ok(d),
        Some(Value::Number(n)) if n.is_i64() => Ok(n.as_i64().unwrap()),
        Some(x) => Err(format!("unsupported value {x}")),
    }
}

/// gltf_positions: every POSITION accessor's f32 x 3 of a .gltf / .glb
pub fn gltf_positions(path: &Path, data: &[u8], folder: &Path) -> Result<Vec<[f32; 3]>, String> {
    let mut bins: BTreeMap<i64, Vec<u8>> = BTreeMap::new();
    let gl: Value = if manifest::py_suffix(&path.to_string_lossy()).to_lowercase() == ".glb" {
        if data.len() < 20 || &data[..4] != b"glTF" {
            return Err("not a GLB file".into());
        }
        let n = u32::from_le_bytes(data[12..16].try_into().unwrap()) as usize;
        let j = pyslice(data, 20, 20 + n as i64);
        let gl = crate::jsonc::parse(&String::from_utf8_lossy(j)).map_err(|e| e.msg)?;
        if data.len() > 28 + n {
            let m = u32::from_le_bytes(data[20 + n..24 + n].try_into().unwrap()) as usize;
            bins.insert(0, pyslice(data, 28 + n as i64, (28 + n + m) as i64).to_vec());
        }
        gl
    } else {
        let s = std::str::from_utf8(data).map_err(|e| format!("'utf-8' codec can't decode: {e}"))?;
        crate::jsonc::parse(s).map_err(|e| e.msg)?
    };
    let empty = vec![];
    let arr = |v: &Value, k: &str| -> Vec<Value> { v.get(k).and_then(Value::as_array).cloned().unwrap_or_default() };
    for (i, b) in arr(&gl, "buffers").iter().enumerate() {
        let Some(uri) = b.get("uri").filter(|u| !u.is_null()) else { continue };
        let uri = uri.as_str().ok_or("buffer uri isn't a string")?;
        if uri.starts_with("data:") {
            let (_, b64) = uri.split_once(',').ok_or("list index out of range")?;
            bins.insert(i as i64, blobs::b64decode(b64)?);
        } else {
            let p = path.parent().unwrap_or(Path::new(".")).join(uri);
            let ok = match (p.canonicalize(), folder.canonicalize()) {
                (Ok(p), Ok(f)) => p != f && p.starts_with(&f) && p.is_file(),
                _ => false,
            };
            if !ok {
                return Err(format!("buffer {} isn't a file of the mod", crate::py::str_repr(uri)));
            }
            bins.insert(i as i64, std::fs::read(&p).map_err(|e| e.to_string())?);
        }
    }
    let mut out = vec![];
    let accessors = arr(&gl, "accessors");
    let views = arr(&gl, "bufferViews");
    for mesh in arr(&gl, "meshes") {
        for pr in arr(&mesh, "primitives") {
            let Some(ai) = pr.get("attributes").and_then(|a| a.get("POSITION")).filter(|v| !v.is_null()) else { continue };
            let ai = ai.as_u64().ok_or("list indices must be integers")? as usize;
            let a = accessors.get(ai).ok_or("list index out of range")?;
            if a.get("componentType") != Some(&json!(5126)) || a.get("type") != Some(&json!("VEC3")) || a.get("bufferView").is_none() {
                continue;
            }
            let bvi = a["bufferView"].as_u64().ok_or("list indices must be integers")? as usize;
            let bv = views.get(bvi).ok_or("list index out of range")?;
            let bi = int_of(bv.get("buffer"), 0)?;
            let buf = bins.get(&bi).unwrap_or(&empty);
            let o = int_of(bv.get("byteOffset"), 0)?.checked_add(int_of(a.get("byteOffset"), 0)?);
            let stride = int_of(bv.get("byteStride"), 12)?;
            let count = int_of(a.get("count"), -1).map_err(|_| "'count'".to_string())?;
            if a.get("count").is_none() {
                return Err("'count'".into());
            }
            // the accessor must lie inside its buffer: a byteStride of 0 (tight) once repeated
            // one 12-byte slice `count` times (gigabytes from a tiny file). Python's xdl_mod
            // skipped a short accessor; this refuses it.
            let stride = if stride == 0 { 12 } else { stride };
            let bad = |why: &str| format!("accessor {ai}: {why}");
            let o = o.filter(|&o| o >= 0).ok_or_else(|| bad("negative or huge byteOffset"))?;
            if count < 0 {
                return Err(bad("negative count"));
            }
            if stride < 12 {
                return Err(bad(&format!("byteStride {stride} is under 12")));
            }
            let span = if count == 0 { Some(0) } else { (count - 1).checked_mul(stride).and_then(|s| s.checked_add(12)) };
            match span.and_then(|s| s.checked_add(o)) {
                Some(end) if end <= buf.len() as i64 => {}
                _ => return Err(bad(&format!("{count} positions at offset {o}, stride {stride} run past its {}-byte buffer", buf.len()))),
            }
            for k in 0..count {
                let c = &buf[(o + k * stride) as usize..][..12];
                let f = |i: usize| f32::from_le_bytes(c[i..i + 4].try_into().unwrap());
                out.push([f(0), f(4), f(8)]);
            }
        }
    }
    Ok(out)
}

/// check_audio: a decoded sound file's samples (each channel, and their mix), then whether it
/// sounds like a base sound.
fn check_audio(index: &Index, rep: &mut Report, rel: &str, path: &Path, shown: &str) -> Result<(), String> {
    let pcm = audio::decode_audio(path, shown)?;
    let mut chans: Vec<Vec<i16>> = (0..pcm.channels).map(|c| pcm.channel(c)).collect();
    if chans.len() > 1 {
        chans.push(pcm.mono());
    }
    let wav = manifest::py_suffix(&path.to_string_lossy()).to_lowercase() == ".wav";
    let nc = chans.len();
    for (ci, ch) in chans.iter().enumerate() {
        let raw: Vec<u8> = ch.iter().flat_map(|v| v.to_le_bytes()).collect();
        if let Some(src) = index.one("snd", h64(&raw)) {
            let a = asset(&src);
            rep.refuse(rel, format!("is the game's sound ({src})"), a);
            return Ok(());
        }
        if !wav || nc > 1 {
            let what = if nc == 1 {
                "the decoded samples".to_string()
            } else if ci < nc - 1 {
                format!("decoded channel {ci}")
            } else {
                "the channels' mix".to_string()
            };
            // _loud_windows: a run counts only if a sample in it is louder than QUIET
            let mut loud = vec![0usize; ch.len() + 1];
            for (i, &s) in ch.iter().enumerate() {
                loud[i + 1] = loud[i] + ((s as i32).abs() >= QUIET) as usize;
            }
            let keep = |off: usize, b: usize| {
                let a = off / 2;
                let e = ((off + b + 1) / 2).min(ch.len());
                loud[e] > loud[a]
            };
            if check_bytes(index, rep, &format!("{rel}: {what}"), &raw, "hold", Some(&keep)) {
                return Ok(());
            }
        }
    }
    if !index.has_sfp() {
        let note = "the index has no sound fingerprints (made by an older xdl_mod.py): sounds were checked by their exact samples only; run index again".to_string();
        if !rep.notes.contains(&note) {
            rep.notes.push(note);
        }
        return Ok(());
    }
    let (k, f) = audio::sound_fp(chans.last().unwrap(), pcm.rate);
    if let Some((n, of, src)) = index.sounds_like(&k, &f)
        && n >= SFP_MIN
        && n as f64 >= SFP_SHARE * of as f64
    {
        let a = asset(&src);
        rep.refuse(
            rel,
            format!("sounds like the game's sound ({src}; {n} of its {of} frames line up): a re-encoded or resampled game sound is still the game's"),
            a,
        );
    }
    Ok(())
}

/// canon: a field value's bytes (little-endian), or what it should be
fn canon(prim: &str, count: usize, value: &Value) -> Result<Vec<u8>, String> {
    let p = crate::schema::prim(prim).unwrap();
    let vals: Vec<Value> = if count > 0 {
        match value.as_array() {
            Some(a) if a.len() == count => a.clone(),
            _ => return Err(format!("needs an array of {count}")),
        }
    } else {
        vec![value.clone()]
    };
    let mut out = vec![];
    for v in &vals {
        let Value::Number(n) = v else { return Err("needs a number".into()) };
        let shown = crate::py::str_of(v);
        if p.float {
            let x = n.as_f64().unwrap();
            if p.size == 4 {
                let y = x as f32;
                if y.is_infinite() && !x.is_infinite() {
                    return Err(format!("{shown} doesn't fit {prim}"));
                }
                out.extend_from_slice(&y.to_le_bytes());
            } else {
                out.extend_from_slice(&x.to_le_bytes());
            }
            continue;
        }
        let i: i128 = if let Some(i) = n.as_i64() {
            i as i128
        } else if let Some(u) = n.as_u64() {
            u as i128
        } else {
            let x = n.as_f64().unwrap();
            if x.is_infinite() {
                return Err(format!("{shown} doesn't fit {prim}"));
            }
            if x != x.trunc() {
                return Err(format!("needs an integer ({prim})"));
            }
            x as i128
        };
        let (lo, hi): (i128, i128) = match (p.code, p.size) {
            ('B', _) => (0, 255),
            ('b', _) => (-128, 127),
            ('H', _) => (0, 65535),
            ('h', _) => (-32768, 32767),
            ('I', _) => (0, u32::MAX as i128),
            _ => (i32::MIN as i128, i32::MAX as i128),
        };
        if i < lo || i > hi {
            return Err(format!("{shown} doesn't fit {prim}"));
        }
        out.extend_from_slice(&(i as i64).to_le_bytes()[..p.size]);
    }
    Ok(out)
}

/// check_mod: -> (Manifest, Report, files {relative path: bytes} as the package gets them)
fn check_mod(folder: &Path, index: &Index, strip: bool, root: &Path) -> Result<(Manifest, Report, BTreeMap<String, Vec<u8>>), ModError> {
    let man = Manifest::load(folder)?;
    let mut rep = Report::default();
    for e in &man.errors {
        rep.refuse("mod.jsonc", e["mod.jsonc: ".len()..].to_string(), None);
    }
    let meta = &index.meta;
    let st = structs::Structs::load(root).map_err(ModError)?;
    let empty = Map::new();
    let textures = meta["textures"].as_object().unwrap_or(&empty);
    let objects = meta["objects"].as_object().unwrap_or(&empty);
    let mut have: Vec<String> =
        meta["releases"].as_array().map(|a| a.iter().filter_map(|x| x.as_str().map(String::from)).collect()).unwrap_or_default();
    have.sort();
    have.dedup();
    let rx = res();
    for (w, tid, _) in &man.recipes {
        if !textures.contains_key(tid) {
            rep.refuse(format!("mod.jsonc: {w}"), format!("no texture {tid} in your game data ({})", have.join(", ")), None);
        }
    }
    for kind in ["cars", "items", "tracks"] {
        if let Some(Value::Object(sec)) = man.doc.get(kind) {
            for (uid, e) in sec {
                if let Some(th) = e.get("thumbnail").and_then(Value::as_str)
                    && rx.tex.is_match(th)
                    && !textures.contains_key(th)
                {
                    rep.refuse(format!("mod.jsonc: {kind}.{uid}.thumbnail"), format!("no texture {th} in your game data"), None);
                }
            }
        }
    }
    // patches: fields that exist, values of their type, and none that restates the base's
    let mut stripped = 0;
    let mut doc = man.doc.clone();
    for (w, base_obj, entry, path, patch) in &man.patches {
        let Some(base_obj) = base_obj else { continue };
        let Some(structs) = objects.get(base_obj).and_then(Value::as_object) else {
            let head = base_obj.rsplit_once('/').map(|x| x.0).unwrap_or(base_obj);
            let mut near: Vec<&String> = objects.keys().filter(|k| k.starts_with(head)).collect();
            near.sort();
            near.truncate(6);
            let parts: Vec<&str> = base_obj.split('/').collect();
            let arch = format!("{}//{}", parts[0], parts.get(2).unwrap_or(&""));
            let mut why = format!("no object {base_obj} in your game data");
            if !near.is_empty() {
                why += &format!(
                    " (there: {})",
                    near.iter().map(|n| n.rsplit_once('/').map(|x| x.1).unwrap_or(n)).collect::<Vec<_>>().join(", ")
                );
            }
            if !objects.keys().any(|o| o.starts_with(&arch)) {
                why += "; is its archive in the index?";
            }
            rep.refuse(format!("mod.jsonc: {w}"), why, None);
            continue;
        };
        let sname = structs.values().next().and_then(Value::as_str).unwrap_or("").to_string();
        let fields = st.fields(&sname);
        let counts = st.counts(&sname);
        for (f, v) in patch {
            let wf = format!("mod.jsonc: {w}.{f}");
            let Some((_, fl)) = fields.iter().find(|(n, _)| n == f) else {
                rep.refuse(wf, format!("{sname} has no field {f}"), None);
                continue;
            };
            let Some(prim) = &fl.prim else {
                if counts.iter().any(|(c, _)| c == f) {
                    rep.refuse(wf, format!("{sname}.{f} counts an array's entries: the game would read past the array"), None);
                } else {
                    rep.refuse(wf, format!("{sname}.{f} is a link or a nested struct: a patch sets numbers"), None);
                }
                continue;
            };
            let raw = match canon(prim, fl.count, v) {
                Ok(r) => r,
                Err(e) => {
                    rep.refuse(wf, e, None);
                    continue;
                }
            };
            if !index.find("obj", &[index::field_key(base_obj, f, &raw)]).0.is_empty() {
                if strip {
                    stripped += 1;
                    let node = match entry {
                        Some((k, u)) => &mut doc[k][u]["patch"][path],
                        None => &mut doc["patches"][base_obj],
                    };
                    if let Some(o) = node.as_object_mut() {
                        o.shift_remove(f);
                    }
                } else {
                    rep.refuse(
                        wf,
                        format!(
                            "restates the game's own value of {base_obj} {f} (a copied record); leave out what the mod doesn't change, or pack with --strip-unchanged"
                        ),
                        None,
                    );
                }
            }
        }
    }
    if stripped > 0 {
        rep.notes.push(format!("{stripped} patch field(s) equal to the game's own left out of the package"));
    }
    // the files
    let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
    let geometry = meta["geometry"].as_bool().unwrap_or(false);
    let folder_shown = py_path(&folder.to_string_lossy());
    for (p, parts) in index::rglob(folder) {
        if p.is_dir() {
            continue;
        }
        let rel = parts.join("/");
        if parts.iter().any(|x| x.starts_with('.')) {
            rep.notes.push(format!("{rel}: hidden, left out"));
            continue;
        }
        if std::fs::symlink_metadata(&p).map(|m| m.file_type().is_symlink()).unwrap_or(false) {
            rep.refuse(&rel, "is a link; a mod holds its own files", None);
            continue;
        }
        let ext = manifest::py_suffix(&rel).to_lowercase();
        if !ALLOWED_EXT.contains(&ext.as_str()) {
            let what = if index::ARCHIVE_EXT.contains(&ext.as_str()) || ext == ".xdl" {
                "is a game archive or cache".to_string()
            } else {
                format!("a {} can't be checked", if ext.is_empty() { "file without extension" } else { &ext })
            };
            rep.refuse(&rel, format!("{what}; a mod holds {}", ALLOWED_EXT.join(" ")), None);
            continue;
        }
        let data = match std::fs::read(&p) {
            Ok(d) => d,
            Err(e) => return Err(ModError(format!("{}: {e}", p.display()))),
        };
        files.insert(rel.clone(), data.clone());
        if check_bytes(index, &mut rep, &rel, &data, "holds", None) {
            continue;
        }
        let shown = format!("{folder_shown}/{rel}");
        let r: Result<(), String> = (|| {
            let rep = &mut rep;
            if [".json", ".jsonc", ".gltf"].contains(&ext.as_str()) {
                let s = std::str::from_utf8(&data).map_err(|e| format!("'utf-8' codec can't decode byte: {e}"))?;
                let tree = if ext == ".gltf" { crate::jsonc::parse(s) } else { crate::jsonc::loads(s) }.map_err(|e| e.msg)?;
                let mut bl = vec![];
                blobs::json_blobs(&tree, "", &mut bl);
                for (jp, blob) in bl {
                    check_bytes(index, rep, &format!("{rel}: {jp}"), &blob, "holds", None);
                }
            } else if ext == ".txt" || ext == ".md" {
                for (what, blob) in blobs::text_blobs(&String::from_utf8_lossy(&data)) {
                    check_bytes(index, rep, &format!("{rel}: {what}"), &blob, "holds", None);
                }
            }
            if ext == ".png" {
                let img = image::read_png(&data).map_err(|e| e.replace("image file", &format!("image file '{shown}'")))?;
                let (a, b) = image::pixel_hashes(&img);
                if let Some(src) = index.one("px", a).or_else(|| index.one("rgb", b)) {
                    let u = format!("a recipe on \"{}\"", py_opt(&asset(&src)));
                    rep.refuse(&rel, format!("is the game's texture ({src})"), Some(u));
                } else if let Some((bits, src)) = index.near(image::dhash(&img))
                    && bits <= DH_NEAR
                {
                    let u = format!("a recipe on \"{}\"", py_opt(&asset(&src)));
                    rep.refuse(
                        &rel,
                        format!("looks like the game's texture ({src}; {bits} of 256 gradient bits differ): an edited game texture is still the game's"),
                        Some(u),
                    );
                }
                check_bytes(index, rep, &format!("{rel}: pixels"), &img.px, "the pixels hold", None);
            } else if AUDIO_EXT.contains(&ext.as_str()) {
                check_audio(index, rep, &rel, &p, &shown)?;
            }
            if ext == ".gltf" || ext == ".glb" {
                if !geometry {
                    rep.refuse(&rel, "the index was made without geometry: meshes can't be checked", None);
                    return Ok(());
                }
                let pos = gltf_positions(&p, &data, folder)?;
                if !pos.is_empty() {
                    let mut h: Vec<u64> = pos.iter().map(|x| geom::vertex_hash(*x)).collect();
                    h.sort_unstable();
                    h.dedup();
                    let (hit, srcs) = index.find("vtx", &h);
                    if hit.len() >= VTX_MIN && hit.len() as f64 >= VTX_SHARE * h.len() as f64 {
                        let mut count: Vec<(String, usize)> = vec![];
                        for s in srcs {
                            match count.iter_mut().find(|(n, _)| *n == s) {
                                Some(e) => e.1 += 1,
                                None => count.push((s, 1)),
                            }
                        }
                        let mut top = count[0].clone();
                        for c in &count {
                            if c.1 > top.1 {
                                top = c.clone();
                            }
                        }
                        rep.refuse(
                            &rel,
                            format!("{} of its {} vertex positions are the game's (most from {})", hit.len(), h.len(), top.0),
                            asset(&top.0),
                        );
                    }
                }
            }
            Ok(())
        })();
        if let Err(e) = r {
            rep.refuse(&rel, format!("can't be read to be checked: {e}"), None);
        }
    }
    if stripped > 0 {
        files.insert("mod.jsonc".into(), (crate::py::dumps_indent(&doc, 2, false) + "\n").into_bytes());
    }
    Ok((man, rep, files))
}

/// fingerprints: {base id: {release: hash}} of the textures the mod refers to
fn fingerprints(man: &Manifest, index: &Index, root: &Path) -> Value {
    let rx = res();
    let mut ids: Vec<String> = man.recipes.iter().map(|r| r.1.clone()).collect();
    ids.extend(man.refs.iter().filter(|r| rx.tex.is_match(r)).cloned());
    ids.sort();
    ids.dedup();
    let mut table: BTreeMap<String, Map<String, Value>> = BTreeMap::new();
    if let Some(dir) = crate::pairing_dir(root) {
        let archs: std::collections::BTreeSet<String> =
            ids.iter().filter_map(|t| rx.tex.captures(t).map(|c| c[1].to_lowercase())).collect();
        for arch in archs {
            let Ok(rd) = std::fs::read_dir(&dir) else { continue };
            for e in rd.flatten() {
                let p = e.path();
                if !p.to_string_lossy().ends_with(".jsonl") || media::stem(&p).to_lowercase() != arch {
                    continue;
                }
                for ln in std::fs::read_to_string(&p).unwrap_or_default().lines() {
                    let Ok(r) = serde_json::from_str::<Value>(ln) else { continue };
                    let Some(id) = r.get("id").and_then(Value::as_str) else { continue };
                    if ids.iter().any(|i| i == id) {
                        let mut m = Map::new();
                        for rel in manifest::RELEASES {
                            if let Some(x) = r.get(rel).and_then(|v| v.get("x")) {
                                m.insert(rel.to_string(), x.clone());
                            }
                        }
                        table.insert(id.to_string(), m);
                    }
                }
            }
        }
    }
    let mut out = Map::new();
    for t in &ids {
        let mut fp = table.get(t).cloned().unwrap_or_default();
        if let Some(v) = index.meta["textures"].get(t).and_then(Value::as_object) {
            for (rel, x) in v {
                fp.insert(rel.clone(), x["x"].clone());
            }
        }
        if !fp.is_empty() {
            out.insert(t.clone(), Value::Object(fp));
        }
    }
    Value::Object(out)
}

/// check / pack options
#[derive(Default, Clone)]
pub struct Options {
    pub strip_unchanged: bool,
    pub allow_partial_index: bool,
    /// the HWVX-SDL checkout (schemas/, and docs/ above it for the pairing table); default:
    /// found above the mod, else above this crate
    pub root: Option<PathBuf>,
}

/// What check / pack decided, and what the Python prints for it.
#[derive(Debug, Clone)]
pub struct Outcome {
    /// 0 passed (and packed), 1 refused, 2 an error (stderr says which)
    pub code: i32,
    pub stdout: String,
    pub stderr: String,
    pub refused: Vec<Refusal>,
    pub notes: Vec<String>,
    /// the package written (pack, when it passed)
    pub packed: Option<PathBuf>,
}

impl Outcome {
    fn error(e: ModError) -> Outcome {
        Outcome { code: 2, stdout: String::new(), stderr: format!("error: {e}\n"), refused: vec![], notes: vec![], packed: None }
    }
}

/// `xdl_mod.py check MOD --index INDEX` (out None) or `xdl_mod.py pack MOD -o OUT --index INDEX`
/// (out Some): the no-game-data check, and the package when it passes.
pub fn check_and_pack(mod_dir: &Path, index: &Path, out: Option<&Path>, opt: &Options) -> Outcome {
    match run_check(mod_dir, index, out, opt) {
        Ok(o) => o,
        Err(e) => Outcome::error(e),
    }
}

fn run_check(mod_dir: &Path, index_path: &Path, out: Option<&Path>, opt: &Options) -> Result<Outcome, ModError> {
    let pack = out.is_some();
    let root = opt
        .root
        .clone()
        .or_else(|| crate::repo_root(Some(mod_dir)))
        .or_else(|| crate::repo_root(None))
        .ok_or_else(|| ModError("no schemas/ found".into()))?;
    let index = Index::load(index_path)?;
    let (man, mut rep, files) = check_mod(mod_dir, &index, opt.strip_unchanged, &root)?;
    let partial = index.meta["partial"].as_bool().unwrap_or(false);
    if partial {
        let msg = "the index doesn't cover the whole game (made with --archives or --no-geometry)";
        if pack && !opt.allow_partial_index {
            rep.refuse("the index", format!("{msg}; make a whole one, or pass --allow-partial-index (recorded)"), None);
        } else {
            rep.notes.push(msg.into());
        }
    }
    let irel: Vec<String> =
        index.meta["releases"].as_array().map(|a| a.iter().filter_map(|x| x.as_str().map(String::from)).collect()).unwrap_or_default();
    let mut lacking: Vec<String> = vec![];
    if man.doc.is_object() && man.doc.get("releases").is_some()
        && let Some(r) = &man.releases
    {
        for x in r {
            let s = x.as_str().unwrap_or("").to_string();
            if !irel.contains(&s) && !lacking.contains(&s) {
                lacking.push(s);
            }
        }
        lacking.sort();
    }
    if !lacking.is_empty() {
        let msg = format!("the mod is for {}, which the index has no data of: what it refers to there isn't checked", lacking.join(", "));
        if pack && !opt.allow_partial_index {
            rep.refuse("mod.jsonc: releases", format!("{msg}; index that release's data too, or pass --allow-partial-index (recorded)"), None);
        } else if !pack {
            rep.refuse("mod.jsonc: releases", format!("{msg}; index that release's data too"), None);
        } else {
            rep.notes.push(msg);
        }
    }
    if files.contains_key("check.json") {
        rep.refuse("check.json", "is written by pack; a mod's sources don't have one", None);
    }
    let shown = mod_dir.to_string_lossy().to_string();
    let mut stdout = rep.show();
    let done = |code, stdout, packed| Outcome { code, stdout, stderr: String::new(), refused: rep.refused.clone(), notes: rep.notes.clone(), packed };
    if !rep.refused.is_empty() {
        stdout += &if pack {
            format!("{shown}: {} problem(s): not packed\n", rep.refused.len())
        } else {
            format!("{shown}: {} problem(s)\n", rep.refused.len())
        };
        return Ok(done(1, stdout, None));
    }
    let Some(out) = out else {
        stdout += &format!("{shown}: {} files, no game data found\n", files.len());
        return Ok(done(0, stdout, None));
    };
    let mut fl = Map::new();
    for (n, d) in &files {
        fl.insert(n.clone(), json!({"size": d.len(), "sha256": sha256_hex(d)}));
    }
    let check = json!({
        "tool": TOOL, "tool_version": TOOL_VERSION, "format_version": FORMAT_VERSION,
        "mod": man.id, "result": "no game data found",
        "index": {"id": index.meta["id"], "releases": index.meta["releases"],
                  "files": index.meta["files"].as_array().map(|a| a.len()).unwrap_or(0), "partial": partial},
        "unindexed_releases": lacking,
        "files": fl,
        "fingerprints": fingerprints(&man, &index, &root),
    });
    let check_text = crate::py::dumps_indent(&check, 2, true) + "\n";
    let mut entries: Vec<(String, &[u8])> = vec![("check.json".into(), check_text.as_bytes())];
    for (n, d) in &files {
        if n != "check.json" {
            entries.push((n.clone(), d.as_slice()));
        }
    }
    let z = zip::write_stored(&entries).map_err(|e| ModError(format!("{}: {e}", py_path(&out.to_string_lossy()))))?;
    if let Some(p) = out.parent().filter(|p| !p.as_os_str().is_empty()) {
        std::fs::create_dir_all(p).map_err(|e| ModError(e.to_string()))?;
    }
    std::fs::write(out, &z).map_err(|e| ModError(format!("{}: {e}", out.display())))?;
    stdout += &format!(
        "{}: {} ({}), {} files, {} bytes, no game data found\n",
        py_path(&out.to_string_lossy()),
        py_opt(&man.name),
        py_opt(&man.id),
        files.len(),
        z.len()
    );
    Ok(done(0, stdout, Some(out.to_path_buf())))
}
