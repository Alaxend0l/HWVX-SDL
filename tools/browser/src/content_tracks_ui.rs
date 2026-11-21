//! The Content tab's Tracks (src/content_tracks.rs): + New track (a mode, the retail track it is
//! made from, by its world select name), its inspector (name, base, the world select picture,
//! unlocked, a level project of its own), ▶ Drive it (a race on it, the mod as it is now), and
//! the tracks' levels compiled into the mod folder before a test and before Save as mod.

use crate::Browser;
use eframe::egui;
use hwvx_browser::content::ModProject;
use hwvx_browser::content_tracks::{self as tracks, Picture, TrackBuild};
use hwvx_browser::testdrive::{self, Prepared, TestDrive};
use serde_json::Value;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{Receiver, channel};
use std::time::Instant;

/// What follows the levels' compile.
#[derive(Clone, PartialEq)]
pub(crate) enum After {
    Nothing,
    Drive(String),
    Pack,
}

type Built = Vec<Result<TrackBuild, (String, Vec<String>)>>;

#[derive(Default)]
pub(crate) struct TracksState {
    /// the selected track (its uid)
    pub sel: Option<String>,
    /// content.sel as last seen: a car or weapon selected since takes the inspector
    seen: Option<(hwvx_browser::content::Kind, String)>,
    // + New track
    add_mode: usize,
    add_base: u32,
    add_name: String,
    add_locked: bool,
    /// the world select's pictures (texture number: its image), from the player's data
    pictures: Option<HashMap<u32, egui::TextureHandle>>,
    pictures_rx: Option<Receiver<Result<Vec<Picture>, String>>>,
    pictures_note: String,
    building: Option<(After, Receiver<Built>)>,
    /// the compile's errors, by track
    errors: Vec<(String, Vec<String>)>,
    /// Save as mod after a compile: the levels are fresh
    pack_ready: bool,
    /// the mod folder last shown: a mod opened with no car or weapon selected shows its first track
    opened: Option<PathBuf>,
    /// the track whose picture is set to "your own" (before a PNG is chosen)
    own_mode: Option<String>,
    /// the own picture's preview: its file, its time, the texture and size (or why not)
    own_preview: Option<(PathBuf, Option<std::time::SystemTime>, Result<(egui::TextureHandle, [usize; 2]), String>)>,
}

/// "made from" values past the retail tracks: Joyride world k (track k) as the template of a
/// track of another mode (the mode's first track its base, the Joyride world its world).
const TEMPLATE: u32 = 1000;

fn base_text(n: u32, worlds: &[String]) -> String {
    if n >= TEMPLATE {
        return format!("Joyride world {} as the template", n - TEMPLATE + 1);
    }
    let m = tracks::mode_of(n);
    let w = m.and_then(|m| worlds.get((n - m.first) as usize)).map(|w| format!("   ({w})")).unwrap_or_default();
    format!("{}{w}", tracks::label(n))
}

impl Browser {
    /// The worlds of a mode's progression, in level order (the viewer's `worlds`; empty
    /// before it answered).
    fn track_worlds(&self, n: u32) -> Vec<String> {
        let Some(m) = tracks::mode_of(n) else { return vec![] };
        let prog = if m.name == "mission" { "adventure" } else { m.name };
        self.world.list.iter().filter(|w| w.mode == prog).map(|w| w.name.clone()).collect()
    }

    fn track_note(&mut self, s: impl Into<String>, error: bool) {
        self.content.message = s.into();
        self.content.error = error;
    }

