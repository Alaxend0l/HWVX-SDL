//! The Media tab's movies (src/movie.rs decodes them): the data's movies listed with their format,
//! size, length, frame rate and audio; the open one plays in the 3D view's place with its sound
//! (the soundtrack decoded whole on a thread, played by the system's player from where the picture
//! is), with play/pause, stop, restart and a scrubber. Bink and PSS need FFmpeg (src/ffmpeg.rs);
//! without it, and for any other video (a modder's mp4...), "Open in your video player" hands the
//! file to the system's default app.

use crate::Browser;
use crate::media_ui::View;
use eframe::egui;
use hwvx_browser::movie::{self, Audio, Info, Kind, MovieFile, VideoStream, fmt_time};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::mpsc::Receiver;
use std::time::{Duration, Instant};

/// Pictures decoded ahead of the one shown.
const AHEAD: usize = 6;

#[derive(Default)]
pub struct MovieState {
    /// the left panel shows the Media tab
    pub tab: bool,
    /// Movies or Audio, in the Media tab
    pub audio_tab: bool,
    pub list: Vec<MovieFile>,
    pub infos: Vec<Option<Result<Info, String>>>,
    pub listed_for: Option<PathBuf>,
    pub filter: String,
    pub open: Option<OpenMovie>,
    /// --movie NAME[@SECONDS] (scripts): opened and paused there
    pub script: Option<(String, f64)>,
    pub script_done: bool,
    pub script_t: Option<Instant>,
}

pub struct OpenMovie {
    pub file: MovieFile,
    pub info: Result<Info, String>,
    stream: Option<VideoStream>,
    audio_rx: Option<Receiver<Result<Option<Audio>, String>>>,
    pub audio: Option<Arc<Audio>>,
    pub audio_note: String,
    pub playing: bool,
    /// when frame 0 would have been shown (playing)
    origin: Instant,
    /// the position, in seconds (paused, or where playing started)
    pos: f64,
    /// the picture shown, and its frame
    tex: Option<egui::TextureHandle>,
    pub shown: Option<u32>,
    /// the frame the stream starts at (a seek's)
    from: u32,
    player: Option<std::process::Child>,
    /// the scrubber being dragged: its value
    scrub: Option<f64>,
    ended: bool,
    /// a picture decoded before its time
    next: Option<movie::Picture>,
    /// a seek's first picture not shown yet
    need_first: bool,
    pub error: Option<String>,
    pub export: String,
}

impl Drop for OpenMovie {
    fn drop(&mut self) {
        if let Some(mut c) = self.player.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }
}

impl OpenMovie {
    fn fps(&self) -> f64 {
        self.info.as_ref().map(|i| i.fps).unwrap_or(29.97)
    }
    fn seconds(&self) -> f64 {
        self.info.as_ref().map(|i| i.seconds).unwrap_or(0.0)
    }
    /// Where the movie is, in seconds.
    pub fn position(&self) -> f64 {
        if self.playing { self.origin.elapsed().as_secs_f64() } else { self.pos }
    }

