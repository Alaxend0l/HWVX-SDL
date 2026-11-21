//! A content project's new tracks (the Content tab's Tracks): a mod's `tracks` section
//! (schemas/Mod.schema.json; src/game/Port_Mods.c, Port_ContentData.c; docs/xdl-format §10.5.1).
//! Each is an entry of its base track's mode's world select: `extends` (a retail track of that
//! mode), a name, a picture (`thumbnail`, a link to one of the world select's own pictures),
//! `unlocked`, and optionally a level of its own: a level project compiled into the mod folder
//! (`level/<id>.json`, the project's sources beside it as `level/<id>-src`, as xdlrs::level's
//! make_mod lays them out) with the world it is made over (`world`).
//!
//! Which level project a track is made from is the editor's, not the mod's: a hidden file in
//! the mod folder ([`LINKS`]), left out of packages (the packer leaves out hidden files), so a
//! shared mod never carries the maker's folders.

use crate::content::{ModProject, new_uid};
use serde_json::{Map, Value, json};
use std::path::{Path, PathBuf};

pub const SECTION: &str = "tracks";
/// tracks' level projects: {"tracks": {uid: folder}} (relative to the mod folder when inside it)
pub const LINKS: &str = ".hwvx-editor-tracks.json";

/// The retail tracks by number, as the world selects name them (GameCube: the interface pack's
/// 57-entry list every select shares; "" where it has a blank). A number past them is a track a
/// mod adds.
pub const RETAIL: [&str; 57] = [
    "Monument City", "Turbine Sands", "Crankshaft Bay", "Burnout Glacier", "Underworld",
    "Mission 1", "Mission 2", "Mission 3", "Mission 4", "Mission 5", "Mission 6", "Mission 7", "Mission 8", "Mission 9", "Mission 10",
    "Mission 11", "Mission 12", "Mission 13", "Mission 14",
    "Downtown", "Turbine City", "Warehouse District", "Combustion Caldera", "Sewer Pipe Arena", "Crankshaft Docks", "Treadwell Plaza",
    "Underworld Arena", "", "",
    "Downtown Race", "Desert Race", "Ghost Town Race", "Bay Street Race", "Underworld Race", "Glacier Race", "", "", "", "",
    "Challenge 1", "Challenge 2", "Challenge 3", "Challenge 4", "Challenge 5", "Challenge 6", "Challenge 7", "Challenge 8", "Challenge 9",
    "Challenge 10", "Challenge 11", "Challenge 12", "Challenge 13", "Challenge 14", "Challenge 15", "Challenge 16", "Challenge 17",
    "Challenge 18",
];

/// A mode with a world select: its HWVX_START name, what the menus call it, its track family.
pub struct Mode {
    pub name: &'static str,
    pub label: &'static str,
    pub first: u32,
    pub end: u32,
}

/// The families (xdlrs::level::MODES, GameCube numbering), in the main menu's order.
pub const MODES: [Mode; 5] = [
    Mode { name: "mission", label: "Adventure", first: 5, end: 19 },
    Mode { name: "challenge", label: "Challenge", first: 39, end: 57 },
    Mode { name: "joyride", label: "Joyride", first: 0, end: 5 },
    Mode { name: "battle", label: "Battle", first: 19, end: 27 },
    Mode { name: "drag", label: "Drag race", first: 29, end: 35 },
];

/// The world select's picture of each retail track: its texture key (the menudump's pictures,
/// GameCube; 0: none). The pictures are textures of HW_IPack's interface pack 6, number = key - 3
/// (checked: a thumbnail texture/16 shows key 19, texture/103 key 106). The PS2's are the same.
/// A link's texture number is the GameCube's on every release (the game finds the same picture
/// in the PC's pack, Port_ContentData.c WorldPictureOn).
const PICTURE_KEYS: [u16; 57] = [
    21, 24, 27, 106, 31, // joyride
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, // missions
    21, 102, 103, 29, 31, 104, 105, 99, // battles
    0, 0, //
    21, 23, 24, 27, 99, 100, // drag races
    0, 0, 0, 0, //
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, // challenges
];
/// The PC's keys for the same pictures: its interface pack 6 has them in another order (the
/// menudump of every world select on the PC data; texture = key - 3 there too, checked against
/// the select's screen and the GameCube's pictures).
const PICTURE_KEYS_PC: [u16; 57] = [
    27, 30, 33, 109, 37, //
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, //
    27, 105, 106, 35, 37, 107, 108, 103, //
    0, 0, //
    27, 29, 30, 33, 103, 104, //
    0, 0, 0, 0, //
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
];

/// The texture of a release's interface pack 6 that shows world select picture `texture` (a
/// link's number, the GameCube's): the PC's own number for it; others as they are.
pub fn release_texture(release: &str, texture: u32) -> u32 {
    if release != "pc" {
        return texture;
    }
    PICTURE_KEYS
        .iter()
        .zip(PICTURE_KEYS_PC)
        .find(|&(&k, _)| k >= 3 && u32::from(k) - 3 == texture)
        .map_or(texture, |(_, pc)| u32::from(pc) - 3)
}
pub const PICTURE_ARCHIVE: &str = "HW_IPack";
pub const PICTURE_PACK_TYPE: u32 = 1; // interface
pub const PICTURE_PACK_ID: u32 = 6;

