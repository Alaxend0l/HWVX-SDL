//! The Level tab and the test drive (src/testdrive.rs; docs-pending/mission-editor.md): a level
//! project open (its folder), its summary, the car, the quick start and "from here" choices, and
//! Test drive (F5, or the top bar's button): the project compiled (only when its files changed)
//! into the editor's scratch folder, the game started straight into the level in a window of its
//! own, in front. Compile errors list here by file and place. Leaving the race (or closing the
//! game's window) ends the test drive and brings the editor back; the project is never written.

use crate::{Browser, Pending};
use eframe::egui;
use hwvx_browser::testdrive::{self, LevelError, Options, Prepared, TestDrive};
use crate::level_edit::EditState;
use serde_json::{Value, json};
use std::path::PathBuf;
use std::sync::mpsc::{Receiver, TryRecvError, channel};
use std::time::{Duration, Instant};

#[derive(Default)]
pub struct LevelState {
    pub tab: bool,
    pub project: String,
    pub summary: Value,
    pub summary_error: Option<String>,
    pub car: u32,
    pub skip_intro: bool,
    pub from_here: bool,
    /// the test drive through a world pack of the level's own (tools/xdl_level_pack.py), the
    /// old way; off (the default): as a shared mod plays it, the level file applied as the world
    /// loads (Port_Level.c)
    pub build_pack: bool,
    /// how the last test drive ran the level (shown with its result)
    route: &'static str,
    pub errors: Vec<LevelError>,
    pub last: Option<Prepared>,
    pub drive: Option<TestDrive>,
    pub message: String,
    preparing: Option<(Receiver<Result<Prepared, Vec<LevelError>>>, Options, Instant)>,
    clicked: Option<Instant>,
    waiting_ground: bool,
    ended_shown: bool,
    auto_started: bool,
    sharing: Option<Receiver<Result<String, String>>>,
    pub edit: EditState,
}

impl Browser {
    /// The scratch folder the test drives use: the editor's own, beside its viewer's preferences.
    pub(crate) fn testdrive_scratch_dir(&self) -> PathBuf {
        self.testdrive_scratch()
    }

    fn testdrive_scratch(&self) -> PathBuf {
        self.args
            .pref
            .clone()
            .unwrap_or_else(|| {
                std::env::current_exe().ok().and_then(|e| e.parent().map(|d| d.join("viewer-pref"))).unwrap_or_else(|| "viewer-pref".into())
            })
            .join("testdrive")
    }

    pub(crate) fn open_level(&mut self, dir: &str) {
        self.level.project = dir.to_string();
        match testdrive::read_level(std::path::Path::new(dir)) {
            Ok(v) => {
                self.level.summary = v;
                self.level.summary_error = None;
            }
            Err(e) => {
                self.level.summary = Value::Null;
                self.level.summary_error = Some(e);
            }
        }
        self.level.errors.clear();
        self.edit_open(dir);
    }

    /// What a test drive compiles: the project as edited (a copy in the scratch folder when it
    /// has unsaved edits), else its folder.
    fn drive_project(&self) -> PathBuf {
        if let Some(p) = self.level.edit.proj.as_ref() {
            if p.dirty() {
                let copy = self.testdrive_scratch().join("drive-copy");
                if p.write_copy(&copy).is_ok() {
                    return copy;
                }
            }
        }
        PathBuf::from(&self.level.project)
    }

    fn testdrive_options(&self) -> Options {
        let game = self.args.game.clone().unwrap_or_else(hwvx_browser::client::default_game);
        let data = self.args.data.clone().or_else(|| self.hello["data"].as_str().map(PathBuf::from));
        Options {
            tools: self.args.tools.clone().or_else(|| testdrive::default_tools(&game)).unwrap_or_else(|| "tools".into()),
            python: std::env::var("HWVX_PYTHON").unwrap_or_else(|_| "python3".into()),
            game,
            data,
            build_pack: false,
            project: self.drive_project(),
            scratch: self.testdrive_scratch(),
            car: Some(self.level.car),
            skip_intro: self.level.skip_intro,
            start_at: None,
            renderer: self.args.testdrive_renderer.clone(),
            env: Vec::new(),
        }
    }