    fn stop_sound(&mut self) {
        if let Some(mut c) = self.player.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    /// The soundtrack from `t` on, through the system's player.
    fn start_sound(&mut self, t: f64) {
        self.stop_sound();
        let Some(a) = &self.audio else { return };
        if a.channels == 0 || t >= a.seconds() {
            return;
        }
        let dir = hwvx_browser::resources::default_pref();
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("movie.wav");
        if let Err(e) = std::fs::write(&path, a.wav_from(t)) {
            self.audio_note = format!("{}: {e}", path.display());
            return;
        }
        match hwvx_browser::media::play_file(&path) {
            Ok(c) => self.player = Some(c),
            Err(e) => self.audio_note = e,
        }
    }

    /// Pictures from `t` on (a new stream).
    fn seek(&mut self, t: f64) {
        let t = t.clamp(0.0, self.seconds().max(0.0));
        let frame = (t * self.fps()) as u32;
        let frame = match &self.info {
            Ok(i) if i.frames > 0 => frame.min(i.frames - 1),
            _ => frame,
        };
        self.stream = None;
        self.next = None;
        self.need_first = true;
        self.from = frame;
        self.ended = false;
        match VideoStream::start(&self.file, frame, AHEAD) {
            Ok(s) => self.stream = Some(s),
            Err(e) => self.error = Some(e),
        }
        self.pos = frame as f64 / self.fps();
        if self.playing {
            self.origin = Instant::now() - Duration::from_secs_f64(self.pos);
            self.start_sound(self.pos);
        }
    }

    pub fn play(&mut self) {
        if self.ended || self.position() >= self.seconds() - 0.05 {
            self.playing = false;
            self.seek(0.0);
        }
        if self.playing || self.info.is_err() {
            return;
        }
        self.playing = true;
        self.origin = Instant::now() - Duration::from_secs_f64(self.pos);
        self.start_sound(self.pos);
    }

    pub fn pause(&mut self) {
        if self.playing {
            self.pos = self.position();
            self.playing = false;
            self.stop_sound();
        }
    }

    /// Back to the start, stopped.
    pub fn stop(&mut self) {
        self.playing = false;
        self.stop_sound();
        self.seek(0.0);
    }

    /// The picture due now: takes the stream's pictures up to it. Returns whether to repaint soon.
    fn tick(&mut self, ctx: &egui::Context) -> bool {
        // the soundtrack, once decoded; started where the picture is if it plays already
        if let Some(rx) = &self.audio_rx {
            if let Ok(r) = rx.try_recv() {
                self.audio_rx = None;
                match r {
                    Ok(Some(a)) => {
                        self.audio = Some(Arc::new(a));
                        if self.playing {
                            let t = self.position();
                            self.start_sound(t);
                        }
                    }
                    Ok(None) => self.audio_note = "no soundtrack".into(),
                    Err(e) => self.audio_note = e,
                }
            }
        }
        let fps = self.fps();
        let due = (self.position() * fps) as u32;
        let mut latest = None;
        if let Some(s) = &self.stream {
            loop {
                if let Some(p) = &latest {
                    let p: &movie::Picture = p;
                    if !self.playing || p.index >= due {
                        break;
                    }
                } else if !self.playing && !self.need_first {
                    break; // paused, the seek's picture shown
                }
                let got = match self.next.take() {
                    Some(p) => Ok(Some(p)),
                    None => s.poll(),
                };
                match got {
                    Ok(Some(p)) => {
                        if self.playing && p.index > due && !self.need_first {
                            self.next = Some(p); // early: kept for its time
                            break;
                        }
                        latest = Some(p);
                    }
                    Ok(None) => break,
                    Err(Some(e)) => {
                        self.error = Some(e);
                        break;
                    }
                    Err(None) => {
                        if self.playing && self.position() * fps >= (self.shown.unwrap_or(0) + 1) as f64 {
                            self.ended = true;
                        }
                        break;
                    }
                }
            }
        }
        if let Some(p) = latest {
            self.need_first = false;
            let (w, h) = (self.stream.as_ref().map(|s| s.width).unwrap_or(1), self.stream.as_ref().map(|s| s.height).unwrap_or(1));
            let img = egui::ColorImage::from_rgba_unmultiplied([w as usize, h as usize], &p.rgba);
            match &mut self.tex {
                Some(t) => t.set(img, egui::TextureOptions::LINEAR),
                None => self.tex = Some(ctx.load_texture("hwvx-movie", img, egui::TextureOptions::LINEAR)),
            }
            self.shown = Some(p.index);
        }
        if self.playing && (self.ended || self.position() >= self.seconds() + 0.5) {
            self.pos = self.seconds();
            self.playing = false;
            self.ended = true;
            self.stop_sound();
        }
        self.playing || self.audio_rx.is_some() || self.need_first && self.stream.is_some()
    }
}

/// The data folder's movies and a mod's.
fn gather(data: Option<&Path>, mod_dir: Option<&Path>) -> Vec<MovieFile> {
    let mut list = data.map(movie::find_movies).unwrap_or_default();
    if let Some(m) = mod_dir {
        for mut f in movie::find_movies(m) {
            f.name = format!("mod: {}", f.name);
            list.push(f);
        }
    }
    list
}

/// A video file, in the system's dialog (the prompt an argument, never inside a script).
fn choose_video() -> Result<Option<String>, String> {
    use std::process::Command;
    let prompt = "Open a video";
    let run = |c: &mut Command| -> Result<Option<String>, String> {
        let out = c.output().map_err(|e| e.to_string())?;
        let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
        Ok((out.status.success() && !s.is_empty()).then_some(s))
    };
    if cfg!(target_os = "macos") {
        run(Command::new("osascript").args(["-e", "on run argv", "-e", "POSIX path of (choose file with prompt (item 1 of argv))", "-e", "end run", prompt]))
    } else if cfg!(windows) {
        run(xdlrs::command("powershell").env("HWVX_PROMPT", prompt).args([
            "-NoProfile",
            "-Command",
            "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = $env:HWVX_PROMPT; if ($d.ShowDialog() -eq 'OK') { $d.FileName }",
        ]))
    } else {
        run(Command::new("zenity").args(["--file-selection", &format!("--title={prompt}")]))
            .or_else(|_| run(Command::new("kdialog").args(["--title", prompt, "--getopenfilename", "."])))
            .map_err(|_| "no file dialog here (install zenity or kdialog)".to_string())
    }
}

impl Browser {
    pub(crate) fn data_folder(&self) -> Option<PathBuf> {
        self.args.data.clone().or_else(|| self.hello["data"].as_str().map(PathBuf::from))
    }