pub fn mode_of(n: u32) -> Option<&'static Mode> {
    MODES.iter().find(|m| n >= m.first && n < m.end)
}

pub fn mode_named(name: &str) -> Option<&'static Mode> {
    MODES.iter().find(|m| m.name == name || (name == "adventure" && m.name == "mission"))
}

/// "39  Challenge 1", or "57  (an added track)".
pub fn label(n: u32) -> String {
    match RETAIL.get(n as usize) {
        Some(name) if !name.is_empty() => format!("{n}  {name}"),
        Some(_) => format!("{n}  (blank)"),
        None => format!("{n}  (an added track)"),
    }
}

pub fn base(n: u32) -> String {
    format!("base://mcp/track/{n}")
}

pub fn base_number(extends: &str) -> Option<u32> {
    extends.strip_prefix("base://mcp/track/")?.parse().ok()
}

/// The thumbnail link of track n's world select picture.
pub fn picture_link(n: u32) -> Option<String> {
    let k = *PICTURE_KEYS.get(n as usize)?;
    (k >= 3).then(|| picture_link_of(u32::from(k) - 3))
}

pub fn picture_link_of(texture: u32) -> String {
    format!("base://{PICTURE_ARCHIVE}/interface_{PICTURE_PACK_ID}/texture/{texture}")
}

/// The texture number a world select picture link names (None: another link, or none).
pub fn picture_texture(link: &str) -> Option<u32> {
    link.strip_prefix(&format!("base://{PICTURE_ARCHIVE}/interface_{PICTURE_PACK_ID}/texture/"))?.parse().ok()
}

/// The world select pictures to choose from: (texture number, the retail tracks showing it).
pub fn pictures() -> Vec<(u32, Vec<u32>)> {
    let mut out: Vec<(u32, Vec<u32>)> = vec![];
    for n in 0..RETAIL.len() as u32 {
        let Some(t) = picture_link(n).and_then(|l| picture_texture(&l)) else { continue };
        match out.iter_mut().find(|(x, _)| *x == t) {
            Some((_, v)) => v.push(n),
            None => out.push((t, vec![n])),
        }
    }
    out
}

/// HWVX_START for a race on the track: race:mode=M,world=ID (the game splits at ',' and '=',
/// so the id must be a plain uid://).
pub fn start_string(uid: &str, base_track: u32) -> Option<String> {
    let rest = uid.strip_prefix("uid://")?;
    let ok = !rest.is_empty() && rest.len() <= 13 && rest.chars().all(|c| c.is_ascii_digit() || c.is_ascii_lowercase());
    let m = mode_of(base_track)?;
    ok.then(|| format!("race:mode={},world={uid}", m.name))
}

/// A track's level file in the mod folder, without .json: "level/<its id's letters>".
pub fn level_stem(uid: &str) -> String {
    let rest = uid.strip_prefix("uid://").unwrap_or(uid);
    let s: String = rest.chars().map(|c| if c.is_ascii_lowercase() || c.is_ascii_digit() { c } else { '-' }).collect();
    format!("level/{s}")
}

/// What a level project says it is: (mode, extends, world, name).
pub fn level_info(proj: &Path) -> Result<(String, String, String, String), String> {
    let lv = xdlrs::level::read_jsonc(&proj.join("level.jsonc")).map_err(|e| xdlrs::level::top_message(&e))?;
    let s = |k: &str| lv[k].as_str().map(str::to_string).ok_or_else(|| format!("{}: {k}: missing (a string)", proj.join("level.jsonc").display()));
    Ok((s("mode")?, s("extends")?, s("world")?, s("name")?))
}

/// A track's level compiled into the mod folder.
#[derive(Clone, Debug, PartialEq)]
pub struct TrackBuild {
    pub uid: String,
    /// "level/<id>.json"
    pub level: String,
    pub world: String,
    pub extends: String,
    /// nothing had changed: the last compile was kept
    pub cached: bool,
}

/// The level project `proj` compiled into `mod_dir` as track `uid`'s level (xdlrs::level, only
/// when a source changed), its sources copied beside it when the project is outside the mod
/// folder. Err: the compiler's errors.
pub fn build_level(env: &xdlrs::level::Env, mod_dir: &Path, uid: &str, proj: &Path, base: &xdlrs::level::BaseOpts) -> Result<TrackBuild, Vec<String>> {
    let one = |m: String| vec![m];
    let (mode, extends, _, _) = level_info(proj).map_err(one)?;
    let n = base_number(&extends).ok_or_else(|| one(format!("level.jsonc: extends: {extends}: not base://mcp/track/N")))?;
    if mode_of(n).map(|m| m.name) != mode_named(&mode).map(|m| m.name) {
        return Err(one(format!("level.jsonc: extends {extends} isn't a {mode} track")));
    }
    let stem = level_stem(uid);
    let out = mod_dir.join(format!("{stem}.json"));
    let c = xdlrs::level::compile(env, proj, Some(&out), base, false, None).map_err(|e| one(xdlrs::level::top_message(&e)))?;
    if !c.ok {
        return Err(if c.error_texts.is_empty() { one("the compile failed".into()) } else { c.error_texts });
    }
    let world = c.doc.as_ref().and_then(|d| d["world"].as_str()).map(str::to_string).ok_or_else(|| one("the compiled level has no world".into()))?;
    if !inside(proj, mod_dir) {
        copy_sources(proj, &mod_dir.join(format!("{stem}-src"))).map_err(one)?;
    }
    Ok(TrackBuild { uid: uid.to_string(), level: format!("{stem}.json"), world, extends, cached: c.cached })
}

