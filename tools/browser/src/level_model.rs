//! A level project as the editor holds it (tools/xdl_level.py's format: level.jsonc and
//! triggers/NAME.jsonc): the values being edited next to the files' text, undo and redo as
//! snapshots, a copy written for checking and the live preview, and saving that keeps each
//! file's comments and order (src/jsonc_edit.rs).

use crate::jsonc_edit;
use crate::testdrive::strip_jsonc;
use serde_json::{Map, Value, json};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug)]
pub struct TriggerFile {
    pub name: String,
    /// the file as last read or saved (None: new, not saved yet)
    pub text: Option<String>,
    pub saved: Value,
    pub value: Value,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Snapshot {
    pub level: Value,
    pub triggers: Vec<(String, Value)>,
}

#[derive(Clone, Debug)]
pub struct Project {
    pub dir: PathBuf,
    pub level_text: String,
    pub level_saved: Value,
    pub level: Value,
    pub triggers: Vec<TriggerFile>,
    pub undo: Vec<Snapshot>,
    pub redo: Vec<Snapshot>,
    /// the trigger files read from the folder (one taken out is deleted when saved)
    pub loaded: Vec<String>,
    /// trigger files taken out, as they were (their text): undo brings them back with it
    removed: Vec<TriggerFile>,
    /// trigger files that didn't parse: (name, why). Never written or deleted; their names are
    /// taken; the checks and the test drive's copy have them as they are (their errors show)
    pub broken: Vec<(String, String)>,
}

fn read_jsonc(p: &Path) -> Result<(String, Value), String> {
    let text = std::fs::read_to_string(p).map_err(|e| format!("{}: {e}", p.display()))?;
    let v = serde_json::from_str(&strip_jsonc(&text)).map_err(|e| format!("{}: {e}", p.display()))?;
    Ok((text, v))
}

impl Project {
    pub fn load(dir: &Path) -> Result<Project, String> {
        let (level_text, level) = read_jsonc(&dir.join("level.jsonc"))?;
        let mut names: Vec<String> = Vec::new();
        if let Some(order) = level["triggers"].as_array() {
            names.extend(order.iter().filter_map(Value::as_str).map(str::to_string));
        }
        if let Ok(rd) = std::fs::read_dir(dir.join("triggers")) {
            let mut more: Vec<String> = rd
                .filter_map(|e| e.ok())
                .filter_map(|e| e.file_name().to_str().and_then(|n| n.strip_suffix(".jsonc")).map(str::to_string))
                .filter(|n| !names.contains(n))
                .collect();
            more.sort();
            names.extend(more);
        }
        let mut triggers = Vec::new();
        let mut broken = Vec::new();
        for n in names {
            let p = dir.join("triggers").join(format!("{n}.jsonc"));
            match read_jsonc(&p) {
                Ok((text, v)) => triggers.push(TriggerFile { name: n, text: Some(text), saved: v.clone(), value: v }),
                Err(e) if p.exists() => broken.push((n, e)),
                Err(_) => {} // listed in level.jsonc, no file: the checks say so
            }
        }
        let loaded = triggers.iter().map(|t| t.name.clone()).collect();
        Ok(Project {
            dir: dir.to_path_buf(),
            level_text,
            level_saved: level.clone(),
            level,
            triggers,
            undo: Vec::new(),
            redo: Vec::new(),
            loaded,
            removed: Vec::new(),
            broken,
        })
    }

    /// base://<archive>/world_0/ of the level's world
    pub fn base_prefix(&self) -> String {
        let w = self.level["world"].as_str().unwrap_or("").replace('/', "\\");
        format!("base://{}/world_0/", w.rsplit('\\').next().unwrap_or("").to_lowercase())
    }

    pub fn dirty(&self) -> bool {
        self.level != self.level_saved
            || self.triggers.iter().any(|t| t.value != t.saved || t.text.is_none())
            || self.loaded.iter().any(|n| self.trigger(n).is_none())
    }

    pub fn snapshot(&self) -> Snapshot {
        Snapshot { level: self.level.clone(), triggers: self.triggers.iter().map(|t| (t.name.clone(), t.value.clone())).collect() }
    }

