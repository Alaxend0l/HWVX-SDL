//! mod.jsonc, checked as xdl_mod.Manifest checks it (the same members, the same messages, in
//! the same order).

use super::{ModError, py_path, re_py};
use crate::py;
use regex::Regex;
use serde_json::Value;
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

pub const FORMAT_VERSION: i64 = 1;
pub const RELEASES: [&str; 3] = ["gc", "pc", "ps2"];
pub const AUDIO_EXT: [&str; 4] = [".wav", ".flac", ".ogg", ".mp3"];

pub struct Res {
    pub uid: Regex,
    pub snd: Regex,
    pub tex: Regex,
    pub obj: Regex,
    pub car: Regex,
    pub item: Regex,
    pub track: Regex,
    pub path: Regex,
    pub texkey: Regex,
}

pub fn res() -> &'static Res {
    static R: OnceLock<Res> = OnceLock::new();
    R.get_or_init(|| Res {
        uid: re_py(r"^uid://[0-9a-z]{1,13}$"),
        snd: re_py(r"^base://([A-Za-z0-9_]+)/([a-z_]+_[0-9]+)/sound/((?:[a-z0-9]+:)?[0-9]+)$"),
        tex: re_py(r"^base://([A-Za-z0-9_]+)/([a-z_]+_[0-9]+)/texture/((?:[a-z0-9]+:)?[0-9]+)$"),
        obj: re_py(r"^base://([A-Za-z0-9_]+)/([a-z_]+_[0-9]+)/([A-Za-z0-9_]+/[0-9]+(?:/[A-Za-z0-9_]+)*)$"),
        car: re_py(r"^base://HW_Cars/car_([0-9]+)$"),
        item: re_py(r"^base://mcp/item/([0-9]+)$"),
        track: re_py(r"^base://mcp/track/([0-9]+)$"),
        path: re_py(r"^[A-Za-z0-9_]+/[0-9]+(/[A-Za-z0-9_]+)*$"),
        texkey: re_py(r"^texture/[0-9]+$"),
    })
}

/// (where, base object id, entry (kind, uid), object path, the patch)
pub type Patch = (String, Option<String>, Option<(String, String)>, String, serde_json::Map<String, Value>);

pub struct Manifest {
    pub dir: PathBuf,
    pub doc: Value,
    pub errors: Vec<String>,
    pub patches: Vec<Patch>,
    pub recipes: Vec<(String, String, Value)>,
    pub sounds: Vec<(String, String, String)>,
    pub refs: BTreeSet<String>,
    pub id: Option<String>,
    pub name: Option<String>,
    pub releases: Option<Vec<Value>>,
}

/// PurePosixPath(s).parts, .suffix
pub fn py_parts(s: &str) -> Vec<String> {
    let mut v = vec![];
    if s.starts_with('/') {
        v.push("/".to_string());
    }
    v.extend(s.split('/').filter(|p| !p.is_empty() && *p != ".").map(String::from));
    v
}

pub fn py_suffix(s: &str) -> String {
    let parts = py_parts(s);
    let name = parts.last().filter(|p| *p != "/").cloned().unwrap_or_default();
    match name.rfind('.') {
        Some(i) if i > 0 && i < name.len() - 1 => name[i..].to_string(),
        _ => String::new(),
    }
}

fn is_num(v: &Value) -> bool {
    v.is_number()
}

fn is_int(v: &Value) -> bool {
    matches!(v, Value::Number(n) if n.is_i64() || n.is_u64())
}

/// Python's `x in (strings)`
fn in_strs(v: &Value, set: &[&str]) -> bool {
    v.as_str().is_some_and(|s| set.contains(&s))
}

