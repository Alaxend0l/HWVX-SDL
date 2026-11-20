//! The Content tab: a mod of new cars and weapons made from the game's own (src/content.rs), its
//! inspector (name, base, paint, the numbers that differ from the base), a test drive with the new
//! car or weapon (the mod run from its folder), and Save as mod (.xmod).

use crate::Browser;
use eframe::egui;
use hwvx_browser::cars;
use hwvx_browser::content::{Kind, ModProject};
use hwvx_browser::content_retail::Target;
use hwvx_browser::testdrive::{Prepared, TestDrive};
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, channel};
use std::time::Instant;

#[derive(Default)]
pub(crate) struct ContentState {
    pub tab: bool,
    /// the folder field
    pub folder: String,
    pub proj: Option<ModProject>,
    pub sel: Option<(Kind, String)>,
    pub message: String,
    pub error: bool,
    // New mod…
    pub new_name: String,
    pub new_author: String,
    pub new_in: String,
    // + New car / weapon
    pub add_name: String,
    pub add_base: u32,
    /// the texture the paint applies to (a car's body is usually its first)
    pub paint_texture: u32,
    pub packing: Option<Receiver<Result<String, String>>>,
    /// Retail changes (src/content_retail_ui.rs)
    pub retail: crate::content_retail_ui::RetailState,
    /// sound replacements (src/content_sounds_ui.rs)
    pub sounds: crate::content_sounds_ui::SoundsState,
    /// the Soundtrack (content_music_ui.rs)
    pub music: crate::content_music_ui::MusicState,
    /// the Tracks section (content_tracks_ui.rs)
    pub tracks: crate::content_tracks_ui::TracksState,
    /// the sections' selections last frame (content_one_selection)
    pub seen: [String; 5],
    /// the game data's release and archives (content_data_info): read again when the data changes
    pub data_info: Option<(PathBuf, Option<std::rc::Rc<hwvx_browser::content::DataInfo>>)>,
}

pub(crate) fn base_label(kind: Kind, n: u32) -> String {
    match kind {
        Kind::Car => cars::label(n),
        Kind::Weapon => format!("{n}  item {n}"),
    }
}

/// A folder name from a mod's name: letters, digits, - and _.
fn slug(name: &str) -> String {
    let s: String = name.trim().chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c.to_ascii_lowercase() } else { '-' }).collect();
    let s = s.trim_matches('-').to_string();
    if s.is_empty() { "my-mod".into() } else { s }
}

impl Browser {
    /// The game data's release and archives (None: no data known, or it can't be read), read
    /// once per data path.
    pub(crate) fn content_data_info(&mut self) -> Option<std::rc::Rc<hwvx_browser::content::DataInfo>> {
        let data = self.args.data.clone().or_else(|| self.hello["data"].as_str().map(PathBuf::from))?;   // as testdrive_options
        if self.content.data_info.as_ref().is_none_or(|(p, _)| *p != data) {
            let info = hwvx_browser::content::DataInfo::read(&data).ok().map(std::rc::Rc::new);
            self.content.data_info = Some((data, info));
        }
        self.content.data_info.as_ref().and_then(|(_, i)| i.clone())
    }

    fn content_note(&mut self, s: impl Into<String>, error: bool) {
        self.content.message = s.into();
        self.content.error = error;
    }

    pub(crate) fn content_open(&mut self, dir: &str) {
        if self.content.proj.as_ref().is_some_and(|p| p.dirty()) {
            self.content_note("save the open mod first (or reopen it to drop the edits)", true);
            return;
        }
        match ModProject::open(Path::new(dir.trim())) {
            Ok(p) => {
                self.content.folder = p.dir.display().to_string();
                self.content_note(format!("opened {}", p.name()), false);
                self.content.sel = p.entries(Kind::Car).first().map(|(u, _)| (Kind::Car, u.clone()));
                self.content.proj = Some(p);
                self.content.tab = true;
                self.level.tab = false;
            }
            Err(e) => self.content_note(e, true),
        }
    }

    pub(crate) fn content_save(&mut self) {
        let Some(p) = self.content.proj.as_mut() else { return };
        match p.save() {
            Ok(()) => self.content_note("saved mod.jsonc", false),
            Err(e) => self.content_note(format!("not saved: {e}"), true),
        }
    }