    fn restore(&mut self, s: Snapshot) {
        self.level = s.level;
        let old = std::mem::take(&mut self.triggers);
        for (name, v) in s.triggers {
            // as it was: in the list now, or taken out earlier (its file's text kept)
            match old.iter().chain(self.removed.iter()).find(|t| t.name == name) {
                Some(t) => self.triggers.push(TriggerFile { value: v, ..t.clone() }),
                None => self.triggers.push(TriggerFile { name, text: None, saved: Value::Null, value: v }),
            }
        }
    }

    /// A new trigger file (saved as triggers/NAME.jsonc), last in the order.
    pub fn add_trigger(&mut self, name: &str, value: Value) {
        self.triggers.push(TriggerFile { name: name.to_string(), text: None, saved: Value::Null, value });
        if let Some(order) = self.level.get_mut("triggers").and_then(Value::as_array_mut) {
            order.push(Value::String(name.to_string()));
        }
    }

    /// A trigger taken out (its file deleted when saved).
    pub fn remove_trigger(&mut self, name: &str) {
        if let Some(i) = self.triggers.iter().position(|t| t.name == name) {
            let t = self.triggers.remove(i);
            self.removed.retain(|r| r.name != name);
            self.removed.push(t);
        }
        if let Some(order) = self.level.get_mut("triggers").and_then(Value::as_array_mut) {
            order.retain(|v| v.as_str() != Some(name));
        }
    }

    /// A name for a new record of a kind (or trigger): stem1, stem2, ... not taken.
    pub fn free_name(&self, kind: &str, stem: &str) -> String {
        (1..)
            .map(|i| format!("{stem}{i}"))
            .find(|n| {
                if kind == "triggers" {
                    // nor a file of the folder (one that didn't parse, one taken out not saved yet)
                    self.trigger(n).is_none()
                        && !self.broken.iter().any(|(b, _)| b == n)
                        && !self.dir.join("triggers").join(format!("{n}.jsonc")).exists()
                } else {
                    self.level[kind].get(n).is_none()
                }
            })
            .unwrap()
    }

    /// Before an edit: what to go back to.
    pub fn push_undo(&mut self) {
        let s = self.snapshot();
        if self.undo.last() != Some(&s) {
            self.undo.push(s);
            if self.undo.len() > 500 {
                self.undo.remove(0);
            }
        }
        self.redo.clear();
    }

    pub fn undo(&mut self) -> bool {
        match self.undo.pop() {
            Some(s) => {
                self.redo.push(self.snapshot());
                self.restore(s);
                true
            }
            None => false,
        }
    }

    pub fn redo(&mut self) -> bool {
        match self.redo.pop() {
            Some(s) => {
                self.undo.push(self.snapshot());
                self.restore(s);
                true
            }
            None => false,
        }
    }

    pub fn trigger(&self, name: &str) -> Option<&TriggerFile> {
        self.triggers.iter().find(|t| t.name == name)
    }

    pub fn trigger_mut(&mut self, name: &str) -> Option<&mut TriggerFile> {
        self.triggers.iter_mut().find(|t| t.name == name)
    }

    /// A field patch of a base record (path: World/0/tileCars/0; field: rotY, pos/x), or None.
    pub fn patch(&self, path: &str, field: &str) -> Option<&Value> {
        self.level["patches"].get(format!("{}{path}", self.base_prefix()))?.get(field)
    }

    /// Sets (Some) or takes out (None) a field patch; an empty record's entry goes too.
    pub fn set_patch(&mut self, path: &str, field: &str, v: Option<Value>) {
        let key = format!("{}{path}", self.base_prefix());
        let lv = self.level.as_object_mut().unwrap();
        let patches = lv.entry("patches").or_insert_with(|| Value::Object(Map::new()));
        if !patches.is_object() {
            *patches = Value::Object(Map::new());
        }
        let pm = patches.as_object_mut().unwrap();
        match v {
            Some(v) => {
                let rec = pm.entry(key).or_insert_with(|| Value::Object(Map::new()));
                if let Some(o) = rec.as_object_mut() {
                    o.insert(field.to_string(), v);
                }
            }
            None => {
                if let Some(rec) = pm.get_mut(&key).and_then(Value::as_object_mut) {
                    rec.remove(field);
                    if rec.is_empty() {
                        pm.remove(&key);
                    }
                }
            }
        }
    }

