//! The level editor (stage 21.3; docs/xdl-format §10.6): a level project open in the Level tab
//! is edited in place. Lists of the level's own records (volumes, timers, variables, strings,
//! triggers) and of the base world's (starts, airbox volumes, items, timers, variables,
//! objectives; the viewer's `records`); select from a list or by clicking in the 3D view (the
//! viewer's `pick`, and the level's own volumes against the same ray). The inspector edits the
//! selection by its schema: enums as menus, flags as boxes, links from a list of what fits or by
//! clicking in the world, numbers, text. A base record is edited as patches (only what differs).
//! Moving: Ctrl/Cmd + drag in the view moves the selection over the ground (the viewer's
//! `raycast`), Ctrl/Cmd + wheel turns a start; the inspector's numbers and "To the ground".
//! Every edit can be undone (Ctrl/Cmd+Z, Shift for redo). As the project changes it is checked
//! (tools/xdl_level.py compile --json on a copy: the same checks as `check`) and, when it
//! compiles, applied to the world on show (`level`): the live preview. Save (Ctrl/Cmd+S) writes
//! the changed files, keeping their comments (src/jsonc_edit.rs).

use crate::{Browser, Pending};
use eframe::egui;
use hwvx_browser::level_model::Project;
use hwvx_browser::testdrive::{LevelError, default_tools};
use serde_json::{Value, json};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::mpsc::{Receiver, TryRecvError, channel};
use std::time::{Duration, Instant};

#[derive(Clone, Debug, PartialEq)]
pub enum Sel {
    Settings,
    /// the level's own: kind (volumes, timers, variables, strings), name
    Own(String, String),
    Trigger(String),
    /// a base record: its table and path (World/0/tileCars/0)
    Base(String, String),
}

/// The base world's tables the editor lists, with their labels.
pub const BASE_TABLES: [(&str, &str); 7] = [
    ("tileCars", "Starts (tile cars)"),
    ("volumes", "Airbox volumes"),
    ("items", "Items"),
    ("fxPoints", "Effect points"),
    ("timers", "Timers"),
    ("variables", "Variables"),
    ("objectives", "Objectives"),
];
const OWN_KINDS: [(&str, &str); 8] = [
    ("volumes", "Volumes"),
    ("starts", "Starts"),
    ("items", "Items"),
    ("fxPoints", "Effect points"),
    ("timers", "Timers"),
    ("variables", "Variables"),
    ("strings", "Texts"),
    ("objectives", "Objectives"),
];

/// What "+ Add" makes.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum AddKind {
    Sphere,
    BoxVolume,
    Start,
    Item,
    FxPoint,
}

#[derive(Default)]
pub struct EditState {
    pub proj: Option<Project>,
    pub load_error: Option<String>,
    pub sel: Option<Sel>,
    pub base: HashMap<String, Vec<Value>>,
    pub base_world: String,
    pub record: Value,
    pub schemas: HashMap<String, Value>,
    /// the checks' errors, and the generation they are of
    pub errors: Vec<LevelError>,
    pub generation: u64,
    checked_generation: u64,
    last_change: Option<Instant>,
    checking: Option<(u64, Receiver<(Result<Vec<LevelError>, Vec<LevelError>>, PathBuf, f64)>)>,
    pub preview: bool,
    pub preview_note: String,
    /// where a "pick in the world" link goes: (trigger, JSON path in it), or the selection's own
    pub pick_link: Option<(String, String)>,
    move_inflight: bool,
    move_offset: f32,
    pub saved_note: String,
    marks_sent: Value,
    pub filter: String,
    pub advanced: bool,
    /// warnings (the starts' boxes overlapping each other: xdl_level.py; against the world:
    /// the viewer's startcheck, by path)
    pub warnings: Vec<LevelError>,
    pub start_problems: HashMap<String, String>,
    /// the cars' box (the viewer's spawnbox) written for xdl_level.py
    pub box_file: Option<PathBuf>,
    pub spawn_box: Value,
}

impl EditState {
    /// Check again soon (something the check depends on changed: the cars' box).
    pub fn recheck(&mut self) {
        self.checked_generation = 0;
    }
}

/// A volume of the level's own: its centre (a box's middle).
fn own_volume_center(v: &Value) -> Option<[f32; 3]> {
    if let (Some(lo), Some(hi)) = (vec3(&v["box"]["min"]), vec3(&v["box"]["max"])) {
        return Some([(lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0, (lo[2] + hi[2]) / 2.0]);
    }
    vec3(&v["center"])
}

fn vec3(v: &Value) -> Option<[f32; 3]> {
    match v {
        Value::Array(a) if a.len() >= 3 => Some([a[0].as_f64()? as f32, a[1].as_f64()? as f32, a[2].as_f64()? as f32]),
        Value::Object(o) => Some([o.get("x")?.as_f64()? as f32, o.get("y")?.as_f64()? as f32, o.get("z")?.as_f64()? as f32]),
        _ => None,
    }
}

/// The base table a path is in.
pub fn table_of(path: &str) -> &str {
    let segs: Vec<&str> = path.split('/').collect();
    if segs.len() >= 4 && segs[2] == "quads" && segs.len() >= 7 {
        return match segs[5] {
            "items" => "items",
            "fxPoints" => "fxPoints",
            _ => "instances",
        };
    }
    segs.get(2).copied().unwrap_or("")
}

/// The vector field of a base record: its place.
fn pos_field(table: &str) -> &'static str {
    if table == "volumes" { "center" } else { "pos" }
}

impl Browser {
    /// The world on show is the level's own (the preview applies only there).
    pub(crate) fn shows_level_world(&self) -> bool {
        let norm = |s: &str| s.replace('/', "\\").to_lowercase();
        let shown = self.world.status["world"].as_str().map(norm);
        let mine = self.level.edit.proj.as_ref().and_then(|p| p.level["world"].as_str().map(norm));
        shown.is_some() && shown == mine
    }

    pub(crate) fn level_editing(&self) -> bool {
        self.level.tab && self.level.edit.proj.is_some() && self.world.status["world"].is_string()
    }

    fn schemas_dir(&self) -> Option<PathBuf> {
        let game = self.args.game.clone().unwrap_or_else(hwvx_browser::client::default_game);
        self.args.schemas.clone().or_else(|| hwvx_browser::client::default_schemas(&game))
    }

    pub(crate) fn schema(&mut self, name: &str) -> Value {
        if let Some(s) = self.level.edit.schemas.get(name) {
            return s.clone();
        }
        let v = self
            .schemas_dir()
            .and_then(|d| std::fs::read_to_string(d.join(format!("{name}.schema.json"))).ok())
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or(Value::Null);
        self.level.edit.schemas.insert(name.to_string(), v.clone());
        v
    }

    pub(crate) fn edit_open(&mut self, dir: &str) {
        let e = &mut self.level.edit;
        match Project::load(std::path::Path::new(dir)) {
            Ok(p) => {
                e.proj = Some(p);
                e.load_error = None;
                e.sel = Some(Sel::Settings);
                e.preview = true;
                e.generation += 1;
                e.errors.clear();
            }
            Err(err) => {
                e.proj = None;
                e.load_error = Some(err);
            }
        }
        self.level.edit.base.clear();
        self.level.edit.base_world.clear();
    }

    /// A native folder chooser (the system's: osascript, zenity, PowerShell); None if cancelled.
    pub(crate) fn choose_folder() -> Option<String> {
        let out = if cfg!(target_os = "macos") {
            std::process::Command::new("osascript")
                .args(["-e", "POSIX path of (choose folder with prompt \"A level project (its folder, with level.jsonc)\")"])
                .output()
                .ok()?
        } else if cfg!(windows) {
            std::process::Command::new("powershell")
                .args(["-NoProfile", "-Command", "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.FolderBrowserDialog; if ($d.ShowDialog() -eq 'OK') { $d.SelectedPath }"])
                .output()
                .ok()?
        } else {
            std::process::Command::new("zenity").args(["--file-selection", "--directory"]).output().ok()?
        };
        let s = String::from_utf8_lossy(&out.stdout).trim().trim_end_matches('/').to_string();
        (out.status.success() && !s.is_empty()).then_some(s)
    }

    // ---- per frame ---------------------------------------------------------------------------

