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

    /// The target's paint on one of its textures: hue shift and brightness.
    pub fn target_paint(&self, t: &Target, texture: u32) -> (f64, f64) {
        match t {
            Target::Entry(_, uid) => self.paint(uid, texture),
            Target::Retail(k, n) => read_paint(&self.doc["recipes"][retail_texture(*k, *n, texture)]),
        }
    }

    pub fn set_target_paint(&mut self, t: &Target, texture: u32, hue: f64, bright: f64) {
        let (k, n) = match t {
            Target::Entry(_, uid) => return self.set_paint(uid, texture, hue, bright),
            Target::Retail(k, n) => (*k, *n),
        };
        let key = retail_texture(k, n, texture);
        let mut ops = vec![];
        if hue.abs() > 0.5 {
            ops.push(json!({ "op": "hue_shift", "degrees": hue.round() }));
        }
        if (bright - 1.0).abs() > 0.005 {
            ops.push(json!({ "op": "brightness", "amount": (bright * 100.0).round() / 100.0 }));
        }
        if ops.is_empty() {
            drop_key(&mut self.doc, "recipes", &key);
        } else {
            section_mut(&mut self.doc, "recipes").insert(key, Value::Array(ops));
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
        p.set_target_paint(&car, 1, 90.0, 1.2);
        assert_eq!(p.doc["patches"]["base://HW_Cars/car_7/CarAInfo/0/accel"]["engineBrake"], json!(0.4));
        assert_eq!(p.doc["patches"]["base://HW_Items/item_1/ItemRecord/0/weapon"]["maxAmmo"], json!(77), "whole numbers stay integers");
        assert_eq!(p.target_value(&car, "CarAInfo/0", "maxRpm"), Some(9000.0));
        assert_eq!(p.target_paint(&car, 1), (90.0, 1.2));
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
        q.set_target_paint(&car, 1, 0.0, 1.0);
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
        p.set_target_paint(&t, 0, 140.0, 0.8);
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
        p.set_target_paint(&Target::Retail(Kind::Car, 0), 3, -30.0, 0.5);
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
}