    /// The left panel's Content tab.
    pub(crate) fn content_panel(&mut self, ui: &mut egui::Ui) {
        self.content_tick();
        ui.label("Mod (its folder, with mod.jsonc)");
        ui.horizontal(|ui| {
            let r = ui.add(egui::TextEdit::singleline(&mut self.content.folder).desired_width(130.0)).on_hover_text(self.content.folder.clone());
            if (r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter))) | ui.button("Open").clicked() {
                let d = self.content.folder.clone();
                self.content_open(&d);
            }
            if ui.button("…").on_hover_text("choose the folder").clicked() {
                if let Ok(Some(d)) = hwvx_browser::data_dir::choose_folder("A mod (its folder, with mod.jsonc)") {
                    self.content_open(&d);
                }
            }
        });
        egui::CollapsingHeader::new("New mod…").default_open(self.content.proj.is_none()).show(ui, |ui| {
            let c = &mut self.content;
            ui.horizontal(|ui| {
                ui.label("name");
                ui.text_edit_singleline(&mut c.new_name);
            });
            ui.horizontal(|ui| {
                ui.label("author");
                ui.text_edit_singleline(&mut c.new_author);
            });
            ui.horizontal(|ui| {
                ui.label("in");
                ui.add(egui::TextEdit::singleline(&mut c.new_in).desired_width(110.0).hint_text("a folder"));
                if ui.button("…").clicked() {
                    if let Ok(Some(d)) = hwvx_browser::data_dir::choose_folder("Where the mod's folder goes") {
                        c.new_in = d;
                    }
                }
            });
            let ok = !c.new_name.trim().is_empty() && !c.new_in.trim().is_empty();
            if ui.add_enabled(ok, egui::Button::new("Make it")).clicked() {
                let dir = Path::new(c.new_in.trim()).join(slug(&c.new_name));
                match ModProject::create(&dir, c.new_name.trim(), &c.new_author) {
                    Ok(p) => {
                        c.folder = p.dir.display().to_string();
                        c.message = format!("made {}", dir.display());
                        c.error = false;
                        c.proj = Some(p);
                        c.sel = None;
                    }
                    Err(e) => {
                        c.message = e;
                        c.error = true;
                    }
                }
            }
        });
        if !self.content.message.is_empty() {
            let col = if self.content.error { egui::Color32::LIGHT_RED } else { ui.visuals().weak_text_color() };
            ui.colored_label(col, self.content.message.clone());
        }
        let Some(p) = self.content.proj.as_ref() else {
            ui.label(egui::RichText::new("Open a mod or make a new one: it holds new cars and weapons made from the game's own (their numbers, their paint), never the game's files.").weak());
            return;
        };
        ui.separator();
        ui.horizontal(|ui| {
            ui.strong(p.name().to_string());
            if p.dirty() {
                ui.label(egui::RichText::new("● unsaved").color(egui::Color32::YELLOW));
            }
        });
        let dirty = p.dirty();
        ui.horizontal(|ui| {
            if ui.add_enabled(dirty, egui::Button::new("💾 Save")).on_hover_text("write mod.jsonc (Ctrl+S)").clicked() {
                self.content_save();
            }
            let packing = self.content.packing.is_some();
            if ui.add_enabled(!packing, egui::Button::new(if packing { "⏳ Save as mod…" } else { "Save as mod…" })).on_hover_text("an .xmod players install (checked for game data first)").clicked() {
                self.content_pack();
            }
        });
        ui.separator();
        for kind in [Kind::Car, Kind::Weapon] {
            let entries = self.content.proj.as_ref().map(|p| p.entries(kind)).unwrap_or_default();
            ui.horizontal(|ui| {
                ui.strong(if kind == Kind::Car { format!("Cars ({})", entries.len()) } else { format!("Weapons ({})", entries.len()) });
                ui.menu_button(if kind == Kind::Car { "+ New car" } else { "+ New weapon" }, |ui| {
                    let c = &mut self.content;
                    c.add_base = c.add_base.min(kind.retail_count() - 1);
                    ui.label(if kind == Kind::Car { "made from" } else { "made from (the item select's weapons)" });
                    egui::ComboBox::from_id_salt(("content_base", kind == Kind::Car)).selected_text(base_label(kind, c.add_base)).height(380.0).show_ui(ui, |ui| {
                        for n in 0..kind.retail_count() {
                            ui.selectable_value(&mut c.add_base, n, base_label(kind, n));
                        }
                    });
                    ui.horizontal(|ui| {
                        ui.label("name");
                        ui.text_edit_singleline(&mut c.add_name);
                    });
                    if ui.add_enabled(!c.add_name.trim().is_empty(), egui::Button::new("Add")).clicked() {
                        let name = c.add_name.trim().to_string();
                        if let Some(p) = c.proj.as_mut() {
                            let uid = p.add(kind, c.add_base, &name);
                            c.sel = Some((kind, uid));
                            c.add_name.clear();
                        }
                        ui.close();
                    }
                });
            });
            for (uid, e) in entries {
                let base = e["extends"].as_str().and_then(|x| kind.base_number(x)).map(|n| base_label(kind, n)).unwrap_or_default();
                let selected = self.content.sel.as_ref().is_some_and(|(k, u)| *k == kind && *u == uid);
                let label = format!("{}   ({})", e["name"].as_str().unwrap_or("?"), base.split_once("  ").map(|x| x.1).unwrap_or(&base));
                if ui.selectable_label(selected, label).on_hover_text(&uid).clicked() {
                    self.content.sel = Some((kind, uid));
                }
            }
            ui.add_space(6.0);
        }
        self.content_retail_panel(ui);
        self.content_sounds_panel(ui);
        self.music_panel(ui);
        self.tracks_list(ui);
    }

    /// One selection across the tab's sections (cars and weapons, retail changes, sounds, songs,
    /// tracks): the one picked last stays, the others are let go.
    fn content_one_selection(&mut self) {
        let c = &mut self.content;
        let now = [format!("{:?}", c.sel), format!("{:?}", c.retail.sel), format!("{:?}", c.sounds.sel), format!("{:?}", c.music.sel), format!("{:?}", c.tracks.sel)];
        let picked = (0..now.len()).find(|&i| now[i] != c.seen[i] && now[i] != "None");
        if let Some(i) = picked {
            if i != 0 { c.sel = None; }
            if i != 1 { c.retail.sel = None; }
            if i != 2 { c.sounds.sel = None; }
            if i != 3 { c.music.sel = None; }
            if i != 4 { c.tracks.sel = None; }
        }
        c.seen = [format!("{:?}", c.sel), format!("{:?}", c.retail.sel), format!("{:?}", c.sounds.sel), format!("{:?}", c.music.sel), format!("{:?}", c.tracks.sel)];
    }

    /// The right panel while the Content tab is on show.
    pub(crate) fn content_inspector(&mut self, ui: &mut egui::Ui) {
        self.content_one_selection();
        if self.content.sel.is_none() {
            if self.content.music.sel.is_some() {
                return self.music_inspector(ui);
            }
            if self.content_retail_inspector(ui) || self.content_sounds_inspector(ui) || self.tracks_inspector(ui) {
                return;
            }
        }
        let Some((kind, uid)) = self.content.sel.clone() else {
            ui.heading("Content");
            ui.label("Select a car or weapon on the left, or add one (+ New car, + New weapon).");
            return;
        };
        let Some(entry) = self.content.proj.as_ref().and_then(|p| p.doc[kind.section()].get(&uid)).cloned() else {
            self.content.sel = None;
            return;
        };
        ui.heading(if kind == Kind::Car { "Car" } else { "Weapon" });
        ui.label(egui::RichText::new(&uid).small().weak());
        let mut name = entry["name"].as_str().unwrap_or("").to_string();
        let mut base = entry["extends"].as_str().and_then(|x| kind.base_number(x)).unwrap_or(0);
        let mut unlocked = entry["unlocked"].as_bool().unwrap_or(false);
        let (old_name, old_base, old_unlocked) = (name.clone(), base, unlocked);
        egui::Grid::new("content_entry").num_columns(2).show(ui, |ui| {
            ui.label("name");
            ui.text_edit_singleline(&mut name);
            ui.end_row();
            ui.label("made from");
            egui::ComboBox::from_id_salt("content_entry_base").selected_text(base_label(kind, base)).height(380.0).show_ui(ui, |ui| {
                for n in 0..kind.retail_count() {
                    ui.selectable_value(&mut base, n, base_label(kind, n));
                }
            });
            ui.end_row();
            ui.label("unlocked");
            ui.checkbox(&mut unlocked, "from the start (else the game's unlocks decide)");
            ui.end_row();
        });
        if let Some(e) = self.content.proj.as_mut().and_then(|p| p.entry_mut(kind, &uid)) {
            if name != old_name {
                e.insert("name".into(), Value::String(name));
            }
            if base != old_base {
                e.insert("extends".into(), Value::String(kind.base(base)));
            }
            if unlocked != old_unlocked {
                e.insert("unlocked".into(), Value::Bool(unlocked));
            }
        }
        ui.horizontal(|ui| {
            let busy = self.test_drive_busy();
            let b = egui::Button::new(egui::RichText::new(if kind == Kind::Car { "▶ Drive it" } else { "▶ Try it" }).strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
            if ui.add_enabled(!busy, b).on_hover_text(if kind == Kind::Car { "a Challenge race in this car (the mod as it is now, saved first)" } else { "a Battle with this weapon (the mod as it is now, saved first)" }).clicked() {
                self.content_test(kind, &uid);
            }
            if ui.button("🗑 Delete").clicked() {
                if let Some(p) = self.content.proj.as_mut() {
                    p.remove(kind, &uid);
                }
                self.content.sel = None;
            }
        });
        ui.separator();
        let target = Target::Entry(kind, uid.clone());
        egui::ScrollArea::vertical().show(ui, |ui| {
            if kind == Kind::Car {
                self.content_paint(ui, &target);
                ui.separator();
                self.content_sounds_car(ui, &uid);
                ui.separator();
            }
            self.content_numbers(ui, &target);
        });
    }

    /// The numbers of a target's parts, a ticked one the mod's: an entry's own patch, or a retail
    /// car or weapon's top-level patch (src/content_retail.rs).
    pub(crate) fn content_numbers(&mut self, ui: &mut egui::Ui, target: &Target) {
        let kind = target.kind();
        ui.label(egui::RichText::new("Numbers: a ticked one is the mod's; the others stay the base's.").weak());
        for &(part, st, what) in kind.parts() {
            let schema = self.schema(st);
            let Some(props) = schema["properties"].as_object().cloned() else { continue };
            let set = self.content.proj.as_ref().map(|p| p.target_changed(target, part)).unwrap_or(0);
            egui::CollapsingHeader::new(format!("{what}  ({st}{})", if set > 0 { format!(", {set} changed") } else { String::new() })).id_salt(("content_part", part)).show(ui, |ui| {
                egui::Grid::new(("content_fields", part)).num_columns(2).striped(true).show(ui, |ui| {
                    for (field, d) in props.iter() {
                        let numeric = matches!(d["type"].as_str(), Some("number" | "integer"));
                        if !numeric || field.starts_with("unk_") || field.starts_with("num") {
                            continue;
                        }
                        let int = d["type"] == "integer";
                        let cur = self.content.proj.as_ref().and_then(|p| p.target_value(target, part, field));
                        let mut on = cur.is_some();
                        let mut v = cur.unwrap_or(0.0);
                        let tip = d["description"].as_str().unwrap_or("").to_string();
                        let r = ui.checkbox(&mut on, field.as_str());
                        if !tip.is_empty() {
                            r.on_hover_text(&tip);
                        }
                        let changed = ui.add_enabled(on, egui::DragValue::new(&mut v).speed(if int { 1.0 } else { 0.01 }).max_decimals(if int { 0 } else { 4 })).changed();
                        ui.end_row();
                        if let Some(p) = self.content.proj.as_mut() {
                            if on != cur.is_some() || changed {
                                p.set_target_value(target, part, field, on.then_some(if int { v.round() } else { v }));
                            }
                        }
                    }
                });
            });
        }
    }

    pub(crate) fn content_paint(&mut self, ui: &mut egui::Ui, target: &Target) {
        ui.strong("Paint");
        let tex = self.content.paint_texture;
        let Some((mut hue, mut bright)) = self.content.proj.as_ref().map(|p| p.target_paint(target, tex)) else { return };
        let (h0, b0) = (hue, bright);
        egui::Grid::new("content_paint").num_columns(2).show(ui, |ui| {
            ui.label("texture");
            ui.add(egui::DragValue::new(&mut self.content.paint_texture).range(0..=63)).on_hover_text("which of the car's textures (its body is usually 0)");
            ui.end_row();
            ui.label("hue");
            ui.add(egui::Slider::new(&mut hue, -180.0..=180.0).suffix("°"));
            ui.end_row();
            ui.label("brightness");
            ui.add(egui::Slider::new(&mut bright, 0.2..=2.0));
            ui.end_row();
        });
        ui.label(egui::RichText::new("A recipe on the player's own texture: the mod holds no pixels.").small().weak());
        if (hue, bright) != (h0, b0) && self.content.paint_texture == tex {
            if let Some(p) = self.content.proj.as_mut() {
                p.set_target_paint(target, tex, hue, bright);
            }
        }
    }

    /// The game started in a race with the entry, the mod run from a copy of its folder.
    pub(crate) fn content_test(&mut self, kind: Kind, uid: &str) {
        self.content_save();
        let Some(p) = self.content.proj.as_ref() else { return };
        if p.dirty() {
            return; // (not saved: the message says why)
        }
        let o = self.testdrive_options();
        let mods = o.scratch.join("content-mods");
        let _ = std::fs::remove_dir_all(&mods);
        let dest = mods.join(p.dir.file_name().unwrap_or_default());
        if let Err(e) = copy_dir(&p.dir, &dest) {
            self.content_note(format!("the mod could not be copied for the test: {e}"), true);
            return;
        }
        let start = match kind {
            Kind::Car => format!("race:mode=challenge,car={uid}"),
            Kind::Weapon => format!("race:mode=battle,item={uid}"),
        };
        let prepared = Prepared { start, mods, level: PathBuf::new(), cached: false, tool_ms: 0.0, ms: 0.0 };
        if let Some(d) = self.level.drive.as_mut() {
            d.stop();
        }
        match TestDrive::launch(&o, &prepared, Instant::now()) {
            Ok(d) => {
                self.level.drive = Some(d);
                self.content_note("starting the game…", false);
            }
            Err(e) => self.content_note(e, true),
        }
    }

    /// Save as mod: the folder packed into an .xmod after the no-game-data check.
    pub(crate) fn content_pack(&mut self) {
        if self.tracks_before_pack() {
            return; // the tracks' levels compiled first: it goes on from there
        }
        let Some(p) = self.content.proj.as_ref() else { return };
        if p.dirty() {
            self.content_note("save the mod first (Ctrl+S): the package is made from its files", true);
            return;
        }
        let name = slug(p.name());
        let Some(out) = Self::choose_save_file(&format!("{name}.xmod")) else { return };
        let o = self.testdrive_options();
        let Some(data) = o.data.clone() else {
            self.content_note("the game data isn't known (File > Game data…): the check for game data needs it", true);
            return;
        };
        let root = o.tools.parent().map(Path::to_path_buf).unwrap_or_default();
        let dir = p.dir.clone();
        let cache = o.scratch.join("base-cache");
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let r = xdlrs::level_share::check_and_pack(&root, &dir, Path::new(&out), None, Some(&data), &cache).map_err(|e| xdlrs::level::top_message(&e));
            let _ = tx.send(r);
        });
        self.content.packing = Some(rx);
        self.content_note("checking for game data and packing (the first time about 30 s)…", false);
    }

    fn content_tick(&mut self) {
        let Some(rx) = &self.content.packing else { return };
        if let Ok(r) = rx.try_recv() {
            self.content.packing = None;
            match r {
                Ok(line) => self.content_note(line, false),
                Err(e) => self.content_note(format!("not packed: {e}"), true),
            }
        } else {
            self.ctx.request_repaint_after(std::time::Duration::from_millis(200));
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