    /// The Tracks list in the Content tab's left panel (after Cars and Weapons).
    pub(crate) fn tracks_list(&mut self, ui: &mut egui::Ui) {
        self.tracks_tick();
        let t = &mut self.content.tracks;
        if self.content.sel.is_some() && self.content.sel != t.seen {
            t.sel = None;
        }
        t.seen = self.content.sel.clone();
        let Some(p) = self.content.proj.as_ref() else { return };
        let list = p.tracks();
        if self.content.tracks.opened.as_ref() != Some(&p.dir) {
            self.content.tracks.opened = Some(p.dir.clone());
            if self.content.sel.is_none() {
                self.content.tracks.sel = list.first().map(|(u, _)| u.clone());
            }
        }
        let linked: Vec<String> = p.track_levels().into_iter().map(|(u, _)| u).collect();
        let mode_first = tracks::MODES[self.content.tracks.add_mode.min(tracks::MODES.len() - 1)].first;
        let add_worlds = self.track_worlds(mode_first);
        let joyride_worlds: Vec<String> = self.world.list.iter().filter(|w| w.mode == "joyride").map(|w| w.world.clone()).collect();
        ui.horizontal(|ui| {
            ui.strong(format!("Tracks ({})", list.len()));
            ui.menu_button("+ New track", |ui| {
                let t = &mut self.content.tracks;
                ui.label("a new entry of the mode's world select");
                egui::Grid::new("track_add").num_columns(2).show(ui, |ui| {
                    ui.label("mode");
                    let before = t.add_mode;
                    egui::ComboBox::from_id_salt("track_add_mode").selected_text(tracks::MODES[t.add_mode].label).show_ui(ui, |ui| {
                        for (i, m) in tracks::MODES.iter().enumerate() {
                            ui.selectable_value(&mut t.add_mode, i, m.label);
                        }
                    });
                    let m = &tracks::MODES[t.add_mode];
                    let template_ok = m.name != "joyride" && (TEMPLATE..TEMPLATE + 5).contains(&t.add_base);
                    if before != t.add_mode || !((m.first..m.end).contains(&t.add_base) || template_ok) {
                        t.add_base = m.first;
                    }
                    ui.end_row();
                    ui.label("made from");
                    egui::ComboBox::from_id_salt("track_add_base").selected_text(base_text(t.add_base, &add_worlds)).height(380.0).show_ui(ui, |ui| {
                        for n in m.first..m.end {
                            ui.selectable_value(&mut t.add_base, n, base_text(n, &add_worlds));
                        }
                        if m.name != "joyride" {
                            // any Joyride world: a track of this mode on it (Challenge on world 4, which
                            // retail has none on); the game shows the Joyride level's picture
                            ui.separator();
                            for k in 0..5 {
                                ui.selectable_value(&mut t.add_base, TEMPLATE + k, base_text(TEMPLATE + k, &add_worlds))
                                    .on_hover_text(joyride_worlds.get(k as usize).cloned().unwrap_or_default());
                            }
                        }
                    });
                    ui.end_row();
                    ui.label("name");
                    ui.text_edit_singleline(&mut t.add_name);
                    ui.end_row();
                    ui.label("");
                    ui.checkbox(&mut t.add_locked, "locked until the game unlocks it");
                    ui.end_row();
                });
                ui.label(egui::RichText::new("Its race is the base track's level; link a level project of its own in its inspector.").small().weak());
                if ui.add_enabled(!t.add_name.trim().is_empty(), egui::Button::new("Add")).clicked() {
                    let name = t.add_name.trim().to_string();
                    if let Some(p) = self.content.proj.as_mut() {
                        let template = (t.add_base >= TEMPLATE).then(|| t.add_base - TEMPLATE);
                        let base = if template.is_some() { tracks::MODES[t.add_mode].first } else { t.add_base };
                        let uid = p.add_track(base, &name, !t.add_locked);
                        // the base track's own picture, named (the select shows it anyway: written out so it can be changed);
                        // a template's: the Joyride world's, and that world
                        if let Some(l) = tracks::picture_link(template.unwrap_or(t.add_base)) {
                            p.set_track_str(&uid, "thumbnail", Some(&l));
                        }
                        if let Some(w) = template.and_then(|k| joyride_worlds.get(k as usize)) {
                            p.set_track_str(&uid, "world", Some(w));
                        }
                        t.sel = Some(uid);
                        t.add_name.clear();
                        self.content.sel = None;
                        t.seen = None;
                    }
                    ui.close();
                }
            });
        });
        for (uid, e) in list {
            let base = e["extends"].as_str().and_then(tracks::base_number);
            let mode = base.and_then(tracks::mode_of).map(|m| m.label).unwrap_or("?");
            let lv = if linked.contains(&uid) { " ◆ level" } else { "" };
            let label = format!("{}   ({mode}{lv})", e["name"].as_str().unwrap_or("?"));
            let selected = self.content.tracks.sel.as_deref() == Some(uid.as_str());
            if ui.selectable_label(selected, label).on_hover_text(format!("{uid}\nmade from {}", base.map(tracks::label).unwrap_or_default())).clicked() {
                self.content.tracks.sel = Some(uid);
                self.content.sel = None;
                self.content.tracks.seen = None;
            }
        }
        ui.add_space(6.0);
    }