/// Is `p` in `dir` (after resolving both)?
fn inside(p: &Path, dir: &Path) -> bool {
    let c = |x: &Path| std::fs::canonicalize(x).unwrap_or_else(|_| x.to_path_buf());
    c(p).starts_with(c(dir))
}

/// level.jsonc and triggers/ (as make_mod copies them), the folder made new.
fn copy_sources(proj: &Path, to: &Path) -> Result<(), String> {
    let e = |p: &Path, x: std::io::Error| format!("{}: {x}", p.display());
    if to.exists() {
        std::fs::remove_dir_all(to).map_err(|x| e(to, x))?;
    }
    std::fs::create_dir_all(to).map_err(|x| e(to, x))?;
    std::fs::copy(proj.join("level.jsonc"), to.join("level.jsonc")).map_err(|x| e(&proj.join("level.jsonc"), x))?;
    fn tree(a: &Path, b: &Path) -> std::io::Result<()> {
        std::fs::create_dir_all(b)?;
        for x in std::fs::read_dir(a)? {
            let x = x?;
            let t = b.join(x.file_name());
            if x.file_type()?.is_dir() {
                tree(&x.path(), &t)?;
            } else {
                std::fs::copy(x.path(), t)?;
            }
        }
        Ok(())
    }
    if proj.join("triggers").is_dir() {
        tree(&proj.join("triggers"), &to.join("triggers")).map_err(|x| e(&proj.join("triggers"), x))?;
    }
    Ok(())
}

/// A decoded picture: RGBA, w * h * 4.
pub struct Picture {
    pub texture: u32,
    pub w: usize,
    pub h: usize,
    pub rgba: Vec<u8>,
}

/// The world select's pictures, decoded from the player's own HW_IPack (its interface pack 6;
/// the archive found in `data` whatever the release's extension and layout: the PS2's is in
/// XPS/ENGLISH). Only what [`pictures`] lists, by their links' numbers: on the PC each is
/// decoded from the texture that shows it there ([`release_texture`]).
pub fn base_pictures(data: &Path) -> Result<Vec<Picture>, String> {
    use xdlrs::modcheck::media::{Archive, RawPack, decode_level, scan_textures, slice};
    let path = xdlrs::cars::find_archive(data, PICTURE_ARCHIVE)
        .filter(|p| p.is_file())
        .ok_or_else(|| format!("{}: no {PICTURE_ARCHIVE} archive", data.display()))?;
    let bytes = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    let a = Archive::from_bytes(bytes, &path)?;
    let e = a
        .entries
        .iter()
        .find(|e| e.typ == PICTURE_PACK_TYPE && e.id == PICTURE_PACK_ID)
        .ok_or_else(|| format!("{}: no interface pack {PICTURE_PACK_ID}", path.display()))?;
    let pk = RawPack::new(a.release, e.typ, a.pack_bytes(e), &e.name)?;
    let texs = scan_textures(&pk)?;
    let mut out = vec![];
    for (t, _) in pictures() {
        let Some(x) = texs.get(release_texture(a.release, t) as usize) else { continue };
        let Some(&(o, n, w, h)) = x.levels.first().filter(|l| l.2 > 0 && l.3 > 0) else { continue }; // chunks(0) panics
        let clut = x.clut.map(|o| slice(pk.b, o, (o + 0x400) as i64));
        let rgba = decode_level(a.release, x.fmt, slice(pk.b, o, (o + n) as i64), w, h, clut)?;
        // stored bottom row first (the select draws them flipped: checked against its screen,
        // GameCube and PC; the PS2's are stored as the GameCube's)
        let rgba: Vec<u8> = rgba.chunks(w * 4).rev().flatten().copied().collect();
        out.push(Picture { texture: t, w, h, rgba });
    }
    Ok(out)
}

fn read_links(dir: &Path) -> Map<String, Value> {
    std::fs::read_to_string(dir.join(LINKS))
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| v["tracks"].as_object().cloned())
        .unwrap_or_default()
}

fn write_links(dir: &Path, links: &Map<String, Value>) -> Result<(), String> {
    let p = dir.join(LINKS);
    if links.is_empty() {
        let _ = std::fs::remove_file(&p);
        return Ok(());
    }
    let v = json!({ "comment": "HWVX Editor: the level project each track of mod.jsonc is made from (this machine's; never packed)", "tracks": links });
    std::fs::write(&p, serde_json::to_string_pretty(&v).unwrap() + "\n").map_err(|e| format!("{}: {e}", p.display()))
}

/// A picture of the mod's own: at most this a side and this many bytes (the packer's and the
/// game's limits, xdlrs modcheck::manifest::THUMB_MAX_*).
pub const OWN_PICTURE_SIDE: u32 = xdlrs::modcheck::manifest::THUMB_MAX_SIDE;
pub const OWN_PICTURE_BYTES: u64 = xdlrs::modcheck::manifest::THUMB_MAX_BYTES;
/// Where the editor copies one: the mod folder's images/.
pub const IMAGES: &str = "images";