    /// Test drive (F5): from the top: compile, start.
    pub(crate) fn test_drive(&mut self) {
        if self.level.project.is_empty() {
            self.level.tab = true;
            self.level.message = "open a level project first (its folder, with level.jsonc)".into();
            return;
        }
        if self.level.preparing.is_some() || self.level.waiting_ground {
            return;
        }
        if let Some(d) = self.level.drive.as_mut() {
            d.stop(); // a new test drive replaces the running one (ours: its PID)
        }
        self.level.drive = None;
        self.level.clicked = Some(Instant::now());
        self.level.errors.clear();
        self.open_level(&self.level.project.clone());
        if self.level.from_here && self.world.status["world"].is_string() {
            // the ground under the camera (fly: where it is; orbit: what it looks at)
            let c = &self.camera;
            let p = if c["mode"].as_str() == Some("fly") { &c["pos"] } else { &c["target"] };
            let at: Vec<f64> = p.as_array().map(|a| a.iter().filter_map(Value::as_f64).collect()).unwrap_or_default();
            if at.len() == 3 {
                self.level.waiting_ground = true;
                self.level.message = "finding the ground under the camera…".into();
                self.request(
                    "raycast",
                    json!({"from": [at[0], at[1] + 50.0, at[2]], "to": [at[0], at[1] - 5000.0, at[2]]}),
                    Pending::Ground,
                );
                return;
            }
        }
        self.start_prepare(None);
    }

    pub(crate) fn ground_reply(&mut self, r: Option<Value>) {
        self.level.waiting_ground = false;
        let hit = r.as_ref().and_then(|r| {
            let p: Vec<f32> = r["point"].as_array()?.iter().filter_map(Value::as_f64).map(|x| x as f32).collect();
            (r["hit"].as_bool() == Some(true) && p.len() == 3).then(|| [p[0], p[1], p[2]])
        });
        match hit {
            Some(g) => {
                let yaw = self.camera["yaw"].as_f64().unwrap_or(0.0) as f32;
                self.start_prepare(Some(testdrive::start_from(g, yaw)));
            }
            None => {
                self.level.message = "no ground under the camera: starting at the level's start".into();
                self.start_prepare(None);
            }
        }
    }

    fn start_prepare(&mut self, start_at: Option<[f32; 4]>) {
        let mut o = self.testdrive_options();
        o.start_at = start_at;
        o.build_pack = self.level.build_pack;
        self.level.route = if self.level.build_pack { "its own world pack, built" } else { "as shared: the level file applied at load" };
        let (tx, rx) = channel();
        let oc = o.clone();
        std::thread::spawn(move || {
            let _ = tx.send(testdrive::prepare(&oc));
        });
        self.level.preparing = Some((rx, o, self.level.clicked.unwrap_or_else(Instant::now)));
        self.level.message = "compiling…".into();
    }

    /// Each frame: the compile's result, the game's progress, its end.
    pub(crate) fn level_tick(&mut self, ctx: &egui::Context) {
        if ctx.input(|i| i.key_pressed(egui::Key::F5)) {
            self.test_drive();
        }
        if self.args.test_drive && !self.level.auto_started {
            if self.viewer.is_some() && !self.hello.is_null() {
                self.level.auto_started = true;
                self.test_drive();
            } else {
                // --test-drive: start as soon as the viewer has said hello, painted or not
                ctx.request_repaint_after(Duration::from_millis(50));
            }
        }
        if let Some(rx) = self.level.sharing.take() {
            match rx.try_recv() {
                Err(TryRecvError::Empty) => {
                    self.level.sharing = Some(rx);
                    ctx.request_repaint_after(Duration::from_millis(100));
                }
                Err(TryRecvError::Disconnected) => self.level.message = "saving the mod stopped".into(),
                Ok(Ok(line)) => self.level.message = format!("saved as a mod: {line}"),
                Ok(Err(why)) => {
                    self.level.message = "not saved as a mod: see the errors".into();
                    self.level.errors = why
                        .lines()
                        .filter(|l| !l.trim().is_empty())
                        .map(|l| LevelError { message: l.trim().to_string(), ..Default::default() })
                        .collect();
                    self.level.tab = true;
                }
            }
        }
        if let Some((rx, o, clicked)) = self.level.preparing.take() {
            match rx.try_recv() {
                Err(TryRecvError::Empty) => {
                    self.level.preparing = Some((rx, o, clicked));
                    ctx.request_repaint_after(Duration::from_millis(20));
                }
                Err(TryRecvError::Disconnected) => self.level.message = "the compile step stopped".into(),
                Ok(Err(errs)) => {
                    self.level.message = format!("{} error{}: not started", errs.len(), if errs.len() == 1 { "" } else { "s" });
                    self.level.errors = errs;
                    self.level.tab = true;
                }
                Ok(Ok(p)) => match TestDrive::launch(&o, &p, clicked) {
                    Ok(d) => {
                        self.level.message = format!(
                            "compiled in {:.0} ms{} · {} · game started (pid {})",
                            p.ms,
                            if p.cached { " (unchanged)" } else { "" },
                            self.level.route,
                            d.pid
                        );
                        self.level.last = Some(p);
                        self.level.drive = Some(d);
                        self.level.ended_shown = false;
                    }
                    Err(e) => self.level.message = format!("the game could not start: {e}"),
                },
            }
        }
        if let Some(d) = self.level.drive.as_mut() {
            ctx.request_repaint_after(Duration::from_millis(100));
            if let Some(how) = d.poll() {
                if !self.level.ended_shown {
                    self.level.ended_shown = true;
                    let pr = d.progress.lock().unwrap().clone();
                    self.level.message = format!("test drive ({}) {how}: back in the editor{}", self.level.route, timings(&pr));
                    if let Some(why) = pr.not_applied {
                        self.level.message = format!("the level was not applied: {why}");
                    }
                    ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
                }
            }
        }
    }

