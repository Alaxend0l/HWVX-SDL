//! Changes to the retail cars and weapons (no new entries): the mod's top-level `patches` and
//! `recipes`, by base object id (src/game/Port_Mods.c). A retail car N is HW_Cars pack N
//! (base://HW_Cars/car_N/CarAInfo/0/accel), a retail weapon N (the item select's number) HW_Items
//! pack N (base://HW_Items/item_N/ItemRecord/0/weapon). A new weapon made from weapon N loads a
//! pack of its own, so these never reach it. The Content tab's inspector edits an entry's own
//! patch and these through one `Target`.

use crate::content::{Kind, ModProject};
use serde_json::{Map, Value, json};

/// What the inspector's numbers and paint write to.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum Target {
    /// a new entry of the mod (its uid): its own `patch` and `recipes`
    Entry(Kind, String),
    /// a retail car or weapon by number: the mod's top-level `patches` and `recipes`
    Retail(Kind, u32),
}

impl Target {
    pub fn kind(&self) -> Kind {
        match self {
            Target::Entry(k, _) | Target::Retail(k, _) => *k,
        }
    }
}

/// The base pack a retail car or weapon's object ids start with.
pub fn retail_pack(kind: Kind, n: u32) -> String {
    match kind {
        Kind::Car => format!("base://HW_Cars/car_{n}"),
        Kind::Weapon => format!("base://HW_Items/item_{n}"),
    }
}

/// A part's object id (`patches`' key), e.g. base://HW_Items/item_1/ItemRecord/0/weapon.
pub fn retail_id(kind: Kind, n: u32, part: &str) -> String {
    format!("{}/{part}", retail_pack(kind, n))
}

/// A texture's id (`recipes`' key), e.g. base://HW_Cars/car_7/texture/1.
pub fn retail_texture(kind: Kind, n: u32, texture: u32) -> String {
    format!("{}/texture/{texture}", retail_pack(kind, n))
}

