//! File > Extract a disc… (and the game data screen's "Extract it…"): a window that takes a disc
//! image, says what it is, asks where to extract it, extracts it on a worker thread with a
//! progress bar, and offers the result as the game data. The logic is src/extract.rs.

use crate::Browser;
use eframe::egui;
use hwvx_browser::data_dir;
use hwvx_browser::extract::{self, Dest, DiscInfo, Job, Plan, human};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, TryRecvError};
use std::time::{Duration, Instant};
use xdlrs::disc::CANCELLED;

/// The destination is checked once its text has rested this long (a check walks the folder).
const DEBOUNCE: Duration = Duration::from_millis(300);

#[derive(Default)]
pub(crate) struct ExtractState {
    pub image: String,
    pub info: Option<Result<DiscInfo, String>>,
    /// the image being read (Disc::open, on a thread: a .bin's tree on a slow drive), for its text
    reading: Option<(String, Receiver<Result<DiscInfo, String>>)>,
    pub dest: String,
    /// the default destination last filled in: replaced by the next disc's unless edited
    default_dest: String,
    /// the destination's check, for the text it was made for
    checked: Option<(String, Result<Plan, String>)>,
    /// a check under way (on a thread), for its text
    checking: Option<(String, Receiver<Result<Plan, String>>)>,
    /// the destination's text and when it last changed (the debounce)
    typed: Option<(String, Instant)>,
    pub job: Option<Job>,
    /// the finished extraction: its folder and the result
    pub done: Option<(PathBuf, Result<PathBuf, String>)>,
}

impl ExtractState {
    pub fn new(image: Option<&Path>, ctx: &egui::Context) -> ExtractState {
        let mut s = ExtractState::default();
        if let Some(p) = image {
            s.set_image(p, ctx);
        }
        s
    }

    /// Reads `p` (on a thread: Disc::open reads the image's file tree).
    fn set_image(&mut self, p: &Path, ctx: &egui::Context) {
        self.image = p.display().to_string();
        self.info = None;
        self.checked = None;
        let (tx, rx) = std::sync::mpsc::channel();
        let (p, ctx) = (p.to_path_buf(), ctx.clone());
        std::thread::spawn(move || {
            let _ = tx.send(extract::inspect(&p));
            ctx.request_repaint();
        });
        self.reading = Some((self.image.clone(), rx));
    }

    /// The image's info once read (an older image's is dropped).
    fn take_info(&mut self, pref: &Path) {
        let Some((text, rx)) = &self.reading else { return };
        let info = match rx.try_recv() {
            Err(TryRecvError::Empty) => return,
            Err(TryRecvError::Disconnected) => Err("the disc could not be read".to_string()),
            Ok(i) => i,
        };
        let current = *text == self.image;
        self.reading = None;
        if !current {
            return;
        }
        if let Ok(i) = &info
            && (self.dest.trim().is_empty() || self.dest == self.default_dest)
        {
            self.dest = extract::default_dest(pref, i.release).display().to_string();
            self.default_dest = self.dest.clone();
        }
        self.info = Some(info);
        self.checked = None;
    }

    /// The destination's check, made again when its text changes: on a thread, once the text
    /// has rested (each keystroke walked the folder and froze the window; backspacing toward `/`
    /// for minutes). None: not known yet.
    fn plan(&mut self, ctx: &egui::Context) -> Option<&Result<Plan, String>> {
        let Some(Ok(info)) = &self.info else { return None };
        let text = self.dest.trim().to_string();
        if let Some((t, rx)) = &self.checking {
            match rx.try_recv() {
                Ok(r) => {
                    self.checked = Some((t.clone(), r));
                    self.checking = None;
                }
                Err(TryRecvError::Disconnected) => self.checking = None,
                Err(TryRecvError::Empty) => {}
            }
        }
        if self.checked.as_ref().is_some_and(|c| c.0 == text) {
            return self.checked.as_ref().map(|c| &c.1);
        }
        if self.checking.as_ref().is_some_and(|c| c.0 == text) {
            return None; // (its thread repaints)
        }
        let rested = match &self.typed {
            Some((t, at)) if *t == text => DEBOUNCE.saturating_sub(at.elapsed()),
            _ => {
                self.typed = Some((text.clone(), Instant::now()));
                DEBOUNCE
            }
        };
        if !rested.is_zero() {
            ctx.request_repaint_after(rested);
            return None;
        }
        // (a check for an older text is left to finish; its answer is dropped)
        let (tx, rx) = std::sync::mpsc::channel();
        let (dest, info, ctx) = (PathBuf::from(&text), info.clone(), ctx.clone());
        std::thread::spawn(move || {
            let _ = tx.send(extract::plan(&dest, &info, None));
            ctx.request_repaint();
        });
        self.checking = Some((text, rx));
        None
    }

    /// Extraction under way (scripts: how far).
    pub fn running(&self) -> Option<extract::Progress> {
        self.job.as_ref().map(|j| j.snapshot())
    }
}

enum Act {
    Start,
    Back,
    Close,
    UseData(PathBuf),
}