    /// --test-drive with --app-shot: the test drive is driving, or ended, or didn't start.
    pub(crate) fn test_drive_settled(&mut self) -> bool {
        if !self.level.auto_started || self.level.preparing.is_some() || self.level.waiting_ground {
            return false;
        }
        match self.level.drive.as_mut() {
            Some(d) => {
                let p = d.progress.lock().unwrap().clone();
                (p.driving.is_some() || (p.in_race.is_some() && !self.level.skip_intro)) && d.clicked.elapsed().as_secs() >= 2
                    || d.poll().is_some()
            }
            None => true,
        }
    }

    /// Save as mod: the level (saved) packed into a .xmod a player installs on the launcher's
    /// Mods page; the game applies it to their own world as it loads (no game data in it).
    pub(crate) fn save_as_mod(&mut self) {
        if self.level.project.is_empty() {
            self.level.message = "open a level project first".into();
            return;
        }
        if self.level.edit.proj.as_ref().map(|p| p.dirty()).unwrap_or(false) {
            self.level.message = "save the level first (⌘S): the mod is made from its files".into();
            return;
        }
        let id = self.level.summary["id"].as_str().unwrap_or("uid://level").trim_start_matches("uid://").to_string();
        let Some(out) = Self::choose_save_file(&format!("{id}.xmod")) else { return };
        let mut o = self.testdrive_options();
        o.project = PathBuf::from(&self.level.project);
        let (tx, rx) = channel();
        let out = PathBuf::from(out);
        std::thread::spawn(move || {
            let _ = tx.send(testdrive::share(&o, &out));
        });
        self.level.sharing = Some(rx);
        self.level.errors.clear();
        self.level.message = "saving as a mod: compiling, checking for game data (the first time about 30 s)…".into();
    }

    fn choose_save_file(name: &str) -> Option<String> {
        let out = if cfg!(target_os = "macos") {
            std::process::Command::new("osascript")
                .args(["-e", &format!("POSIX path of (choose file name with prompt \"Save the level as a mod\" default name \"{name}\")")])
                .output()
                .ok()?
        } else if cfg!(windows) {
            std::process::Command::new("powershell")
                .args(["-NoProfile", "-Command", &format!("Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.SaveFileDialog; $d.Filter = 'Mod package (*.xmod)|*.xmod'; $d.FileName = '{name}'; if ($d.ShowDialog() -eq 'OK') {{ $d.FileName }}")])
                .output()
                .ok()?
        } else {
            std::process::Command::new("zenity").args(["--file-selection", "--save", "--confirm-overwrite", &format!("--filename={name}")]).output().ok()?
        };
        let mut s = String::from_utf8_lossy(&out.stdout).trim().to_string();
        if !(out.status.success() && !s.is_empty()) {
            return None;
        }
        if !s.ends_with(".xmod") {
            s.push_str(".xmod");
        }
        Some(s)
    }

