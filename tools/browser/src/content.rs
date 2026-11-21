//! A content project: a mod folder (mod.jsonc, schemas/Mod.schema.json) with new cars and weapons
//! made from base ones (docs/xdl-format/README.md §8, §10.3; src/game/Port_Mods.c). The editor's
//! Content tab edits it; saving keeps the file's comments (xdlrs::jsonc_edit). A mod folder runs
//! as it is from a mods folder (HWVX_MODS_DIR), and packs into an .xmod (xdlrs::level_share).

use serde_json::{Map, Value, json};
use std::path::{Path, PathBuf};

pub const FILE: &str = "mod.jsonc";

/// The two kinds a mod adds here: its section, what an entry extends, the root of its pack's patches.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Car,
    Weapon,
}

impl Kind {
    pub fn section(self) -> &'static str {
        match self {
            Kind::Car => "cars",
            Kind::Weapon => "items",
        }
    }
    /// the base entry's link for number n
    pub fn base(self, n: u32) -> String {
        match self {
            Kind::Car => format!("base://HW_Cars/car_{n}"),
            Kind::Weapon => format!("base://mcp/item/{n}"),
        }
    }
    /// the base number an `extends` names
    pub fn base_number(self, extends: &str) -> Option<u32> {
        let prefix = match self {
            Kind::Car => "base://HW_Cars/car_",
            Kind::Weapon => "base://mcp/item/",
        };
        extends.strip_prefix(prefix)?.parse().ok()
    }
    /// How many retail entries there are to start from.
    pub fn retail_count(self) -> u32 {
        match self {
            Kind::Car => 33,
            Kind::Weapon => 12,
        }
    }
    /// The objects of the entry's pack whose numbers the Content tab edits, with their schema.
    pub fn parts(self) -> &'static [(&'static str, &'static str, &'static str)] {
        match self {
            // (patch path, schema struct, what it is)
            Kind::Car => &[
                ("CarAInfo/0", "CarAInfo", "engine and gears"),
                ("CarAInfo/0/engine", "CarAEngine", "handling"),
                ("CarAInfo/0/accel", "CarAAccel", "acceleration and brakes"),
                ("CarAInfo/0/stunt", "CarAStuntInfo", "stunts"),
            ],
            Kind::Weapon => &[("ItemRecord/0/weapon", "WeaponInfo", "the weapon"), ("ItemRecord/0", "ItemRecord", "the pickup")],
        }
    }
}

/// What the player's game data is: its release (gc, pc, ps2, ps2beta) and its archives' names.
/// The releases don't all have the same files (the PC has no multi-player drag worlds,
/// race1l1..race6l1): what names one it hasn't says so instead of failing.
#[derive(Clone, Debug)]
pub struct DataInfo {
    pub release: &'static str,
    /// the archives' names without extension, lower case
    pub archives: std::collections::HashSet<String>,
}

impl DataInfo {
    /// A folder or a disc image, as the game reads it (xdlrs::disc).
    pub fn read(data: &Path) -> Result<DataInfo, String> {
        let d = xdlrs::disc::Disc::open(data)?;
        let archives = d
            .entries
            .iter()
            .filter(|e| e.name().rsplit('.').next().is_some_and(|x| ["xgc", "xpc", "xps"].contains(&x.to_ascii_lowercase().as_str())))
            .map(|e| e.stem().to_ascii_lowercase())
            .collect();
        Ok(DataInfo { release: d.release.key(), archives })
    }

    pub fn has_archive(&self, stem: &str) -> bool {
        self.archives.contains(&stem.to_ascii_lowercase())
    }

    /// "PC", "GameCube", ... for messages
    pub fn release_name(&self) -> &'static str {
        release_name(self.release)
    }
}

/// An archive's name as the GameCube spells it, the spelling base:// ids use (the game matches
/// them without case, but a mod's keys and the editor's lists should not depend on the release
/// read): the PS2's disc has every name in upper case (HW_SHARE.XPS, WORLD1L1.XPS).
pub fn archive_name(stem: &str) -> String {
    const MIXED: [&str; 6] = ["HW_brief", "HW_Cars", "HW_IPack", "HW_Items", "HW_music", "HW_share"];
    if let Some(m) = MIXED.iter().find(|m| m.eq_ignore_ascii_case(stem)) {
        return m.to_string();
    }
    if stem.get(..4).is_some_and(|p| p.eq_ignore_ascii_case("HWB_")) && stem.len() > 4 {
        return stem.to_ascii_uppercase();
    }
    stem.to_ascii_lowercase()
}