    /// The level's own records of a kind (volumes, timers, variables, strings), by name.
    pub fn own(&self, kind: &str) -> Vec<(String, Value)> {
        self.level[kind].as_object().map(|m| m.iter().map(|(k, v)| (k.clone(), v.clone())).collect()).unwrap_or_default()
    }

    /// The level and its triggers as plain JSON in `to` (for xdl_level.py: checking, the preview).
    pub fn write_copy(&self, to: &Path) -> Result<(), String> {
        let tdir = to.join("triggers");
        let _ = std::fs::remove_dir_all(&tdir);
        std::fs::create_dir_all(&tdir).map_err(|e| format!("{}: {e}", tdir.display()))?;
        let mut level = self.level.clone();
        if let Some(order) = level.get_mut("triggers").and_then(Value::as_array_mut) {
            // the listed order, then any other trigger of the project (a broken file's too: its
            // error shows in the checks)
            for n in self.triggers.iter().map(|t| &t.name).chain(self.broken.iter().map(|(n, _)| n)) {
                if !order.iter().any(|v| v.as_str() == Some(n)) {
                    order.push(json!(n));
                }
            }
        }
        write(&to.join("level.jsonc"), &serde_json::to_string_pretty(&level).unwrap())?;
        for t in &self.triggers {
            write(&tdir.join(format!("{}.jsonc", t.name)), &serde_json::to_string_pretty(&t.value).unwrap())?;
        }
        for (n, _) in &self.broken {
            let from = self.dir.join("triggers").join(format!("{n}.jsonc"));
            if let Ok(text) = std::fs::read_to_string(&from) {
                write(&tdir.join(format!("{n}.jsonc")), &text)?;
            }
        }
        Ok(())
    }

    /// Files changed on disk since they were read (or saved): another editor's edits. Saving
    /// refuses while there are any (reload the project to take them in).
    pub fn changed_on_disk(&self) -> Vec<PathBuf> {
        let same = |p: &Path, text: Option<&String>| match (std::fs::read_to_string(p).ok(), text) {
            (Some(now), Some(t)) => &now == t,
            (None, None) => true,
            _ => false,
        };
        let mut out = Vec::new();
        let lp = self.dir.join("level.jsonc");
        if !same(&lp, Some(&self.level_text)) {
            out.push(lp);
        }
        for t in self.triggers.iter().chain(self.removed.iter().filter(|r| self.loaded.contains(&r.name) && self.trigger(&r.name).is_none())) {
            let p = self.dir.join("triggers").join(format!("{}.jsonc", t.name));
            if !same(&p, t.text.as_ref()) {
                out.push(p);
            }
        }
        out
    }

