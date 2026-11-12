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
pub const BASE_TABLES: [(&str, &str); 6] = [
    ("tileCars", "Starts (tile cars)"),
    ("volumes", "Airbox volumes"),
    ("items", "Items"),
    ("timers", "Timers"),
    ("variables", "Variables"),
    ("objectives", "Objectives"),
];
const OWN_KINDS: [(&str, &str); 4] = [("volumes", "Volumes"), ("timers", "Timers"), ("variables", "Variables"), ("strings", "Texts")];

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
    checking: Option<(u64, Receiver<(Result<(), Vec<LevelError>>, PathBuf, f64)>)>,
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
        if self.level.edit.proj.is_some() && !world.is_empty() && self.level.edit.base_world != world {
            self.level.edit.base_world = world;
            self.level.edit.base.clear();
            for (t, _) in BASE_TABLES {
                self.request("records", json!({"table": t}), Pending::Records(t.to_string()));
            }
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
                        Ok(()) => {
                            self.level.edit.errors.clear();
                            if self.level.edit.preview && self.world.status["world"].is_string() {
                                self.request("level", json!({"path": path, "overlay": false}), Pending::LevelApplied);
                            }
                            self.level.edit.preview_note = format!("checked in {ms:.0} ms");
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
            let res = match c.output() {
                Ok(o) => {
                    let text = String::from_utf8_lossy(&o.stdout).to_string();
                    match text.lines().rev().find_map(|l| serde_json::from_str::<Value>(l).ok()) {
                        Some(v) if v["ok"] == true => Ok(()),
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
            for (name, v) in p.own("volumes") {
                if let Some(c) = vec3(&v["center"]) {
                    let r = v["radius"].as_f64().or_else(|| v["radiusSq"].as_f64().map(f64::sqrt)).unwrap_or(5.0);
                    let sel = self.level.edit.sel == Some(Sel::Own("volumes".into(), name.clone()));
                    marks.push(json!({"kind": "sphere", "pos": c, "radius": r, "color": if sel { json!([255, 240, 40]) } else { json!([255, 80, 220]) }}));
                }
            }
            if let Some((pos, rot)) = self.sel_place() {
                let s = &self.level.edit.sel;
                let yellow = json!([255, 240, 40]);
                match s {
                    Some(Sel::Base(t, _)) if t == "tileCars" => {
                        marks.push(json!({"kind": "arrow", "pos": pos, "rotY": rot.unwrap_or(0.0), "radius": 10.0, "color": yellow}));
                    }
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
            Sel::Own(k, n) if k == "volumes" => vec3(&p.level["volumes"][n]["center"]).map(|c| (c, None)),
            Sel::Base(t, path) => {
                let e = self.base_entry(path.as_str())?;
                let mut c = vec3(&e["pos"])?;
                let f = pos_field(t.as_str());
                for (i, a) in ["x", "y", "z"].iter().enumerate() {
                    if let Some(v) = p.patch(path, &format!("{f}/{a}")).and_then(Value::as_f64) {
                        c[i] = v as f32;
                    }
                }
                let rot = (t == "tileCars").then(|| p.patch(path, "rotY").and_then(Value::as_f64).or_else(|| e["rotY"].as_f64()).unwrap_or(0.0));
                Some((c, rot))
            }
            _ => None,
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
                v["center"] = json!([round(c[0]), round(c[1]), round(c[2])]);
            }
            Sel::Base(t, path) => {
                let f = pos_field(t.as_str());
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
            for (name, v) in p.own("volumes") {
                if let Some(c) = vec3(&v["center"]) {
                    let r = v["radius"].as_f64().or_else(|| v["radiusSq"].as_f64().map(f64::sqrt)).unwrap_or(5.0) as f32;
                    let w = [c[0] - o[0], c[1] - o[1], c[2] - o[2]];
                    let tc = w[0] * d[0] + w[1] * d[1] + w[2] * d[2];
                    let d2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2] - tc * tc;
                    if tc + r >= 0.0 && d2 <= r * r && best.as_ref().map(|b| tc < b.0).unwrap_or(true) {
                        best = Some((tc.max(0.0), Sel::Own("volumes".into(), name)));
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
        if let Some((c, _)) = self.sel_place() {
            self.level.edit.move_offset = 0.0;
            self.request("raycast", json!({"from": [c[0], c[1] + 30.0, c[2]], "to": [c[0], c[1] - 3000.0, c[2]]}), Pending::EditMove);
            self.level.edit.move_inflight = true;
        }
    }

    // ---- the lists --------------------------------------------------------------------------------

    pub(crate) fn edit_lists(&mut self, ui: &mut egui::Ui) {
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

    fn inspect_own(&mut self, ui: &mut egui::Ui, kind: &str, name: &str) {
        ui.heading(format!("{name}"));
        ui.label(egui::RichText::new(format!("level://{kind}/{name}")).monospace().weak());
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
        let changed = fields_editor(ui, &mut v, &schema, advanced, &cands, &format!("{kind}/{name}"), &mut link_pick, kind == "volumes");
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
            let mut val = v.get(k).cloned().unwrap_or(Value::Null);
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
            let _ = present; // (untouched fields stay out of the file)
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
    for (list, title) in [("conditions", "if"), ("actions", "do"), ("elseActions", "else")] {
        let Some(arr) = v.get(list).and_then(Value::as_array).cloned() else { continue };
        ui.separator();
        ui.strong(format!("{title} ({})", arr.len()));
        let mut arr = arr;
        for (i, item) in arr.iter_mut().enumerate() {
            let at = format!("{list}/{i}");
            egui::Frame::group(ui.style()).show(ui, |ui| {
                if list == "conditions" {
                    let mut t = item["type"].clone();
                    ui.horizontal(|ui| {
                        ui.label(format!("{}.", i + 1));
                        if scalar_widget(ui, &mut t, &tprops["type"], &at) {
                            item["type"] = t;
                            changed = true;
                        }
                    });
                    let lay = schema["x-variants"]["TriggerType"][item["type"].as_str().unwrap_or("")].clone();
                    if item.get("param").is_some() {
                        let mut pv = item["param"].clone();
                        if params_editor(ui, &mut pv, &lay, &tokens, cands, &format!("{at}/param"), pick) {
                            item["param"] = pv;
                            changed = true;
                        }
                    }
                } else {
                    ui.horizontal(|ui| {
                        ui.label(format!("{}.", i + 1));
                        let mut t = item["type"].clone();
                        if scalar_widget(ui, &mut t, &aprops["type"], &at) {
                            item["type"] = t;
                            changed = true;
                        }
                        ui.label("after");
                        let mut d = item["delayInit"].as_f64().unwrap_or(0.0);
                        if ui.add(egui::DragValue::new(&mut d).speed(10.0).range(0.0..=600000.0).suffix(" ms")).changed() {
                            item["delayInit"] = json!(d as i64);
                            if let Some(o) = item.as_object_mut() {
                                o.remove("delay");
                            }
                            changed = true;
                        }
                        let always = item["count"] == json!(4294967295u64) || item["count"] == "always";
                        let mut a = always;
                        if ui.checkbox(&mut a, "every time").changed() {
                            item["count"] = if a { json!("always") } else { json!(1) };
                            changed = true;
                        }
                    });
                    let lay = schema["x-variants"]["ActionType"][item["type"].as_str().unwrap_or("")].clone();
                    if item.get("data").is_some() {
                        let mut pv = item["data"].clone();
                        if params_editor(ui, &mut pv, &lay, &tokens, cands, &format!("{at}/data"), pick) {
                            item["data"] = pv;
                            changed = true;
                        }
                    }
                }
            });
        }
        if changed {
            v[list] = Value::Array(arr);
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
                        if link_list && ui.small_button("✕").on_hover_text("take it out of the list").clicked() {
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
