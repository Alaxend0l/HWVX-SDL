//! Whole worlds (the viewer's `worlds`, `world`, `overlays`, `goto`; src/port/viewer_world.c):
//! the left panel's Worlds tab (every level of the MCP's progression scripts, by mode; a
//! double-click loads one as the game does, its quadrants streaming around the camera), the
//! world's overlay toggles, the camera mode (orbit / free fly: WASD, Q/E down/up, shift faster,
//! the wheel sets the speed, a left drag looks), go to the start, focus the selected object, and
//! the world's numbers (quadrants loaded, the camera's, fps).

use crate::{Browser, Pending};
use eframe::egui;
use serde_json::{Value, json};
use std::time::Instant;

/// The overlays `overlays` takes, with what they show.
pub const OVERLAYS: [(&str, &str); 7] = [
    ("regions", "quadrant regions (BSPN0, as boxes)"),
    ("loaded", "loaded quadrants: green in view, blue loaded, yellow the camera's"),
    ("collision", "the collision pack's crease edges"),
    ("volumes", "fog / reverb / trigger volumes"),
    ("spawns", "tile cars (green: players), creatures, pickups, teleporters, lights"),
    ("paths", "the AI's nav graph (yellow: way points)"),
    ("objects", "placed instances and effect points"),
];

#[derive(Clone)]
pub struct WorldEntry {
    pub mode: String,
    pub name: String,
    pub world: String,
}

#[derive(Default)]
pub struct WorldState {
    pub list: Vec<WorldEntry>,
    pub filter: String,
    pub status: Value,     // the viewer's "world" (null: none loaded)
    pub tab_worlds: bool,  // the left panel shows the worlds
    pub fly: bool,
    pub last_move: Option<Instant>,
    /// the 3D view's switches (the viewer's view3d; off: as the game draws)
    pub no_clip: bool,
    pub all_quads: bool,
    pub no_fog: bool,
}

impl Browser {
    /// The 3D view's switches to the viewer (view3d; the game's own play never has them).
    pub(crate) fn send_view3d(&mut self) {
        let v = json!({"clip": !self.world.no_clip, "allQuads": self.world.all_quads, "fog": !self.world.no_fog});
        self.request("view3d", v, Pending::Quiet);
    }

    pub(crate) fn worlds_reply(&mut self, r: Value) {
        self.world.list = r["worlds"]
            .as_array()
            .map(|a| {
                a.iter()
                    .map(|w| WorldEntry {
                        mode: w["mode"].as_str().unwrap_or("").into(),
                        name: w["name"].as_str().unwrap_or("").into(),
                        world: w["world"].as_str().unwrap_or("").into(),
                    })
                    .collect()
            })
            .unwrap_or_default();
    }

    /// A world loaded (or the status's): its outline when it is new.
    pub(crate) fn world_status(&mut self, w: &Value) {
        let new_pack = w["pack"].as_str().map(str::to_string);
        let old_pack = self.world.status["pack"].as_str().map(str::to_string);
        self.world.status = w.clone();
        if let Some(p) = new_pack {
            if Some(&p) != old_pack.as_ref() || self.outline.as_ref().map(|o| &o.pack) != Some(&p) {
                self.selected = None;
                self.detail = None;
                self.request("outline", json!({"pack": p}), Pending::Outline(p.clone()));
            }
        }
    }

    pub(crate) fn load_world(&mut self, w: &WorldEntry) {
        self.note(format!("loading the world {} ({})…", w.name, w.mode));
        self.request("world", json!({"name": w.world}), Pending::World);
    }