/// The car and weapon pack textures whose number differs between the retail releases: (archive,
/// pack, [GameCube, PC, PS2] numbers; -1: that release hasn't it). From the cross-release pairing
/// table (superproject docs/xdl-format/pairing/HW_Cars.jsonl, HW_Items.jsonl: by content, 16 x 16
/// thumbnails); every other texture has the same number on all three. The PS2 has extra car
/// textures before the shared ones (car_1, 3, 7, 20) and its own order in car_15, 20, 24; the PC
/// lacks car_3's and car_13's fourth and third, and most weapons' second (a texture of the
/// GameCube's own, where the PS2 has another of its own), its numbers after them one less. A
/// recipe's `texture/N` is texture N of the release it runs on (Port_Mods.c ApplyRecipe), so on
/// these the editor writes `texture/<release>:N` for each.
const TEXTURE_ROWS: &[(&str, &str, [i8; 3])] = &[
    ("HW_Cars", "car_1", [0, 0, 1]),
    ("HW_Cars", "car_1", [1, 1, 2]),
    ("HW_Cars", "car_1", [2, 2, 3]),
    ("HW_Cars", "car_1", [3, 3, 4]),
    ("HW_Cars", "car_1", [4, 4, 5]),
    ("HW_Cars", "car_1", [5, 5, 6]),
    ("HW_Cars", "car_1", [-1, -1, 0]),
    ("HW_Cars", "car_13", [2, -1, 2]),
    ("HW_Cars", "car_13", [3, 2, 3]),
    ("HW_Cars", "car_15", [1, 1, 2]),
    ("HW_Cars", "car_15", [2, 2, 1]),
    ("HW_Cars", "car_20", [1, 1, 2]),
    ("HW_Cars", "car_20", [2, 2, 1]),
    ("HW_Cars", "car_20", [3, 3, 4]),
    ("HW_Cars", "car_20", [-1, -1, 3]),
    ("HW_Cars", "car_24", [2, 2, 3]),
    ("HW_Cars", "car_24", [3, 3, 2]),
    ("HW_Cars", "car_3", [3, -1, 4]),
    ("HW_Cars", "car_3", [4, 3, 5]),
    ("HW_Cars", "car_3", [-1, -1, 3]),
    ("HW_Cars", "car_7", [3, 3, 4]),
    ("HW_Cars", "car_7", [4, 4, 5]),
    ("HW_Cars", "car_7", [-1, -1, 3]),
    ("HW_Cars", "share_2", [21, 21, -1]),
    ("HW_Cars", "share_2", [22, 22, -1]),
    ("HW_Cars", "share_2", [-1, -1, 21]),
    ("HW_Cars", "share_2", [-1, -1, 22]),
    ("HW_Items", "item_0", [1, -1, -1]),
    ("HW_Items", "item_0", [2, 1, 2]),
    ("HW_Items", "item_0", [-1, -1, 1]),
    ("HW_Items", "item_1", [1, -1, -1]),
    ("HW_Items", "item_1", [2, 1, 2]),
    ("HW_Items", "item_1", [3, 2, 3]),
    ("HW_Items", "item_1", [4, 3, 4]),
    ("HW_Items", "item_1", [-1, -1, 1]),
    ("HW_Items", "item_10", [1, -1, -1]),
    ("HW_Items", "item_10", [2, 1, 2]),
    ("HW_Items", "item_10", [-1, -1, 1]),
    ("HW_Items", "item_11", [1, -1, -1]),
    ("HW_Items", "item_11", [-1, -1, 1]),
    ("HW_Items", "item_2", [1, -1, -1]),
    ("HW_Items", "item_2", [2, 1, 2]),
    ("HW_Items", "item_2", [3, 2, 3]),
    ("HW_Items", "item_2", [4, 3, 4]),
    ("HW_Items", "item_2", [-1, -1, 1]),
    ("HW_Items", "item_3", [1, -1, -1]),
    ("HW_Items", "item_3", [2, 1, 2]),
    ("HW_Items", "item_3", [3, 2, 3]),
    ("HW_Items", "item_3", [-1, -1, 1]),
    ("HW_Items", "item_4", [1, -1, -1]),
    ("HW_Items", "item_4", [2, 1, 2]),
    ("HW_Items", "item_4", [3, -1, -1]),
    ("HW_Items", "item_4", [4, 3, 4]),
    ("HW_Items", "item_4", [5, 4, 5]),
    ("HW_Items", "item_4", [6, 5, 6]),
    ("HW_Items", "item_4", [-1, 2, 3]),
    ("HW_Items", "item_4", [-1, -1, 1]),
    ("HW_Items", "item_5", [1, -1, -1]),
    ("HW_Items", "item_5", [2, 1, 2]),
    ("HW_Items", "item_5", [3, 2, 3]),
    ("HW_Items", "item_5", [4, 3, 4]),
    ("HW_Items", "item_5", [-1, -1, 1]),
    ("HW_Items", "item_51", [3, 3, 4]),
    ("HW_Items", "item_51", [-1, -1, 3]),
    ("HW_Items", "item_6", [1, -1, -1]),
    ("HW_Items", "item_6", [2, 1, 2]),
    ("HW_Items", "item_6", [3, 2, 3]),
    ("HW_Items", "item_6", [-1, -1, 1]),
    ("HW_Items", "item_68", [2, 2, 3]),
    ("HW_Items", "item_68", [-1, -1, 2]),
    ("HW_Items", "item_69", [2, 2, 3]),
    ("HW_Items", "item_69", [-1, -1, 2]),
    ("HW_Items", "item_7", [1, -1, -1]),
    ("HW_Items", "item_7", [-1, -1, 1]),
    ("HW_Items", "item_77", [3, -1, -1]),
    ("HW_Items", "item_77", [4, 3, 4]),
    ("HW_Items", "item_77", [5, 4, 6]),
    ("HW_Items", "item_77", [-1, -1, 3]),
    ("HW_Items", "item_77", [-1, -1, 5]),
    ("HW_Items", "item_8", [1, -1, -1]),
    ("HW_Items", "item_8", [2, 1, 2]),
    ("HW_Items", "item_8", [-1, -1, 1]),
    ("HW_Items", "item_9", [1, -1, -1]),
    ("HW_Items", "item_9", [2, 1, 2]),
    ("HW_Items", "item_9", [3, 2, 3]),
    ("HW_Items", "item_9", [-1, -1, 1]),
];

/// The retail releases, in the order of TEXTURE_ROWS' numbers.
pub const RELEASES: [&str; 3] = ["gc", "pc", "ps2"];