    fn mod_folder(&self) -> Option<PathBuf> {
        self.content.proj.as_ref().map(|p| p.dir.clone())
    }

    /// The list, made once per data folder (and again with Rescan).
    pub(crate) fn movies_refresh(&mut self, force: bool) {
        let data = self.data_folder();
        if !force && self.movies.listed_for.is_some() && self.movies.listed_for == data {
            return;
        }
        self.movies.list = gather(data.as_deref(), self.mod_folder().as_deref());
        self.movies.infos = vec![None; self.movies.list.len()];
        self.movies.listed_for = data;
    }

    pub(crate) fn open_movie(&mut self, f: MovieFile, play: bool) {
        if let Some(i) = self.movies.list.iter().position(|m| m.path == f.path) {
            self.movies.infos[i].get_or_insert_with(|| movie::probe(&f));
        }
        let info = movie::probe(&f);
        let mut om = OpenMovie {
            info: info.clone(),
            stream: None,
            audio_rx: None,
            audio: None,
            audio_note: String::new(),
            playing: false,
            origin: Instant::now(),
            pos: 0.0,
            tex: None,
            shown: None,
            from: 0,
            player: None,
            scrub: None,
            ended: false,
            next: None,
            need_first: false,
            error: None,
            export: std::env::current_dir().unwrap_or_default().join(format!("{}.wav", f.path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default())).display().to_string(),
            file: f.clone(),
        };
        if info.is_ok() {
            let (tx, rx) = std::sync::mpsc::channel();
            let c = self.ctx.clone();
            std::thread::spawn(move || {
                let _ = tx.send(movie::decode_audio(&f));
                c.request_repaint();
            });
            om.audio_rx = Some(rx);
            om.seek(0.0);
            if play {
                om.play();
            }
        }
        self.movies.open = Some(om);
        self.media.view = View::Movie;
    }

    pub(crate) fn movie_list(&mut self, ui: &mut egui::Ui) {
        self.movies_refresh(false);
        ui.horizontal(|ui| {
            ui.label("Filter");
            ui.text_edit_singleline(&mut self.movies.filter);
        });
        let mut open = None;
        let mut external = None;
        ui.horizontal(|ui| {
            if ui.button("Rescan").on_hover_text("the data folder's FMV_ and an open mod's folder, again").clicked() {
                self.movies.listed_for = None;
            }
            if ui.button("Open a video…").on_hover_text("any video file: the game's kinds play here, the others in your video player").clicked() {
                match choose_video() {
                    Ok(Some(p)) => {
                        let p = PathBuf::from(p);
                        match movie::kind_of(&p) {
                            Some(kind) => {
                                let bytes = std::fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
                                open = Some(MovieFile { name: p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), path: p, kind, bytes });
                            }
                            None => external = Some(p),
                        }
                    }
                    Ok(None) => {}
                    Err(e) => self.fail(e),
                }
            }
        });
        ui.separator();
        if self.movies.list.is_empty() {
            ui.weak("no movies in the data folder");
        }
        let filter = self.movies.filter.to_lowercase();
        let current = self.movies.open.as_ref().map(|o| o.file.path.clone());
        egui::ScrollArea::vertical().id_salt("movies").auto_shrink([false, false]).show(ui, |ui| {
            for (i, m) in self.movies.list.iter().enumerate() {
                if !filter.is_empty() && !m.name.to_lowercase().contains(&filter) {
                    continue;
                }
                let info = self.movies.infos[i].get_or_insert_with(|| if m.kind == Kind::Other { Err("other".into()) } else { movie::probe(m) });
                let sub = match (&*info, m.kind) {
                    (_, Kind::Other) => "other video: your video player".to_string(),
                    (Ok(i), _) => format!("{}  ·  {}×{}  ·  {}  ·  {}", m.kind.name(), i.width, i.height, fmt_time(i.seconds), if i.channels > 0 { format!("{} ch {} Hz", i.channels, i.rate) } else { "silent".into() }),
                    (Err(e), _) if e.contains("FFmpeg not found") => format!("{}  ·  FFmpeg not found", m.kind.name()),
                    (Err(_), _) => format!("{}  ·  unreadable", m.kind.name()),
                };
                let sel = current.as_ref() == Some(&m.path);
                let r = ui.selectable_label(sel, egui::RichText::new(format!("{}\n{}", m.name, sub)));
                let r = r.on_hover_text(format!("{}\n{:.1} MB{}", m.path.display(), m.bytes as f64 / 1e6, info.as_ref().err().map(|e| format!("\n{e}")).unwrap_or_default()));
                if r.clicked() {
                    open = Some(m.clone());
                }
            }
        });
        if let Some(p) = external {
            match hwvx_browser::media::open_external(&p) {
                Ok(()) => self.note(format!("opened {} in your video player", p.display())),
                Err(e) => self.fail(e),
            }
        }
        if let Some(m) = open {
            self.open_movie(m, false);
        }
    }

