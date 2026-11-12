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
        for n in names {
            let p = dir.join("triggers").join(format!("{n}.jsonc"));
            if let Ok((text, v)) = read_jsonc(&p) {
                triggers.push(TriggerFile { name: n, text: Some(text), saved: v.clone(), value: v });
            }
        }
        Ok(Project { dir: dir.to_path_buf(), level_text, level_saved: level.clone(), level, triggers, undo: Vec::new(), redo: Vec::new() })
    }

    /// base://<archive>/world_0/ of the level's world
    pub fn base_prefix(&self) -> String {
        let w = self.level["world"].as_str().unwrap_or("").replace('/', "\\");
        format!("base://{}/world_0/", w.rsplit('\\').next().unwrap_or("").to_lowercase())
    }

    pub fn dirty(&self) -> bool {
        self.level != self.level_saved || self.triggers.iter().any(|t| t.value != t.saved || t.text.is_none())
    }

    pub fn snapshot(&self) -> Snapshot {
        Snapshot { level: self.level.clone(), triggers: self.triggers.iter().map(|t| (t.name.clone(), t.value.clone())).collect() }
    }

    fn restore(&mut self, s: Snapshot) {
        self.level = s.level;
        for (name, v) in s.triggers {
            if let Some(t) = self.triggers.iter_mut().find(|t| t.name == name) {
                t.value = v;
            }
        }
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
        if level.get("triggers").is_none() || level["triggers"].is_array() {
            level["triggers"] = json!(self.triggers.iter().map(|t| t.name.clone()).collect::<Vec<_>>());
        }
        write(&to.join("level.jsonc"), &serde_json::to_string_pretty(&level).unwrap())?;
        for t in &self.triggers {
            write(&tdir.join(format!("{}.jsonc", t.name)), &serde_json::to_string_pretty(&t.value).unwrap())?;
        }
        Ok(())
    }

    /// Writes what changed, each file edited in place (its comments kept). The files written.
    pub fn save(&mut self) -> Result<Vec<PathBuf>, String> {
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
        Ok(out)
    }
}

fn write(p: &Path, text: &str) -> Result<(), String> {
    let tmp = p.with_extension("jsonc.part");
    std::fs::write(&tmp, text).map_err(|e| format!("{}: {e}", tmp.display()))?;
    std::fs::rename(&tmp, p).map_err(|e| format!("{}: {e}", p.display()))
}