    /// The track's inspector, when a track is selected (true: drawn).
    pub(crate) fn tracks_inspector(&mut self, ui: &mut egui::Ui) -> bool {
        let Some(uid) = self.content.tracks.sel.clone() else { return false };
        let Some(entry) = self.content.proj.as_ref().and_then(|p| p.doc[tracks::SECTION].get(&uid)).cloned() else {
            self.content.tracks.sel = None;
            return false;
        };
        let level = self.content.proj.as_ref().and_then(|p| p.track_level(&uid));
        ui.heading("Track");
        ui.label(egui::RichText::new(&uid).small().weak());
        let mut name = entry["name"].as_str().unwrap_or("").to_string();
        let mut base = entry["extends"].as_str().and_then(tracks::base_number).unwrap_or(39);
        let mut unlocked = entry["unlocked"].as_bool().unwrap_or(false);
        let (old_name, old_base, old_unlocked) = (name.clone(), base, unlocked);
        let worlds = self.track_worlds(base);
        egui::Grid::new("track_entry").num_columns(2).show(ui, |ui| {
            ui.label("name");
            ui.text_edit_singleline(&mut name);
            ui.end_row();
            ui.label("made from");
            ui.add_enabled_ui(level.is_none(), |ui| {
                let m = tracks::mode_of(base);
                egui::ComboBox::from_id_salt("track_entry_base").selected_text(base_text(base, &worlds)).height(380.0).show_ui(ui, |ui| {
                    for n in m.map(|m| m.first..m.end).unwrap_or(0..0) {
                        ui.selectable_value(&mut base, n, base_text(n, &worlds));
                    }
                });
            })
            .response
            .on_disabled_hover_text("the level's own base track (its level.jsonc's extends)");
            ui.end_row();
            ui.label("mode");
            ui.label(tracks::mode_of(base).map(|m| m.label).unwrap_or("?"));
            ui.end_row();
            ui.label("unlocked");
            ui.checkbox(&mut unlocked, "from the start (else the game's unlocks decide)");
            ui.end_row();
        });
        if let Some(p) = self.content.proj.as_mut() {
            if name != old_name {
                p.set_track_str(&uid, "name", Some(&name));
            }
            if base != old_base {
                // the picture follows the base while it is the base's
                let thumb = entry["thumbnail"].as_str();
                if thumb.is_none() || thumb == tracks::picture_link(old_base).as_deref() {
                    p.set_track_str(&uid, "thumbnail", tracks::picture_link(base).as_deref());
                }
                p.set_track_str(&uid, "extends", Some(&tracks::base(base)));
            }
            if unlocked != old_unlocked {
                if let Some(e) = p.track_mut(&uid) {
                    e.insert("unlocked".into(), Value::Bool(unlocked));
                }
            }
        }
        ui.horizontal(|ui| {
            let busy = self.test_drive_busy() || self.content.tracks.building.is_some();
            let b = egui::Button::new(egui::RichText::new("▶ Drive it").strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
            if ui.add_enabled(!busy, b).on_hover_text("a race on this track (the mod as it is now, saved first; its level compiled)").clicked() {
                self.track_drive(&uid);
            }
            if ui.button("🗑 Delete").on_hover_text("the track, its compiled level and its link").clicked() {
                if let Some(p) = self.content.proj.as_mut() {
                    p.remove_track(&uid);
                }
                self.content.tracks.sel = None;
            }
        });
        ui.separator();
        egui::ScrollArea::vertical().id_salt("track_inspector").show(ui, |ui| {
            self.track_level_ui(ui, &uid, &entry, level.as_deref());
            ui.separator();
            self.track_picture_ui(ui, &uid, &entry, base);
        });
        true
    }

    fn track_level_ui(&mut self, ui: &mut egui::Ui, uid: &str, entry: &Value, level: Option<&Path>) {
        ui.strong("Level");
        match level {
            None => {
                ui.label("none: the race is the base track's level as it is");
            }
            Some(l) => {
                ui.label(egui::RichText::new(l.display().to_string()).monospace()).on_hover_text("the level project (the editor's link: never in the package)");
                match (entry["level"].as_str(), entry["world"].as_str()) {
                    (Some(f), Some(w)) => ui.label(egui::RichText::new(format!("compiled: {f}, over {w}")).small().weak()),
                    _ => ui.label(egui::RichText::new("not compiled yet: ▶ Drive it and Save as mod compile it").small().weak()),
                };
                if let Some(w) = entry["world"].as_str() {
                    // a world of the base game the data hasn't (the PC's multi-player drag)
                    let stem = w.rsplit(['\\', '/']).next().unwrap_or(w);
                    if let Some(info) = self.content_data_info().filter(|i| !w.starts_with('@') && !i.has_archive(stem)) {
                        ui.colored_label(egui::Color32::LIGHT_RED, format!("its world {stem}: {}: the game leaves this track out", hwvx_browser::content_music::not_on_release(&info)));
                    }
                }
                if !l.join("level.jsonc").is_file() {
                    ui.colored_label(egui::Color32::LIGHT_RED, "the folder has no level.jsonc any more");
                }
            }
        }
        ui.horizontal(|ui| {
            if ui.button(if level.is_some() { "Link another…" } else { "Link a level project…" }).on_hover_text("a folder with level.jsonc (the Level tab's projects): its race on this track").clicked() {
                if let Ok(Some(d)) = hwvx_browser::data_dir::choose_folder("A level project (its folder, with level.jsonc)") {
                    let r = self.content.proj.as_mut().map(|p| p.set_track_level(uid, Some(Path::new(&d))));
                    match r {
                        Some(Ok(())) => self.track_note(format!("linked {d}"), false),
                        Some(Err(e)) => self.track_note(format!("not linked: {e}"), true),
                        None => {}
                    }
                }
            }
            if let Some(l) = level {
                if ui.button("Open in Level tab").clicked() {
                    let d = l.display().to_string();
                    self.open_level(&d);
                    self.level.tab = true;
                    self.content.tab = false;
                }
                let busy = self.content.tracks.building.is_some();
                if ui.add_enabled(!busy, egui::Button::new("Compile")).on_hover_text("the level into the mod folder now").clicked() {
                    self.tracks_build(vec![(uid.to_string(), l.to_path_buf())], After::Nothing);
                }
                if ui.button("Unlink").on_hover_text("the base track's level again (the compiled level goes)").clicked() {
                    if let Some(Err(e)) = self.content.proj.as_mut().map(|p| p.set_track_level(uid, None)) {
                        self.track_note(e, true);
                    }
                }
            }
        });
        let errs: Vec<String> = self.content.tracks.errors.iter().filter(|(u, _)| u == uid).flat_map(|(_, e)| e.clone()).collect();
        for e in errs.iter().take(12) {
            ui.colored_label(egui::Color32::LIGHT_RED, e);
        }
    }

    fn track_picture_ui(&mut self, ui: &mut egui::Ui, uid: &str, entry: &Value, base: u32) {
        ui.strong("Picture in the world select");
        let cur = entry["thumbnail"].as_str().map(str::to_string);
        let own = cur.as_deref().is_some_and(tracks::is_own_picture);
        let mut want_own = own || self.content.tracks.own_mode.as_deref() == Some(uid);
        ui.horizontal(|ui| {
            ui.label("Picture:");
            ui.radio_value(&mut want_own, false, "the game's");
            ui.radio_value(&mut want_own, true, "your own");
        });
        if want_own != (own || self.content.tracks.own_mode.as_deref() == Some(uid)) {
            if want_own {
                self.content.tracks.own_mode = Some(uid.to_string());
            } else {
                self.content.tracks.own_mode = None;
                if own {
                    if let Some(p) = self.content.proj.as_mut() {
                        p.set_track_str(uid, "thumbnail", tracks::picture_link(base).as_deref());
                    }
                }
            }
        }
        if want_own {
            self.track_own_picture_ui(ui, uid, cur.as_deref().filter(|c| tracks::is_own_picture(c)));
            return;
        }
        self.track_pictures_load();
        let cur_tex = cur.as_deref().and_then(tracks::picture_texture);
        match cur.as_deref() {
            None => ui.label("the base track's own"),
            Some(l) if cur_tex.is_none() => ui.label(format!("{l} (another texture)")),
            Some(_) => ui.label(""),
        };
        if !self.content.tracks.pictures_note.is_empty() {
            ui.label(egui::RichText::new(&self.content.tracks.pictures_note).small().weak());
        }
        let mut pick: Option<Option<String>> = None;
        let pics = self.content.tracks.pictures.clone();
        let size = egui::vec2(112.0, 84.0);
        ui.horizontal_wrapped(|ui| {
            for (t, users) in tracks::pictures() {
                let names: Vec<&str> = users.iter().map(|&n| tracks::RETAIL[n as usize]).collect();
                let tip = format!("{}\n{}", names.join(", "), tracks::picture_link_of(t));
                let selected = cur_tex == Some(t);
                let r = match pics.as_ref().and_then(|m| m.get(&t)) {
                    Some(h) => ui.add(egui::Button::image(egui::Image::new(h).fit_to_exact_size(size)).selected(selected)),
                    None => ui.add(egui::Button::new(names.first().copied().unwrap_or("?")).selected(selected)),
                };
                if r.on_hover_text(tip).clicked() {
                    pick = Some(Some(tracks::picture_link_of(t)));
                }
            }
        });
        if ui.small_button("the base track's own").on_hover_text(tracks::picture_link(base).unwrap_or_default()).clicked() {
            pick = Some(None);
        }
        if let (Some(v), Some(p)) = (pick, self.content.proj.as_mut()) {
            p.set_track_str(uid, "thumbnail", v.as_deref());
        }
    }

    /// "your own": a PNG chosen from disk, copied into the mod's images/, with its preview as
    /// the select draws it (stretched to about 5:3).
    fn track_own_picture_ui(&mut self, ui: &mut egui::Ui, uid: &str, cur: Option<&str>) {
        if ui.button("Choose image…").on_hover_text(format!("a PNG of yours, at most {0} x {0}: copied into the mod's images/", tracks::OWN_PICTURE_SIDE)).clicked() {
            if let Some(f) = choose_png("The track's picture (a PNG)") {
                let r = self.content.proj.as_mut().map(|p| p.set_track_own_picture(uid, Path::new(&f)));
                match r {
                    Some(Ok(rel)) => {
                        self.content.tracks.own_preview = None;
                        self.track_note(format!("the picture is {rel}"), false);
                    }
                    Some(Err(e)) => self.track_note(format!("not taken: {e}"), true),
                    None => {}
                }
            }
        }
        let Some(rel) = cur else {
            ui.label(egui::RichText::new("No picture of yours yet: until there is, the select shows the base track's.").weak());
            return;
        };
        let Some(path) = self.content.proj.as_ref().map(|p| p.dir.join(rel)) else { return };
        let stamp = std::fs::metadata(&path).and_then(|m| m.modified()).ok();
        let fresh = self.content.tracks.own_preview.as_ref().is_some_and(|(p, s, _)| *p == path && *s == stamp);
        if !fresh {
            let r = tracks::read_picture(&path).map(|pic| {
                let size = [pic.w, pic.h];
                let img = egui::ColorImage::from_rgba_unmultiplied(size, &pic.rgba);
                (self.ctx.load_texture("track_own_picture", img, egui::TextureOptions::LINEAR), size)
            });
            self.content.tracks.own_preview = Some((path.clone(), stamp, r));
        }
        ui.label(egui::RichText::new(rel).monospace());
        match &self.content.tracks.own_preview {
            Some((_, _, Ok((h, size)))) => {
                ui.add(egui::Image::new(h).fit_to_exact_size(egui::vec2(250.0, 150.0)).corner_radius(4.0));
                ui.label(
                    egui::RichText::new(format!("{} x {}: the game draws it at the base picture's 256 x 256, stretched to the select's frame (about 5:3) as it does its own", size[0], size[1]))
                        .small()
                        .weak(),
                );
            }
            Some((_, _, Err(e))) => {
                ui.colored_label(egui::Color32::LIGHT_RED, e);
            }
            None => {}
        }
    }

    /// The pictures decoded once from the player's data, off the UI thread.
    fn track_pictures_load(&mut self) {
        let t = &mut self.content.tracks;
        if t.pictures.is_some() || t.pictures_rx.is_some() || !t.pictures_note.is_empty() {
            return;
        }
        let Some(data) = self.testdrive_options().data else {
            self.content.tracks.pictures_note = "the game data isn't known (File > Game data…): the pictures are named, not shown".into();
            return;
        };
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let _ = tx.send(tracks::base_pictures(&data));
        });
        self.content.tracks.pictures_rx = Some(rx);
    }

    /// The tracks' levels compiled into the mod folder off the UI thread, then `after`.
    fn tracks_build(&mut self, levels: Vec<(String, PathBuf)>, after: After) {
        let Some(p) = self.content.proj.as_ref() else { return };
        let o = self.testdrive_options();
        let Some(data) = o.data.clone() else {
            self.track_note("the game data isn't known (File > Game data…): a level compiles over the player's own world", true);
            return;
        };
        let dir = p.dir.clone();
        let (tx, rx) = channel();
        let n = levels.len();
        std::thread::spawn(move || {
            let base = xdlrs::level::BaseOpts { base: None, data: Some(data), cache: Some(testdrive::base_cache(&o.scratch)) };
            let r: Built = match testdrive::level_env(&o.tools) {
                Ok(env) => levels.iter().map(|(u, l)| tracks::build_level(&env, &dir, u, l, &base).map_err(|e| (u.clone(), e))).collect(),
                Err(e) => levels.iter().map(|(u, _)| Err((u.clone(), vec![e.clone()]))).collect(),
            };
            let _ = tx.send(r);
        });
        self.content.tracks.building = Some((after, rx));
        self.content.tracks.errors.clear();
        self.track_note(format!("compiling {n} track level{} (the first time about 30 s)…", if n == 1 { "" } else { "s" }), false);
    }

    fn tracks_tick(&mut self) {
        if let Some(rx) = &self.content.tracks.pictures_rx {
            if let Ok(r) = rx.try_recv() {
                self.content.tracks.pictures_rx = None;
                match r {
                    Ok(pics) => {
                        let m = pics
                            .into_iter()
                            .map(|p| {
                                let img = egui::ColorImage::from_rgba_unmultiplied([p.w, p.h], &p.rgba);
                                (p.texture, self.ctx.load_texture(format!("track_picture_{}", p.texture), img, egui::TextureOptions::LINEAR))
                            })
                            .collect();
                        self.content.tracks.pictures = Some(m);
                    }
                    Err(e) => self.content.tracks.pictures_note = format!("the pictures: {e}"),
                }
            } else {
                self.ctx.request_repaint_after(std::time::Duration::from_millis(200));
            }
        }
        let Some((after, rx)) = &self.content.tracks.building else { return };
        let Ok(built) = rx.try_recv() else {
            self.ctx.request_repaint_after(std::time::Duration::from_millis(200));
            return;
        };
        let after = after.clone();
        self.content.tracks.building = None;
        let mut errors = vec![];
        let mut done = 0;
        if let Some(p) = self.content.proj.as_mut() {
            for b in built {
                match b {
                    Ok(b) => {
                        p.apply_build(&b);
                        done += 1;
                    }
                    Err(e) => errors.push(e),
                }
            }
        }
        if !errors.is_empty() {
            let names: Vec<String> = errors.iter().map(|(u, _)| self.track_name(u)).collect();
            self.track_note(format!("not compiled: {} (the errors are in the track's inspector)", names.join(", ")), true);
            self.content.tracks.errors = errors;
            return;
        }
        self.content_save();
        if self.content.proj.as_ref().is_some_and(ModProject::dirty) {
            return;
        }
        self.track_note(format!("compiled {done} level{}", if done == 1 { "" } else { "s" }), false);
        match after {
            After::Nothing => {}
            After::Drive(uid) => self.track_launch(&uid),
            After::Pack => {
                self.content.tracks.pack_ready = true;
                self.content_pack();
                self.content.tracks.pack_ready = false;
            }
        }
    }

    fn track_name(&self, uid: &str) -> String {
        self.content.proj.as_ref().and_then(|p| p.doc[tracks::SECTION][uid]["name"].as_str().map(str::to_string)).unwrap_or_else(|| uid.to_string())
    }

    /// Save as mod's first step (content_pack): the tracks' levels compiled into the folder
    /// first (true: started; Save as mod goes on once they are).
    pub(crate) fn tracks_before_pack(&mut self) -> bool {
        if self.content.tracks.pack_ready || self.content.tracks.building.is_some() {
            return self.content.tracks.building.is_some();
        }
        let Some(p) = self.content.proj.as_ref() else { return false };
        let levels = p.track_levels();
        if levels.is_empty() || p.dirty() {
            return false;
        }
        self.tracks_build(levels, After::Pack);
        true
    }

    /// ▶ Drive it: saved, its level compiled (if it has one), then the game into a race on it.
    fn track_drive(&mut self, uid: &str) {
        self.content_save();
        let Some(p) = self.content.proj.as_ref() else { return };
        if p.dirty() {
            return; // (not saved: the message says why)
        }
        match p.track_level(uid) {
            Some(l) => self.tracks_build(vec![(uid.to_string(), l)], After::Drive(uid.to_string())),
            None => self.track_launch(uid),
        }
    }

    /// The game started in a race on the track, the mod run from a copy of its folder.
    fn track_launch(&mut self, uid: &str) {
        let Some(p) = self.content.proj.as_ref() else { return };
        let base = p.doc[tracks::SECTION][uid]["extends"].as_str().and_then(tracks::base_number).unwrap_or(u32::MAX);
        let Some(start) = tracks::start_string(uid, base) else {
            self.track_note(format!("{uid}: not a track the game can start (made from {base})"), true);
            return;
        };
        let o = self.testdrive_options();
        let mods = o.scratch.join("content-mods");
        let _ = std::fs::remove_dir_all(&mods);
        let dest = mods.join(p.dir.file_name().unwrap_or_default());
        if let Err(e) = copy_dir(&p.dir, &dest) {
            self.track_note(format!("the mod could not be copied for the test: {e}"), true);
            return;
        }
        let prepared = Prepared { start, mods, level: PathBuf::new(), cached: false, tool_ms: 0.0, ms: 0.0 };
        if let Some(d) = self.level.drive.as_mut() {
            d.stop();
        }
        match TestDrive::launch(&o, &prepared, Instant::now()) {
            Ok(d) => {
                self.level.drive = Some(d);
                self.track_note("starting the game…", false);
            }
            Err(e) => self.track_note(e, true),
        }
    }
}