    pub(crate) fn movie_panel(&mut self, ui: &mut egui::Ui) {
        let ctx = ui.ctx().clone();
        let Some(om) = self.movies.open.as_mut() else {
            ui.centered_and_justified(|ui| ui.label("no movie open (the Media tab lists them)"));
            return;
        };
        let soon = om.tick(&ctx);
        let (mut external, mut export) = (false, false);
        let mut note = None;
        ui.horizontal(|ui| {
            let ok = om.info.is_ok();
            ui.add_enabled_ui(ok, |ui| {
                if om.playing {
                    if ui.button("⏸ Pause").clicked() {
                        om.pause();
                    }
                } else if ui.button("▶ Play").clicked() {
                    om.play();
                }
                if ui.button("■ Stop").on_hover_text("back to the start").clicked() {
                    om.stop();
                }
                if ui.button("⏮ Restart").clicked() {
                    om.seek(0.0);
                    om.play();
                }
                let len = om.seconds().max(0.01);
                let mut t = om.scrub.unwrap_or_else(|| om.position().min(len));
                ui.spacing_mut().slider_width = (ui.available_width() - 150.0).max(80.0);
                let r = ui.add(egui::Slider::new(&mut t, 0.0..=len).show_value(false).trailing_fill(true));
                if r.dragged() {
                    om.scrub = Some(t);
                } else if r.drag_stopped() || r.changed() {
                    om.scrub = None;
                    om.seek(t);
                }
                ui.monospace(format!("{} / {}", fmt_time(om.scrub.unwrap_or_else(|| om.position().min(len))), fmt_time(len)));
            });
        });
        ui.horizontal(|ui| {
            external = ui.button("Open in your video player").on_hover_text(om.file.path.display().to_string()).clicked();
            ui.add_enabled_ui(om.audio.is_some(), |ui| {
                ui.add(egui::TextEdit::singleline(&mut om.export).desired_width((ui.available_width() - 140.0).clamp(80.0, 420.0)));
                export = ui.button("Export audio WAV").clicked();
            });
        });
        let full = ui.available_rect_before_wrap().intersect(ui.clip_rect());
        let status_h = ui.text_style_height(&egui::TextStyle::Body) * 2.0 + 10.0;
        let rect = egui::Rect::from_min_max(full.min, egui::pos2(full.max.x, (full.max.y - status_h).max(full.min.y + 16.0)));
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::BLACK);
        match (&om.info, &om.tex) {
            (Err(e), _) => {
                let msg = if e.contains("FFmpeg not found") {
                    format!("FFmpeg not found: the {} movies play through FFmpeg 6-9 (libavformat, libavcodec, libavutil), looked for beside the editor, in its package and the system's (or HWVX_FFMPEG_DIR).\n\nFFmpeg not found: open it in your video player instead.", om.file.kind.name())
                } else if om.file.kind == Kind::Other {
                    "not one of the game's movies (THP, Bink, PSS):\nopen it in your video player".to_string()
                } else {
                    e.clone()
                };
                let g = painter.layout(msg, egui::FontId::proportional(16.0), egui::Color32::LIGHT_GRAY, (rect.width() - 60.0).max(100.0));
                painter.galley(rect.center() - g.size() / 2.0, g, egui::Color32::LIGHT_GRAY);
            }
            (Ok(_), Some(tex)) => {
                // the game's movies are 4:3 (the PS2's 720x480 too)
                let aspect = 4.0 / 3.0;
                let mut size = rect.size();
                if size.x / size.y > aspect {
                    size.x = size.y * aspect;
                } else {
                    size.y = size.x / aspect;
                }
                let r = egui::Rect::from_center_size(rect.center(), size);
                painter.image(tex.id(), r, egui::Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)), egui::Color32::WHITE);
            }
            (Ok(_), None) => {
                painter.text(rect.center(), egui::Align2::CENTER_CENTER, "decoding…", egui::FontId::proportional(16.0), egui::Color32::GRAY);
            }
        }
        let status = egui::Rect::from_min_max(egui::pos2(full.min.x, rect.max.y), full.max);
        let line1 = match &om.info {
            Ok(i) => format!("{}  ·  {}  ·  {:.1} MB", om.file.name, i.summary(), om.file.bytes as f64 / 1e6),
            Err(_) => format!("{}  ·  {}  ·  {:.1} MB", om.file.name, om.file.kind.name(), om.file.bytes as f64 / 1e6),
        };
        let line2 = format!(
            "frame {}  ·  {}  ·  sound: {}{}",
            om.shown.map(|n| n.to_string()).unwrap_or_else(|| "-".into()),
            if om.playing { "playing" } else if om.ended { "ended" } else { "paused" },
            if om.audio_rx.is_some() {
                "decoding…".to_string()
            } else if let Some(a) = &om.audio {
                format!("{} ch {} Hz, {:.2} s, through the system's player", a.channels, a.rate, a.seconds())
            } else {
                "-".into()
            },
            om.error.as_ref().map(|e| format!("  ·  {e}")).or_else(|| (!om.audio_note.is_empty()).then(|| format!("  ·  {}", om.audio_note))).unwrap_or_default()
        );
        ui.scope_builder(egui::UiBuilder::new().max_rect(status), |ui| {
            ui.label(egui::RichText::new(line1).small());
            ui.label(egui::RichText::new(line2).weak().small());
        });
        if export {
            if let Some(a) = &om.audio {
                let path = PathBuf::from(om.export.trim());
                note = Some(match std::fs::write(&path, a.wav_from(0.0)) {
                    Ok(()) => Ok(format!("saved {}", path.display())),
                    Err(e) => Err(format!("{}: {e}", path.display())),
                });
            }
        }
        if external {
            om.pause();
            let p = om.file.path.clone();
            note = Some(hwvx_browser::media::open_external(&p).map(|_| format!("opened {} in your video player", p.display())));
        }
        if soon {
            ctx.request_repaint_after(Duration::from_millis(8));
        }
        match note {
            Some(Ok(s)) => self.note(s),
            Some(Err(e)) => self.fail(e),
            None => {}
        }
    }

    /// --movie NAME[@SECONDS]: ready (true) once the movie shows the frame asked for.
    pub(crate) fn movie_script(&mut self) -> bool {
        let Some((name, at)) = self.movies.script.clone() else { return true };
        if !self.movies.script_done {
            self.movies.script_done = true;
            self.movies.tab = true;
            self.movies.audio_tab = false;
            self.movies_refresh(false);
            let n = name.trim_end_matches('!').to_lowercase();
            match self.movies.list.iter().find(|m| m.name.to_lowercase().contains(&n)).cloned() {
                Some(m) => {
                    self.open_movie(m, false);
                    if let Some(om) = self.movies.open.as_mut() {
                        om.seek(at);
                    }
                }
                None => {
                    self.fail(format!("--movie: no movie {name}"));
                    return true;
                }
            }
        }
        let play = name.ends_with('!');
        match self.movies.open.as_mut() {
            Some(om) => {
                let ready = om.info.is_err() || (!om.need_first && om.shown.is_some() && om.audio_rx.is_none());
                // NAME! plays from there; the shot is taken a second in
                if ready && play && !om.playing && !om.ended {
                    om.play();
                    self.movies.script_t = Some(Instant::now());
                }
                ready && (!play || self.movies.script_t.is_some_and(|t| t.elapsed() > Duration::from_secs(1)))
            }
            None => true,
        }
    }
}