impl Manifest {
    pub fn load(folder: &Path) -> Result<Manifest, ModError> {
        let p = folder.join("mod.jsonc");
        let shown = py_path(&folder.to_string_lossy());
        if !p.is_file() {
            return Err(ModError(format!("{shown}: no mod.jsonc")));
        }
        let ps = py_path(&p.to_string_lossy());
        let bytes = std::fs::read(&p).map_err(|e| ModError(format!("{ps}: {e}")))?;
        let text = String::from_utf8(bytes).map_err(|e| ModError(format!("{ps}: 'utf-8' codec can't decode bytes: {e}")))?;
        let doc = crate::jsonc::loads(&text).map_err(|e| ModError(format!("{ps}: {}", e.msg)))?;
        let mut m = Manifest {
            dir: folder.to_path_buf(),
            doc,
            errors: vec![],
            patches: vec![],
            recipes: vec![],
            sounds: vec![],
            refs: BTreeSet::new(),
            id: None,
            name: None,
            releases: None,
        };
        m.check();
        Ok(m)
    }

    fn err(&mut self, w: &str, msg: &str) {
        self.errors.push(format!("mod.jsonc: {w}: {msg}"));
    }

    fn get_str(&mut self, d: &Value, k: &str, w: &str, required: bool, pat: Option<&Regex>) -> Option<String> {
        let v = d.get(k).filter(|v| !v.is_null());
        let Some(v) = v else {
            if required {
                self.err(w, &format!("\"{k}\" is missing"));
            }
            return None;
        };
        match v.as_str() {
            Some(s) if pat.is_none_or(|p| p.is_match(s)) => Some(s.to_string()),
            Some(s) => {
                self.err(&format!("{w}.{k}"), &format!("{} isn't the form expected", py::str_repr(s)));
                None
            }
            None => {
                self.err(&format!("{w}.{k}"), "isn't a string");
                None
            }
        }
    }

    fn ops(&mut self, ops: &Value, w: &str) {
        let Some(list) = ops.as_array().filter(|a| !a.is_empty()) else {
            return self.err(w, "needs a list of operations");
        };
        let opsets: [(&str, &str); 5] =
            [("hue_shift", "degrees"), ("tint", "color"), ("saturation", "amount"), ("brightness", "amount"), ("matrix", "rows")];
        for (i, op) in list.iter().enumerate() {
            let wi = format!("{w}[{i}]");
            let kind = op.as_object().and_then(|o| o.get("op")).and_then(Value::as_str);
            let Some((name, arg)) = kind.and_then(|k| opsets.iter().find(|(n, _)| *n == k)).copied() else {
                self.err(&wi, "\"op\" is one of brightness, hue_shift, matrix, saturation, tint");
                continue;
            };
            let o = op.as_object().unwrap();
            let others: Vec<&String> = o.keys().filter(|k| *k != "op").collect();
            if others.len() != 1 || others[0] != arg {
                self.err(&wi, &format!("{name} takes {arg}"));
                continue;
            }
            let v = &o[arg];
            let ok = match name {
                "tint" => v.as_array().is_some_and(|a| a.len() == 3 && a.iter().all(is_num)),
                "matrix" => v.as_array().is_some_and(|a| {
                    a.len() == 3 && a.iter().all(|r| r.as_array().is_some_and(|r| r.len() == 4 && r.iter().all(is_num)))
                }),
                _ => is_num(v),
            };
            if !ok {
                self.err(&wi, "numbers of the wrong shape");
            }
        }
    }

    fn patch(&mut self, patch: &Value, w: &str, base_obj: Option<String>, entry: Option<(String, String)>, path: &str) {
        match patch.as_object().filter(|o| !o.is_empty()) {
            None => self.err(w, "needs an object of field: value"),
            Some(o) => self.patches.push((w.to_string(), base_obj, entry, path.to_string(), o.clone())),
        }
    }

    /// `x.items() if isinstance(x, dict) else []` over d.get(k) (or {} when missing or null)
    fn items(d: &Value, k: &str) -> Vec<(String, Value)> {
        match d.get(k) {
            Some(Value::Object(o)) => o.iter().map(|(a, b)| (a.clone(), b.clone())).collect(),
            _ => vec![],
        }
    }