    pub(crate) fn left_tabs(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            if ui.selectable_label(!self.level.tab && !self.world.tab_worlds, "Packs").clicked() {
                self.level.tab = false;
                self.world.tab_worlds = false;
            }
            if ui.selectable_label(!self.level.tab && self.world.tab_worlds, "Worlds").clicked() {
                self.level.tab = false;
                self.world.tab_worlds = true;
            }
            if ui.selectable_label(self.level.tab, "Level").clicked() {
                self.level.tab = true;
            }
        });
        ui.separator();
        if self.level.tab {
            self.level_panel(ui);
        } else if self.world.tab_worlds {
            self.world_list(ui);
        } else {
            self.pack_list(ui);
        }
    }

    fn world_list(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label("Filter");
            ui.text_edit_singleline(&mut self.world.filter);
        });
        let loaded = self.world.status["world"].as_str().map(str::to_string);
        if loaded.is_some() {
            self.world_controls(ui);
            ui.separator();
        }
        let filter = self.world.filter.to_lowercase();
        let mut modes: Vec<String> = Vec::new();
        for w in &self.world.list {
            if !modes.contains(&w.mode) {
                modes.push(w.mode.clone());
            }
        }
        let mut load = None;
        egui::ScrollArea::vertical().id_salt("worlds").auto_shrink([false, false]).show(ui, |ui| {
            for m in &modes {
                let rows: Vec<&WorldEntry> = self
                    .world
                    .list
                    .iter()
                    .filter(|w| &w.mode == m)
                    .filter(|w| filter.is_empty() || format!("{} {}", w.mode, w.world).to_lowercase().contains(&filter))
                    .collect();
                if rows.is_empty() {
                    continue;
                }
                egui::CollapsingHeader::new(format!("{m} ({})", rows.len())).default_open(!filter.is_empty()).show(ui, |ui| {
                    for w in rows {
                        let r = ui.selectable_label(loaded.as_deref() == Some(w.world.as_str()), egui::RichText::new(&w.name).monospace());
                        if r.on_hover_text(format!("{}\ndouble-click to load", w.world)).double_clicked() {
                            load = Some(w.clone());
                        }
                    }
                });
            }
        });
        if let Some(w) = load {
            self.load_world(&w);
        }
    }

    fn world_controls(&mut self, ui: &mut egui::Ui) {
        let st = self.world.status.clone();
        ui.strong(st["name"].as_str().unwrap_or(""));
        ui.label(
            egui::RichText::new(format!(
                "{} quadrants · {} loaded ({} in view) · camera in {} · loaded in {:.0} ms",
                st["quads"],
                st["loaded"],
                st["visible"],
                st["cameraQuad"],
                st["loadMs"].as_f64().unwrap_or(0.0)
            ))
            .small()
            .weak(),
        );
        ui.horizontal(|ui| {
            let mut fly = self.world.fly;
            ui.selectable_value(&mut fly, false, "Orbit");
            ui.selectable_value(&mut fly, true, "Fly").on_hover_text("WASD, Q/E down/up, shift faster, the wheel sets the speed, drag to look");
            if fly != self.world.fly {
                self.world.fly = fly;
                self.request("camera", json!({"mode": if fly { "fly" } else { "orbit" }}), Pending::Camera);
            }
            if ui.button("Start").on_hover_text("go to player 1's start (G)").clicked() {
                self.request("goto", json!({"port": 1}), Pending::Camera);
            }
            if ui.add_enabled(self.selected.is_some(), egui::Button::new("Focus")).on_hover_text("the camera on the selected object (F)").clicked() {
                if let (Some(n), Some(o)) = (self.selected.clone(), &self.outline) {
                    let pack = o.pack.clone();
                    self.request("focus", json!({"pack": pack, "node": n}), Pending::Quiet);
                }
            }
        });
        ui.label("Overlays");
        let ov = st["overlays"].clone();
        let mut changes = serde_json::Map::new();
        ui.horizontal_wrapped(|ui| {
            for (name, tip) in OVERLAYS {
                let mut on = ov[name].as_bool().unwrap_or(false);
                if ui.checkbox(&mut on, name).on_hover_text(tip).changed() {
                    changes.insert(name.to_string(), Value::Bool(on));
                }
            }
            // hooks other layers add (trigger volumes ...): whatever else the viewer lists
            if let Some(o) = ov.as_object() {
                for (name, v) in o {
                    if OVERLAYS.iter().any(|(n, _)| n == name) {
                        continue;
                    }
                    let mut on = v.as_bool().unwrap_or(false);
                    if ui.checkbox(&mut on, name.as_str()).changed() {
                        changes.insert(name.clone(), Value::Bool(on));
                    }
                }
            }
        });
        if !changes.is_empty() {
            for (k, v) in &changes {
                self.world.status["overlays"][k] = v.clone();
            }
            self.request("overlays", Value::Object(changes), Pending::Quiet);
        }
        ui.label("View");
        ui.horizontal_wrapped(|ui| {
            let mut clip = !self.world.no_clip;
            let mut all = self.world.all_quads;
            let mut fog = !self.world.no_fog;
            let a = ui.checkbox(&mut clip, "Geometry clipping")
                .on_hover_text("off: nothing disappears with distance (no far clipping plane, no draw distances, no culling)")
                .changed();
            let b = ui.checkbox(&mut all, "Load all quadrants")
                .on_hover_text("every quadrant loaded at once, not only those around the camera")
                .changed();
            let c = ui.checkbox(&mut fog, "Fog").on_hover_text("off: no distance fog (the sky keeps its haze)").changed();
            if a || b || c {
                self.world.no_clip = !clip;
                self.world.all_quads = all;
                self.world.no_fog = !fog;
                self.send_view3d();
            }
        });
        if ui.button("Unload world").clicked() {
            self.request("world_unload", json!({}), Pending::Quiet);
            self.world.status = Value::Null;
            self.outline = None;
            self.selected = None;
            self.detail = None;
        }
    }

    /// Held fly keys over the preview: one `move` event a UI frame (the time since the last).
    pub(crate) fn fly_keys(&mut self, ui: &egui::Ui, events: &mut Vec<Value>) {
        use egui::Key;
        let (w, s, a, d, q, e, shift) = ui.input(|i| {
            (i.key_down(Key::W), i.key_down(Key::S), i.key_down(Key::A), i.key_down(Key::D), i.key_down(Key::Q), i.key_down(Key::E), i.modifiers.shift)
        });
        let fwd = (w as i32 - s as i32) as f32;
        let right = (d as i32 - a as i32) as f32;
        let up = (e as i32 - q as i32) as f32;
        let now = Instant::now();
        if fwd == 0.0 && right == 0.0 && up == 0.0 {
            self.world.last_move = None;
            return;
        }
        let dt = self.world.last_move.map(|t| now.duration_since(t).as_secs_f32()).unwrap_or(1.0 / 60.0).min(0.1);
        self.world.last_move = Some(now);
        self.world.fly = true;
        events.push(json!({"type": "move", "forward": fwd, "right": right, "up": up, "dt": dt, "fast": shift}));
        ui.ctx().request_repaint();
    }
}