    pub(crate) fn edit_tick(&mut self, ctx: &egui::Context) {
        let cmd = ctx.input(|i| i.modifiers.command);
        let typing = ctx.egui_wants_keyboard_input();
        if cmd && !typing {
            let (z, s, shift) = ctx.input(|i| (i.key_pressed(egui::Key::Z), i.key_pressed(egui::Key::S), i.modifiers.shift));
            if z {
                self.edit_undo(shift);
            }
            if s {
                self.edit_save();
            }
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Escape)) {
            self.level.edit.pick_link = None;
        }
        // the base world's lists, once a world is on show
        let world = self.world.status["world"].as_str().unwrap_or("").to_string();
        if self.level.edit.proj.is_some() && !world.is_empty() && self.level.edit.base_world != world && !self.shows_level_world() {
            // another world: none of its records are the level's
            self.level.edit.base_world = world.clone();
            self.level.edit.base.clear();
            self.level.edit.start_problems.clear();
        }
        if self.level.edit.proj.is_some() && !world.is_empty() && self.level.edit.base_world != world {
            self.level.edit.base_world = world;
            self.level.edit.base.clear();
            for (t, _) in BASE_TABLES {
                self.request("records", json!({"table": t}), Pending::Records(t.to_string()));
            }
            let f = self.testdrive_scratch_dir().join("spawn-box.json");
            let _ = std::fs::create_dir_all(f.parent().unwrap());
            self.request("spawnbox", json!({"path": f}), Pending::SpawnBox(f));
            self.level.edit.checked_generation = 0; // check and apply again on the new world
        }
        self.edit_check(ctx);
        self.edit_marks();
        // --level-select, once what it names is there
        if let Some(sel) = self.args.level_select.clone() {
            let s = if let Some(n) = sel.strip_prefix("trigger:") {
                Some(Sel::Trigger(n.into()))
            } else if let Some(r) = sel.strip_prefix("own:") {
                r.split_once('/').map(|(k, n)| Sel::Own(k.into(), n.into()))
            } else if let Some(path) = sel.strip_prefix("base:") {
                (!self.level.edit.base.is_empty()).then(|| Sel::Base(table_of(path).into(), path.into()))
            } else {
                Some(Sel::Settings)
            };
            if let (Some(s), true) = (s, self.level.edit.proj.is_some()) {
                self.args.level_select = None;
                self.edit_select(s);
            }
        }
    }

    fn edit_undo(&mut self, redo: bool) {
        if let Some(p) = self.level.edit.proj.as_mut() {
            let done = if redo { p.redo() } else { p.undo() };
            if done {
                self.level.edit.generation += 1;
                self.level.edit.saved_note = if redo { "redone".into() } else { "undone".into() };
                if let Some(Sel::Base(_, path)) = self.level.edit.sel.clone() {
                    self.request("record", json!({"path": path}), Pending::Record(path));
                }
            }
        }
    }

    pub(crate) fn edit_save(&mut self) {
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        self.level.edit.saved_note = match p.save() {
            Ok(files) if files.is_empty() => "nothing to save".into(),
            Ok(files) => format!("saved {}", files.iter().map(|f| f.file_name().unwrap_or_default().to_string_lossy().to_string()).collect::<Vec<_>>().join(", ")),
            Err(e) => format!("not saved: {e}"),
        };
    }

    /// An edit happened: the undo point (one per burst of changes) and a check soon.
    fn edited(&mut self, before: hwvx_browser::level_model::Snapshot) {
        let e = &mut self.level.edit;
        let now = Instant::now();
        if let Some(p) = e.proj.as_mut() {
            if e.last_change.map(|t| now.duration_since(t) > Duration::from_millis(600)).unwrap_or(true) {
                if p.undo.last() != Some(&before) {
                    p.undo.push(before);
                }
                p.redo.clear();
            }
        }
        e.last_change = Some(now);
        e.generation += 1;
    }

    /// The project as it is: checked (xdl_level.py compile --json on a copy) a quarter of a second
    /// after the last change, and when it compiles, applied to the world shown.
    fn edit_check(&mut self, ctx: &egui::Context) {
        if let Some((g, rx)) = self.level.edit.checking.take() {
            match rx.try_recv() {
                Err(TryRecvError::Empty) => {
                    self.level.edit.checking = Some((g, rx));
                    ctx.request_repaint_after(Duration::from_millis(30));
                    return;
                }
                Err(TryRecvError::Disconnected) => {}
                Ok((res, path, ms)) => {
                    self.level.edit.checked_generation = g;
                    match res {
                        Ok(warnings) => {
                            self.level.edit.errors.clear();
                            self.level.edit.warnings = warnings;
                            self.start_check();
                            self.level.edit.preview_note = format!("checked in {ms:.0} ms");
                            if self.level.edit.preview && self.world.status["world"].is_string() {
                                if self.shows_level_world() {
                                    self.request("level", json!({"path": path, "overlay": false}), Pending::LevelApplied);
                                } else {
                                    self.level.edit.preview_note += " · no preview: the world shown isn't the level's";
                                }
                            }
                        }
                        Err(errs) => {
                            self.level.edit.preview_note = format!("{} problem{} (the preview keeps the last good one)", errs.len(), if errs.len() == 1 { "" } else { "s" });
                            self.level.edit.errors = errs;
                        }
                    }
                }
            }
        }
        let e = &self.level.edit;
        let Some(p) = e.proj.as_ref() else { return };
        if e.checked_generation == e.generation || e.last_change.map(|t| t.elapsed() < Duration::from_millis(250)).unwrap_or(false) {
            if e.checked_generation != e.generation {
                ctx.request_repaint_after(Duration::from_millis(60));
            }
            return;
        }
        let scratch = self.testdrive_scratch_dir();
        let copy = scratch.join("edit-copy");
        let out = scratch.join("preview").join("level.json");
        if let Err(err) = p.write_copy(&copy) {
            self.level.edit.preview_note = err;
            self.level.edit.checked_generation = self.level.edit.generation;
            return;
        }
        let game = self.args.game.clone().unwrap_or_else(hwvx_browser::client::default_game);
        let tools = self.args.tools.clone().or_else(|| default_tools(&game)).unwrap_or_else(|| "tools".into());
        let data = self.args.data.clone().or_else(|| self.hello["data"].as_str().map(PathBuf::from));
        let cache = scratch.join("base-cache");
        let box_file = self.level.edit.box_file.clone();
        let g = e.generation;
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let t0 = Instant::now();
            let _ = std::fs::create_dir_all(out.parent().unwrap());
            let mut c = std::process::Command::new(std::env::var("HWVX_PYTHON").unwrap_or_else(|_| "python3".into()));
            c.arg(tools.join("xdl_level.py")).arg("compile").arg(&copy).arg("--json").arg("-o").arg(&out);
            if let Some(d) = &data {
                c.arg("--data").arg(d).arg("--cache").arg(&cache);
            }
            if let Some(b) = &box_file {
                c.arg("--spawn-box").arg(b);
            }
            let res = match c.output() {
                Ok(o) => {
                    let text = String::from_utf8_lossy(&o.stdout).to_string();
                    match text.lines().rev().find_map(|l| serde_json::from_str::<Value>(l).ok()) {
                        Some(v) if v["ok"] == true => Ok(records(&v["warnings"])),
                        Some(v) => Err(v["errors"]
                            .as_array()
                            .map(|a| {
                                a.iter()
                                    .map(|e| LevelError {
                                        file: e["file"].as_str().unwrap_or("").into(),
                                        path: e["path"].as_str().unwrap_or("").into(),
                                        message: e["message"].as_str().unwrap_or("").into(),
                                    })
                                    .collect()
                            })
                            .unwrap_or_default()),
                        None => Err(vec![LevelError { message: format!("xdl_level.py: {}{}", text.trim(), String::from_utf8_lossy(&o.stderr).trim()), ..Default::default() }]),
                    }
                }
                Err(err) => Err(vec![LevelError { message: format!("python3: {err}"), ..Default::default() }]),
            };
            let _ = tx.send((res, out, t0.elapsed().as_secs_f64() * 1000.0));
        });
        self.level.edit.checking = Some((g, rx));
    }

    /// What the world shows for the editor: the level's own volumes, the selection, its gizmo.
    fn edit_marks(&mut self) {
        if !self.world.status["world"].is_string() || self.viewer.is_none() {
            return;
        }
        let mut marks = Vec::new();
        let editing = self.level.tab && self.level.edit.proj.is_some();
        if editing {
            let p = self.level.edit.proj.as_ref().unwrap();
            for (name, v) in p.own("starts") {
                if let Some(c) = vec3(&v["pos"]) {
                    let sel = self.level.edit.sel == Some(Sel::Own("starts".into(), name.clone()));
                    marks.push(json!({"kind": "start", "pos": c, "rotY": v["rotY"].as_f64().unwrap_or(0.0),
                                      "color": if sel { json!([255, 240, 40]) } else { json!([80, 220, 255]) }}));
                }
            }
            for (name, v) in p.own("fxPoints") {
                if let Some(c) = vec3(&v["pos"]) {
                    let sel = self.level.edit.sel == Some(Sel::Own("fxPoints".into(), name.clone()));
                    marks.push(json!({"kind": "sphere", "pos": c, "radius": 2.0, "color": if sel { json!([255, 240, 40]) } else { json!([255, 160, 40]) }}));
                }
            }
            for (name, v) in p.own("items") {
                if let Some(c) = vec3(&v["pos"]) {
                    let sel = self.level.edit.sel == Some(Sel::Own("items".into(), name.clone()));
                    marks.push(json!({"kind": "cross", "pos": c, "radius": 4.0, "color": if sel { json!([255, 240, 40]) } else { json!([80, 220, 255]) }}));
                }
            }
            for (name, v) in p.own("volumes") {
                if let (Some(lo), Some(hi)) = (vec3(&v["box"]["min"]), vec3(&v["box"]["max"])) {
                    let sel = self.level.edit.sel == Some(Sel::Own("volumes".into(), name.clone()));
                    let col = if sel { json!([255, 240, 40]) } else { json!([255, 80, 220]) };
                    for (a, b) in box_edges(lo, hi) {
                        marks.push(json!({"kind": "line", "pos": a, "to": b, "color": col}));
                    }
                    continue;
                }
                if let Some(c) = vec3(&v["center"]) {
                    let r = v["radius"].as_f64().or_else(|| v["radiusSq"].as_f64().map(f64::sqrt)).unwrap_or(5.0);
                    let sel = self.level.edit.sel == Some(Sel::Own("volumes".into(), name.clone()));
                    marks.push(json!({"kind": "sphere", "pos": c, "radius": r, "color": if sel { json!([255, 240, 40]) } else { json!([255, 80, 220]) }}));
                }
            }
            // every start as the cars' box (the viewer's spawnbox), facing its way
            let paths: Vec<String> = self.level.edit.base.get("tileCars").map(|l| l.iter().filter_map(|e| e["path"].as_str().map(str::to_string)).collect()).unwrap_or_default();
            for path in paths {
                if let Some((pos, rot)) = self.base_place("tileCars", &path) {
                    let sel = self.level.edit.sel == Some(Sel::Base("tileCars".into(), path.clone()));
                    let bad = self.level.edit.start_problems.get(&path).map(|p| !p.is_empty()).unwrap_or(false);
                    let col = if sel { json!([255, 240, 40]) } else if bad { json!([255, 120, 40]) } else { json!([60, 255, 90]) };
                    marks.push(json!({"kind": "start", "pos": pos, "rotY": rot.unwrap_or(0.0), "color": col}));
                }
            }
            if let Some((pos, _rot)) = self.sel_place() {
                let s = &self.level.edit.sel;
                let yellow = json!([255, 240, 40]);
                match s {
                    Some(Sel::Base(t, _)) if t == "tileCars" => {}
                    Some(Sel::Base(t, path)) if t == "volumes" => {
                        let r = self.base_entry(path.as_str()).and_then(|e| e["radius"].as_f64()).unwrap_or(5.0);
                        marks.push(json!({"kind": "sphere", "pos": pos, "radius": r, "color": yellow}));
                    }
                    Some(Sel::Base(_, _)) => marks.push(json!({"kind": "cross", "pos": pos, "radius": 4.0, "color": yellow})),
                    _ => {}
                }
                // the gizmo: x red, y green, z blue
                for (k, col) in [(0, [255, 60, 60]), (1, [60, 255, 60]), (2, [80, 120, 255])] {
                    let mut to = pos;
                    to[k] += 15.0;
                    marks.push(json!({"kind": "line", "pos": pos, "to": to, "color": col}));
                }
            }
        }
        let m = Value::Array(marks);
        if m != self.level.edit.marks_sent {
            self.level.edit.marks_sent = m.clone();
            self.request("marks", json!({"marks": m}), Pending::Quiet);
        }
    }

    fn base_entry(&self, path: &str) -> Option<&Value> {
        self.level.edit.base.get(table_of(path))?.iter().find(|e| e["path"] == path)
    }

    /// The selection's place (and a start's rotY), as edited.
    pub(crate) fn sel_place(&self) -> Option<([f32; 3], Option<f64>)> {
        let p = self.level.edit.proj.as_ref()?;
        match self.level.edit.sel.as_ref()? {
            Sel::Own(k, n) if k == "volumes" => own_volume_center(&p.level["volumes"][n]).map(|c| (c, None)),
            Sel::Own(k, n) if k == "starts" => vec3(&p.level["starts"][n]["pos"]).map(|c| (c, Some(p.level["starts"][n]["rotY"].as_f64().unwrap_or(0.0)))),
            Sel::Own(k, n) if k == "items" || k == "fxPoints" => vec3(&p.level[k.as_str()][n]["pos"]).map(|c| (c, None)),
            Sel::Base(t, path) => self.base_place(t, path),
            _ => None,
        }
    }

    /// A base record's place (and a start's rotY), as the level's patches put it.
    pub(crate) fn base_place(&self, t: &str, path: &str) -> Option<([f32; 3], Option<f64>)> {
        let p = self.level.edit.proj.as_ref()?;
        let e = self.base_entry(path)?;
        let mut c = vec3(&e["pos"])?;
        let f = pos_field(t);
        for (i, a) in ["x", "y", "z"].iter().enumerate() {
            if let Some(v) = p.patch(path, &format!("{f}/{a}")).and_then(Value::as_f64) {
                c[i] = v as f32;
            }
        }
        let rot = (t == "tileCars").then(|| p.patch(path, "rotY").and_then(Value::as_f64).or_else(|| e["rotY"].as_f64()).unwrap_or(0.0));
        Some((c, rot))
    }

    /// Every start against the world's collision (the viewer's startcheck), as the level has them.
    fn start_check(&mut self) {
        let mut paths: Vec<String> = self.level.edit.base.get("tileCars").map(|l| l.iter().filter_map(|e| e["path"].as_str().map(str::to_string)).collect()).unwrap_or_default();
        let mut starts: Vec<Value> = paths.iter().filter_map(|p| self.base_place("tileCars", p)).map(|(c, r)| json!({"pos": c, "rotY": r.unwrap_or(0.0)})).collect();
        if starts.len() != paths.len() {
            return;
        }
        if let Some(p) = self.level.edit.proj.as_ref() {
            for (n, v) in p.own("starts") {
                if let Some(c) = vec3(&v["pos"]) {
                    paths.push(format!("level://starts/{n}"));
                    starts.push(json!({"pos": c, "rotY": v["rotY"].as_f64().unwrap_or(0.0)}));
                }
            }
        }
        if !starts.is_empty() && self.shows_level_world() {
            self.request("startcheck", json!({"starts": starts}), Pending::StartCheck(paths));
        }
    }

    pub(crate) fn start_check_reply(&mut self, paths: Vec<String>, r: Value) {
        let mut m = HashMap::new();
        for (p, s) in paths.iter().zip(r["starts"].as_array().cloned().unwrap_or_default()) {
            m.insert(p.clone(), s["problems"].as_str().unwrap_or("").to_string());
        }
        self.level.edit.start_problems = m;
    }

    /// "To the ground" for a start: its box's bottom on the highest ground under it.
    pub(crate) fn start_seat_reply(&mut self, r: Value) {
        let seat = r["starts"][0]["seat"].as_f64();
        if let (Some(seat), Some((c, rot))) = (seat, self.sel_place()) {
            self.set_place([c[0], seat as f32, c[2]], rot);
        }
    }

    /// Moves the selection to `c` (and turns a start to rot).
    fn set_place(&mut self, c: [f32; 3], rot: Option<f64>) {
        let Some(sel) = self.level.edit.sel.clone() else { return };
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        match &sel {
            Sel::Own(k, n) if k == "volumes" => {
                let v = &mut p.level["volumes"][n.as_str()];
                if let (Some(lo), Some(hi)) = (vec3(&v["box"]["min"]), vec3(&v["box"]["max"])) {
                    // a box: moved whole (its bottom where the centre goes, less half its height)
                    let h = [(hi[0] - lo[0]) / 2.0, (hi[1] - lo[1]) / 2.0, (hi[2] - lo[2]) / 2.0];
                    v["box"] = json!({"min": [round(c[0] - h[0]), round(c[1] - h[1]), round(c[2] - h[2])],
                                      "max": [round(c[0] + h[0]), round(c[1] + h[1]), round(c[2] + h[2])]});
                } else {
                    v["center"] = json!([round(c[0]), round(c[1]), round(c[2])]);
                }
            }
            Sel::Own(k, n) if k == "starts" || k == "items" || k == "fxPoints" => {
                let v = &mut p.level[k.as_str()][n.as_str()];
                v["pos"] = json!([round(c[0]), round(c[1]), round(c[2])]);
                if let (Some(r), true) = (rot, k == "starts") {
                    v["rotY"] = json!((r * 10000.0).round() / 10000.0);
                }
                if k == "items" || k == "fxPoints" {
                    let key = format!("{k}/{n}");
                    self.edited(before);
                    self.request("quadat", json!({"pos": c}), Pending::ItemQuad(key));
                    return;
                }
            }
            Sel::Base(t, path) => {
                let f = pos_field(t);
                for (i, a) in ["x", "y", "z"].iter().enumerate() {
                    p.set_patch(path, &format!("{f}/{a}"), Some(json!(round(c[i]))));
                }
                if let (Some(r), true) = (rot, t == "tileCars") {
                    p.set_patch(path, "rotY", Some(json!((r * 10000.0).round() / 10000.0)));
                }
            }
            _ => return,
        }
        self.edited(before);
    }

    // ---- adding and taking out --------------------------------------------------------------------

    /// "+ Add" of something placed: on the ground under the camera (fly: below it; orbit: what it
    /// looks at), then made.
    pub(crate) fn add_placed(&mut self, kind: AddKind) {
        let c = &self.camera;
        let p = if c["mode"].as_str() == Some("fly") { &c["pos"] } else { &c["target"] };
        let Some(at) = vec3(p) else {
            self.note("load the level's world to place things in it".into());
            return;
        };
        self.request("raycast", json!({"from": [at[0], at[1] + 50.0, at[2]], "to": [at[0], at[1] - 5000.0, at[2]]}), Pending::AddAt(kind));
    }

    pub(crate) fn add_at_reply(&mut self, kind: AddKind, r: Value) {
        let Some(g) = vec3(&r["point"]).filter(|_| r["hit"] == true) else {
            self.note("no ground under the camera".into());
            return;
        };
        if kind == AddKind::Item || kind == AddKind::FxPoint {
            self.request("quadat", json!({"pos": g}), Pending::AddItem(g, kind));
            return;
        }
        let yaw = self.camera["yaw"].as_f64().unwrap_or(0.0).to_radians();
        let mut rot = yaw + std::f64::consts::PI;
        if rot > std::f64::consts::PI {
            rot -= std::f64::consts::TAU;
        }
        let lift = -self.level.edit.spawn_box["min"][1].as_f64().unwrap_or(0.0) as f32;
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        let (k, name, v) = match kind {
            AddKind::Sphere => ("volumes", p.free_name("volumes", "volume"), json!({"center": [round(g[0]), round(g[1] + 5.0), round(g[2])], "radius": 30})),
            AddKind::BoxVolume => ("volumes", p.free_name("volumes", "box"), json!({"box": {"min": [round(g[0] - 20.0), round(g[1]), round(g[2] - 20.0)], "max": [round(g[0] + 20.0), round(g[1] + 25.0), round(g[2] + 20.0)]}})),
            AddKind::Start => ("starts", p.free_name("starts", "start"), json!({"pos": [round(g[0]), round(g[1] + lift), round(g[2])], "rotY": (rot * 10000.0).round() / 10000.0, "port": 0})),
            AddKind::Item | AddKind::FxPoint => return,
        };
        set_own(p, k, &name, v);
        self.edited(before);
        self.edit_select(Sel::Own(k.into(), name));
    }

    pub(crate) fn add_item_reply(&mut self, g: [f32; 3], kind: AddKind, r: Value) {
        let q = r["quad"].as_i64().unwrap_or(-1);
        // an effect point: a copy of the base one selected (its emitter, its corona)
        let template = match (&self.level.edit.sel, self.level.edit.proj.as_ref()) {
            (Some(Sel::Base(t, path)), Some(p)) if t == "fxPoints" => Some(format!("{}{path}", p.base_prefix())),
            _ => None,
        };
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        let (k, name, v) = if kind == AddKind::FxPoint {
            let Some(t) = template else {
                self.note("select a base effect point first (Effect points list, or click one): the new one copies it".into());
                return;
            };
            ("fxPoints", p.free_name("fxPoints", "fx"), json!({"pos": [round(g[0]), round(g[1] + 3.0), round(g[2])], "quad": q, "template": t}))
        } else {
            ("items", p.free_name("items", "item"), json!({"pos": [round(g[0]), round(g[1] + 2.0), round(g[2])], "quad": q, "id": 0}))
        };
        set_own(p, k, &name, v);
        self.edited(before);
        if q < 0 {
            self.note("that place is in no quadrant: move it onto the track".into());
        }
        self.edit_select(Sel::Own(k.into(), name));
    }

    pub(crate) fn item_quad_reply(&mut self, key: &str, r: Value) {
        let Some((kind, name)) = key.split_once('/') else { return };
        if let (Some(q), Some(p)) = (r["quad"].as_i64(), self.level.edit.proj.as_mut()) {
            if p.level[kind][name].is_object() && p.level[kind][name]["quad"] != q {
                p.level[kind][name]["quad"] = json!(q);
                self.level.edit.generation += 1;
            }
        }
    }

    /// "+ Add" of a record that isn't placed.
    pub(crate) fn add_plain(&mut self, kind: &str) {
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        let (name, v) = match kind {
            "timers" => (p.free_name("timers", "timer"), json!({"time": 60000, "countUp": 0, "type": 12})),
            "variables" => (p.free_name("variables", "var"), json!(0)),
            "objectives" => {
                let text = p.own("strings").first().map(|(n, _)| format!("level://strings/{n}")).unwrap_or_default();
                (p.free_name("objectives", "objective"), json!({"text": text, "kind": "none", "hud": 0, "shown": true}))
            }
            _ => (p.free_name("strings", "text"), json!("A message")),
        };
        set_own(p, kind, &name, v);
        self.edited(before);
        self.edit_select(Sel::Own(kind.into(), name));
    }

    /// A new trigger of a kind, its parameters as the kind's layout has them (zero, empty).
    pub(crate) fn add_trigger(&mut self, kind: &str) {
        let schema = self.schema("Trigger");
        let layout = schema["x-variants"]["TriggerType"][kind].clone();
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        let name = p.free_name("triggers", "trigger");
        let mut t = json!({"$schema": "../../../../schemas/Trigger.schema.json", "format_version": 1, "type": kind});
        if layout.as_array().map(|a| !a.is_empty()).unwrap_or(false) {
            t["param"] = default_params(&layout);
        }
        p.add_trigger(&name, t);
        self.edited(before);
        self.edit_select(Sel::Trigger(name));
    }

    /// The selection taken out of the level (links to it then show as problems).
    pub(crate) fn delete_selected(&mut self) {
        let Some(sel) = self.level.edit.sel.clone() else { return };
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        match &sel {
            Sel::Own(k, n) => {
                if let Some(m) = p.level.get_mut(k.as_str()).and_then(Value::as_object_mut) {
                    m.remove(n);
                }
            }
            Sel::Trigger(n) => p.remove_trigger(n),
            _ => return,
        }
        self.edited(before);
        self.level.edit.sel = Some(Sel::Settings);
    }

    /// The level's links to the selection (what deleting it would break).
    pub(crate) fn uses_of(&self, link: &str) -> Vec<String> {
        let Some(p) = self.level.edit.proj.as_ref() else { return Vec::new() };
        let mut out = Vec::new();
        for t in &p.triggers {
            if t.value.to_string().contains(&format!("\"{link}\"")) || t.value.to_string().contains(&format!("\"{link}/")) {
                out.push(format!("trigger {}", t.name));
            }
        }
        out
    }

    // ---- the view's input -----------------------------------------------------------------------

    /// A click in the view while editing: what is there (or a link's target).
    pub(crate) fn edit_pick(&mut self, x: f32, y: f32) {
        self.request("pick", json!({"x": x, "y": y}), Pending::EditPick);
    }

    pub(crate) fn edit_pick_reply(&mut self, r: Value) {
        // the level's own volumes against the same ray, nearer than the base object
        let o = vec3(&r["origin"]);
        let d = vec3(&r["dir"]);
        let mut best: Option<(f32, Sel)> = r["path"].as_str().map(|p| (r["t"].as_f64().unwrap_or(1e9) as f32, Sel::Base(table_of(p).to_string(), p.to_string())));
        if let (Some(o), Some(d), Some(p)) = (o, d, self.level.edit.proj.as_ref()) {
            let mut own: Vec<(String, String, [f32; 3], f32)> = Vec::new();
            for (name, v) in p.own("volumes") {
                if let Some(c) = own_volume_center(&v) {
                    let r = v["radius"].as_f64().or_else(|| v["radiusSq"].as_f64().map(f64::sqrt)).unwrap_or(10.0) as f32;
                    own.push(("volumes".into(), name, c, r));
                }
            }
            for (k, r) in [("starts", 10.0f32), ("items", 4.0), ("fxPoints", 3.0)] {
                for (name, v) in p.own(k) {
                    if let Some(c) = vec3(&v["pos"]) {
                        own.push((k.into(), name, [c[0], c[1] + if k == "starts" { 2.5 } else { 0.0 }, c[2]], r));
                    }
                }
            }
            for (kind, name, c, r) in own {
                {
                    let w = [c[0] - o[0], c[1] - o[1], c[2] - o[2]];
                    let tc = w[0] * d[0] + w[1] * d[1] + w[2] * d[2];
                    let d2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2] - tc * tc;
                    if tc + r >= 0.0 && d2 <= r * r && best.as_ref().map(|b| tc < b.0).unwrap_or(true) {
                        best = Some((tc.max(0.0), Sel::Own(kind, name)));
                    }
                }
            }
        }
        let Some((_, sel)) = best else {
            self.note("nothing there".into());
            return;
        };
        if let Some((trig, path)) = self.level.edit.pick_link.take() {
            let link = match &sel {
                Sel::Own(k, n) => format!("level://{k}/{n}"),
                Sel::Base(_, p) => format!("{}{p}", self.level.edit.proj.as_ref().unwrap().base_prefix()),
                _ => return,
            };
            self.set_link(&trig, &path, link);
            return;
        }
        self.edit_select(sel);
    }

    pub(crate) fn edit_select(&mut self, sel: Sel) {
        if let Sel::Base(_, path) = &sel {
            self.request("record", json!({"path": path}), Pending::Record(path.clone()));
        }
        self.level.edit.record = Value::Null;
        self.level.edit.sel = Some(sel);
    }

    fn set_link(&mut self, trig: &str, path: &str, link: String) {
        let Some(p) = self.level.edit.proj.as_mut() else { return };
        let before = p.snapshot();
        let ptr = format!("/{path}");
        let done = if trig.is_empty() {
            p.level.pointer_mut(&ptr).map(|v| *v = Value::String(link.clone())).is_some()
        } else {
            p.trigger_mut(trig).and_then(|t| t.value.pointer_mut(&ptr)).map(|v| *v = Value::String(link.clone())).is_some()
        };
        if done {
            self.note(format!("linked {link}"));
            self.edited(before);
        }
    }

    /// Ctrl/Cmd + drag in the view: the selection follows the ground under the pointer.
    pub(crate) fn edit_drag(&mut self, x: f32, y: f32, started: bool) {
        if self.sel_place().is_none() {
            return;
        }
        if started {
            // keep a volume's height above the ground as it was
            self.level.edit.move_offset = f32::NAN;
        }
        if !self.level.edit.move_inflight {
            self.level.edit.move_inflight = true;
            self.request("raycast", json!({"x": x, "y": y}), Pending::EditMove);
        }
    }

    pub(crate) fn edit_move_reply(&mut self, r: Option<Value>) {
        self.level.edit.move_inflight = false;
        let Some(hit) = r.as_ref().filter(|r| r["hit"] == true).and_then(|r| vec3(&r["point"])) else { return };
        let Some((cur, rot)) = self.sel_place() else { return };
        let lift = match self.level.edit.sel {
            Some(Sel::Own(_, _)) | Some(Sel::Base(_, _)) if matches!(self.level.edit.sel, Some(Sel::Own(_, _))) || matches!(&self.level.edit.sel, Some(Sel::Base(t, _)) if t == "volumes") => {
                if self.level.edit.move_offset.is_nan() {
                    self.level.edit.move_offset = 0.0f32.max(cur[1] - hit[1]).min(200.0);
                }
                self.level.edit.move_offset
            }
            Some(Sel::Base(ref t, _)) if t == "tileCars" => 2.0,
            _ => 0.0,
        };
        self.set_place([hit[0], hit[1] + lift, hit[2]], rot);
    }

    /// Ctrl/Cmd + wheel: a start turns (15 degrees a notch).
    pub(crate) fn edit_rotate(&mut self, notches: f32) {
        if let Some((c, Some(rot))) = self.sel_place() {
            let mut r = rot + (notches as f64) * 15f64.to_radians();
            while r > std::f64::consts::PI {
                r -= std::f64::consts::TAU;
            }
            while r < -std::f64::consts::PI {
                r += std::f64::consts::TAU;
            }
            self.set_place(c, Some(r));
        }
    }

    /// "To the ground": straight down from the selection.
    fn snap_down(&mut self) {
        if let (Some((c, Some(rot))), Some(sel)) = (self.sel_place(), self.level.edit.sel.clone()) {
            if matches!(&sel, Sel::Base(t, _) if t == "tileCars") || matches!(&sel, Sel::Own(k, _) if k == "starts") {
                self.request("startcheck", json!({"starts": [{"pos": c, "rotY": rot}]}), Pending::StartSeat);
                return;
            }
        }
        if let Some((c, _)) = self.sel_place() {
            self.level.edit.move_offset = 0.0;
            self.request("raycast", json!({"from": [c[0], c[1] + 30.0, c[2]], "to": [c[0], c[1] - 3000.0, c[2]]}), Pending::EditMove);
            self.level.edit.move_inflight = true;
        }
    }

    // ---- the lists --------------------------------------------------------------------------------

    pub(crate) fn edit_lists(&mut self, ui: &mut egui::Ui) {
        if self.level.edit.proj.is_some() {
            // + Add: placed things go on the ground under the camera
            let placed = self.shows_level_world();
            let kinds: Vec<String> = self.schema("Trigger")["properties"]["type"]["x-enum"]
                .as_object()
                .map(|m| m.values().filter_map(|x| x.as_str().map(str::to_string)).collect())
                .unwrap_or_default();
            let mut act: Option<Box<dyn FnOnce(&mut Browser)>> = None;
            ui.horizontal(|ui| {
                ui.menu_button("+ Add", |ui| {
                    ui.add_enabled_ui(placed, |ui| {
                        if ui.button("Sphere volume, here").clicked() {
                            act = Some(Box::new(|b| b.add_placed(AddKind::Sphere)));
                        }
                        if ui.button("Box volume (BSP), here").clicked() {
                            act = Some(Box::new(|b| b.add_placed(AddKind::BoxVolume)));
                        }
                        if ui.button("Start, here").clicked() {
                            act = Some(Box::new(|b| b.add_placed(AddKind::Start)));
                        }
                        if ui.button("Item, here").clicked() {
                            act = Some(Box::new(|b| b.add_placed(AddKind::Item)));
                        }
                        if ui.button("Effect point, here (a copy of the selected one)").clicked() {
                            act = Some(Box::new(|b| b.add_placed(AddKind::FxPoint)));
                        }
                    });
                    if !placed {
                        ui.label(egui::RichText::new("(load the level's world to place things)").small().weak());
                    }
                    ui.separator();
                    for (k, label) in [("timers", "Timer"), ("variables", "Variable"), ("strings", "Text"), ("objectives", "Objective")] {
                        if ui.button(label).clicked() {
                            act = Some(Box::new(move |b| b.add_plain(k)));
                        }
                    }
                    ui.menu_button("Trigger", |ui| {
                        egui::ScrollArea::vertical().max_height(320.0).show(ui, |ui| {
                            for k in &kinds {
                                if ui.button(k).clicked() {
                                    let k = k.clone();
                                    act = Some(Box::new(move |b| b.add_trigger(&k)));
                                }
                            }
                        });
                    });
                });
            });
            if let Some(f) = act {
                f(self);
            }
        }
        let Some(p) = self.level.edit.proj.as_ref() else {
            if let Some(e) = &self.level.edit.load_error {
                ui.colored_label(egui::Color32::LIGHT_RED, e);
            }
            return;
        };
        let errs = self.level.edit.errors.clone();
        let has_err = |file: &str, path: &str| errs.iter().any(|e| e.file == file && (path.is_empty() || e.path == path || e.path.starts_with(&format!("{path}/"))));
        let mut pick: Option<Sel> = None;
        let sel = self.level.edit.sel.clone();
        let label = |ui: &mut egui::Ui, text: String, me: Sel, bad: bool, pick: &mut Option<Sel>| {
            let t = if bad { egui::RichText::new(format!("⚠ {text}")).color(egui::Color32::LIGHT_RED) } else { egui::RichText::new(text) };
            if ui.selectable_label(sel.as_ref() == Some(&me), t).clicked() {
                *pick = Some(me);
            }
        };
        ui.horizontal(|ui| {
            ui.label("Filter");
            ui.text_edit_singleline(&mut self.level.edit.filter);
        });
        let filter = self.level.edit.filter.to_lowercase();
        let keep = |s: &str| filter.is_empty() || s.to_lowercase().contains(&filter);
        egui::ScrollArea::vertical().id_salt("level_lists").auto_shrink([false, false]).show(ui, |ui| {
            label(ui, "Level settings".into(), Sel::Settings, has_err("level.jsonc", "") && errs.iter().any(|e| e.file == "level.jsonc" && !e.path.contains('/')), &mut pick);
            egui::CollapsingHeader::new(format!("Triggers ({})", p.triggers.len())).default_open(true).show(ui, |ui| {
                for t in &p.triggers {
                    if keep(&t.name) {
                        let kind = t.value["type"].as_str().unwrap_or("?");
                        label(ui, format!("{}  ·  {kind}", t.name), Sel::Trigger(t.name.clone()), has_err(&format!("triggers/{}.jsonc", t.name), ""), &mut pick);
                    }
                }
            });
            for (k, title) in OWN_KINDS {
                let own = p.own(k);
                egui::CollapsingHeader::new(format!("{title} ({})", own.len())).default_open(k == "volumes").show(ui, |ui| {
                    for (n, _) in own {
                        if keep(&n) {
                            label(ui, n.clone(), Sel::Own(k.into(), n.clone()), has_err("level.jsonc", &format!("{k}/{n}")), &mut pick);
                        }
                    }
                });
            }
            ui.separator();
            ui.label(egui::RichText::new("The base world").strong());
            if self.level.edit.base.is_empty() {
                ui.label(egui::RichText::new("load the level's world (Worlds tab) to list and pick its records").weak());
            }
            for (t, title) in BASE_TABLES {
                let Some(list) = self.level.edit.base.get(t) else { continue };
                // airboxes: only the spheres (the BSP ones can't be moved)
                let rows: Vec<&Value> = list.iter().filter(|e| t != "volumes" || e["shape"] == "sphere").filter(|e| keep(e["path"].as_str().unwrap_or(""))).collect();
                egui::CollapsingHeader::new(format!("{title} ({})", rows.len())).id_salt(t).show(ui, |ui| {
                    for e in rows.into_iter().take(400) {
                        let path = e["path"].as_str().unwrap_or("").to_string();
                        let patched = p.level["patches"].get(format!("{}{path}", p.base_prefix())).is_some();
                        let short = path.trim_start_matches("World/0/").to_string();
                        label(ui, format!("{short}{}", if patched { "  (patched)" } else { "" }), Sel::Base(t.into(), path), false, &mut pick);
                    }
                });
            }
        });
        if let Some(s) = pick {
            self.edit_select(s);
        }
    }

    // ---- the inspector ------------------------------------------------------------------------------

    pub(crate) fn edit_inspector(&mut self, ui: &mut egui::Ui) {
        let Some(sel) = self.level.edit.sel.clone() else {
            ui.label("Select something in the Level tab's lists, or click it in the view.");
            return;
        };
        ui.horizontal(|ui| {
            let p = self.level.edit.proj.as_ref().unwrap();
            if ui.add_enabled(!p.undo.is_empty(), egui::Button::new("↶ Undo")).on_hover_text("Ctrl/Cmd+Z").clicked() {
                self.edit_undo(false);
            }
            let p = self.level.edit.proj.as_ref().unwrap();
            if ui.add_enabled(!p.redo.is_empty(), egui::Button::new("↷ Redo")).on_hover_text("Ctrl/Cmd+Shift+Z").clicked() {
                self.edit_undo(true);
            }
            let dirty = self.level.edit.proj.as_ref().unwrap().dirty();
            if ui.add_enabled(dirty, egui::Button::new(if dirty { "💾 Save*" } else { "💾 Save" })).on_hover_text("Ctrl/Cmd+S: the changed files, their comments kept").clicked() {
                self.edit_save();
            }
            ui.checkbox(&mut self.level.edit.advanced, "all fields").on_hover_text("also the fields the game never reads (editor ids, unknown bytes)");
        });
        if !self.level.edit.saved_note.is_empty() {
            ui.label(egui::RichText::new(&self.level.edit.saved_note).small().weak());
        }
        if let Some((t, p)) = &self.level.edit.pick_link {
            ui.colored_label(egui::Color32::YELLOW, format!("click the target in the view for {t} {p} (Esc: cancel)"));
        }
        ui.separator();
        egui::ScrollArea::vertical().id_salt("inspector").auto_shrink([false, false]).show(ui, |ui| match sel {
            Sel::Settings => self.inspect_settings(ui),
            Sel::Own(k, n) => self.inspect_own(ui, &k, &n),
            Sel::Trigger(n) => self.inspect_trigger(ui, &n),
            Sel::Base(t, path) => self.inspect_base(ui, &t, &path),
        });
    }

    fn errors_for(&self, file: &str, path: &str) -> Vec<LevelError> {
        self.level.edit.errors.iter().filter(|e| e.file == file && (path.is_empty() || e.path == path || e.path.starts_with(&format!("{path}/")))).cloned().collect()
    }

    fn show_errors(ui: &mut egui::Ui, errs: &[LevelError]) {
        for e in errs {
            ui.add(egui::Label::new(egui::RichText::new(format!("⚠ {}{}{}", e.path, if e.path.is_empty() { "" } else { ": " }, e.message)).color(egui::Color32::LIGHT_RED)).wrap());
        }
    }

    fn inspect_settings(&mut self, ui: &mut egui::Ui) {
        let errs: Vec<LevelError> = self.level.edit.errors.iter().filter(|e| e.file == "level.jsonc" && !e.path.contains('/')).cloned().collect();
        Self::show_errors(ui, &errs);
        let p = self.level.edit.proj.as_mut().unwrap();
        let before = p.snapshot();
        let mut changed = false;
        egui::Grid::new("settings").num_columns(2).show(ui, |ui| {
            for k in ["name", "description", "author", "version"] {
                ui.label(k);
                let mut s = p.level[k].as_str().unwrap_or("").to_string();
                if ui.add(egui::TextEdit::singleline(&mut s).desired_width(220.0)).changed() {
                    p.level[k] = Value::String(s);
                    changed = true;
                }
                ui.end_row();
            }
            for k in ["id", "mode", "extends", "world"] {
                ui.label(k);
                ui.monospace(p.level[k].as_str().unwrap_or("?"));
                ui.end_row();
            }
            ui.label("unlocked");
            let mut u = p.level["unlocked"].as_bool().unwrap_or(true);
            if ui.checkbox(&mut u, "").changed() {
                p.level["unlocked"] = Value::Bool(u);
                changed = true;
            }
            ui.end_row();
            ui.label("base triggers");
            let cur = match &p.level["baseTriggers"] {
                Value::String(s) => s.clone(),
                Value::Array(_) => "some".into(),
                _ => "all".into(),
            };
            let mut pickv = cur.clone();
            egui::ComboBox::from_id_salt("baseTriggers").selected_text(&pickv).show_ui(ui, |ui| {
                for o in ["all", "none"] {
                    ui.selectable_value(&mut pickv, o.to_string(), o);
                }
            });
            if pickv != cur {
                p.level["baseTriggers"] = Value::String(pickv);
                changed = true;
            }
            ui.end_row();
        });
        if let Value::Array(a) = &p.level["baseTriggers"] {
            let mut text = a.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(", ");
            ui.horizontal(|ui| {
                ui.label("kept (numbers)");
                if ui.text_edit_singleline(&mut text).changed() {
                    let nums: Vec<Value> = text.split(',').filter_map(|s| s.trim().parse::<u64>().ok()).map(Value::from).collect();
                    p.level["baseTriggers"] = Value::Array(nums);
                    changed = true;
                }
            });
        }
        let patches = p.level["patches"].as_object().map(|m| m.len()).unwrap_or(0);
        ui.label(format!("{patches} base record{} patched", if patches == 1 { "" } else { "s" }));
        if changed {
            self.edited(before);
        }
    }

    fn delete_row(&mut self, ui: &mut egui::Ui, link: &str) {
        let uses = self.uses_of(link);
        ui.horizontal(|ui| {
            if ui.button("🗑 Delete").on_hover_text(if uses.is_empty() { "take it out of the level".to_string() } else { format!("used by {}: those links will show as problems", uses.join(", ")) }).clicked() {
                self.delete_selected();
            }
            if !uses.is_empty() {
                ui.label(egui::RichText::new(format!("used by {}", uses.join(", "))).small().weak());
            }
        });
    }

    fn inspect_own(&mut self, ui: &mut egui::Ui, kind: &str, name: &str) {
        ui.heading(format!("{name}"));
        ui.label(egui::RichText::new(format!("level://{kind}/{name}")).monospace().weak());
        self.delete_row(ui, &format!("level://{kind}/{name}"));
        if kind == "starts" || kind == "items" || kind == "fxPoints" {
            let errs = self.errors_for("level.jsonc", &format!("{kind}/{name}"));
            Self::show_errors(ui, &errs);
            let link = format!("level://{kind}/{name}");
            if let Some(pr) = self.level.edit.start_problems.get(&link).filter(|p| !p.is_empty()) {
                ui.add(egui::Label::new(egui::RichText::new(format!("⚠ the cars' box here: {pr}")).color(egui::Color32::from_rgb(255, 180, 60))).wrap());
            }
            for w in self.level.edit.warnings.iter().filter(|w| w.path == format!("starts/{name}")) {
                ui.add(egui::Label::new(egui::RichText::new(format!("⚠ {}", w.message)).color(egui::Color32::from_rgb(255, 180, 60))).wrap());
            }
            ui.horizontal(|ui| {
                if ui.button("To the ground").clicked() {
                    self.snap_down();
                }
                ui.label(egui::RichText::new("Ctrl/Cmd + drag moves it, Ctrl/Cmd + wheel turns a start").small().weak());
            });
            let p = self.level.edit.proj.as_mut().unwrap();
            let before = p.snapshot();
            let v = &mut p.level[kind][name];
            let mut changed = false;
            egui::Grid::new(("own_placed", kind, name)).num_columns(2).show(ui, |ui| {
                let mut c = vec3(&v["pos"]).unwrap_or([0.0; 3]);
                ui.label("pos");
                let mut cc = false;
                ui.horizontal(|ui| {
                    for (i, a) in ["x", "y", "z"].iter().enumerate() {
                        ui.label(*a);
                        cc |= ui.add(egui::DragValue::new(&mut c[i]).speed(0.5)).changed();
                    }
                });
                if cc {
                    v["pos"] = json!([round(c[0]), round(c[1]), round(c[2])]);
                    changed = true;
                }
                ui.end_row();
                if kind == "starts" {
                    ui.label("facing");
                    let mut deg = v["rotY"].as_f64().unwrap_or(0.0).to_degrees();
                    if ui.add(egui::DragValue::new(&mut deg).speed(1.0).range(-180.0..=180.0).suffix("°")).changed() {
                        v["rotY"] = json!((deg.to_radians() * 10000.0).round() / 10000.0);
                        changed = true;
                    }
                    ui.end_row();
                    ui.label("port");
                    let mut port = v["port"].as_i64().unwrap_or(0);
                    if ui.add(egui::DragValue::new(&mut port).range(0..=8)).on_hover_text("1-4: a player's start; 0: an AI car's").changed() {
                        v["port"] = json!(port);
                        changed = true;
                    }
                    ui.end_row();
                } else if kind == "fxPoints" {
                    ui.label("quadrant");
                    ui.label(v["quad"].to_string());
                    ui.end_row();
                    ui.label("a copy of");
                    ui.label(egui::RichText::new(v["template"].as_str().unwrap_or("").rsplit_once("/world_0/").map(|x| x.1).unwrap_or("")).monospace());
                    ui.end_row();
                } else {
                    ui.label("quadrant");
                    ui.label(v["quad"].to_string()).on_hover_text("the streaming region it is in (found when it moves)");
                    ui.end_row();
                    for (k, tip) in [("id", "the item in the world's item pack (0..)"), ("respawn", "ms before it comes back once taken, 0: never")] {
                        ui.label(k);
                        let mut x = v[k].as_i64().unwrap_or(0);
                        if ui.add(egui::DragValue::new(&mut x).range(0..=600000)).on_hover_text(tip).changed() {
                            v[k] = json!(x);
                            changed = true;
                        }
                        ui.end_row();
                    }
                }
            });
            if changed {
                self.edited(before);
            }
            return;
        }
        let errs = self.errors_for("level.jsonc", &format!("{kind}/{name}"));
        Self::show_errors(ui, &errs);
        let advanced = self.level.edit.advanced;
        if kind == "strings" {
            let p = self.level.edit.proj.as_mut().unwrap();
            let before = p.snapshot();
            let v = &mut p.level["strings"][name];
            let mut paras: Vec<String> = match v {
                Value::Array(a) => a.iter().map(|x| x.as_str().unwrap_or("").to_string()).collect(),
                Value::String(s) => vec![s.clone()],
                _ => vec![String::new()],
            };
            let mut changed = false;
            for (i, t) in paras.iter_mut().enumerate() {
                ui.label(format!("paragraph {}  ({} characters, at most 255)", i + 1, t.len()));
                changed |= ui.add(egui::TextEdit::multiline(t).desired_rows(3).desired_width(f32::INFINITY)).changed();
            }
            if ui.button("+ paragraph").clicked() {
                paras.push(String::new());
                changed = true;
            }
            if changed {
                *v = if paras.len() == 1 { Value::String(paras.remove(0)) } else { json!(paras) };
                self.edited(before);
            }
            ui.label(egui::RichText::new("shown by print_debug_string (what 4: the objective line of the pause, win and lose screens)").small().weak());
            return;
        }
        if kind == "objectives" {
            ui.label(egui::RichText::new("text, doneText: texts (level://strings/...); done: a variable whose value != 0 shows doneText; kind none / counter / timer and value: what fills the text's '#'; hud: the HUD object's id; shown").small().weak());
            let cands = self.link_candidates(None);
            let p = self.level.edit.proj.as_mut().unwrap();
            let before = p.snapshot();
            let mut v = p.level[kind][name].clone();
            let mut link_pick = None;
            let mut kinds = HashMap::new();
            for k in ["text", "doneText", "done", "value"] {
                kinds.insert(k.to_string(), "link".to_string());
            }
            if value_editor(ui, &mut v, "", &kinds, &[], &cands, &format!("{kind}/{name}"), &mut link_pick, 0) {
                p.level[kind][name] = v;
                self.edited(before);
            }
            if let Some(path) = link_pick {
                self.level.edit.pick_link = Some((String::new(), path));
            }
            return;
        }
        let sname = match kind {
            "volumes" => "WorldVolume",
            "timers" => "WorldTimer",
            _ => "WorldVariable",
        };
        let schema = self.schema(sname);
        let cands = self.link_candidates(None);
        if kind == "volumes" {
            ui.horizontal(|ui| {
                if ui.button("To the ground").on_hover_text("straight down onto the collision").clicked() {
                    self.snap_down();
                }
                ui.label(egui::RichText::new("Ctrl/Cmd + drag in the view moves it").small().weak());
            });
        }
        let p = self.level.edit.proj.as_mut().unwrap();
        let before = p.snapshot();
        let mut v = p.level[kind][name].clone();
        if kind == "variables" && v.is_number() {
            v = json!({"value": v});
        }
        let mut link_pick = None;
        let mut changed = fields_editor(ui, &mut v, &schema, advanced, &cands, &format!("{kind}/{name}"), &mut link_pick, kind == "volumes");
        if kind == "volumes" {
            // a box's size, and the type's settings
            if let (Some(mut lo), Some(mut hi)) = (vec3(&v["box"]["min"]), vec3(&v["box"]["max"])) {
                ui.label("box (a BSP volume)");
                let mut cc = false;
                for (lbl, c) in [("min", &mut lo), ("max", &mut hi)] {
                    ui.horizontal(|ui| {
                        ui.label(lbl);
                        for i in 0..3 {
                            cc |= ui.add(egui::DragValue::new(&mut c[i]).speed(0.5)).changed();
                        }
                    });
                }
                if cc {
                    v["box"] = json!({"min": [round(lo[0]), round(lo[1]), round(lo[2])], "max": [round(hi[0]), round(hi[1]), round(hi[2])]});
                    changed = true;
                }
            }
            let t = v["type"].as_str().unwrap_or("airbox").to_string();
            let defaults = match t.as_str() {
                "force_box" => Some(json!({"force": [0, 50, 0], "oneShot": 0, "carOnly": 0, "type": 0})),
                "reverb" => Some(json!({"type": 2, "volume": 16383, "feedback": 16383})),
                "fog" => Some(json!({"type": 0, "near": 50, "far": 400, "color": [128, 128, 128, 255]})),
                "light" => Some(json!({"direction": [0, -1, 0], "color": [255, 255, 255], "lightType": 1, "subtract": 0})),
                "creature" => Some(json!({"rescue": 0})),
                _ => None,
            };
            if let Some(d) = defaults {
                ui.separator();
                ui.label(format!("{t} settings (the game finds new ones only once the volume grid lists them: see the warning)"));
                if !v["data"].is_object() {
                    v["data"] = d;
                    changed = true;
                }
                let mut dv = v["data"].clone();
                let kinds = HashMap::new();
                if value_editor(ui, &mut dv, "", &kinds, &[], &[], &format!("{kind}/{name}/data"), &mut link_pick, 0) {
                    v["data"] = dv;
                    changed = true;
                }
            } else if v.get("data").map(|d| d.is_object()).unwrap_or(false) {
                if let Some(o) = v.as_object_mut() {
                    o.remove("data");
                }
                changed = true;
            }
        }
        if changed {
            p.level[kind][name] = v;
            self.edited(before);
        }
        if let Some(path) = link_pick {
            self.level.edit.pick_link = Some((String::new(), path));
        }
    }

    fn inspect_base(&mut self, ui: &mut egui::Ui, table: &str, path: &str) {
        ui.heading(path.trim_start_matches("World/0/"));
        let prefix = self.level.edit.proj.as_ref().unwrap().base_prefix();
        ui.label(egui::RichText::new(format!("{prefix}{path}")).monospace().weak());
        let errs = self.errors_for("level.jsonc", &format!("patches/{prefix}{path}"));
        Self::show_errors(ui, &errs);
        // warnings: the start's box against the world and the other starts
        if let Some(p) = self.level.edit.start_problems.get(path).filter(|p| !p.is_empty()) {
            ui.add(egui::Label::new(egui::RichText::new(format!("⚠ the cars' box here: {p}")).color(egui::Color32::from_rgb(255, 180, 60))).wrap());
        }
        for w in self.level.edit.warnings.iter().filter(|w| w.path.contains(path)) {
            ui.add(egui::Label::new(egui::RichText::new(format!("⚠ {}", w.message)).color(egui::Color32::from_rgb(255, 180, 60))).wrap());
        }
        if table == "tileCars" {
            if let Some(b) = self.level.edit.spawn_box.get("min") {
                let mx = &self.level.edit.spawn_box["max"];
                let size = |i: usize| mx[i].as_f64().unwrap_or(0.0) - b[i].as_f64().unwrap_or(0.0);
                ui.label(egui::RichText::new(format!("drawn and checked as every car's box at once: {:.2} wide, {:.2} high, {:.2} long ({} cars)", size(0), size(1), size(2), self.level.edit.spawn_box["cars"])).small().weak());
            }
        }
        if table == "tileCars" || table == "volumes" || table == "items" {
            ui.horizontal(|ui| {
                if ui.button("To the ground").clicked() {
                    self.snap_down();
                }
                ui.label(egui::RichText::new(if table == "tileCars" { "Ctrl/Cmd + drag moves it, Ctrl/Cmd + wheel turns it" } else { "Ctrl/Cmd + drag moves it" }).small().weak());
            });
        }
        let rec = self.level.edit.record.clone();
        if rec["path"] != path {
            ui.spinner();
            return;
        }
        if let Some(d) = rec["volumeData"].as_object() {
            ui.label(egui::RichText::new("its settings (by type; read only)").strong());
            egui::Grid::new("volume_data").num_columns(2).show(ui, |ui| {
                for (k, x) in d {
                    ui.label(k);
                    ui.monospace(x.to_string());
                    ui.end_row();
                }
            });
            ui.separator();
        }
        let sname = rec["struct"].as_str().unwrap_or("").to_string();
        let schema = self.schema(&sname);
        let advanced = self.level.edit.advanced;
        ui.label(egui::RichText::new(format!("{sname} at {} of the world pack · edits are patches: only what differs is kept", rec["at"].as_str().unwrap_or(""))).small().weak());
        let p = self.level.edit.proj.as_mut().unwrap();
        let before = p.snapshot();
        let mut changed = false;
        let props = schema["properties"].as_object().cloned().unwrap_or_default();
        egui::Grid::new("base_fields").num_columns(3).striped(true).show(ui, |ui| {
            for (k, prop) in &props {
                if prop.get("x-offset").is_none() || (!advanced && hidden_field(k)) {
                    continue;
                }
                let live = &rec["fields"][k];
                if link_of(prop).is_some() {
                    if advanced {
                        ui.label(k);
                        ui.label(egui::RichText::new(live.as_str().unwrap_or("null")).small().weak());
                        ui.label("");
                        ui.end_row();
                    }
                    continue;
                }
                // embedded structs (pos): their members, each its own patch
                if let Value::Object(members) = live {
                    for (mk, mv) in members {
                        if mk == "w" && !advanced {
                            continue;
                        }
                        let field = format!("{k}/{mk}");
                        let patched = p.patch(path, &field).cloned();
                        let mut x = patched.as_ref().and_then(Value::as_f64).unwrap_or(mv.as_f64().unwrap_or(0.0));
                        ui.label(&field);
                        if ui.add(egui::DragValue::new(&mut x).speed(0.5)).changed() {
                            p.set_patch(path, &field, Some(json!(round(x as f32))));
                            changed = true;
                        }
                        changed |= revert_button(ui, p, path, &field, patched.is_some());
                        ui.end_row();
                    }
                    continue;
                }
                if live.is_array() {
                    continue; // arrays: not patched from here
                }
                let patched = p.patch(path, k).cloned();
                let cur = patched.clone().unwrap_or_else(|| live.clone());
                ui.label(k);
                let mut nv = cur.clone();
                if scalar_widget(ui, &mut nv, prop, k) && nv != cur {
                    p.set_patch(path, k, Some(nv));
                    changed = true;
                }
                changed |= revert_button(ui, p, path, k, patched.is_some());
                ui.end_row();
            }
        });
        if changed {
            self.edited(before);
        }
    }

    fn inspect_trigger(&mut self, ui: &mut egui::Ui, name: &str) {
        ui.heading(format!("trigger {name}"));
        self.delete_row(ui, &format!("level://triggers/{name}"));
        let errs = self.errors_for(&format!("triggers/{name}.jsonc"), "");
        Self::show_errors(ui, &errs);
        let schema = self.schema("Trigger");
        let cands = self.link_candidates(Some(&schema));
        let p = self.level.edit.proj.as_mut().unwrap();
        let before = p.snapshot();
        let Some(t) = p.trigger_mut(name) else { return };
        let mut link_pick = None;
        let changed = trigger_editor(ui, &mut t.value, &schema, &cands, &mut link_pick);
        if changed {
            self.edited(before);
        }
        if let Some(path) = link_pick {
            self.level.edit.pick_link = Some((name.to_string(), path));
        }
    }

    /// What a link can name: the level's own records and the base world's listed ones.
    fn link_candidates(&self, _schema: Option<&Value>) -> Vec<(String, String)> {
        let mut out = Vec::new();
        let Some(p) = self.level.edit.proj.as_ref() else { return out };
        for (k, _) in OWN_KINDS {
            for (n, _) in p.own(k) {
                out.push((format!("{k}: {n} (level)"), format!("level://{k}/{n}")));
            }
        }
        for t in &p.triggers {
            out.push((format!("trigger: {}", t.name), format!("level://triggers/{}", t.name)));
        }
        let prefix = p.base_prefix();
        for (t, _) in BASE_TABLES {
            if let Some(list) = self.level.edit.base.get(t) {
                for e in list.iter().filter(|e| t != "volumes" || e["shape"] == "sphere").take(300) {
                    let path = e["path"].as_str().unwrap_or("");
                    out.push((path.trim_start_matches("World/0/").to_string(), format!("{prefix}{path}")));
                }
            }
        }
        out
    }
}