    fn not_dict(d: &Value, k: &str) -> bool {
        d.get(k).is_some_and(|v| !v.is_object())
    }

    fn audio_path_bad(f: &str) -> bool {
        !AUDIO_EXT.contains(&py_suffix(f).to_lowercase().as_str())
            || py_parts(f).iter().any(|p| p == "..")
            || f.starts_with('/')
            || f.contains('\\')
    }

    fn check(&mut self) {
        let d = self.doc.clone();
        let Some(obj) = d.as_object() else {
            return self.err("the file", "isn't an object");
        };
        let rx = res();
        let known = [
            "$schema", "format_version", "id", "name", "version", "author", "description", "releases", "after", "cars", "items",
            "tracks", "patches", "recipes", "sounds", "music",
        ];
        for k in obj.keys() {
            if !known.contains(&k.as_str()) {
                self.err(k, "isn't a member of a mod (schemas/Mod.schema.json)");
            }
        }
        if !d.get("format_version").is_some_and(|v| py::eq(v, &Value::from(FORMAT_VERSION))) {
            self.err("format_version", &format!("must be {FORMAT_VERSION}"));
        }
        self.id = self.get_str(&d, "id", "the mod", true, Some(&rx.uid));
        self.name = self.get_str(&d, "name", "the mod", true, None);
        self.get_str(&d, "version", "the mod", true, None);
        self.get_str(&d, "author", "the mod", false, None);
        self.get_str(&d, "description", "the mod", false, None);
        let all: Vec<Value> = RELEASES.iter().map(|r| Value::from(*r)).collect();
        let rel = d.get("releases").cloned().unwrap_or(Value::Array(all.clone()));
        let rel = match rel.as_array() {
            Some(a) if !a.is_empty() && a.iter().all(|r| in_strs(r, &RELEASES)) => a.clone(),
            _ => {
                self.err("releases", "a list of gc, pc, ps2");
                all
            }
        };
        self.releases = Some(rel);
        let after = d.get("after").cloned().unwrap_or(Value::Array(vec![]));
        if !after.as_array().is_some_and(|a| a.iter().all(|x| x.as_str().is_some_and(|s| rx.uid.is_match(s)))) {
            self.err("after", "a list of mod ids (uid://...)");
        }
        let mut uids: Vec<Option<String>> = vec![self.id.clone()];
        let kinds = [("cars", &rx.car, "HW_Cars", "car_", "base://HW_Cars/car_<n>"), ("items", &rx.item, "HW_Items", "item_", "base://mcp/item/<n>")];
        for (kind, regex, archive, pack, form) in kinds {
            let sec = d.get(kind).cloned().unwrap_or(Value::Object(Default::default()));
            let Some(sec) = sec.as_object() else {
                self.err(kind, "isn't an object");
                continue;
            };
            for (uid, e) in sec {
                let w = format!("{kind}.{uid}");
                if !rx.uid.is_match(uid) {
                    self.err(&w, "the key is the entry's id: uid:// and up to 13 of 0-9 a-z");
                }
                if uids.contains(&Some(uid.clone())) {
                    self.err(&w, "the id is used twice");
                }
                uids.push(Some(uid.clone()));
                let Some(eo) = e.as_object() else {
                    self.err(&w, "isn't an object");
                    continue;
                };
                for k in eo.keys() {
                    if !["extends", "name", "thumbnail", "unlocked", "patch", "recipes"].contains(&k.as_str()) {
                        self.err(&format!("{w}.{k}"), "isn't a member of an entry");
                    }
                }
                let ext = self.get_str(e, "extends", &w, true, Some(regex));
                if eo.contains_key("extends") && ext.is_none() {
                    self.err(
                        &format!("{w}.extends"),
                        &format!("an entry extends a base one, {form} (data of its own isn't loaded by the port yet)"),
                    );
                }
                self.get_str(e, "name", &w, false, None);
                if let Some(th) = self.get_str(e, "thumbnail", &w, false, Some(&rx.tex)) {
                    self.refs.insert(th);
                }
                if eo.get("unlocked").is_some_and(|u| !u.is_boolean()) {
                    self.err(&format!("{w}.unlocked"), "true or false");
                }
                let mut base_pack = None;
                if let Some(ext) = &ext {
                    self.refs.insert(ext.clone());
                    let n = regex.captures(ext).unwrap()[1].to_string();
                    base_pack = Some(format!("base://{archive}/{pack}{n}"));
                }
                for (path, patch) in Self::items(e, "patch") {
                    if !rx.path.is_match(&path) {
                        self.err(&format!("{w}.patch.{path}"), "an object of the entry's pack: CarAInfo/0/engine");
                        continue;
                    }
                    let bo = base_pack.as_ref().map(|b| format!("{b}/{path}"));
                    self.patch(&patch, &format!("{w}.patch.{path}"), bo, Some((kind.to_string(), uid.clone())), &path);
                }
                for (tex, ops) in Self::items(e, "recipes") {
                    if !rx.texkey.is_match(&tex) {
                        self.err(&format!("{w}.recipes.{tex}"), "a texture of the entry's pack: texture/3");
                        continue;
                    }
                    self.ops(&ops, &format!("{w}.recipes.{tex}"));
                    if let Some(b) = &base_pack {
                        self.recipes.push((format!("{w}.recipes.{tex}"), format!("{b}/{tex}"), ops.clone()));
                    }
                }
                for k in ["patch", "recipes"] {
                    if Self::not_dict(e, k) {
                        self.err(&format!("{w}.{k}"), "isn't an object");
                    }
                }
            }
        }
        // tracks
        let sec = match d.get("tracks").cloned().unwrap_or(Value::Object(Default::default())) {
            Value::Object(o) => o,
            _ => {
                self.err("tracks", "isn't an object");
                Default::default()
            }
        };
        for (uid, e) in &sec {
            let w = format!("tracks.{uid}");
            if !rx.uid.is_match(uid) {
                self.err(&w, "the key is the entry's id: uid:// and up to 13 of 0-9 a-z");
            }
            if uids.contains(&Some(uid.clone())) {
                self.err(&w, "the id is used twice");
            }
            uids.push(Some(uid.clone()));
            let Some(eo) = e.as_object() else {
                self.err(&w, "isn't an object");
                continue;
            };
            for k in eo.keys() {
                if k == "worldPack" {
                    self.err(
                        &format!("{w}.worldPack"),
                        "a world pack is game data: package the level file (\"level\", tools/xdl_level.py mod) instead",
                    );
                } else if !["extends", "name", "thumbnail", "unlocked", "world", "level"].contains(&k.as_str()) {
                    self.err(&format!("{w}.{k}"), "isn't a member of a track (extends, name, thumbnail, unlocked, world, level)");
                }
            }
            let lvl = self.get_str(e, "level", &w, false, None);
            if let Some(l) = lvl.filter(|l| !l.is_empty())
                && (l.starts_with('/') || l.starts_with('\\') || l.replace('\\', "/").split('/').any(|p| p == "..") || !l.ends_with(".json"))
            {
                self.err(&format!("{w}.level"), "the level file (tools/xdl_level.py compile): a .json path inside the mod");
            }
            if let Some(ext) = self.get_str(e, "extends", &w, true, Some(&rx.track)) {
                self.refs.insert(ext);
            }
            self.get_str(e, "name", &w, false, None);
            self.get_str(e, "world", &w, false, None);
            if let Some(th) = self.get_str(e, "thumbnail", &w, false, Some(&rx.tex)) {
                self.refs.insert(th);
            }
            if eo.get("unlocked").is_some_and(|u| !u.is_boolean()) {
                self.err(&format!("{w}.unlocked"), "true or false");
            }
        }
        for (target, patch) in Self::items(&d, "patches") {
            let m = rx.obj.captures(&target).map(|c| c[3].to_string());
            let Some(path) = m.filter(|_| !rx.tex.is_match(&target)) else {
                self.err(&format!("patches.{target}"), "an object's id: base://HW_Cars/car_5/CarAInfo/0/engine");
                continue;
            };
            self.refs.insert(target.clone());
            self.patch(&patch, &format!("patches.{target}"), Some(target.clone()), None, &path);
        }
        for (target, ops) in Self::items(&d, "recipes") {
            if !rx.tex.is_match(&target) {
                self.err(&format!("recipes.{target}"), "a texture's id: base://HW_Cars/car_5/texture/3");
                continue;
            }
            self.refs.insert(target.clone());
            self.ops(&ops, &format!("recipes.{target}"));
            self.recipes.push((format!("recipes.{target}"), target.clone(), ops.clone()));
        }
        let exts = AUDIO_EXT.join(" ");
        for (target, v) in Self::items(&d, "sounds") {
            let w = format!("sounds.{target}");
            if !rx.snd.is_match(&target) {
                self.err(&w, "a sound's id: base://HW_Cars/car_5/sound/3");
                continue;
            }
            let f = match &v {
                Value::String(s) => Some(s.clone()),
                Value::Object(o) => o.get("file").and_then(Value::as_str).map(String::from),
                _ => None,
            };
            if let Some(o) = v.as_object() {
                for k in o.keys() {
                    if k != "file" && k != "loop" {
                        self.err(&format!("{w}.{k}"), "isn't a member of a sound (file, loop)");
                    }
                }
                if let Some(lp) = o.get("loop")
                    && !lp.is_null()
                    && (!is_int(lp) || lp.as_i64().is_some_and(|x| x < 0))
                {
                    self.err(&format!("{w}.loop"), "a sample of the file, or null (plays once)");
                }
            }
            let Some(f) = f.filter(|f| !Self::audio_path_bad(f)) else {
                self.err(&w, &format!("a sound file of the mod ({exts}), or {{\"file\", \"loop\"}}"));
                continue;
            };
            if !self.dir.join(&f).is_file() {
                self.err(&w, &format!("{f} isn't in the mod"));
                continue;
            }
            self.refs.insert(target.clone());
            self.sounds.push((w, target.clone(), f));
        }
        for (target, v) in Self::items(&d, "music") {
            let list = v.as_array().cloned();
            let es = list.clone().unwrap_or_else(|| vec![v.clone()]);
            for (i, e) in es.iter().enumerate() {
                let w = format!("music.{target}") + &(if list.is_some() { format!("[{i}]") } else { String::new() });
                let f = match e {
                    Value::String(s) => Some(s.clone()),
                    Value::Object(o) => o.get("file").and_then(Value::as_str).map(String::from),
                    _ => None,
                };
                if let Some(o) = e.as_object() {
                    for k in o.keys() {
                        if !["file", "intro", "loopStart", "loopEnd", "loop", "volume"].contains(&k.as_str()) {
                            self.err(&format!("{w}.{k}"), "isn't a member of a song (file, intro, loopStart, loopEnd, loop, volume)");
                        }
                    }
                }
                match f.filter(|f| !Self::audio_path_bad(f)) {
                    None => self.err(&w, &format!("a music file of the mod ({exts}), or {{\"file\", …}}")),
                    Some(f) if !self.dir.join(&f).is_file() => self.err(&w, &format!("{f} isn't in the mod")),
                    _ => {}
                }
                let intro = e.as_object().and_then(|o| o.get("intro")).filter(|x| !x.is_null());
                let Some(intro) = intro else { continue };
                match intro.as_str().filter(|s| !Self::audio_path_bad(s)) {
                    None => self.err(&format!("{w}.intro"), &format!("a music file of the mod ({exts})")),
                    Some(s) if !self.dir.join(s).is_file() => self.err(&format!("{w}.intro"), &format!("{s} isn't in the mod")),
                    _ => {}
                }
            }
        }
        for k in ["patches", "recipes", "sounds", "music"] {
            if Self::not_dict(&d, k) {
                self.err(k, "isn't an object");
            }
        }
    }
}