pub fn release_name(key: &str) -> &'static str {
    match key {
        "gc" => "GameCube",
        "pc" => "PC",
        "ps2" => "PlayStation 2",
        "ps2beta" => "PlayStation 2 prototype",
        _ => "?",
    }
}

/// A new id: uid:// and 12 base-36 characters (the schema allows 13), from the time and a counter.
pub fn new_uid() -> String {
    use std::hash::{BuildHasher, Hasher};
    use std::sync::atomic::{AtomicU64, Ordering};
    static N: AtomicU64 = AtomicU64::new(0);
    let mut h = std::collections::hash_map::RandomState::new().build_hasher();
    h.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0));
    h.write_u64(N.fetch_add(1, Ordering::Relaxed));
    h.write_u32(std::process::id());
    let mut v = h.finish();
    let mut s = String::from("uid://");
    for _ in 0..12 {
        s.push(char::from_digit((v % 36) as u32, 36).unwrap());
        v /= 36;
    }
    s
}

pub struct ModProject {
    pub dir: PathBuf,
    /// the file as on disk, and its value: what saving rewrites in place
    text: String,
    saved: Value,
    pub doc: Value,
    /// the tracks' links file as last read, by its time and size (content_tracks: read every frame)
    pub(crate) links: std::sync::Mutex<Option<((Option<std::time::SystemTime>, u64), Map<String, Value>)>>,
}

impl ModProject {
    pub fn open(dir: &Path) -> Result<ModProject, String> {
        let p = dir.join(FILE);
        let text = std::fs::read_to_string(&p).map_err(|e| format!("{}: {e}", p.display()))?;
        let doc: Value = serde_json::from_str(&xdlrs::jsonc::strip(&text)).map_err(|e| format!("{}: {e}", p.display()))?;
        if !doc.is_object() {
            return Err(format!("{}: not a mod (an object expected)", p.display()));
        }
        Ok(ModProject { dir: dir.to_path_buf(), text, saved: doc.clone(), doc, links: Default::default() })
    }