/// Is a thumbnail a picture of the mod's own (a .png path in it), not a base:// link?
pub fn is_own_picture(thumb: &str) -> bool {
    !thumb.contains("://") && thumb.to_ascii_lowercase().ends_with(".png")
}

/// A PNG read for a preview and the limits: RGBA, top row first.
pub fn read_picture(path: &Path) -> Result<Picture, String> {
    let meta = std::fs::metadata(path).map_err(|e| format!("{}: {e}", path.display()))?;
    if meta.len() > OWN_PICTURE_BYTES {
        return Err(format!("{}: {} bytes; a picture is at most {OWN_PICTURE_BYTES}", path.display(), meta.len()));
    }
    let data = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let img = xdlrs::modcheck::image::read_png(&data).map_err(|e| format!("{}: {e}", path.display()))?;
    if img.w as u32 > OWN_PICTURE_SIDE || img.h as u32 > OWN_PICTURE_SIDE {
        return Err(format!("{}: {} x {}; a picture is at most {OWN_PICTURE_SIDE} x {OWN_PICTURE_SIDE}", path.display(), img.w, img.h));
    }
    Ok(Picture { texture: 0, w: img.w, h: img.h, rgba: img.px })
}

impl ModProject {
    /// The track's picture of the mod's own, as a file (None: a base picture, or none).
    pub fn track_own_picture(&self, uid: &str) -> Option<PathBuf> {
        let t = self.doc[SECTION][uid]["thumbnail"].as_str().filter(|t| is_own_picture(t))?;
        Some(self.dir.join(t))
    }