fn copy_dir(from: &Path, to: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(to)?;
    for e in std::fs::read_dir(from)? {
        let e = e?;
        let t = to.join(e.file_name());
        if e.file_type()?.is_dir() {
            copy_dir(&e.path(), &t)?;
        } else {
            std::fs::copy(e.path(), t)?;
        }
    }
    Ok(())
}

/// A PNG, in the system's dialog (None: cancelled, or no dialog here). The prompt reaches it as
/// an argument or an environment variable, never inside a script's text.
fn choose_png(prompt: &str) -> Option<String> {
    let out = if cfg!(target_os = "macos") {
        Command::new("osascript")
            .args(["-e", "on run argv", "-e", "POSIX path of (choose file with prompt (item 1 of argv) of type {\"png\"})", "-e", "end run", prompt])
            .output()
    } else if cfg!(windows) {
        xdlrs::command("powershell")
            .env("HWVX_PROMPT", prompt)
            .args([
                "-NoProfile",
                "-Command",
                "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = $env:HWVX_PROMPT; $d.Filter = 'PNG image (*.png)|*.png'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }",
            ])
            .output()
    } else {
        Command::new("zenity")
            .args(["--file-selection", &format!("--title={prompt}"), "--file-filter=PNG image | *.png *.PNG"])
            .output()
            .or_else(|_| Command::new("kdialog").args(["--title", prompt, "--getopenfilename", ".", "*.png *.PNG|PNG image"]).output())
    };
    let out = out.ok().filter(|o| o.status.success())?;
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    (!s.is_empty()).then_some(s)
}