fn records(v: &Value) -> Vec<LevelError> {
    v.as_array()
        .map(|a| {
            a.iter()
                .map(|e| LevelError {
                    file: e["file"].as_str().unwrap_or("").into(),
                    path: e["path"].as_str().unwrap_or("").into(),
                    message: e["message"].as_str().unwrap_or("").into(),
                })
                .collect()
        })
        .unwrap_or_default()
}

fn box_edges(lo: [f32; 3], hi: [f32; 3]) -> Vec<([f32; 3], [f32; 3])> {
    let c = |i: usize| [if i & 1 != 0 { hi[0] } else { lo[0] }, if i & 2 != 0 { hi[1] } else { lo[1] }, if i & 4 != 0 { hi[2] } else { lo[2] }];
    [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)].iter().map(|&(a, b)| (c(a), c(b))).collect()
}

fn set_own(p: &mut Project, kind: &str, name: &str, v: Value) {
    if !p.level[kind].is_object() {
        p.level[kind] = json!({});
    }
    p.level[kind][name] = v;
}

/// Parameters as a kind's layout has them: lists empty, numbers 0, keywords the first, links null.
pub(crate) fn default_params(layout: &Value) -> Value {
    let mut m = serde_json::Map::new();
    for it in layout.as_array().cloned().unwrap_or_default() {
        let (Some(kind), Some(name)) = (it.get(0).and_then(Value::as_str), it.get(1).and_then(Value::as_str)) else { continue };
        let v = match kind {
            "list" | "words" => json!([]),
            "u32" | "s32" | "any" => json!(0),
            "f32" => json!(0.0),
            "token" => json!("on"),
            "link" => Value::Null,
            "operand" => json!({"const": 0}),
            "string" => json!(""),
            "codevar" => json!({"var": 0, "arg": 0}),
            _ => continue,
        };
        m.insert(name.to_string(), v);
    }
    Value::Object(m)
}