    /// A PNG of the modder's made the track's picture: checked (a PNG within the limits),
    /// copied into the mod's images/ (named after the track and the file), the thumbnail set
    /// to it. Ok: its path in the mod.
    pub fn set_track_own_picture(&mut self, uid: &str, src: &Path) -> Result<String, String> {
        read_picture(src)?;
        let stem: String = src
            .file_stem()
            .map(|s| s.to_string_lossy().into_owned())
            .unwrap_or_default()
            .chars()
            .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c.to_ascii_lowercase() } else { '-' })
            .take(40)
            .collect();
        let stem = stem.trim_matches('-');
        let id = uid.trim_start_matches("uid://");
        let rel = if stem.is_empty() { format!("{IMAGES}/{id}.png") } else { format!("{IMAGES}/{id}-{stem}.png") };
        let dest = self.dir.join(&rel);
        std::fs::create_dir_all(self.dir.join(IMAGES)).map_err(|e| format!("{}: {e}", dest.display()))?;
        let same = std::fs::canonicalize(src).ok().is_some_and(|a| std::fs::canonicalize(&dest).ok() == Some(a));
        if !same {
            std::fs::copy(src, &dest).map_err(|e| format!("{}: {e}", dest.display()))?;
        }
        // the copy it replaces goes, when the editor made it for this track
        if let Some(old) = self.track_own_picture(uid).filter(|o| *o != dest) {
            if old.parent() == dest.parent() && old.file_name().is_some_and(|n| n.to_string_lossy().starts_with(id)) {
                let _ = std::fs::remove_file(old);
            }
        }
        self.set_track_str(uid, "thumbnail", Some(&rel));
        Ok(rel)
    }

    /// The mod's tracks: (uid, entry), in the file's order.
    pub fn tracks(&self) -> Vec<(String, Value)> {
        self.doc[SECTION].as_object().map(|m| m.iter().map(|(k, v)| (k.clone(), v.clone())).collect()).unwrap_or_default()
    }

    pub fn track_mut(&mut self, uid: &str) -> Option<&mut Map<String, Value>> {
        self.doc.get_mut(SECTION)?.get_mut(uid)?.as_object_mut()
    }

    /// A new track over retail track `base_track`: its uid.
    pub fn add_track(&mut self, base_track: u32, name: &str, unlocked: bool) -> String {
        let uid = new_uid();
        if !self.doc[SECTION].is_object() {
            self.doc[SECTION] = json!({});
        }
        self.doc[SECTION][&uid] = json!({ "extends": base(base_track), "name": name, "unlocked": unlocked });
        uid
    }

    /// The track gone, with its level link, its compiled level and the picture the editor copied
    /// for it.
    pub fn remove_track(&mut self, uid: &str) {
        if let Some(old) = self.track_own_picture(uid) {
            let id = uid.trim_start_matches("uid://");
            if old.parent() == Some(&self.dir.join(IMAGES)) && old.file_name().is_some_and(|n| n.to_string_lossy().starts_with(id)) {
                let _ = std::fs::remove_file(old);
            }
        }
        if let Some(m) = self.doc.get_mut(SECTION).and_then(Value::as_object_mut) {
            m.remove(uid);
        }
        let _ = self.set_track_level(uid, None);
    }

    /// Sets (Some) or drops (None) a text field of the track (name, thumbnail, extends).
    pub fn set_track_str(&mut self, uid: &str, field: &str, v: Option<&str>) {
        let Some(e) = self.track_mut(uid) else { return };
        match v {
            Some(s) => {
                e.insert(field.into(), Value::String(s.into()));
            }
            None => {
                e.remove(field);
            }
        }
    }

    /// The links file, read again only when its time or size changed (the lists ask every frame).
    fn links(&self) -> Map<String, Value> {
        let key: crate::content::FileStamp = std::fs::metadata(self.dir.join(LINKS)).map(|m| (m.modified().ok(), m.len())).unwrap_or((None, 0));
        let mut c = self.links.lock().unwrap_or_else(|e| e.into_inner());
        match c.as_ref() {
            Some((k, m)) if *k == key => m.clone(),
            _ => {
                let m = read_links(&self.dir);
                *c = Some((key, m.clone()));
                m
            }
        }
    }

    /// After a write: one within the file time's granularity, of the same size, looks unchanged.
    fn forget_links(&self) {
        *self.links.lock().unwrap_or_else(|e| e.into_inner()) = None;
    }

    fn link_path(&self, links: &Map<String, Value>, uid: &str) -> Option<PathBuf> {
        let p = PathBuf::from(links.get(uid)?.as_str()?);
        Some(if p.is_relative() { self.dir.join(p) } else { p })
    }

    /// The level project the track is made from (None: the base track's level as it is).
    pub fn track_level(&self, uid: &str) -> Option<PathBuf> {
        self.link_path(&self.links(), uid)
    }

    /// Every track's level project: (uid, folder), for tracks the mod still has.
    pub fn track_levels(&self) -> Vec<(String, PathBuf)> {
        let links = self.links();
        self.tracks().into_iter().filter_map(|(u, _)| self.link_path(&links, &u).map(|p| (u, p))).collect()
    }

    /// Links the track to a level project (its `extends` becomes the level's: a level whose mode
    /// and extends disagree is refused) or unlinks it (None: its
    /// `level` and `world` go, and the compiled file). The link is written at once (it isn't
    /// mod.jsonc's); the entry's changes are saved with the mod.
    pub fn set_track_level(&mut self, uid: &str, proj: Option<&Path>) -> Result<(), String> {
        let mut links = read_links(&self.dir);
        match proj {
            Some(p) => {
                let (mode, extends, _, name) = level_info(p)?;
                let n = base_number(&extends).filter(|&n| mode_of(n).is_some()).ok_or_else(|| format!("level.jsonc: extends {extends}: not a retail track of a world select"))?;
                if mode_of(n).map(|m| m.name) != mode_named(&mode).map(|m| m.name) {
                    return Err(format!("level.jsonc: extends {extends} isn't a {mode} track"));
                }
                let dir = std::fs::canonicalize(&self.dir).unwrap_or_else(|_| self.dir.clone());
                let abs = std::fs::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
                let stored = abs.strip_prefix(&dir).map(Path::to_path_buf).unwrap_or(abs);
                links.insert(uid.into(), Value::String(stored.display().to_string()));
                write_links(&self.dir, &links)?;
                self.forget_links();
                if let Some(e) = self.track_mut(uid) {
                    e.insert("extends".into(), Value::String(extends));
                    if e.get("name").and_then(Value::as_str).is_none_or(|s| s.trim().is_empty()) {
                        e.insert("name".into(), Value::String(name));
                    }
                }
            }
            None => {
                let old = self.track_level(uid);
                if links.remove(uid).is_some() {
                    write_links(&self.dir, &links)?;
                    self.forget_links();
                }
                if let Some(e) = self.track_mut(uid) {
                    e.remove("level");
                    e.remove("world");
                }
                let stem = level_stem(uid);
                let _ = std::fs::remove_file(self.dir.join(format!("{stem}.json")));
                // build_level's copy, made only of a project outside the mod: the folder can be
                // the linked project itself (or hold another track's), and then it stays
                let src = self.dir.join(format!("{stem}-src"));
                let copy = old.is_some_and(|p| !inside(&p, &self.dir)) && !self.track_levels().iter().any(|(_, p)| inside(p, &src));
                if copy {
                    let _ = std::fs::remove_dir_all(src);
                }
            }
        }
        Ok(())
    }

    /// A compiled level into the track's entry: its `level`, `world` and `extends`.
    pub fn apply_build(&mut self, b: &TrackBuild) {
        let Some(e) = self.track_mut(&b.uid) else { return };
        for (k, v) in [("extends", &b.extends), ("world", &b.world), ("level", &b.level)] {
            if e.get(k).and_then(Value::as_str) != Some(v.as_str()) {
                e.insert(k.into(), Value::String(v.clone()));
            }
        }
    }

    /// Tracks with a level project whose entry lacks its compiled level (never compiled).
    pub fn tracks_uncompiled(&self) -> Vec<String> {
        self.track_levels()
            .into_iter()
            .filter(|(u, _)| {
                let e = &self.doc[SECTION][u];
                e["level"].as_str().is_none_or(|l| !self.dir.join(l).is_file()) || e["world"].as_str().is_none()
            })
            .map(|(u, _)| u)
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("hwvx-tracks-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    fn schema_errors(doc: &Value) -> Vec<String> {
        let schema: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap();
        xdlrs::validate::MiniValidator::new(&schema).errors(doc)
    }

    /// A level project like the level editor's New level makes.
    fn level_project(dir: &Path, mode: &str, extends: u32, world: &str) {
        std::fs::create_dir_all(dir.join("triggers")).unwrap();
        let lv = json!({"$schema": "../../schemas/Level.schema.json", "format_version": 1, "id": "uid://mylevel", "name": "My Level",
                        "mode": mode, "extends": base(extends), "world": world, "unlocked": true, "baseTriggers": "all", "triggers": []});
        std::fs::write(dir.join("level.jsonc"), serde_json::to_string_pretty(&lv).unwrap()).unwrap();
    }

    #[test]
    fn retail_tables() {
        assert_eq!(label(39), "39  Challenge 1");
        assert_eq!(label(4), "4  Underworld");
        assert_eq!(label(57), "57  (an added track)");
        assert_eq!(mode_of(40).unwrap().name, "challenge");
        assert_eq!(mode_of(5).unwrap().name, "mission");
        assert!(mode_of(27).is_none() && mode_of(36).is_none(), "blanks between the families");
        for m in &MODES {
            for n in m.first..m.end {
                assert!(!RETAIL[n as usize].is_empty(), "{n}");
                assert!(picture_link(n).is_some(), "{n} has a picture");
            }
        }
        assert_eq!(picture_link(39).as_deref(), Some("base://HW_IPack/interface_6/texture/42"));
        assert_eq!(picture_link(5).as_deref(), Some("base://HW_IPack/interface_6/texture/16"));
        assert_eq!(picture_texture("base://HW_IPack/interface_6/texture/103"), Some(103));
        assert_eq!(picture_texture("base://HW_IPack/interface_0/texture/1"), None);
        // the PC's pack numbers the same pictures otherwise: challenge 1, mission 1, a battle's
        assert_eq!((release_texture("pc", 42), release_texture("pc", 16), release_texture("pc", 99)), (46, 22, 102));
        assert_eq!((release_texture("gc", 42), release_texture("ps2", 42), release_texture("pc", 5)), (42, 42, 5));
        // joyride 0, battle 19 and drag 29 share Monument City's picture
        assert!(pictures().iter().any(|(t, v)| *t == 18 && v.contains(&0) && v.contains(&19) && v.contains(&29)));
        assert_eq!(start_string("uid://abc123", 40).as_deref(), Some("race:mode=challenge,world=uid://abc123"));
        assert_eq!(start_string("uid://abc123", 6).as_deref(), Some("race:mode=mission,world=uid://abc123"));
        assert_eq!(start_string("uid://a,car=3", 40), None, "never a key of its own");
        assert_eq!(level_stem("uid://ab12"), "level/ab12");
    }

    /// Tracks added, edited, linked to a level, saved: mod.jsonc as the game reads it, and valid.
    #[test]
    fn tracks_in_mod_jsonc() {
        let d = scratch("model");
        let mut p = ModProject::create(&d, "Small Championship", "me").unwrap();
        let a = p.add_track(39, "Night Loop", true);
        let b = p.add_track(19, "Arena Two", false);
        p.set_track_str(&a, "thumbnail", picture_link(44).as_deref());
        p.set_track_str(&b, "name", Some("Arena 2"));
        assert_eq!(p.tracks().len(), 2);
        p.save().unwrap();
        let q = ModProject::open(&d).unwrap();
        assert_eq!(q.doc["tracks"][&a], json!({"extends": "base://mcp/track/39", "name": "Night Loop", "unlocked": true, "thumbnail": "base://HW_IPack/interface_6/texture/47"}));
        assert_eq!(q.doc["tracks"][&b], json!({"extends": "base://mcp/track/19", "name": "Arena 2", "unlocked": false}));
        assert!(schema_errors(&q.doc).is_empty(), "{:?}", schema_errors(&q.doc));

        // a level project outside the mod, linked to track b (a battle): its extends comes along
        let mut q = q;
        let lp = d.with_extension("level");
        level_project(&lp, "battle", 20, "WORLDS_\\MP_\\BATTLE_\\bat2_l9");
        q.set_track_level(&b, Some(&lp)).unwrap();
        assert_eq!(q.doc["tracks"][&b]["extends"], "base://mcp/track/20");
        assert_eq!(q.track_level(&b).map(|x| std::fs::canonicalize(x).unwrap()), Some(std::fs::canonicalize(&lp).unwrap()));
        assert_eq!(q.tracks_uncompiled(), vec![b.clone()]);
        // a level inside the mod folder
        let lc = d.join("levels").join("loop");
        level_project(&lc, "challenge", 41, "worlds_\\sp_\\chal1_\\ch2_l1");
        q.set_track_level(&a, Some(&lc)).unwrap();
        let links: Value = serde_json::from_str(&std::fs::read_to_string(d.join(LINKS)).unwrap()).unwrap();
        assert_eq!(links["tracks"][&a], "levels/loop", "inside the mod folder: relative");
        // what a compile gives, applied
        q.apply_build(&TrackBuild { uid: b.clone(), level: "level/x.json".into(), world: "WORLDS_\\MP_\\BATTLE_\\bat2_l9".into(), extends: base(20), cached: false });
        q.save().unwrap();
        let r = ModProject::open(&d).unwrap();
        assert_eq!(r.doc["tracks"][&b]["level"], "level/x.json");
        assert_eq!(r.doc["tracks"][&b]["world"], "WORLDS_\\MP_\\BATTLE_\\bat2_l9");
        assert!(schema_errors(&r.doc).is_empty(), "{:?}", schema_errors(&r.doc));
        // unlinked: level and world go, the link too
        let mut r = r;
        r.set_track_level(&b, None).unwrap();
        assert!(r.doc["tracks"][&b].get("level").is_none() && r.doc["tracks"][&b].get("world").is_none());
        assert!(r.track_level(&b).is_none() && r.track_level(&a).is_some());
        // a project that is the track's own level/<id>-src folder survives an unlink
        let own = d.join(format!("{}-src", level_stem(&b)));
        level_project(&own, "battle", 20, "WORLDS_\\MP_\\BATTLE_\\bat2_l9");
        r.set_track_level(&b, Some(&own)).unwrap();
        r.set_track_level(&b, None).unwrap();
        assert!(own.join("level.jsonc").is_file(), "the linked project was deleted");
        // an outside project's copy goes
        let copy = d.join(format!("{}-src", level_stem(&a)));
        std::fs::create_dir_all(&copy).unwrap();
        r.set_track_level(&a, Some(&lp)).unwrap();
        r.remove_track(&a);
        assert!(!copy.exists());
        assert!(!d.join(LINKS).exists(), "no links left: no file");
        r.save().unwrap();
        assert_eq!(ModProject::open(&d).unwrap().tracks().len(), 1);
        let _ = std::fs::remove_dir_all(&d);
        let _ = std::fs::remove_dir_all(&lp);
    }

    /// A picture made small and grey, its mean taken out and its length 1: two pictures' dot
    /// product says how alike they are, whatever their brightness (the PC's are lighter).
    fn shape(p: &Picture) -> Vec<f64> {
        const N: usize = 64;
        let mut v = vec![0f64; N * N];
        for y in 0..p.h {
            for x in 0..p.w {
                let o = (y * p.w + x) * 4;
                let g = 0.3 * p.rgba[o] as f64 + 0.59 * p.rgba[o + 1] as f64 + 0.11 * p.rgba[o + 2] as f64;
                v[(y * N / p.h) * N + x * N / p.w] += g;
            }
        }
        let mean = v.iter().sum::<f64>() / v.len() as f64;
        v.iter_mut().for_each(|x| *x -= mean);
        let len = v.iter().map(|x| x * x).sum::<f64>().sqrt().max(1e-9);
        v.iter_mut().for_each(|x| *x /= len);
        v
    }

    /// With the GameCube's data (HWVX_TEST_DATA) and the PC's or the PS2's (HWVX_TEST_PC_DATA,
    /// HWVX_TEST_PS2_DATA; each skipped without): every world select picture the editor decodes
    /// from that release, by a link's number, is the GameCube's picture of that number (the one
    /// most like it of all of them). The GameCube's numbering on the PC's pack fails this.
    #[test]
    fn world_select_pictures_per_release() {
        let Some(gc) = std::env::var_os("HWVX_TEST_DATA").map(PathBuf::from) else {
            eprintln!("HWVX_TEST_DATA not set: skipped");
            return;
        };
        let gc: Vec<_> = base_pictures(&gc).unwrap().iter().map(|p| (p.texture, shape(p))).collect();
        assert_eq!(gc.len(), pictures().len());
        for var in ["HWVX_TEST_PC_DATA", "HWVX_TEST_PS2_DATA"] {
            let Some(data) = std::env::var_os(var).map(PathBuf::from) else {
                eprintln!("{var} not set: skipped");
                continue;
            };
            let pics = base_pictures(&data).unwrap();
            assert_eq!(pics.len(), gc.len(), "{var}");
            for p in &pics {
                let s = shape(p);
                let dot = |g: &Vec<f64>| g.iter().zip(&s).map(|(a, b)| a * b).sum::<f64>();
                let (best, score) = gc.iter().map(|(t, g)| (*t, dot(g))).fold((0, f64::MIN), |a, b| if b.1 > a.1 { b } else { a });
                eprintln!("{var}: texture {} is most like the GameCube's {best} ({score:.2})", p.texture);
                assert_eq!(best, p.texture, "{var}: the picture decoded for texture {} is the GameCube's {best}", p.texture);
            }
        }
    }

    /// With game data (HWVX_TEST_DATA, else skipped): the sample checkpoint-run compiled into a
    /// mod as one of its tracks, and the world select's pictures decoded.
    #[test]
    fn level_compiled_into_the_mod() {
        let Some(data) = std::env::var_os("HWVX_TEST_DATA").map(PathBuf::from) else {
            eprintln!("HWVX_TEST_DATA not set: skipped");
            return;
        };
        let pics = base_pictures(&data).unwrap();
        assert_eq!(pics.len(), pictures().len());
        assert!(pics.iter().all(|p| p.w > 0 && p.rgba.len() == p.w * p.h * 4));
        let tools = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf();
        let env = crate::testdrive::level_env(&tools).unwrap();
        // HWVX_TEST_KEEP_MOD=DIR: the mod kept in DIR/small-championship for a game run
        let keep = std::env::var_os("HWVX_TEST_KEEP_MOD").map(|k| PathBuf::from(k).join("small-championship"));
        let d = keep.clone().unwrap_or_else(|| scratch("compile"));
        let _ = std::fs::remove_dir_all(&d);
        let mut p = ModProject::create(&d, "Small Championship", "").unwrap();
        let t = p.add_track(40, "", true);
        let sample = tools.join("level-samples").join("checkpoint-run");
        p.set_track_level(&t, Some(&sample)).unwrap();
        let cache = std::env::var_os("HWVX_TEST_CACHE").map(PathBuf::from).unwrap_or_else(|| d.with_extension("cache"));
        let bo = xdlrs::level::BaseOpts { base: None, data: Some(data), cache: Some(cache) };
        let b = build_level(&env, &d, &t, &sample, &bo).unwrap();
        p.apply_build(&b);
        assert_eq!(p.doc["tracks"][&t]["extends"], "base://mcp/track/39");
        assert_eq!(p.doc["tracks"][&t]["name"], "Checkpoint Run");
        assert!(d.join(&b.level).is_file() && d.join(format!("{}-src", level_stem(&t))).join("level.jsonc").is_file());
        assert!(p.tracks_uncompiled().is_empty());
        assert!(schema_errors(&p.doc).is_empty());
        if keep.is_some() {
            // two more: a challenge track with another's picture, a battle arena
            let n = p.add_track(44, "Night Loop", true);
            p.set_track_str(&n, "thumbnail", picture_link(50).as_deref());
            p.add_track(20, "Arena Two", true);
            p.save().unwrap();
            assert!(schema_errors(&ModProject::open(&d).unwrap().doc).is_empty());
            // Save as mod's step: the folder checked for game data and packed (the links file left out)
            let xmod = d.with_extension("xmod");
            let cache = bo.cache.clone().unwrap();
            let line = xdlrs::level_share::check_and_pack(tools.parent().unwrap(), &d, &xmod, None, bo.data.as_deref(), &cache).unwrap();
            eprintln!("{line}");
            assert!(xmod.is_file());
            return;
        }
        let _ = std::fs::remove_dir_all(&d);
    }
}

