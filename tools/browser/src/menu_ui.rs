//! The menu bar (File, Edit, View, Tools, Help) along the top of the window, the game data
//! screen (where the editor asks for the player's copy of the game: on the first start, from
//! File > Game data, or when the game can't find it), and the About window.

use crate::{Browser, Pending, shown_nodes};
use eframe::egui;
use hwvx_browser::{data_dir, extract};
use serde_json::json;
use std::path::{Path, PathBuf};

const GUIDE_URL: &str =
    "https://github.com/Alaxend0l/HWVX-X-FILE-EDITOR/blob/main/docs/level-editor.md";

/// The game data screen's state.
#[derive(Default)]
pub(crate) struct DataPick {
    pub text: String,
    pub error: Option<String>,
    /// what the game's own launcher plays (data_dir::from_launcher)
    pub suggestion: Option<PathBuf>,
    /// a folder is in use already (changing it from the menu): Cancel goes back to it
    pub can_cancel: bool,
    /// a disc image (or the PC CD as a folder) chosen: what it is, with "Extract it…" (and for a
    /// GameCube image "Use it directly")
    pub disc: Option<extract::DiscInfo>,
}

fn shortcut(ctx: &egui::Context, cmd: bool, shift: bool, key: egui::Key) -> String {
    let m = match (cmd, shift) {
        (true, true) => egui::Modifiers::COMMAND | egui::Modifiers::SHIFT,
        (true, false) => egui::Modifiers::COMMAND,
        _ => egui::Modifiers::NONE,
    };
    ctx.format_shortcut(&egui::KeyboardShortcut::new(m, key))
}

impl Browser {
    // ---- game data ---------------------------------------------------------------------------

    /// Shows the game data screen in place of the editor.
    pub(crate) fn ask_for_data(&mut self, why: Option<String>) {
        let current = self.args.data.clone();
        self.data_pick = Some(DataPick {
            text: current
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_default(),
            error: why,
            suggestion: data_dir::from_launcher().filter(|s| Some(s) != current.as_ref()),
            can_cancel: current.is_some() && self.connect_error.is_none(),
            disc: None,
        });
    }

    /// Uses `p` if it holds the game: remembered for next time, the game restarted on it.
    pub(crate) fn use_data(&mut self, p: &Path) {
        match data_dir::check(p) {
            Ok(d) => {
                let pref = self
                    .args
                    .pref
                    .clone()
                    .unwrap_or_else(hwvx_browser::resources::default_pref);
                let kept = data_dir::remember(&pref, &d);
                self.args.data = Some(d);
                self.data_pick = None;
                self.connect();
                if let Err(e) = kept {
                    self.fail(format!(
                        "the game data folder will be asked for again next time: {e}"
                    ));
                }
            }
            Err(e) => {
                if let Some(dp) = self.data_pick.as_mut() {
                    dp.error = Some(e);
                } else {
                    self.fail(e);
                }
            }
        }
    }

    /// A choice on the game data screen: a disc image (or the PC CD as a folder) is shown with what
    /// it is and "Extract it…" (a GameCube image also "Use it directly"); anything else is used.
    fn pick_data(&mut self, p: &Path) {
        let disc_like = (p.is_file() && xdlrs::disc::is_image_name(p)) || (p.is_dir() && data_dir::check(p).is_err());
        if disc_like {
            let r = extract::inspect(p);
            if let Some(dp) = self.data_pick.as_mut() {
                match r {
                    Ok(info) => {
                        dp.error = None;
                        dp.disc = Some(info);
                        return;
                    }
                    Err(e) if p.is_file() => {
                        dp.error = Some(e);
                        dp.disc = None;
                        return;
                    }
                    Err(_) => {} // a folder: data_dir::check's own message
                }
            }
        }
        self.use_data(p);
    }