fn round(x: f32) -> f64 {
    ((x as f64) * 100.0).round() / 100.0
}

fn hidden_field(k: &str) -> bool {
    k == "editorId" || k.starts_with("unk_") || k.starts_with("pad") || k.starts_with("unused")
}

fn link_of(p: &Value) -> Option<&Value> {
    p.get("x-link").or_else(|| p["anyOf"].as_array()?.iter().find_map(|a| a.get("x-link")))
}

fn enum_names(p: &Value) -> Option<Vec<(i64, String)>> {
    let e = p.get("x-enum")?.as_object()?;
    let mut v: Vec<(i64, String)> = e.iter().filter_map(|(k, n)| Some((k.parse().ok()?, n.as_str()?.to_string()))).collect();
    v.sort();
    Some(v)
}

fn revert_button(ui: &mut egui::Ui, p: &mut Project, path: &str, field: &str, patched: bool) -> bool {
    if patched {
        if ui.small_button("⟲").on_hover_text("back to the base world's value (the patch goes)").clicked() {
            p.set_patch(path, field, None);
            return true;
        }
    } else {
        ui.label("");
    }
    false
}

/// A number, an enum (a menu of its names) or a flags field (boxes): true if changed.
fn scalar_widget(ui: &mut egui::Ui, v: &mut Value, prop: &Value, key: &str) -> bool {
    if let Some(names) = enum_names(prop) {
        let cur = match v {
            Value::String(s) => s.clone(),
            Value::Number(n) => names.iter().find(|(k, _)| Some(*k) == n.as_i64()).map(|(_, s)| s.clone()).unwrap_or_else(|| n.to_string()),
            _ => names.first().map(|x| x.1.clone()).unwrap_or_default(),
        };
        let mut pick = cur.clone();
        egui::ComboBox::from_id_salt(("enum", key, ui.next_auto_id())).selected_text(&pick).show_ui(ui, |ui| {
            for (_, n) in &names {
                ui.selectable_value(&mut pick, n.clone(), n);
            }
        });
        if pick != cur {
            *v = Value::String(pick);
            return true;
        }
        return false;
    }
    if let Some(flags) = prop.get("x-flags").and_then(Value::as_object) {
        let mut set: Vec<String> = v.as_array().map(|a| a.iter().filter_map(|x| x.as_str().map(str::to_string)).collect()).unwrap_or_default();
        let mut changed = false;
        ui.vertical(|ui| {
            for (_, n) in flags {
                let n = n.as_str().unwrap_or("").to_string();
                let mut on = set.contains(&n);
                if ui.checkbox(&mut on, &n).changed() {
                    if on {
                        set.push(n);
                    } else {
                        set.retain(|x| x != &n);
                    }
                    changed = true;
                }
            }
        });
        if changed {
            *v = json!(set);
        }
        return changed;
    }
    let float = prop.get("type").and_then(Value::as_str) == Some("number") || prop["x-c-type"].as_str() == Some("f32");
    let mut x = v.as_f64().unwrap_or(0.0);
    let r = if float {
        ui.add(egui::DragValue::new(&mut x).speed(0.5))
    } else {
        let lo = prop["minimum"].as_f64().unwrap_or(-2147483648.0);
        let hi = prop["maximum"].as_f64().unwrap_or(4294967295.0);
        ui.add(egui::DragValue::new(&mut x).speed(1.0).range(lo..=hi).fixed_decimals(0))
    };
    if r.changed() {
        *v = if float { json!(x) } else { json!(x as i64) };
        return true;
    }
    false
}