    pub(crate) fn test_drive_button(&mut self, ui: &mut egui::Ui) {
        let running = self.level.drive.as_mut().map(|d| d.running()).unwrap_or(false);
        let busy = self.level.preparing.is_some() || self.level.waiting_ground;
        let label = if busy { "⏳ Test drive" } else if running { "↻ Test drive again (F5)" } else { "▶ Test drive (F5)" };
        let b = egui::Button::new(egui::RichText::new(label).strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
        if ui.add_enabled(!busy, b).on_hover_text("compile the level and drive it in the game (F5)").clicked() {
            self.test_drive();
        }
        if running && ui.button("■ Stop").on_hover_text("end the test drive (the game it started)").clicked() {
            if let Some(d) = self.level.drive.as_mut() {
                d.stop();
            }
        }
    }

    pub(crate) fn level_panel(&mut self, ui: &mut egui::Ui) {
        ui.label("Level project (its folder)");
        ui.horizontal(|ui| {
            let r = ui.add(egui::TextEdit::singleline(&mut self.level.project).desired_width(130.0))
                .on_hover_text(self.level.project.clone());
            if (r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter))) | ui.button("Open").clicked() {
                let p = self.level.project.clone();
                self.open_level(&p);
            }
            if ui.button("…").on_hover_text("choose the folder").clicked() {
                if let Some(d) = Self::choose_folder() {
                    self.open_level(&d);
                }
            }
        });
        self.new_level_dialog(ui);
        if let Some(e) = &self.level.summary_error {
            ui.colored_label(egui::Color32::LIGHT_RED, e);
        } else if self.level.summary.is_object() {
            let s = &self.level.summary;
            ui.add(
                egui::Label::new(egui::RichText::new(format!(
                    "{} · {} · {}",
                    s["name"].as_str().unwrap_or("?"),
                    s["mode"].as_str().unwrap_or("?"),
                    s["world"].as_str().unwrap_or("?").rsplit(['\\', '/']).next().unwrap_or("")
                )).strong())
                .truncate(),
            );
        }
        ui.separator();
        ui.horizontal(|ui| {
            ui.label("Car");
            ui.add(egui::DragValue::new(&mut self.level.car).range(0..=40));
        });
        ui.checkbox(&mut self.level.skip_intro, "Quick start")
            .on_hover_text("leave out the intro screen and the start's delays (driving at once)");
        ui.add_enabled(self.world.status["world"].is_string(), egui::Checkbox::new(&mut self.level.from_here, "From here"))
            .on_hover_text("start on the ground under the camera, facing its way (a world must be loaded)");
        ui.checkbox(&mut self.level.build_pack, "Build world pack")
            .on_hover_text("off (the default): drive it exactly as a player who installed it as a mod gets it, \
                            the level file applied to the world as it loads; on: through a world pack of its \
                            own built from your data (the older way)");
        ui.horizontal(|ui| self.test_drive_button(ui));
        let sharing = self.level.sharing.is_some();
        if ui
            .add_enabled(!sharing, egui::Button::new(if sharing { "⏳ Save as mod…" } else { "📦 Save as mod…" }))
            .on_hover_text("pack the level into a .xmod a player installs on the launcher's Mods page \
                            (its own records only: checked for game data)")
            .clicked()
        {
            self.save_as_mod();
        }
        if !self.level.message.is_empty() {
            ui.label(&self.level.message);
        }
        if let Some(d) = self.level.drive.as_ref() {
            let pr = d.progress.lock().unwrap().clone();
            ui.label(egui::RichText::new(timings(&pr)).small());
        }
        if !self.level.errors.is_empty() {
            ui.separator();
            ui.strong(format!("{} compile error{}", self.level.errors.len(), if self.level.errors.len() == 1 { "" } else { "s" }));
            egui::ScrollArea::vertical().id_salt("level_errors").max_height(140.0).show(ui, |ui| {
                for e in &self.level.errors {
                    ui.horizontal_wrapped(|ui| {
                        if !e.file.is_empty() {
                            ui.monospace(egui::RichText::new(&e.file).strong());
                        }
                        if !e.path.is_empty() {
                            ui.monospace(&e.path);
                        }
                    });
                    ui.add(egui::Label::new(egui::RichText::new(&e.message).color(egui::Color32::LIGHT_RED)).wrap());
                    ui.add_space(4.0);
                }
            });
        }
        if self.level.edit.proj.is_some() {
            ui.separator();
            ui.horizontal(|ui| {
                ui.checkbox(&mut self.level.edit.preview, "Live preview")
                    .on_hover_text("apply the level to the world on show as it is edited (Worlds tab: load its world)");
                let n = self.level.edit.errors.len();
                if n > 0 {
                    ui.colored_label(egui::Color32::LIGHT_RED, format!("⚠ {n}"));
                } else {
                    ui.colored_label(egui::Color32::LIGHT_GREEN, "✓");
                }
                ui.label(egui::RichText::new(&self.level.edit.preview_note).small().weak());
            });
            if !self.world.status["world"].is_string() {
                if let Some(w) = self.level.summary["world"].as_str().map(str::to_string) {
                    if ui.button(format!("Load its world ({})", w.rsplit(['\\', '/']).next().unwrap_or(""))).clicked() {
                        self.request("world", json!({"name": w}), Pending::World);
                    }
                }
            }
            self.edit_lists(ui);
        }
    }
}

fn timings(p: &testdrive::Progress) -> String {
    let ms = |d: Option<Duration>| d.map(|d| format!("{} ms", d.as_millis())).unwrap_or_else(|| "-".into());
    if p.applied.is_none() && p.in_race.is_none() {
        return String::new();
    }
    format!(" · level applied {} · in the race {} · driving {}", ms(p.applied), ms(p.in_race), ms(p.driving))
}