/// The same texture's number on each retail release (GameCube, PC, PS2; None: that release
/// hasn't it), from its number `index` in `pack` ("car_7") of `archive` on `release`. A release
/// the table doesn't know (the PS2 prototype) gets `index` everywhere.
pub fn texture_counterparts(archive: &str, pack: &str, release: &str, index: u32) -> [Option<u32>; 3] {
    let Some(col) = RELEASES.iter().position(|r| *r == release) else { return [Some(index); 3] };
    let rows: Vec<[i8; 3]> = TEXTURE_ROWS.iter().filter(|(a, p, _)| *a == archive && *p == pack).map(|r| r.2).collect();
    let some = |x: i8| (x >= 0).then_some(x as u32);
    if let Some(r) = rows.iter().find(|r| some(r[col]) == Some(index)) {
        return r.map(some);
    }
    // the same number, unless that number is another texture there
    std::array::from_fn(|c| (!rows.iter().any(|r| some(r[c]) == Some(index))).then_some(index))
}

/// A texture's recipe keys (without the pack: "texture/4", or "texture/gc:4", "texture/pc:3",
/// "texture/ps2:5" where the releases number it otherwise), from its number on `release`.
pub fn texture_keys(archive: &str, pack: &str, release: &str, index: u32) -> Vec<String> {
    let c = texture_counterparts(archive, pack, release, index);
    if c.iter().all(|x| *x == Some(index)) {
        return vec![format!("texture/{index}")];
    }
    RELEASES.iter().zip(c).filter_map(|(r, i)| Some(format!("texture/{r}:{}", i?))).collect()
}

/// What the editor says of a texture whose number differs on another release, or that another
/// release hasn't (None: the same everywhere).
pub fn texture_note(archive: &str, pack: &str, release: &str, index: u32) -> Option<String> {
    let c = texture_counterparts(archive, pack, release, index);
    if c.iter().all(|x| *x == Some(index)) {
        return None;
    }
    let parts: Vec<String> = RELEASES
        .iter()
        .zip(c)
        .filter(|(r, _)| **r != release)
        .map(|(r, i)| match i {
            Some(i) => format!("number {i} on the {}", crate::content::release_name(r)),
            None => format!("not on the {}", crate::content::release_name(r)),
        })
        .collect();
    Some(format!("this texture is {}: the recipe is written for each release that has it", parts.join(", ")))
}

/// The pack a target's textures are in: (archive, "car_7"); an entry's is its base car's.
fn target_pack(p: &ModProject, t: &Target) -> Option<(&'static str, String)> {
    match t {
        Target::Retail(Kind::Car, n) => Some(("HW_Cars", format!("car_{n}"))),
        Target::Retail(Kind::Weapon, n) => Some(("HW_Items", format!("item_{n}"))),
        Target::Entry(k, uid) => {
            let n = k.base_number(p.doc[k.section()][uid]["extends"].as_str()?)?;
            Some(if *k == Kind::Car { ("HW_Cars", format!("car_{n}")) } else { ("HW_Items", format!("item_{n}")) })
        }
    }
}

/// The number as the mod writes it: whole numbers as integers.
fn number(x: f64) -> Value {
    if x.fract() == 0.0 && x.abs() < 1e15 {
        json!(x as i64)
    } else {
        serde_json::Number::from_f64(x).map(Value::Number).unwrap_or(Value::Null)
    }
}

/// A recipe's hue shift and brightness (0 and 1: none), as ModProject::paint reads them.
fn read_paint(r: &Value) -> (f64, f64) {
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

/// Drops `key` from the top-level section, and the section when it is left empty.
fn drop_key(doc: &mut Value, section: &str, key: &str) {
    if let Some(m) = doc.get_mut(section).and_then(Value::as_object_mut) {
        m.remove(key);
        if m.is_empty() {
            if let Some(d) = doc.as_object_mut() {
                d.remove(section);
            }
        }
    }
}

fn section_mut<'a>(doc: &'a mut Value, section: &str) -> &'a mut Map<String, Value> {
    if !doc[section].is_object() {
        doc[section] = json!({});
    }
    doc[section].as_object_mut().unwrap()
}