/// A link: the current target, a menu of what fits, and "pick in the world". true if changed.
fn link_widget(ui: &mut egui::Ui, v: &mut Value, cands: &[(String, String)], path: &str, pick: &mut Option<String>) -> bool {
    let cur = v.as_str().unwrap_or("").to_string();
    let shown = cands.iter().find(|(_, l)| *l == cur).map(|(n, _)| n.clone()).unwrap_or_else(|| {
        cur.rsplit_once("/world_0/").map(|(_, p)| p.to_string()).unwrap_or_else(|| if cur.is_empty() { "(none)".into() } else { cur.clone() })
    });
    let mut new = cur.clone();
    ui.horizontal(|ui| {
        egui::ComboBox::from_id_salt(("link", path)).width(200.0).selected_text(egui::RichText::new(shown).monospace()).show_ui(ui, |ui| {
            for (n, l) in cands {
                ui.selectable_value(&mut new, l.clone(), n);
            }
        });
        if ui.small_button("🎯").on_hover_text("pick the target in the view").clicked() {
            *pick = Some(path.to_string());
        }
    })
    .response
    .on_hover_text(&cur);
    if new != cur {
        *v = Value::String(new);
        return true;
    }
    false
}

/// A record of the level's own, by its schema: true if changed.
#[allow(clippy::too_many_arguments)]
fn fields_editor(ui: &mut egui::Ui, v: &mut Value, schema: &Value, advanced: bool, cands: &[(String, String)], at: &str, pick: &mut Option<String>, volume: bool) -> bool {
    let props = schema["properties"].as_object().cloned().unwrap_or_default();
    let mut changed = false;
    if !v.is_object() {
        *v = json!({});
    }
    egui::Grid::new(("fields", at)).num_columns(2).striped(true).show(ui, |ui| {
        if volume {
            // the editor's shorthand: radius (radiusSq is its square unless given)
            ui.label("radius");
            let mut r = v["radius"].as_f64().or_else(|| v["radiusSq"].as_f64().map(f64::sqrt)).unwrap_or(10.0);
            if ui.add(egui::DragValue::new(&mut r).speed(0.5).range(0.1..=10000.0)).changed() {
                v["radius"] = json!(round(r as f32));
                if let Some(o) = v.as_object_mut() {
                    o.remove("radiusSq");
                }
                changed = true;
            }
            ui.end_row();
        }
        for (k, prop) in &props {
            if prop.get("x-offset").is_none() || (!advanced && hidden_field(k)) || (volume && (k == "radius" || k == "radiusSq" || k == "bsp" || k == "data")) {
                continue;
            }
            ui.label(k);
            let present = v.get(k).is_some();
            // left out: what the compiler puts there (xdl_level.py's defaults), else zero
            let default = match (volume, k.as_str()) {
                (true, "enabled") => json!(1),
                (true, "shape") => json!("sphere"),
                (true, "type") => json!("airbox"),
                (false, "active") if v.get("value").is_none() => json!(1),
                _ => Value::Null,
            };
            let mut val = v.get(k).cloned().unwrap_or(if default.is_null() && link_of(prop).is_none() && prop.get("$ref").is_none() { json!(0) } else { default });
            if !present {
                ui.visuals_mut().override_text_color = Some(ui.visuals().weak_text_color());
            }
            let c = if link_of(prop).is_some() {
                if k == "mode" && val.is_object() {
                    ui.label(egui::RichText::new(val.to_string()).monospace());
                    false
                } else {
                    link_widget(ui, &mut val, cands, &format!("{at}/{k}"), pick)
                }
            } else if prop.get("$ref").is_some() {
                // a vector: [x, y, z] (or {x, y, z})
                let mut c3 = vec3(&val).unwrap_or([0.0; 3]);
                let mut cc = false;
                ui.horizontal(|ui| {
                    for (i, a) in ["x", "y", "z"].iter().enumerate() {
                        ui.label(*a);
                        cc |= ui.add(egui::DragValue::new(&mut c3[i]).speed(0.5)).changed();
                    }
                });
                if cc {
                    val = json!([round(c3[0]), round(c3[1]), round(c3[2])]);
                }
                cc
            } else if prop["x-c-type"].as_str().map(|t| t.contains('[')).unwrap_or(false) {
                ui.label(egui::RichText::new(val.to_string()).monospace().weak());
                false
            } else {
                scalar_widget(ui, &mut val, prop, k)
            };
            if c {
                v[k.as_str()] = val;
                changed = true;
            }
            ui.visuals_mut().override_text_color = None;
            ui.end_row();
        }
    });
    changed
}