    /// A new mod folder `dir` (it must not hold a mod already).
    pub fn create(dir: &Path, name: &str, author: &str) -> Result<ModProject, String> {
        if dir.join(FILE).exists() {
            return Err(format!("{} holds a mod already: open it instead", dir.display()));
        }
        std::fs::create_dir_all(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        let mut doc = json!({
            "$schema": "https://raw.githubusercontent.com/Alaxend0l/HWVX-SDL/main/schemas/Mod.schema.json",
            "format_version": 1,
            "id": new_uid(),
            "name": name,
            "version": "1.0.0",
        });
        if !author.trim().is_empty() {
            doc["author"] = json!(author.trim());
        }
        doc["cars"] = json!({});
        doc["items"] = json!({});
        let text = xdlrs::jsonc_edit::new_text(&doc, "A mod made with HWVX Editor's Content tab: new cars and weapons made from the game's own.");
        std::fs::write(dir.join(FILE), &text).map_err(|e| format!("{}: {e}", dir.join(FILE).display()))?;
        ModProject::open(dir)
    }

    pub fn dirty(&self) -> bool {
        self.doc != self.saved
    }

    pub fn save(&mut self) -> Result<(), String> {
        if !self.dirty() {
            return Ok(());
        }
        let text = xdlrs::jsonc_edit::rewrite(&self.text, &self.saved, &self.doc)?;
        let p = self.dir.join(FILE);
        std::fs::write(&p, &text).map_err(|e| format!("{}: {e}", p.display()))?;
        self.text = text;
        self.saved = self.doc.clone();
        Ok(())
    }

    pub fn name(&self) -> &str {
        self.doc["name"].as_str().unwrap_or("?")
    }

    /// The entries of a kind: (uid, entry), in the file's order.
    pub fn entries(&self, kind: Kind) -> Vec<(String, Value)> {
        self.doc[kind.section()].as_object().map(|m| m.iter().map(|(k, v)| (k.clone(), v.clone())).collect()).unwrap_or_default()
    }

    pub fn entry_mut(&mut self, kind: Kind, uid: &str) -> Option<&mut Map<String, Value>> {
        self.doc.get_mut(kind.section())?.get_mut(uid)?.as_object_mut()
    }

    /// A new entry made from base number `base`: its uid.
    pub fn add(&mut self, kind: Kind, base: u32, name: &str) -> String {
        let uid = new_uid();
        if !self.doc[kind.section()].is_object() {
            self.doc[kind.section()] = json!({});
        }
        self.doc[kind.section()][&uid] = json!({ "extends": kind.base(base), "name": name, "unlocked": true });
        uid
    }

    pub fn remove(&mut self, kind: Kind, uid: &str) {
        if let Some(m) = self.doc.get_mut(kind.section()).and_then(Value::as_object_mut) {
            m.remove(uid);
        }
    }

    /// A patched value (None: the base's).
    pub fn patch_value(&self, kind: Kind, uid: &str, part: &str, field: &str) -> Option<f64> {
        self.doc[kind.section()][uid]["patch"][part][field].as_f64()
    }

    /// Sets (Some) or drops (None) a patched value; empty patch objects go with it.
    pub fn set_patch(&mut self, kind: Kind, uid: &str, part: &str, field: &str, v: Option<f64>) {
        let Some(e) = self.entry_mut(kind, uid) else { return };
        match v {
            Some(x) => {
                let n = serde_json::Number::from_f64(x).map(Value::Number).unwrap_or(Value::Null);
                // IndexMut panics on a non-object (a hand-written "patch": []): replaced
                let patch = e.entry("patch").or_insert_with(|| json!({}));
                if !patch.is_object() {
                    *patch = json!({});
                }
                if !patch[part].is_object() {
                    patch[part] = json!({});
                }
                patch[part][field] = if x.fract() == 0.0 && x.abs() < 1e15 { json!(x as i64) } else { n };
            }
            None => {
                if let Some(p) = e.get_mut("patch").and_then(Value::as_object_mut) {
                    if let Some(o) = p.get_mut(part).and_then(Value::as_object_mut) {
                        o.remove(field);
                        if o.is_empty() {
                            p.remove(part);
                        }
                    }
                    if p.is_empty() {
                        e.remove("patch");
                    }
                }
            }
        }
    }

    /// The car's paint: a hue shift (degrees) and brightness on one of its textures, as a recipe
    /// (the player's own texture, recoloured as it loads: no pixels in the mod). 0 and 1: none.
    pub fn paint(&self, uid: &str, texture: u32) -> (f64, f64) {
        let r = &self.doc["cars"][uid]["recipes"][format!("texture/{texture}")];
        let mut hue = 0.0;
        let mut bright = 1.0;
        for op in r.as_array().into_iter().flatten() {
            match op["op"].as_str() {
                Some("hue_shift") => hue = op["degrees"].as_f64().unwrap_or(0.0),
                Some("brightness") => bright = op["amount"].as_f64().unwrap_or(1.0),
                _ => {}
            }
        }
        (hue, bright)
    }

    pub fn set_paint(&mut self, uid: &str, texture: u32, hue: f64, bright: f64) {
        let Some(e) = self.entry_mut(Kind::Car, uid) else { return };
        let key = format!("texture/{texture}");
        let mut ops = vec![];
        if hue.abs() > 0.5 {
            ops.push(json!({ "op": "hue_shift", "degrees": hue.round() }));
        }
        if (bright - 1.0).abs() > 0.005 {
            ops.push(json!({ "op": "brightness", "amount": (bright * 100.0).round() / 100.0 }));
        }
        let recipes = e.entry("recipes").or_insert_with(|| json!({}));
        if !recipes.is_object() {
            *recipes = json!({});
        }
        if ops.is_empty() {
            if let Some(m) = recipes.as_object_mut() {
                m.remove(&key);
            }
        } else {
            recipes[&key] = Value::Array(ops);
        }
        if recipes.as_object().is_some_and(|m| m.is_empty()) {
            e.remove("recipes");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The PS2's upper-case names (and any other spelling) as the GameCube spells them.
    #[test]
    fn archive_names() {
        for (a, b) in [("HW_SHARE", "HW_share"), ("hw_ipack", "HW_IPack"), ("HW_Cars", "HW_Cars"), ("WORLD1L1", "world1l1"), ("BAT2_L9", "bat2_l9"), ("hwb_m01", "HWB_M01"), ("MCP", "mcp"), ("hé_x", "hé_x"), ("ééé", "ééé")] {
            assert_eq!(archive_name(a), b);
        }
    }

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("hwvx-content-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    #[test]
    fn uids_fit_the_schema() {
        for _ in 0..100 {
            let u = new_uid();
            let rest = u.strip_prefix("uid://").unwrap();
            assert!(rest.len() <= 13 && rest.chars().all(|c| c.is_ascii_digit() || c.is_ascii_lowercase()), "{u}");
        }
        assert_ne!(new_uid(), new_uid());
    }

    #[test]
    fn create_edit_save_reopen() {
        let d = scratch("roundtrip");
        let mut p = ModProject::create(&d, "Test Mod", "me").unwrap();
        assert!(ModProject::create(&d, "again", "").is_err(), "a mod there already");
        let car = p.add(Kind::Car, 5, "Fast One");
        let gun = p.add(Kind::Weapon, 1, "Big Gun");
        p.set_patch(Kind::Car, &car, "CarAInfo/0", "maxRpm", Some(8200.0));
        p.set_patch(Kind::Car, &car, "CarAInfo/0/engine", "steerRate", Some(7.5));
        p.set_patch(Kind::Weapon, &gun, "ItemRecord/0/weapon", "maxAmmo", Some(99.0));
        p.set_paint(&car, 0, 140.0, 0.8);
        assert!(p.dirty());
        p.save().unwrap();
        assert!(!p.dirty());
        let q = ModProject::open(&d).unwrap();
        assert_eq!(q.entries(Kind::Car).len(), 1);
        assert_eq!(q.doc["cars"][&car]["extends"], "base://HW_Cars/car_5");
        assert_eq!(q.doc["items"][&gun]["extends"], "base://mcp/item/1");
        assert_eq!(q.patch_value(Kind::Car, &car, "CarAInfo/0", "maxRpm"), Some(8200.0));
        assert_eq!(q.doc["cars"][&car]["patch"]["CarAInfo/0"]["maxRpm"], json!(8200), "whole numbers stay integers");
        assert_eq!(q.paint(&car, 0), (140.0, 0.8));
        // the header comment is kept by later saves
        let mut q = q;
        q.set_patch(Kind::Car, &car, "CarAInfo/0", "maxRpm", None);
        q.set_patch(Kind::Car, &car, "CarAInfo/0/engine", "steerRate", None);
        q.set_paint(&car, 0, 0.0, 1.0);
        q.save().unwrap();
        let text = std::fs::read_to_string(d.join(FILE)).unwrap();
        assert!(text.starts_with("// A mod made"), "{text}");
        let r = ModProject::open(&d).unwrap();
        assert!(r.doc["cars"][&car].get("patch").is_none() && r.doc["cars"][&car].get("recipes").is_none(), "empty patches go");
        let _ = std::fs::remove_dir_all(d);
    }

    #[test]
    fn hand_written_non_objects_are_replaced() {
        let d = scratch("nonobject");
        let mut p = ModProject::create(&d, "T", "").unwrap();
        let car = p.add(Kind::Car, 1, "C");
        p.doc["cars"][&car]["patch"] = json!([]);
        p.doc["cars"][&car]["recipes"] = json!(3);
        p.set_patch(Kind::Car, &car, "CarAInfo/0", "maxRpm", Some(1.0));
        p.doc["cars"][&car]["patch"]["CarAInfo/0"] = json!("x");
        p.set_patch(Kind::Car, &car, "CarAInfo/0", "maxRpm", Some(2.0));
        p.set_paint(&car, 0, 90.0, 1.0);
        assert_eq!(p.patch_value(Kind::Car, &car, "CarAInfo/0", "maxRpm"), Some(2.0));
        let _ = std::fs::remove_dir_all(d);
    }

    #[test]
    fn kinds() {
        assert_eq!(Kind::Car.base_number("base://HW_Cars/car_12"), Some(12));
        assert_eq!(Kind::Weapon.base_number("base://mcp/item/3"), Some(3));
        assert_eq!(Kind::Weapon.base_number("base://HW_Cars/car_3"), None);
    }

    /// What the editor writes passes the mod schema (the packer's check validates the same).
    #[test]
    fn written_mod_matches_the_schema() {
        let d = scratch("schema");
        let mut p = ModProject::create(&d, "Schema Mod", "").unwrap();
        let car = p.add(Kind::Car, 0, "A");
        p.set_patch(Kind::Car, &car, "CarAInfo/0/accel", "brakeDecel", Some(1.25));
        p.set_paint(&car, 1, -30.0, 1.0);
        let gun = p.add(Kind::Weapon, 2, "B");
        p.set_patch(Kind::Weapon, &gun, "ItemRecord/0/weapon", "reloadTime", Some(0.5));
        p.save().unwrap();
        let schema: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap();
        let msgs = xdlrs::validate::MiniValidator::new(&schema).errors(&ModProject::open(&d).unwrap().doc);
        assert!(msgs.is_empty(), "{msgs:?}");
        let _ = std::fs::remove_dir_all(d);
    }
}