impl ModProject {
    /// A patched value of the target (None: the base's).
    pub fn target_value(&self, t: &Target, part: &str, field: &str) -> Option<f64> {
        match t {
            Target::Entry(k, uid) => self.patch_value(*k, uid, part, field),
            Target::Retail(k, n) => self.doc["patches"][retail_id(*k, *n, part)][field].as_f64(),
        }
    }

    /// Sets (Some) or drops (None) a patched value of the target; empty patches go with it.
    pub fn set_target_value(&mut self, t: &Target, part: &str, field: &str, v: Option<f64>) {
        let (k, n) = match t {
            Target::Entry(k, uid) => return self.set_patch(*k, uid, part, field, v),
            Target::Retail(k, n) => (*k, *n),
        };
        let id = retail_id(k, n, part);
        match v {
            Some(x) => {
                let p = section_mut(&mut self.doc, "patches");
                if !p.get(&id).is_some_and(Value::is_object) {
                    p.insert(id.clone(), json!({}));
                }
                p[&id][field] = number(x);
            }
            None => {
                let empty = match self.doc.get_mut("patches").and_then(|p| p.get_mut(&id)).and_then(Value::as_object_mut) {
                    Some(o) => {
                        o.remove(field);
                        o.is_empty()
                    }
                    None => false,
                };
                if empty {
                    drop_key(&mut self.doc, "patches", &id);
                }
            }
        }
    }

    /// How many values of a part the target changes.
    pub fn target_changed(&self, t: &Target, part: &str) -> usize {
        let o = match t {
            Target::Entry(k, uid) => &self.doc[k.section()][uid]["patch"][part],
            Target::Retail(k, n) => &self.doc["patches"][retail_id(*k, *n, part)],
        };
        o.as_object().map(|m| m.len()).unwrap_or(0)
    }

    /// The recipes a target's are in, and the prefix of their keys: the mod's top-level
    /// `recipes` (keys base://HW_Cars/car_7/texture/...) or the entry's own (texture/...).
    fn recipes_of(&self, t: &Target) -> (&Value, String) {
        match t {
            Target::Entry(k, uid) => (&self.doc[k.section()][uid]["recipes"], String::new()),
            Target::Retail(k, n) => (&self.doc["recipes"], format!("{}/", retail_pack(*k, *n))),
        }
    }

    /// The target's paint on one of its textures (its number on `release`, the data the editor
    /// reads): hue shift and brightness. The release's own key first (texture/pc:3), else
    /// texture/N.
    pub fn target_paint(&self, t: &Target, release: &str, texture: u32) -> (f64, f64) {
        let (r, prefix) = self.recipes_of(t);
        let own = &r[format!("{prefix}texture/{release}:{texture}")];
        read_paint(if own.is_null() { &r[format!("{prefix}texture/{texture}")] } else { own })
    }

    /// What differs on the other releases for a target's texture (texture_note), if anything.
    pub fn target_texture_note(&self, t: &Target, release: &str, texture: u32) -> Option<String> {
        let (archive, pack) = target_pack(self, t)?;
        texture_note(archive, &pack, release, texture)
    }

    /// Paints texture `texture` (its number on `release`): texture/N where every retail release
    /// numbers it N, else a recipe for each release that has it, under its own number there
    /// (texture_keys). Hue 0 and brightness 1 drop them.
    pub fn set_target_paint(&mut self, t: &Target, release: &str, texture: u32, hue: f64, bright: f64) {
        let keys = match target_pack(self, t) {
            Some((archive, pack)) => texture_keys(archive, &pack, release, texture),
            None => vec![format!("texture/{texture}")],
        };
        let (_, prefix) = self.recipes_of(t);
        let mut ops = vec![];
        if hue.abs() > 0.5 {
            ops.push(json!({ "op": "hue_shift", "degrees": hue.round() }));
        }
        if (bright - 1.0).abs() > 0.005 {
            ops.push(json!({ "op": "brightness", "amount": (bright * 100.0).round() / 100.0 }));
        }
        let section = match t {
            Target::Entry(k, uid) => {
                let Some(e) = self.entry_mut(*k, uid) else { return };
                e.entry("recipes").or_insert_with(|| json!({}))
            }
            Target::Retail(..) => {
                section_mut(&mut self.doc, "recipes");
                &mut self.doc["recipes"]
            }
        };
        let Some(m) = section.as_object_mut() else { return };
        // what an earlier paint of this texture wrote (texture/N of the release it was made on)
        m.remove(&format!("{prefix}texture/{texture}"));
        for k in &keys {
            m.remove(&format!("{prefix}{k}"));
            if !ops.is_empty() {
                m.insert(format!("{prefix}{k}"), Value::Array(ops.clone()));
            }
        }
        let empty = m.is_empty();
        match t {
            Target::Entry(k, uid) if empty => {
                if let Some(e) = self.entry_mut(*k, uid) {
                    e.remove("recipes");
                }
            }
            Target::Retail(..) if empty => {
                if let Some(d) = self.doc.as_object_mut() {
                    d.remove("recipes");
                }
            }
            _ => {}
        }
    }