fn plan_text(p: &Plan, info: &DiscInfo) -> String {
    let space = match p.free {
        Some(f) => format!("Needs {}; {} free there.", human(p.needed), human(f)),
        None => format!("Needs {}.", human(p.needed)),
    };
    match &p.found {
        Dest::Fresh => format!("A new or empty folder. {space}"),
        Dest::Previous { files, parts, .. } => format!(
            "An earlier extraction of this disc ({files} of {} files{}): extracting again finishes it. {space}",
            info.files,
            if *parts > 0 { format!(", {parts} unfinished") } else { String::new() }
        ),
    }
}

impl Browser {
    /// Opens the window, on `image` if given (else it asks for one).
    pub(crate) fn open_extract(&mut self, image: Option<&Path>) {
        if self.extract.as_ref().is_some_and(|e| e.job.is_some()) {
            return; // one at a time
        }
        self.extract = Some(ExtractState::new(image, &self.ctx));
    }

    /// File > Extract a disc…: the system's dialog first (ask_then), then the window.
    pub(crate) fn extract_from_menu(&mut self) {
        self.ask_then(
            || data_dir::choose_image("The Hot Wheels: Velocity X disc image to extract"),
            |b, r| match r {
                Ok(Some(p)) => b.open_extract(Some(Path::new(&p))),
                Ok(None) => {}
                Err(_) => b.open_extract(None), // no dialog: the window's own path field
            },
        );
    }

    /// The window's two Choose… buttons: the system's dialog, its answer into the window if it
    /// is still open and not extracting.
    fn extract_choose(&mut self, image: bool) {
        self.ask_then(
            move || {
                if image {
                    data_dir::choose_image("The Hot Wheels: Velocity X disc image to extract")
                } else {
                    data_dir::choose_folder("Where to extract the disc")
                }
            },
            move |b, r| {
                let ctx = b.ctx.clone();
                let Some(st) = b.extract.as_mut().filter(|st| st.job.is_none() && st.done.is_none()) else { return };
                match (r, image) {
                    (Ok(Some(p)), true) => st.set_image(Path::new(&p), &ctx),
                    (Ok(Some(p)), false) => st.dest = p,
                    (Ok(None), _) => {}
                    (Err(e), true) => st.info = Some(Err(e)),
                    (Err(e), false) => st.checked = Some((st.dest.trim().to_string(), Err(e))),
                }
            },
        );
    }