#[cfg(test)]
mod own_picture_tests {
    use super::*;

    /// A PNG made here: a gradient (red across, blue down) with a yellow square in the middle.
    pub fn gradient_png(w: u32, h: u32) -> Vec<u8> {
        let mut px = vec![];
        for y in 0..h {
            for x in 0..w {
                let inside = x >= w / 4 && x < w * 3 / 4 && y >= h / 4 && y < h * 3 / 4;
                px.extend_from_slice(&if inside { [255, 230, 0, 255] } else { [(x * 255 / (w - 1)) as u8, 40, (y * 255 / (h - 1)) as u8, 255] });
            }
        }
        let mut out = vec![];
        let mut e = png::Encoder::new(&mut out, w, h);
        e.set_color(png::ColorType::Rgba);
        e.set_depth(png::BitDepth::Eight);
        e.write_header().unwrap().write_image_data(&px).unwrap();
        out
    }

    #[test]
    fn a_picture_of_the_mods_own() {
        let d = std::env::temp_dir().join(format!("hwvx-tracks-test-{}-own", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        let mut p = ModProject::create(&d, "Pictures", "").unwrap();
        let t = p.add_track(39, "Mine", true);
        let srcs = d.with_extension("src");
        std::fs::create_dir_all(&srcs).unwrap();
        let src = srcs.join("My Picture.png");
        std::fs::write(&src, gradient_png(320, 192)).unwrap();
        let rel = p.set_track_own_picture(&t, &src).unwrap();
        assert_eq!(rel, format!("images/{}-my-picture.png", t.trim_start_matches("uid://")));
        assert_eq!(p.track_own_picture(&t), Some(d.join(&rel)));
        assert_eq!(std::fs::read(d.join(&rel)).unwrap(), std::fs::read(&src).unwrap());
        let pic = read_picture(&d.join(&rel)).unwrap();
        assert_eq!((pic.w, pic.h), (320, 192));
        // another replaces it (the old copy goes)
        let src2 = srcs.join("second.png");
        std::fs::write(&src2, gradient_png(64, 64)).unwrap();
        let rel2 = p.set_track_own_picture(&t, &src2).unwrap();
        assert!(!d.join(&rel).exists() && d.join(&rel2).exists());
        // refused: too big, not a PNG
        std::fs::write(&src2, gradient_png(1030, 4)).unwrap();
        assert!(p.set_track_own_picture(&t, &src2).unwrap_err().contains("at most 1024"));
        std::fs::write(&src2, b"nope").unwrap();
        assert!(p.set_track_own_picture(&t, &src2).is_err());
        assert_eq!(p.doc["tracks"][&t]["thumbnail"], rel2.as_str());
        p.save().unwrap();
        let schema: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap();
        let errs = xdlrs::validate::MiniValidator::new(&schema).errors(&ModProject::open(&d).unwrap().doc);
        assert!(errs.is_empty(), "{errs:?}");
        // the track deleted: its copied picture goes too
        let mut q = ModProject::open(&d).unwrap();
        q.remove_track(&t);
        assert!(!d.join(&rel2).exists());
        // a base link is not one
        p.set_track_str(&t, "thumbnail", picture_link(40).as_deref());
        assert!(p.track_own_picture(&t).is_none());
        let _ = std::fs::remove_dir_all(&d);
        let _ = std::fs::remove_dir_all(&srcs);
    }
}