    /// The mod's `patches` and `recipes` keys on a retail car or weapon (any part or texture,
    /// also ones the Content tab doesn't show).
    fn retail_keys(&self, kind: Kind, n: u32) -> Vec<(&'static str, String)> {
        let prefix = format!("{}/", retail_pack(kind, n));
        let mut keys = vec![];
        for section in ["patches", "recipes"] {
            for k in self.doc[section].as_object().into_iter().flat_map(|m| m.keys()) {
                if k.starts_with(&prefix) {
                    keys.push((section, k.clone()));
                }
            }
        }
        keys
    }

    /// How many of the mod's patches and recipes change a retail car or weapon (0: none).
    pub fn retail_changes(&self, kind: Kind, n: u32) -> usize {
        self.retail_keys(kind, n).len()
    }

    /// The retail car or weapon as the game has it again: its patches and recipes gone.
    pub fn retail_reset(&mut self, kind: Kind, n: u32) {
        for (section, key) in self.retail_keys(kind, n) {
            drop_key(&mut self.doc, section, &key);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::{Path, PathBuf};

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("hwvx-retail-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    #[test]
    fn ids() {
        assert_eq!(retail_id(Kind::Car, 7, "CarAInfo/0/accel"), "base://HW_Cars/car_7/CarAInfo/0/accel");
        assert_eq!(retail_id(Kind::Weapon, 1, "ItemRecord/0/weapon"), "base://HW_Items/item_1/ItemRecord/0/weapon");
        assert_eq!(retail_texture(Kind::Car, 7, 1), "base://HW_Cars/car_7/texture/1");
    }

    #[test]
    fn retail_edits_and_reset() {
        let d = scratch("edits");
        let mut p = ModProject::create(&d, "Balance", "").unwrap();
        let car = Target::Retail(Kind::Car, 7);
        let gun = Target::Retail(Kind::Weapon, 1);
        p.set_target_value(&car, "CarAInfo/0/accel", "engineBrake", Some(0.4));
        p.set_target_value(&car, "CarAInfo/0", "maxRpm", Some(9000.0));
        p.set_target_value(&gun, "ItemRecord/0/weapon", "maxAmmo", Some(77.0));
        p.set_target_paint(&car, "gc", 1, 90.0, 1.2);
        assert_eq!(p.doc["patches"]["base://HW_Cars/car_7/CarAInfo/0/accel"]["engineBrake"], json!(0.4));
        assert_eq!(p.doc["patches"]["base://HW_Items/item_1/ItemRecord/0/weapon"]["maxAmmo"], json!(77), "whole numbers stay integers");
        assert_eq!(p.target_value(&car, "CarAInfo/0", "maxRpm"), Some(9000.0));
        assert_eq!(p.target_paint(&car, "gc", 1), (90.0, 1.2));
        assert_eq!(p.target_changed(&car, "CarAInfo/0/accel"), 1);
        assert_eq!((p.retail_changes(Kind::Car, 7), p.retail_changes(Kind::Weapon, 1), p.retail_changes(Kind::Car, 1)), (3, 1, 0));
        // car 7 isn't car 70
        p.set_target_value(&Target::Retail(Kind::Car, 70), "CarAInfo/0", "maxRpm", Some(1.0));
        assert_eq!(p.retail_changes(Kind::Car, 7), 3);
        p.save().unwrap();
        let mut q = ModProject::open(&d).unwrap();
        assert_eq!(q.target_value(&gun, "ItemRecord/0/weapon", "maxAmmo"), Some(77.0));
        // unticking drops the value, then its object, then the section
        q.set_target_value(&gun, "ItemRecord/0/weapon", "maxAmmo", None);
        assert!(q.doc["patches"].get("base://HW_Items/item_1/ItemRecord/0/weapon").is_none());
        q.set_target_paint(&car, "gc", 1, 0.0, 1.0);
        assert!(q.doc.get("recipes").is_none(), "an empty recipes section goes");
        q.retail_reset(Kind::Car, 7);
        assert_eq!(q.retail_changes(Kind::Car, 7), 0);
        q.retail_reset(Kind::Car, 70);
        assert!(q.doc.get("patches").is_none(), "{}", q.doc);
        let _ = std::fs::remove_dir_all(d);
    }

    /// An entry target edits the entry's own patch, as before.
    #[test]
    fn entry_targets_are_the_entry() {
        let d = scratch("entry");
        let mut p = ModProject::create(&d, "E", "").unwrap();
        let uid = p.add(Kind::Car, 5, "Five");
        let t = Target::Entry(Kind::Car, uid.clone());
        p.set_target_value(&t, "CarAInfo/0", "maxRpm", Some(8200.0));
        p.set_target_paint(&t, "gc", 0, 140.0, 0.8);
        assert_eq!(p.patch_value(Kind::Car, &uid, "CarAInfo/0", "maxRpm"), Some(8200.0));
        assert_eq!(p.paint(&uid, 0), (140.0, 0.8));
        assert_eq!(p.target_changed(&t, "CarAInfo/0"), 1);
        assert!(p.doc.get("patches").is_none() && p.retail_changes(Kind::Car, 5) == 0);
        let _ = std::fs::remove_dir_all(d);
    }

    /// Retail changes (and an entry's beside them) pass the mod schema.
    #[test]
    fn retail_changes_match_the_schema() {
        let d = scratch("schema");
        let mut p = ModProject::create(&d, "Schema", "").unwrap();
        for &(part, _, _) in Kind::Car.parts() {
            p.set_target_value(&Target::Retail(Kind::Car, 32), part, "x", Some(1.5));
        }
        for &(part, _, _) in Kind::Weapon.parts() {
            p.set_target_value(&Target::Retail(Kind::Weapon, 11), part, "x", Some(2.0));
        }
        p.set_target_paint(&Target::Retail(Kind::Car, 0), "gc", 3, -30.0, 0.5);
        // a texture the releases number otherwise: texture/gc:4, pc:3, ps2:5
        p.set_target_paint(&Target::Retail(Kind::Car, 3), "gc", 4, 30.0, 1.0);
        let gun = p.add(Kind::Weapon, 1, "G");
        p.set_target_value(&Target::Entry(Kind::Weapon, gun), "ItemRecord/0/weapon", "maxAmmo", Some(5.0));
        p.save().unwrap();
        let schema: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap();
        let doc = ModProject::open(&d).unwrap().doc;
        assert_eq!(doc["patches"].as_object().unwrap().len(), 6);
        let msgs = xdlrs::validate::MiniValidator::new(&schema).errors(&doc);
        assert!(msgs.is_empty(), "{msgs:?}");
        // the validator skips propertyNames: each id is checked against its pattern here
        let id_ok = |section: &str, id: &str| {
            let s = json!({ "type": "string", "pattern": schema["properties"][section]["propertyNames"]["pattern"] });
            xdlrs::validate::MiniValidator::new(&s).errors(&json!(id)).is_empty()
        };
        for section in ["patches", "recipes"] {
            for id in doc[section].as_object().unwrap().keys() {
                assert!(id_ok(section, id), "{section}: {id}");
            }
        }
        assert!(!id_ok("patches", "base://HW_Items/item/ItemRecord") && !id_ok("recipes", "base://HW_Cars/car_7/tex/1"), "the patterns check");
        let _ = std::fs::remove_dir_all(d);
    }

    /// The releases' numbers of a texture (the pairing table's), and the keys a paint writes.
    #[test]
    fn texture_numbers_by_release() {
        let c = |a, p, r, i| texture_counterparts(a, p, r, i);
        // car_3: the PS2 has an extra before; the PC lacks the GameCube's fourth
        assert_eq!(c("HW_Cars", "car_3", "gc", 4), [Some(4), Some(3), Some(5)]);
        assert_eq!(c("HW_Cars", "car_3", "pc", 3), [Some(4), Some(3), Some(5)]);
        assert_eq!(c("HW_Cars", "car_3", "gc", 3), [Some(3), None, Some(4)]);
        assert_eq!(c("HW_Cars", "car_3", "ps2", 3), [None, None, Some(3)], "the PS2's own");
        assert_eq!(c("HW_Cars", "car_3", "gc", 0), [Some(0); 3]);
        assert_eq!(c("HW_Cars", "car_0", "pc", 1), [Some(1); 3]);
        assert_eq!(c("HW_Items", "item_0", "gc", 1), [Some(1), None, None], "the GameCube's own");
        assert_eq!(c("HW_Items", "item_0", "pc", 1), [Some(2), Some(1), Some(2)]);
        assert_eq!(c("HW_Cars", "car_5", "ps2beta", 2), [Some(2); 3], "a release the table doesn't know");
        assert_eq!(texture_keys("HW_Cars", "car_0", "gc", 1), ["texture/1"]);
        assert_eq!(texture_keys("HW_Cars", "car_3", "gc", 4), ["texture/gc:4", "texture/pc:3", "texture/ps2:5"]);
        assert_eq!(texture_keys("HW_Cars", "car_3", "ps2", 3), ["texture/ps2:3"]);
        assert!(texture_note("HW_Cars", "car_0", "gc", 0).is_none());
        assert_eq!(
            texture_note("HW_Cars", "car_3", "gc", 3).unwrap(),
            "this texture is not on the PC, number 4 on the PlayStation 2: the recipe is written for each release that has it"
        );
        // painted on the GameCube's data, read on the PC's: the same texture, under its number there
        let d = scratch("numbers");
        let mut p = ModProject::create(&d, "N", "").unwrap();
        let car = Target::Retail(Kind::Car, 3);
        p.set_target_paint(&car, "gc", 4, 45.0, 1.0);
        assert_eq!(p.doc["recipes"].as_object().unwrap().keys().collect::<Vec<_>>(), ["base://HW_Cars/car_3/texture/gc:4", "base://HW_Cars/car_3/texture/pc:3", "base://HW_Cars/car_3/texture/ps2:5"]);
        assert_eq!((p.target_paint(&car, "pc", 3), p.target_paint(&car, "ps2", 5), p.target_paint(&car, "pc", 4)), ((45.0, 1.0), (45.0, 1.0), (0.0, 1.0)));
        // a recipe an older editor wrote (texture/4 on the GameCube's data) gives way to them
        p.doc["recipes"] = json!({ "base://HW_Cars/car_3/texture/4": [{ "op": "hue_shift", "degrees": 10 }] });
        assert_eq!(p.target_paint(&car, "gc", 4), (10.0, 1.0));
        p.set_target_paint(&car, "gc", 4, 20.0, 1.0);
        assert_eq!(p.doc["recipes"].as_object().unwrap().len(), 3);
        p.set_target_paint(&car, "gc", 4, 0.0, 1.0);
        assert!(p.doc.get("recipes").is_none());
        // an entry made from car 3: its own recipes, the same keys
        let uid = p.add(Kind::Car, 3, "Three");
        p.set_target_paint(&Target::Entry(Kind::Car, uid.clone()), "ps2", 5, -20.0, 1.0);
        assert_eq!(p.doc["cars"][&uid]["recipes"].as_object().unwrap().keys().collect::<Vec<_>>(), ["texture/gc:4", "texture/pc:3", "texture/ps2:5"]);
        let _ = std::fs::remove_dir_all(d);
    }

    /// A pack's textures, each made small and grey with its mean out and its length 1 (None: not
    /// decoded), and its size.
    fn pack_shapes(data: &Path, archive: &str, typ: u32, id: u32) -> Vec<Option<((usize, usize), Vec<f64>)>> {
        use xdlrs::modcheck::media::{Archive, RawPack, decode_level, scan_textures, slice};
        let path = xdlrs::cars::find_archive(data, archive).unwrap();
        let a = Archive::from_bytes(std::fs::read(&path).unwrap(), &path).unwrap();
        let Some(e) = a.entries.iter().find(|e| e.typ == typ && e.id == id) else { return vec![] };
        let pk = RawPack::new(a.release, e.typ, a.pack_bytes(e), &e.name).unwrap();
        scan_textures(&pk)
            .unwrap()
            .iter()
            .map(|x| {
                let &(o, n, w, h) = x.levels.first()?;
                let clut = x.clut.map(|o| slice(pk.b, o, (o + 0x400) as i64));
                let px = decode_level(a.release, x.fmt, slice(pk.b, o, (o + n) as i64), w, h, clut).ok()?;
                const N: usize = 16;
                let mut v = vec![0f64; N * N * 3];
                for y in 0..h {
                    for x in 0..w {
                        let p = (y * w + x) * 4;
                        let c = (y * N / h) * N + x * N / w;
                        for k in 0..3 {
                            // the GameCube's colours are squared (CMPR): taken to the others' space as pairing.md does
                            let c8 = px[p + k] as f64;
                            v[c * 3 + k] += if a.release == "gc" { (255.0 * c8).sqrt() } else { c8 } * px[p + 3] as f64 / 255.0;
                        }
                    }
                }
                let mean = v.iter().sum::<f64>() / v.len() as f64;
                v.iter_mut().for_each(|x| *x -= mean);
                let len = v.iter().map(|x| x * x).sum::<f64>().sqrt().max(1e-9);
                v.iter_mut().for_each(|x| *x /= len);
                Some(((w, h), v))
            })
            .collect()
    }

    /// With the GameCube's data (HWVX_TEST_DATA) and the PC's or the PS2's (HWVX_TEST_PC_DATA,
    /// HWVX_TEST_PS2_DATA; each skipped without): every car pack's and every weapon pack's
    /// textures, by the GameCube's numbers, are on the other release where texture_counterparts
    /// says: the same size there, and of that pack's textures of that size the one most like it
    /// (or as alike within 0.005: the PS2's car_20 has a copy of its first among its extras).
    #[test]
    fn texture_numbers_against_the_data() {
        let Some(gc) = std::env::var_os("HWVX_TEST_DATA").map(PathBuf::from) else { return };
        let mut packs: Vec<(&str, u32, u32)> = (0..33).map(|n| ("HW_Cars", 0, n)).collect();
        packs.extend((0..12).chain([51, 68, 69, 76, 77]).map(|n| ("HW_Items", 2, n)));
        for (col, var) in [(1, "HWVX_TEST_PC_DATA"), (2, "HWVX_TEST_PS2_DATA")] {
            let Some(data) = std::env::var_os(var).map(PathBuf::from) else {
                eprintln!("{var} not set: skipped");
                continue;
            };
            let (mut checked, mut wrong) = (0, vec![]);
            for &(archive, typ, id) in &packs {
                let pack = format!("{}_{id}", if typ == 0 { "car" } else { "item" });
                let (a, b) = (pack_shapes(&gc, archive, typ, id), pack_shapes(&data, archive, typ, id));
                for (i, x) in a.iter().enumerate() {
                    let Some((size, s)) = x else { continue };
                    let Some(j) = texture_counterparts(archive, &pack, "gc", i as u32)[col] else { continue };
                    let dot = |t: &Vec<f64>| t.iter().zip(s).map(|(p, q)| p * q).sum::<f64>();
                    let best = b.iter().enumerate().filter_map(|(k, y)| y.as_ref().filter(|y| y.0 == *size).map(|y| (k, dot(&y.1)))).fold(None, |m: Option<(usize, f64)>, c| if m.is_none_or(|m| c.1 > m.1) { Some(c) } else { m });
                    checked += 1;
                    let at = b.get(j as usize).and_then(|y| y.as_ref());
                    let ok = at.is_some_and(|y| y.0 == *size) && best.is_some_and(|(k, d)| k == j as usize || at.is_some_and(|y| dot(&y.1) >= d - 0.005));
                    if !ok {
                        wrong.push(format!("{archive}/{pack} texture {i}: here {j} ({:?}, {:?}), most like {best:?}", at.map(|y| y.0), at.map(|y| dot(&y.1))));
                    }
                }
            }
            eprintln!("{var}: {checked} textures checked");
            assert!(wrong.is_empty(), "{var}: {wrong:#?}");
        }
    }
}