    pub(crate) fn extract_window(&mut self, ctx: &egui::Context) {
        let pref = self.args.pref.clone().unwrap_or_else(hwvx_browser::resources::default_pref);
        let Some(st) = self.extract.as_mut() else { return };
        st.take_info(&pref);
        let can_use = self.args.port.is_none();
        let mut act: Option<Act> = None;
        let mut choose: Option<bool> = None;
        let mut open = true;
        if let Some(job) = &st.job
            && let Some(r) = job.poll()
        {
            st.done = Some((job.dest.clone(), r));
            st.job = None;
            st.checked = None;
        }
        egui::Window::new("Extract a disc")
            .open(&mut open)
            .collapsible(false)
            .resizable(false)
            .default_width(600.0)
            .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
            .show(ctx, |ui| {
                ui.set_width(600.0);
                if let Some(job) = &st.job {
                    let p = job.snapshot();
                    let what = st.info.as_ref().and_then(|i| i.as_ref().ok()).map(|i| i.release.name()).unwrap_or("the");
                    ui.heading(format!("Extracting the {what} disc…"));
                    ui.add_space(6.0);
                    ui.label(egui::RichText::new(job.dest.display().to_string()).monospace());
                    ui.add_space(6.0);
                    ui.add(egui::ProgressBar::new(p.fraction()).show_percentage().desired_width(600.0));
                    ui.label(format!("{} of {} files · {} of {}", p.files, p.files_total, human(p.bytes), human(p.bytes_total)));
                    if p.waiting {
                        ui.label(egui::RichText::new("waiting for the extraction stopped before to let go of its file…").weak());
                    } else if !p.last.is_empty() {
                        ui.label(egui::RichText::new(format!("done: {}", p.last)).weak());
                    }
                    ui.add_space(6.0);
                    if job.cancelling() {
                        ui.add_enabled(false, egui::Button::new("Cancelling…"));
                    } else if ui.button("Cancel").on_hover_text("stops within a moment; the files done stay").clicked() {
                        job.cancel();
                    }
                    ctx.request_repaint_after(Duration::from_millis(100));
                    return;
                }
                if let Some((dest, r)) = &st.done {
                    match r {
                        Ok(data) => {
                            let n = st.info.as_ref().and_then(|i| i.as_ref().ok()).map(|i| i.files).unwrap_or(0);
                            ui.heading("Extracted");
                            ui.label(format!("{n} files in {}. The game plays them as its data.", dest.display()));
                            ui.add_space(8.0);
                            ui.horizontal(|ui| {
                                if can_use && ui.button("Use this as the game data").clicked() {
                                    act = Some(Act::UseData(data.clone()));
                                }
                                if ui.button("Close").clicked() {
                                    act = Some(Act::Close);
                                }
                            });
                        }
                        Err(e) if e == CANCELLED => {
                            ui.heading("Cancelled");
                            ui.label(format!(
                                "The files done so far stay in {}: extracting into the same folder again finishes it (or delete the folder).",
                                dest.display()
                            ));
                            if ui.button("Back").clicked() {
                                act = Some(Act::Back);
                            }
                        }
                        Err(e) => {
                            ui.heading("The extraction failed");
                            ui.colored_label(egui::Color32::LIGHT_RED, e);
                            if ui.button("Back").clicked() {
                                act = Some(Act::Back);
                            }
                        }
                    }
                    return;
                }
                // the disc
                ui.label("The PC CD (.cue/.bin or .iso), the PS2 DVD (.iso), or a GameCube image (.iso, .gcm, .gcz):");
                let mut chosen: Option<String> = None;
                ui.horizontal(|ui| {
                    let r = ui.add(egui::TextEdit::singleline(&mut st.image).hint_text("the disc image").desired_width(480.0));
                    if r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                        chosen = Some(st.image.trim().to_string());
                    }
                    if ui.button("Choose…").clicked() {
                        choose = Some(true);
                    }
                });
                if let Some(c) = chosen {
                    st.set_image(Path::new(&c), ctx);
                }
                let info = match &st.info {
                    None => {
                        if st.reading.is_some() {
                            ui.horizontal(|ui| {
                                ui.spinner();
                                ui.label("Reading the disc…");
                            });
                        }
                        return;
                    }
                    Some(Err(e)) => {
                        ui.colored_label(egui::Color32::LIGHT_RED, e);
                        return;
                    }
                    Some(Ok(i)) => i.clone(),
                };
                ui.add_space(8.0);
                egui::Frame::group(ui.style()).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    egui::Grid::new("disc info").num_columns(2).spacing([16.0, 4.0]).show(ui, |ui| {
                        ui.label("Release");
                        ui.strong(format!("Hot Wheels: Velocity X, {}", info.release.name()));
                        ui.end_row();
                        ui.label("Disc");
                        ui.label(info.kind_text());
                        ui.end_row();
                        if let Some(id) = &info.game_id {
                            ui.label("Game ID");
                            ui.label(match &info.title {
                                Some(t) => format!("{id} ({t})"),
                                None => id.clone(),
                            });
                            ui.end_row();
                        }
                        if let Some(v) = info.archive_version {
                            ui.label("Archive version");
                            ui.label(v.to_string());
                            ui.end_row();
                        }
                        ui.label("Files");
                        ui.label(info.files.to_string());
                        ui.end_row();
                        ui.label("Size");
                        ui.label(format!("{} extracted (the image: {})", human(info.bytes), human(info.image_size)));
                        ui.end_row();
                    });
                });
                ui.add_space(4.0);
                if info.plays_directly() {
                    ui.label("The game reads this image as it is: extracting it is optional (it gives a folder of files instead).");
                    if can_use && ui.button("Use the image directly").clicked() {
                        act = Some(Act::UseData(info.path.clone()));
                    }
                } else {
                    ui.label("The game can't read this disc as it is: extracted, it plays as the game data.");
                }
                // where to
                ui.add_space(8.0);
                ui.label("Extract into:");
                ui.horizontal(|ui| {
                    ui.add(egui::TextEdit::singleline(&mut st.dest).desired_width(480.0));
                    if ui.button("Choose…").clicked() {
                        choose = Some(false);
                    }
                });
                let ok = match st.plan(ctx) {
                    Some(Ok(p)) => {
                        ui.label(egui::RichText::new(plan_text(p, &info)).weak());
                        true
                    }
                    Some(Err(e)) => {
                        ui.colored_label(egui::Color32::LIGHT_RED, e);
                        false
                    }
                    None => {
                        ui.label(egui::RichText::new("Checking the folder…").weak());
                        false
                    }
                };
                ui.add_space(8.0);
                ui.horizontal(|ui| {
                    if ui.add_enabled(ok, egui::Button::new("Extract")).clicked() {
                        act = Some(Act::Start);
                    }
                    if ui.button("Close").clicked() {
                        act = Some(Act::Close);
                    }
                });
            });
        if !open {
            act = Some(Act::Close); // (a running extraction stops: Job's drop)
        }
        if let Some(image) = choose {
            self.extract_choose(image);
        }
        match act {
            Some(Act::Start) => self.extract_start(),
            Some(Act::Back) => {
                if let Some(st) = self.extract.as_mut() {
                    st.done = None;
                    st.checked = None;
                }
            }
            Some(Act::Close) => self.extract = None,
            Some(Act::UseData(p)) => {
                self.extract = None;
                self.use_data(&p);
            }
            None => {}
        }
    }

    /// Starts the extraction the window shows.
    pub(crate) fn extract_start(&mut self) {
        let ctx = self.ctx.clone();
        let Some(st) = self.extract.as_mut() else { return };
        let Some(Ok(info)) = &st.info else { return };
        let dest = PathBuf::from(st.dest.trim());
        st.done = None;
        st.job = Some(Job::start(&info.path, &dest, move || ctx.request_repaint()));
    }
}