    /// Writes what changed, each file edited in place (its comments kept). The files written.
    /// Nothing is written when a file changed on disk since it was read.
    pub fn save(&mut self) -> Result<Vec<PathBuf>, String> {
        let changed = self.changed_on_disk();
        if !changed.is_empty() {
            return Err(format!(
                "not saved: changed on disk since it was opened: {} (reload the level to take those edits in)",
                changed.iter().map(|p| p.display().to_string()).collect::<Vec<_>>().join(", ")
            ));
        }
        let mut out = Vec::new();
        if self.level != self.level_saved {
            let p = self.dir.join("level.jsonc");
            let text = jsonc_edit::rewrite(&self.level_text, &self.level_saved, &self.level)?;
            write(&p, &text)?;
            self.level_text = text;
            self.level_saved = self.level.clone();
            out.push(p);
        }
        for t in &mut self.triggers {
            if t.value == t.saved && t.text.is_some() {
                continue;
            }
            let p = self.dir.join("triggers").join(format!("{}.jsonc", t.name));
            let text = match &t.text {
                Some(old) => jsonc_edit::rewrite(old, &t.saved, &t.value)?,
                None => jsonc_edit::new_text(&t.value, &format!("trigger {} (made in the level editor)", t.name)),
            };
            std::fs::create_dir_all(self.dir.join("triggers")).map_err(|e| e.to_string())?;
            write(&p, &text)?;
            t.text = Some(text);
            t.saved = t.value.clone();
            out.push(p);
        }
        // trigger files taken out
        let gone: Vec<String> = self.loaded.iter().filter(|n| self.trigger(n).is_none()).cloned().collect();
        for n in gone {
            let p = self.dir.join("triggers").join(format!("{n}.jsonc"));
            if p.exists() {
                std::fs::remove_file(&p).map_err(|e| format!("{}: {e}", p.display()))?;
                out.push(p);
            }
            // its file is gone now: undone, it comes back as a new one
            if let Some(r) = self.removed.iter_mut().find(|r| r.name == n) {
                r.text = None;
                r.saved = Value::Null;
            }
        }
        self.loaded = self.triggers.iter().map(|t| t.name.clone()).collect();
        Ok(out)
    }
}

fn write(p: &Path, text: &str) -> Result<(), String> {
    let tmp = p.with_extension("jsonc.part");
    std::fs::write(&tmp, text).map_err(|e| format!("{}: {e}", tmp.display()))?;
    std::fs::rename(&tmp, p).map_err(|e| format!("{}: {e}", p.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn project(name: &str) -> PathBuf {
        let d = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../build-browser-test/model").join(name);
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(d.join("triggers")).unwrap();
        std::fs::write(d.join("level.jsonc"), "// mine\n{\n  \"name\": \"x\",\n  \"triggers\": [\"a\", \"bad\"]\n}\n").unwrap();
        std::fs::write(d.join("triggers/a.jsonc"), "// keep this comment\n{ \"type\": \"level_start\" }\n").unwrap();
        std::fs::write(d.join("triggers/bad.jsonc"), "{ \"type\": oops ").unwrap();
        d
    }

    #[test]
    fn broken_trigger_files_are_kept_and_named() {
        let d = project("broken");
        let mut p = Project::load(&d).unwrap();
        assert_eq!(p.broken.len(), 1);
        assert_eq!(p.broken[0].0, "bad");
        // its name isn't free; saving never touches it
        std::fs::write(d.join("triggers/trigger1.jsonc"), "{}").unwrap();
        assert_eq!(p.free_name("triggers", "trigger"), "trigger2", "a file of the folder is taken");
        p.level["name"] = "y".into();
        p.save().unwrap();
        assert_eq!(std::fs::read_to_string(d.join("triggers/bad.jsonc")).unwrap(), "{ \"type\": oops ");
        // the checks' copy has it as it is
        let copy = d.with_file_name("broken-copy");
        p.write_copy(&copy).unwrap();
        assert!(copy.join("triggers/bad.jsonc").exists());
        let lv: Value = serde_json::from_str(&std::fs::read_to_string(copy.join("level.jsonc")).unwrap()).unwrap();
        assert!(lv["triggers"].as_array().unwrap().iter().any(|v| v == "bad"));
    }

    #[test]
    fn undoing_a_trigger_deletion_keeps_its_file() {
        let d = project("undo");
        let mut p = Project::load(&d).unwrap();
        p.push_undo();
        p.remove_trigger("a");
        assert!(p.undo());
        let t = p.trigger("a").unwrap();
        assert!(t.text.is_some(), "its text comes back");
        assert!(!p.dirty());
        // deleted and saved, then undone: a new file again
        p.push_undo();
        p.remove_trigger("a");
        p.save().unwrap();
        assert!(!d.join("triggers/a.jsonc").exists());
        assert!(p.undo());
        assert!(p.trigger("a").unwrap().text.is_none());
        p.save().unwrap();
        assert!(d.join("triggers/a.jsonc").exists());
    }

    #[test]
    fn save_refuses_files_changed_on_disk() {
        let d = project("disk");
        let mut p = Project::load(&d).unwrap();
        std::fs::write(d.join("level.jsonc"), "// edited elsewhere\n{ \"name\": \"z\", \"triggers\": [\"a\", \"bad\"] }\n").unwrap();
        p.level["name"] = "y".into();
        let e = p.save().unwrap_err();
        assert!(e.contains("changed on disk"), "{e}");
        assert!(std::fs::read_to_string(d.join("level.jsonc")).unwrap().contains("edited elsewhere"));
    }
}