/// A trigger file: its kind, mode, parameters, conditions and actions, by the trigger schema.
fn trigger_editor(ui: &mut egui::Ui, v: &mut Value, schema: &Value, cands: &[(String, String)], pick: &mut Option<String>) -> bool {
    let tokens: Vec<String> = schema["x-tokens"].as_object().map(|m| m.values().filter_map(|x| x.as_str().map(str::to_string)).collect()).unwrap_or_default();
    let tprops = &schema["properties"];
    let aprops = &schema["$defs"]["TriggerAction"]["properties"];
    let mut changed = false;
    egui::Grid::new("trigger_head").num_columns(2).show(ui, |ui| {
        ui.label("type");
        let mut t = v["type"].clone();
        if scalar_widget(ui, &mut t, &tprops["type"], "type") {
            v["type"] = t;
            let lay = &schema["x-variants"]["TriggerType"][v["type"].as_str().unwrap_or("")];
            if lay.as_array().map(|a| !a.is_empty()).unwrap_or(false) {
                v["param"] = default_params(lay);
            } else if let Some(o) = v.as_object_mut() {
                o.remove("param");
            }
            changed = true;
        }
        ui.end_row();
        ui.label("conditions are");
        let mut m = v.get("mode").cloned().unwrap_or(json!("all"));
        if scalar_widget(ui, &mut m, &tprops["mode"], "mode") {
            v["mode"] = m;
            changed = true;
        }
        ui.end_row();
    });
    let kind = v["type"].as_str().unwrap_or("").to_string();
    let layout = schema["x-variants"]["TriggerType"][&kind].clone();
    if v.get("param").is_some() {
        ui.separator();
        ui.strong("when");
        let mut pv = v["param"].clone();
        if params_editor(ui, &mut pv, &layout, &tokens, cands, "param", pick) {
            v["param"] = pv;
            changed = true;
        }
    }
    for (list, title) in [("conditions", "if"), ("actions", "do"), ("elseActions", "else (landing)")] {
        let cond = list == "conditions";
        let arr = v.get(list).and_then(Value::as_array).cloned().unwrap_or_default();
        if list == "elseActions" && arr.is_empty() && kind != "player_vehicle_in_air" {
            continue;
        }
        ui.separator();
        ui.strong(format!("{title} ({})", arr.len()));
        let mut arr = arr;
        let mut remove = None;
        let mut list_changed = false;
        let (props, enum_name, data_key) = if cond { (tprops, "TriggerType", "param") } else { (aprops, "ActionType", "data") };
        for (i, item) in arr.iter_mut().enumerate() {
            let at = format!("{list}/{i}");
            egui::Frame::group(ui.style()).show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label(format!("{}.", i + 1));
                    let mut t = item["type"].clone();
                    if scalar_widget(ui, &mut t, &props["type"], &at) {
                        // another kind: its own parameters
                        item["type"] = t;
                        let lay = &schema["x-variants"][enum_name][item["type"].as_str().unwrap_or("")];
                        if lay.as_array().map(|a| !a.is_empty()).unwrap_or(false) {
                            item[data_key] = default_params(lay);
                        } else if let Some(o) = item.as_object_mut() {
                            o.remove(data_key);
                        }
                        list_changed = true;
                    }
                    if !cond {
                        ui.label("after");
                        let mut d = item["delayInit"].as_f64().unwrap_or(0.0);
                        if ui.add(egui::DragValue::new(&mut d).speed(10.0).range(0.0..=600000.0).suffix(" ms")).changed() {
                            item["delayInit"] = json!(d as i64);
                            if let Some(o) = item.as_object_mut() {
                                o.remove("delay");
                            }
                            list_changed = true;
                        }
                        let always = item["count"] == json!(4294967295u64) || item["count"] == "always";
                        let mut a = always;
                        if ui.checkbox(&mut a, "every time").changed() {
                            item["count"] = if a { json!("always") } else { json!(1) };
                            list_changed = true;
                        }
                    }
                    if ui.small_button("−").on_hover_text("take it out").clicked() {
                        remove = Some(i);
                    }
                });
                let lay = schema["x-variants"][enum_name][item["type"].as_str().unwrap_or("")].clone();
                if item.get(data_key).is_some() {
                    let mut pv = item[data_key].clone();
                    if params_editor(ui, &mut pv, &lay, &tokens, cands, &format!("{at}/{data_key}"), pick) {
                        item[data_key] = pv;
                        list_changed = true;
                    }
                }
            });
        }
        if let Some(i) = remove {
            arr.remove(i);
            list_changed = true;
        }
        // + another, of a kind from the menu
        let names: Vec<String> = props["type"]["x-enum"].as_object().map(|m| m.values().filter_map(|x| x.as_str().map(str::to_string)).collect()).unwrap_or_default();
        let mut add = None;
        egui::ComboBox::from_id_salt(("add", list)).selected_text(if cond { "+ condition" } else { "+ action" }).show_ui(ui, |ui| {
            for n in &names {
                if ui.selectable_label(false, n).clicked() {
                    add = Some(n.clone());
                }
            }
        });
        if let Some(n) = add {
            let lay = &schema["x-variants"][enum_name][&n];
            let mut it = json!({"type": n});
            if lay.as_array().map(|a| !a.is_empty()).unwrap_or(false) {
                it[data_key] = default_params(lay);
            }
            arr.push(it);
            list_changed = true;
        }
        if list_changed {
            changed = true;
            if arr.is_empty() {
                if let Some(o) = v.as_object_mut() {
                    o.remove(list); // (an empty list is null in the format)
                }
            } else {
                v[list] = Value::Array(arr);
            }
        }
    }
    changed
}