    pub(crate) fn data_screen(&mut self, ui: &mut egui::Ui) {
        let Some(dp) = self.data_pick.as_mut() else {
            return;
        };
        let mut chosen: Option<PathBuf> = None;
        let mut direct: Option<PathBuf> = None;
        let mut extract_it: Option<PathBuf> = None;
        let mut cancel = false;
        egui::CentralPanel::default().show(ui, |ui| {
            ui.add_space(24.0);
            // one column of 720 points in the middle, its contents left-aligned
            let margin = ((ui.available_width() - 720.0) / 2.0).max(0.0);
            ui.horizontal_top(|ui| {
            ui.add_space(margin);
            ui.vertical(|ui| {
                ui.set_max_width(720.0);
                ui.heading("Where is your copy of the game?");
                ui.add_space(8.0);
                ui.label(
                    "The editor shows and test-drives your levels in your own copy of Hot Wheels: Velocity X. \
                     Choose the GameCube disc's extracted files (the folder with HW_share.xgc, or the files/ \
                     folder Dolphin's Extract Entire Disc makes), a GameCube disc image (.iso, .gcm), or the PC or \
                     PS2 release's extracted data. A PC or PS2 disc image (or a .gcz) is extracted first: choose it and \
                     the editor offers to (File > Extract a disc… too). Nothing from it goes into your levels.",
                );
                ui.add_space(16.0);
                if let Some(s) = dp.suggestion.clone() {
                    egui::Frame::group(ui.style()).show(ui, |ui| {
                        ui.set_width(ui.available_width());
                        ui.label("The game's launcher plays this one:");
                        ui.horizontal(|ui| {
                            ui.add(egui::Label::new(egui::RichText::new(s.display().to_string()).monospace()).truncate());
                        });
                        if ui.button("Use it").clicked() {
                            direct = Some(s.clone());
                        }
                    });
                    ui.add_space(12.0);
                }
                ui.label(if dp.suggestion.is_some() { "Or another one:" } else { "The game data:" });
                ui.horizontal(|ui| {
                    let r = ui.add(egui::TextEdit::singleline(&mut dp.text).hint_text("a folder or a disc image").desired_width(440.0));
                    if r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                        chosen = Some(PathBuf::from(dp.text.trim()));
                    }
                    for (label, image) in [("Folder…", false), ("Disc image…", true)] {
                        if ui.button(label).clicked() {
                            let r = if image {
                                data_dir::choose_image("Your Hot Wheels: Velocity X disc image")
                            } else {
                                data_dir::choose_folder("Your Hot Wheels: Velocity X files")
                            };
                            match r {
                                Ok(Some(p)) => {
                                    dp.text = p.clone();
                                    chosen = Some(PathBuf::from(p));
                                }
                                Ok(None) => {}
                                Err(e) => dp.error = Some(e),
                            }
                        }
                    }
                });
                ui.horizontal(|ui| {
                    if ui.add_enabled(!dp.text.trim().is_empty(), egui::Button::new("Use this")).clicked() {
                        chosen = Some(PathBuf::from(dp.text.trim()));
                    }
                    if dp.can_cancel && ui.button("Cancel").clicked() {
                        cancel = true;
                    }
                });
                if let Some(info) = dp.disc.clone() {
                    ui.add_space(8.0);
                    egui::Frame::group(ui.style()).show(ui, |ui| {
                        ui.set_width(ui.available_width());
                        ui.strong(format!(
                            "Hot Wheels: Velocity X, {}: {}, {} files, {}",
                            info.release.name(),
                            info.kind_text(),
                            info.files,
                            extract::human(info.bytes)
                        ));
                        ui.label(if info.plays_directly() {
                            "The game reads this image as it is. Or extract it into a folder of files."
                        } else {
                            "The game can't read this disc as it is: extract it first (once). The extracted folder is then the game data."
                        });
                        ui.horizontal(|ui| {
                            if info.plays_directly() && ui.button("Use it directly").clicked() {
                                direct = Some(info.path.clone());
                            }
                            if ui.button("Extract it…").clicked() {
                                extract_it = Some(info.path.clone());
                            }
                        });
                    });
                }
                if let Some(e) = &dp.error {
                    ui.add_space(8.0);
                    ui.colored_label(egui::Color32::LIGHT_RED, e);
                }
                ui.add_space(16.0);
                ui.label(egui::RichText::new("Your choice is remembered for next time. File > Game data… changes it.").weak());
            });
            });
        });
        if cancel {
            self.data_pick = None;
        } else if let Some(p) = direct {
            self.use_data(&p);
        } else if let Some(p) = extract_it {
            self.open_extract(Some(&p));
        } else if let Some(p) = chosen {
            self.pick_data(&p);
        }
    }

    // ---- the menu bar ------------------------------------------------------------------------

    pub(crate) fn menu_bar(&mut self, ui: &mut egui::Ui) {
        let ctx = ui.ctx().clone();
        let connected = self.viewer.is_some();
        let picking = self.data_pick.is_some();
        let proj = self.level.edit.proj.as_ref();
        let has_level = proj.is_some();
        let dirty = proj.is_some_and(|p| p.dirty());
        let (can_undo, can_redo) = proj
            .map(|p| (!p.undo.is_empty(), !p.redo.is_empty()))
            .unwrap_or((false, false));
        let can_delete = has_level
            && matches!(
                self.level.edit.sel,
                Some(crate::level_edit::Sel::Own(..) | crate::level_edit::Sel::Trigger(_))
            );
        egui::MenuBar::new().ui(ui, |ui| {
            ui.menu_button("File", |ui| {
                if ui
                    .add_enabled(!picking, egui::Button::new("New level…"))
                    .on_hover_text("a level project of its own, in a new folder")
                    .clicked()
                {
                    self.level.tab = true;
                    self.level.edit.new_requested = true;
                }
                if ui
                    .add_enabled(!picking, egui::Button::new("Open level…"))
                    .on_hover_text("a level project's folder (with level.jsonc)")
                    .clicked()
                {
                    if let Some(d) = Self::choose_folder() {
                        self.level.tab = true;
                        self.open_level(&d);
                    }
                }
                ui.separator();
                if ui
                    .add_enabled(
                        dirty,
                        egui::Button::new("Save level").shortcut_text(shortcut(
                            &ctx,
                            true,
                            false,
                            egui::Key::S,
                        )),
                    )
                    .clicked()
                {
                    self.edit_save();
                }
                if ui
                    .add_enabled(has_level && !picking, egui::Button::new("Save as mod…"))
                    .on_hover_text("an .xmod that players install")
                    .clicked()
                {
                    self.save_as_mod();
                }
                ui.separator();
                if ui
                    .add_enabled(self.args.port.is_none(), egui::Button::new("Game data…"))
                    .on_hover_text("where your copy of the game is")
                    .clicked()
                {
                    self.ask_for_data(None);
                }
                if ui
                    .add_enabled(self.extract.as_ref().is_none_or(|e| e.job.is_none()), egui::Button::new("Extract a disc…"))
                    .on_hover_text("the PC or PS2 disc (or a GameCube image) into a folder the game plays")
                    .clicked()
                {
                    self.extract_from_menu();
                }
                ui.separator();
                if ui.button("Quit").clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });
            ui.menu_button("Edit", |ui| {
                if ui
                    .add_enabled(
                        can_undo,
                        egui::Button::new("Undo").shortcut_text(shortcut(
                            &ctx,
                            true,
                            false,
                            egui::Key::Z,
                        )),
                    )
                    .clicked()
                {
                    self.edit_undo(false);
                }
                if ui
                    .add_enabled(
                        can_redo,
                        egui::Button::new("Redo").shortcut_text(shortcut(
                            &ctx,
                            true,
                            true,
                            egui::Key::Z,
                        )),
                    )
                    .clicked()
                {
                    self.edit_undo(true);
                }
                ui.separator();
                if ui
                    .add_enabled(can_delete, egui::Button::new("Delete"))
                    .on_hover_text("take the selected record or trigger out of the level")
                    .clicked()
                {
                    self.delete_selected();
                }
            });
            ui.menu_button("View", |ui| {
                let (level, worlds, content, media) = (self.level.tab, self.world.tab_worlds, self.content.tab, self.movies.tab);
                let other = content || media;
                if ui.radio(!other && !level && !worlds, "Packs").clicked() {
                    (self.movies.tab, self.content.tab, self.level.tab, self.world.tab_worlds) = (false, false, false, false);
                }
                if ui.radio(!other && !level && worlds, "Worlds").clicked() {
                    (self.movies.tab, self.content.tab, self.level.tab, self.world.tab_worlds) = (false, false, false, true);
                }
                if ui.radio(!other && level, "Level").clicked() {
                    (self.movies.tab, self.content.tab, self.level.tab) = (false, false, true);
                }
                if ui.radio(content && !media, "Content (new cars, weapons, tracks, sounds, music)").clicked() {
                    (self.movies.tab, self.content.tab, self.level.tab) = (false, true, false);
                }
                if ui.radio(media, "Media (movies, every sound and song)").clicked() {
                    (self.movies.tab, self.content.tab, self.level.tab) = (true, false, false);
                }
                ui.separator();
                if ui
                    .checkbox(&mut self.show_raw, "Raw nodes (no schema)")
                    .changed()
                {
                    if let Some(o) = self.outline.as_mut() {
                        o.shown = shown_nodes(&o.nodes, &o.children, self.show_raw);
                    }
                }
                if ui
                    .add_enabled(
                        connected,
                        egui::Checkbox::new(&mut self.spin, "Spin the camera"),
                    )
                    .changed()
                {
                    let s = self.spin;
                    self.request("camera", json!({"spin": s}), Pending::Camera);
                }
            });
            ui.menu_button("Tools", |ui| {
                let running = self.level.drive.as_mut().is_some_and(|d| d.running());
                let busy = self.test_drive_busy();
                if ui
                    .add_enabled(
                        has_level && !busy && !picking,
                        egui::Button::new("Test drive").shortcut_text(shortcut(
                            &ctx,
                            false,
                            false,
                            egui::Key::F5,
                        )),
                    )
                    .clicked()
                {
                    self.test_drive();
                }
                if ui
                    .add_enabled(running, egui::Button::new("Stop the test drive"))
                    .clicked()
                {
                    if let Some(d) = self.level.drive.as_mut() {
                        d.stop();
                    }
                }
                ui.separator();
                let world = self.world.status["world"].as_str().map(str::to_string);
                let pack = self.outline.as_ref().map(|o| {
                    (
                        o.info["archive"].as_str().unwrap_or("").to_string(),
                        o.info["type"].as_str().unwrap_or("").to_string(),
                        o.info["id"].as_u64().unwrap_or(0),
                    )
                });
                if let Some(w) = world {
                    if ui
                        .add_enabled(connected, egui::Button::new("Reload world"))
                        .on_hover_text("load it again from the files (edits are dropped)")
                        .clicked()
                    {
                        self.request("world", json!({"name": w}), Pending::World);
                    }
                } else if ui
                    .add_enabled(
                        connected && pack.is_some(),
                        egui::Button::new("Reload pack"),
                    )
                    .on_hover_text("load it again from the files (edits are dropped)")
                    .clicked()
                {
                    if let Some((a, t, id)) = pack {
                        self.load(&a, &t, id);
                    }
                }
                if ui
                    .add_enabled(connected, egui::Button::new("Screenshot of the 3D view"))
                    .on_hover_text(format!("saved as {}", self.shot_path))
                    .clicked()
                {
                    let p = self.shot_path.clone();
                    self.request("screenshot", json!({"path": p}), Pending::Shot);
                }
                ui.separator();
                if ui
                    .add_enabled(
                        self.connecting.is_none() && !picking,
                        egui::Button::new("Restart the game"),
                    )
                    .on_hover_text("the game behind the 3D view (Reconnect)")
                    .clicked()
                {
                    self.connect();
                }
            });
            ui.menu_button("Help", |ui| {
                if ui.button("Editor guide").on_hover_text(GUIDE_URL).clicked() {
                    ctx.open_url(egui::OpenUrl::new_tab(GUIDE_URL));
                }
                if ui.button("About").clicked() {
                    self.about = true;
                }
            });
        });
        self.about_window(&ctx);
    }

    fn about_window(&mut self, ctx: &egui::Context) {
        let mut open = self.about;
        egui::Window::new("About HWVX Editor")
            .open(&mut open)
            .collapsible(false)
            .resizable(false)
            .show(ctx, |ui| {
                ui.label(format!(
                    "HWVX Editor (hwvx-browser {})",
                    env!("CARGO_PKG_VERSION")
                ));
                ui.label(
                    "Levels and data browsing for HWVX-SDL, with the game itself as the 3D view.",
                );
                ui.separator();
                let h = &self.hello;
                if self.viewer.is_some() {
                    ui.label(format!(
                        "Game: {} data, {} renderer, viewer protocol {}",
                        h["release"].as_str().unwrap_or("?"),
                        h["renderer"].as_str().unwrap_or("?"),
                        h["protocol"]
                    ));
                }
                let data = self
                    .args
                    .data
                    .as_ref()
                    .map(|p| p.display().to_string())
                    .or_else(|| h["data"].as_str().map(str::to_string));
                ui.label(format!(
                    "Game data: {}",
                    data.as_deref().unwrap_or("not chosen yet")
                ));
                ui.hyperlink_to("The editor guide", GUIDE_URL);
            });
        self.about = open;
    }
}