/// The kinds of a parameter list's items, by name (from the layout: [kind, name, ...]).
fn item_kinds(layout: &Value, out: &mut HashMap<String, String>) {
    if let Some(a) = layout.as_array() {
        for it in a {
            if let (Some(k), Some(n)) = (it.get(0).and_then(Value::as_str), it.get(1).and_then(Value::as_str)) {
                out.insert(n.to_string(), k.to_string());
            }
            if let Some(more) = it.as_array() {
                for x in more {
                    if x.is_array() && x.get(0).map(Value::is_array).unwrap_or(false) {
                        item_kinds(x, out);
                    }
                }
            }
        }
    }
}

/// A parameter list (any shape: objects, lists, links, keywords, numbers): true if changed.
fn params_editor(ui: &mut egui::Ui, v: &mut Value, layout: &Value, tokens: &[String], cands: &[(String, String)], at: &str, pick: &mut Option<String>) -> bool {
    let mut kinds = HashMap::new();
    item_kinds(layout, &mut kinds);
    value_editor(ui, v, "", &kinds, tokens, cands, at, pick, 0)
}

#[allow(clippy::too_many_arguments)]
fn value_editor(ui: &mut egui::Ui, v: &mut Value, key: &str, kinds: &HashMap<String, String>, tokens: &[String], cands: &[(String, String)], at: &str, pick: &mut Option<String>, depth: usize) -> bool {
    let mut changed = false;
    match v {
        Value::Object(m) => {
            let keys: Vec<String> = m.keys().cloned().collect();
            ui.vertical(|ui| {
                egui::Grid::new(("params", at)).num_columns(2).show(ui, |ui| {
                    for k in keys {
                        ui.label(&k);
                        let x = m.get_mut(&k).unwrap();
                        changed |= value_editor(ui, x, &k, kinds, tokens, cands, &format!("{at}/{k}"), pick, depth + 1);
                        ui.end_row();
                    }
                });
            });
        }
        Value::Array(a) => {
            let link_list = kinds.get(key).map(|k| k == "list").unwrap_or(false) || a.iter().any(|x| x.as_str().map(is_link).unwrap_or(false));
            ui.vertical(|ui| {
                let mut remove = None;
                for (i, x) in a.iter_mut().enumerate() {
                    ui.horizontal(|ui| {
                        changed |= value_editor(ui, x, key, kinds, tokens, cands, &format!("{at}/{i}"), pick, depth + 1);
                        if link_list && ui.small_button("−").on_hover_text("take it out of the list").clicked() {
                            remove = Some(i);
                        }
                    });
                }
                if let Some(i) = remove {
                    a.remove(i);
                    changed = true;
                }
                if link_list && ui.small_button("+ add").clicked() {
                    a.push(Value::String(cands.first().map(|c| c.1.clone()).unwrap_or_default()));
                    changed = true;
                }
            });
        }
        Value::String(s) if is_link(s) || kinds.get(key).map(|k| k == "link" || k == "list" || k == "any").unwrap_or(false) && s.contains("://") => {
            changed |= link_widget(ui, v, cands, at, pick);
        }
        Value::String(s) if tokens.contains(s) || kinds.get(key).map(|k| k == "token").unwrap_or(false) => {
            let cur = s.clone();
            let mut pickv = cur.clone();
            egui::ComboBox::from_id_salt(("token", at)).selected_text(&pickv).show_ui(ui, |ui| {
                for t in tokens {
                    ui.selectable_value(&mut pickv, t.clone(), t);
                }
            });
            if pickv != cur {
                *v = Value::String(pickv);
                changed = true;
            }
        }
        Value::String(s) => {
            changed |= ui.add(egui::TextEdit::singleline(s).desired_width(160.0)).changed();
        }
        Value::Number(n) => {
            let float = n.is_f64() || kinds.get(key).map(|k| k == "f32").unwrap_or(false);
            let mut x = n.as_f64().unwrap_or(0.0);
            let r = if float { ui.add(egui::DragValue::new(&mut x).speed(0.1)) } else { ui.add(egui::DragValue::new(&mut x).speed(1.0).fixed_decimals(0)) };
            if r.changed() {
                *v = if float { json!(x) } else { json!(x as i64) };
                changed = true;
            }
        }
        Value::Bool(b) => changed |= ui.checkbox(b, "").changed(),
        Value::Null => {
            ui.label(egui::RichText::new("null").weak());
        }
    }
    changed
}

fn is_link(s: &str) -> bool {
    s.starts_with("base://") || s.starts_with("level://") || s.starts_with("uid://")
}
