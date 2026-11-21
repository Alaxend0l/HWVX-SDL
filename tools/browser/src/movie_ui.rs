//! The Media tab's movies (src/movie.rs decodes them): the data's movies listed with their format,
//! size, length, frame rate and audio; the open one plays in the 3D view's place with its sound
//! (the soundtrack decoded whole on a thread, played by the system's player from where the picture
//! is), with play/pause, stop, restart and a scrubber. Bink and PSS need FFmpeg (src/ffmpeg.rs);
//! without it, and for any other video (a modder's mp4...), "Open in your video player" hands the
//! file to the system's default app. Nothing reads a movie on the UI thread: the list is probed,
//! a movie opened and sought, and its sound written for the player, each on a thread.

use crate::Browser;
use crate::media_ui::View;
use eframe::egui;
use hwvx_browser::movie::{self, Audio, Info, Kind, MovieFile, VideoStream, fmt_time};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, Sender, TryRecvError};
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
    /// the list's descriptions, probed in turn on a thread (a new list's replaces it, ending it)
    pub probes: Option<Receiver<(usize, Result<Info, String>)>>,
    pub listed_for: Option<PathBuf>,
    pub filter: String,
    pub open: Option<OpenMovie>,
    /// --movie NAME[@SECONDS] (scripts): opened and paused there
    pub script: Option<(String, f64)>,
    pub script_done: bool,
    pub script_t: Option<Instant>,
}

/// What the sound thread does: the soundtrack from the position of a clock (its start and where
/// it started, in seconds), or nothing.
enum Sound {
    Play(Arc<Audio>, Instant, f64),
    Stop,
}

/// The soundtrack through the system's player, on a thread: each play writes the WAV from where
/// the picture is (the players start at the beginning only), so it is written here, not on the UI
/// thread (35 MB for 3 minutes). Only the latest command counts. The thread ends, its player
/// killed, when its sender is dropped.
fn sound_thread(rx: Receiver<Sound>, notes: Sender<String>, ctx: egui::Context) {
    // a file of its own: an earlier movie's thread may still be writing its own
    static N: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let dir = hwvx_browser::resources::default_pref();
    let _ = std::fs::create_dir_all(&dir);
    let path = dir.join(format!("movie-{}.wav", N.fetch_add(1, Ordering::Relaxed)));
    let mut player: Option<std::process::Child> = None;
    let kill = |p: &mut Option<std::process::Child>| {
        if let Some(mut c) = p.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    };
    let mut next = rx.recv().ok();
    while let Some(mut cmd) = next.take() {
        while let Ok(c) = rx.try_recv() {
            cmd = c;
        }
        kill(&mut player);
        let Sound::Play(a, since, at) = cmd else {
            next = rx.recv().ok();
            continue;
        };
        // where the picture is now
        let t = at + since.elapsed().as_secs_f64();
        if t < a.seconds() {
            let r = std::fs::write(&path, a.wav_from(t)).map_err(|e| format!("{}: {e}", path.display()));
            // a newer command while writing: it, not this
            if let Ok(c) = rx.try_recv() {
                next = Some(c);
                continue;
            }
            match r.and_then(|()| hwvx_browser::media::play_file(&path)) {
                Ok(c) => player = Some(c),
                Err(e) => {
                    let _ = notes.send(e);
                    ctx.request_repaint();
                }
            }
        }
        next = rx.recv().ok();
    }
    kill(&mut player);
    let _ = std::fs::remove_file(&path);
}

pub struct OpenMovie {
    pub file: MovieFile,
    pub info: Result<Info, String>,
    /// the description, being probed (the list hadn't yet)
    info_rx: Option<Receiver<Result<Info, String>>>,
    stream: Option<VideoStream>,
    audio_rx: Option<Receiver<Result<Option<Audio>, String>>>,
    /// set when the movie closes: its soundtrack's decode gives up
    audio_stop: Arc<AtomicBool>,
    pub audio: Option<Arc<Audio>>,
    pub audio_note: String,
    sound: Sender<Sound>,
    sound_notes: Receiver<String>,
    pub playing: bool,
    /// when playing started (at `pos`)
    started: Instant,
    /// the position, in seconds (paused, or where playing started)
    pos: f64,
    /// the picture shown, and its frame
    tex: Option<egui::TextureHandle>,
    pub shown: Option<u32>,
    /// the scrubber being dragged: its value
    scrub: Option<f64>,
    ended: bool,
    /// a picture decoded before its time
    next: Option<movie::Picture>,
    /// a seek's first picture not shown yet
    need_first: bool,
    /// asked for before the description was in: the seek, and whether to play
    pending_seek: Option<f64>,
    play_on_open: bool,
    pub error: Option<String>,
    pub export: String,
}

impl Drop for OpenMovie {
    fn drop(&mut self) {
        self.audio_stop.store(true, Ordering::Relaxed);
    }
}

impl OpenMovie {
    fn new(file: MovieFile, ctx: &egui::Context) -> OpenMovie {
        let (sound, rx) = std::sync::mpsc::channel();
        let (notes_tx, sound_notes) = std::sync::mpsc::channel();
        let c = ctx.clone();
        std::thread::spawn(move || sound_thread(rx, notes_tx, c));
        OpenMovie {
            info: Err(String::new()),
            info_rx: None,
            stream: None,
            audio_rx: None,
            audio_stop: Arc::new(AtomicBool::new(false)),
            audio: None,
            audio_note: String::new(),
            sound,
            sound_notes,
            playing: false,
            started: Instant::now(),
            pos: 0.0,
            tex: None,
            shown: None,
            scrub: None,
            ended: false,
            next: None,
            need_first: false,
            pending_seek: None,
            play_on_open: false,
            error: None,
            export: std::env::current_dir().unwrap_or_default().join(format!("{}.wav", file.path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default())).display().to_string(),
            file,
        }
    }

    /// The description in: the soundtrack's decode started, and the seek and play asked for.
    fn described(&mut self, info: Result<Info, String>, ctx: &egui::Context) {
        self.info = info;
        if self.info.is_err() {
            return;
        }
        let (tx, rx) = std::sync::mpsc::channel();
        let (c, f, stop) = (ctx.clone(), self.file.clone(), self.audio_stop.clone());
        std::thread::spawn(move || {
            let _ = tx.send(movie::decode_audio_until(&f, &stop));
            c.request_repaint();
        });
        self.audio_rx = Some(rx);
        let t = self.pending_seek.take().unwrap_or(0.0);
        self.seek(t);
        if std::mem::take(&mut self.play_on_open) {
            self.play();
        }
    }

    /// Still being probed.
    fn opening(&self) -> bool {
        self.info_rx.is_some()
    }
    fn fps(&self) -> f64 {
        self.info.as_ref().map(|i| i.fps).unwrap_or(29.97)
    }
    fn seconds(&self) -> f64 {
        self.info.as_ref().map(|i| i.seconds).unwrap_or(0.0)
    }
    /// Where the movie is, in seconds.
    pub fn position(&self) -> f64 {
        if self.playing { self.pos + self.started.elapsed().as_secs_f64() } else { self.pos }
    }

    fn stop_sound(&mut self) {
        let _ = self.sound.send(Sound::Stop);
    }

    /// The soundtrack from where the picture is, through the system's player.
    fn start_sound(&mut self) {
        match &self.audio {
            Some(a) if a.channels > 0 && self.playing => {
                let _ = self.sound.send(Sound::Play(a.clone(), self.started, self.pos));
            }
            _ => self.stop_sound(),
        }
    }

    /// Pictures from `t` on (a new stream, opened and sought on its thread).
    fn seek(&mut self, t: f64) {
        if self.opening() {
            self.pending_seek = Some(t);
            return;
        }
        let t = if t.is_finite() { t.clamp(0.0, self.seconds().max(0.0)) } else { 0.0 };
        let frame = (t * self.fps()) as u32;
        let frame = match &self.info {
            Ok(i) if i.frames > 0 => frame.min(i.frames - 1),
            _ => frame,
        };
        self.next = None;
        self.need_first = true;
        self.ended = false;
        self.stream = Some(VideoStream::start(&self.file, frame, AHEAD));
        self.pos = frame as f64 / self.fps();
        if self.playing {
            self.started = Instant::now();
            self.start_sound();
        }
    }

    pub fn play(&mut self) {
        if self.opening() {
            self.play_on_open = true;
            return;
        }
        if self.ended || self.position() >= self.seconds() - 0.05 {
            self.playing = false;
            self.seek(0.0);
        }
        if self.playing || self.info.is_err() {
            return;
        }
        self.playing = true;
        self.started = Instant::now();
        self.start_sound();
    }

    pub fn pause(&mut self) {
        self.play_on_open = false;
        if self.playing {
            self.pos = self.position();
            self.playing = false;
            self.stop_sound();
        }
    }

    /// Back to the start, stopped.
    pub fn stop(&mut self) {
        self.playing = false;
        self.play_on_open = false;
        self.stop_sound();
        self.seek(0.0);
    }

    /// The picture due now: takes the stream's pictures up to it. Returns how soon to repaint
    /// (None: nothing to wait for).
    fn tick(&mut self, ctx: &egui::Context) -> Option<Duration> {
        if let Some(rx) = &self.info_rx {
            match rx.try_recv() {
                Err(TryRecvError::Empty) => {}
                r => {
                    self.info_rx = None;
                    self.described(r.unwrap_or_else(|_| Err("the movie could not be read".into())), ctx);
                }
            }
        }
        // the soundtrack, once decoded; started where the picture is if it plays already
        if let Some(rx) = &self.audio_rx {
            match rx.try_recv() {
                Err(TryRecvError::Empty) => {}
                r => {
                    self.audio_rx = None;
                    match r {
                        Ok(Ok(Some(a))) => {
                            self.audio = Some(Arc::new(a));
                            self.start_sound();
                        }
                        Ok(Ok(None)) => self.audio_note = "no soundtrack".into(),
                        Ok(Err(e)) => self.audio_note = e,
                        Err(_) => self.audio_note = "the soundtrack could not be decoded".into(),
                    }
                }
            }
        }
        while let Ok(n) = self.sound_notes.try_recv() {
            self.audio_note = n;
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
                        self.need_first = false;
                        break;
                    }
                    Err(None) => {
                        self.need_first = false;
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
            if p.rgba.len() == p.width as usize * p.height as usize * 4 {
                let img = egui::ColorImage::from_rgba_unmultiplied([p.width as usize, p.height as usize], &p.rgba);
                match &mut self.tex {
                    Some(t) => t.set(img, egui::TextureOptions::LINEAR),
                    None => self.tex = Some(ctx.load_texture("hwvx-movie", img, egui::TextureOptions::LINEAR)),
                }
            }
            self.shown = Some(p.index);
        }
        if self.playing && (self.ended || self.position() >= self.seconds() + 0.5) {
            self.pos = self.seconds();
            self.playing = false;
            self.ended = true;
            self.stop_sound();
        }
        if self.playing {
            // the next frame's time (pictures not yet decoded are polled again then)
            let frame = 1.0 / fps;
            let left = (due + 1) as f64 * frame - self.position();
            Some(Duration::from_secs_f64(left.clamp(0.002, frame)))
        } else if self.opening() || self.need_first && self.stream.is_some() {
            Some(Duration::from_millis(15))
        } else {
            // the soundtrack's decode repaints when it's in
            None
        }
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
        // UTF-8 out: a path with letters outside the console's code page arrives whole
        run(xdlrs::command("powershell").env("HWVX_PROMPT", prompt).args([
            "-NoProfile",
            "-Command",
            "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = $env:HWVX_PROMPT; if ($d.ShowDialog() -eq 'OK') { $d.FileName }",
        ]))
    } else {
        run(Command::new("zenity").args(["--file-selection", &format!("--title={prompt}")]))
            .or_else(|_| run(Command::new("kdialog").args(["--title", prompt, "--getopenfilename", "."])))
            .map_err(|_| "no file dialog here (install zenity or kdialog)".to_string())
    }
}

/// A list's descriptions, probed in turn on a thread; it ends when the receiver is dropped.
fn probe_all(list: Vec<MovieFile>, ctx: egui::Context) -> Receiver<(usize, Result<Info, String>)> {
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        for (i, m) in list.iter().enumerate() {
            let info = if m.kind == Kind::Other { Err("other".into()) } else { movie::probe(m) };
            if tx.send((i, info)).is_err() {
                return;
            }
            ctx.request_repaint();
        }
    });
    rx
}

impl Browser {
    pub(crate) fn data_folder(&self) -> Option<PathBuf> {
        self.args.data.clone().or_else(|| self.hello["data"].as_str().map(PathBuf::from))
    }

    fn mod_folder(&self) -> Option<PathBuf> {
        self.content.proj.as_ref().map(|p| p.dir.clone())
    }

    /// The list, made once per data folder (and again with Rescan); its descriptions probed on a
    /// thread.
    pub(crate) fn movies_refresh(&mut self, force: bool) {
        let data = self.data_folder();
        if !force && self.movies.listed_for.is_some() && self.movies.listed_for == data {
            return;
        }
        self.movies.list = gather(data.as_deref(), self.mod_folder().as_deref());
        self.movies.infos = vec![None; self.movies.list.len()];
        self.movies.probes = Some(probe_all(self.movies.list.clone(), self.ctx.clone()));
        self.movies.listed_for = data;
    }

    /// The descriptions probed since the last frame.
    fn movies_take_probes(&mut self) {
        let Some(rx) = &self.movies.probes else { return };
        loop {
            match rx.try_recv() {
                Ok((i, info)) => {
                    if let Some(slot) = self.movies.infos.get_mut(i) {
                        *slot = Some(info);
                    }
                }
                Err(TryRecvError::Empty) => break,
                Err(TryRecvError::Disconnected) => {
                    self.movies.probes = None;
                    break;
                }
            }
        }
    }

    pub(crate) fn open_movie(&mut self, f: MovieFile, play: bool) {
        self.movies_take_probes();
        // the list's description if it has one, else probed on a thread of its own
        let known = self.movies.list.iter().position(|m| m.path == f.path).and_then(|i| self.movies.infos[i].clone());
        let mut om = OpenMovie::new(f, &self.ctx);
        om.play_on_open = play;
        match known {
            Some(info) => om.described(info, &self.ctx),
            None => {
                let (tx, rx) = std::sync::mpsc::channel();
                let (c, f) = (self.ctx.clone(), om.file.clone());
                std::thread::spawn(move || {
                    let _ = tx.send(movie::probe(&f));
                    c.request_repaint();
                });
                om.info_rx = Some(rx);
            }
        }
        self.movies.open = Some(om);
        self.media.view = View::Movie;
    }

    pub(crate) fn movie_list(&mut self, ui: &mut egui::Ui) {
        self.movies_refresh(false);
        self.movies_take_probes();
        ui.horizontal(|ui| {
            ui.label("Filter");
            ui.text_edit_singleline(&mut self.movies.filter);
        });
        let mut open = None;
        let mut ask = false;
        ui.horizontal(|ui| {
            if ui.button("Rescan").on_hover_text("the data folder's FMV_ and an open mod's folder, again").clicked() {
                self.movies.listed_for = None;
            }
            ask = ui.button("Open a video…").on_hover_text("any video file: the game's kinds play here, the others in your video player").clicked();
        });
        if ask {
            self.ask_then(choose_video, |b, r| match r {
                Ok(Some(p)) => {
                    let p = PathBuf::from(p);
                    match movie::kind_of(&p) {
                        Some(kind) => {
                            let bytes = std::fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
                            b.open_movie(MovieFile { name: p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), path: p, kind, bytes }, false);
                        }
                        None => match hwvx_browser::media::open_external(&p) {
                            Ok(()) => b.note(format!("opened {} in your video player", p.display())),
                            Err(e) => b.fail(e),
                        },
                    }
                }
                Ok(None) => {}
                Err(e) => b.fail(e),
            });
        }
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
                let info = &self.movies.infos[i];
                let sub = match (info, m.kind) {
                    (_, Kind::Other) => "other video: your video player".to_string(),
                    (None, _) => format!("{}  ·  …", m.kind.name()),
                    (Some(Ok(i)), _) => format!("{}  ·  {}×{}  ·  {}  ·  {}", m.kind.name(), i.width, i.height, fmt_time(i.seconds), if i.channels > 0 { format!("{} ch {} Hz", i.channels, i.rate) } else { "silent".into() }),
                    (Some(Err(e)), _) if e.contains("FFmpeg not found") => format!("{}  ·  FFmpeg not found", m.kind.name()),
                    (Some(Err(_)), _) => format!("{}  ·  unreadable", m.kind.name()),
                };
                let sel = current.as_ref() == Some(&m.path);
                let r = ui.selectable_label(sel, egui::RichText::new(format!("{}\n{}", m.name, sub)));
                let err = info.as_ref().and_then(|i| i.as_ref().err()).map(|e| format!("\n{e}")).unwrap_or_default();
                let r = r.on_hover_text(format!("{}\n{:.1} MB{err}", m.path.display(), m.bytes as f64 / 1e6));
                if r.clicked() {
                    open = Some(m.clone());
                }
            }
        });
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
        let (mut external, mut export) = (false, None);
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
                if ui.button("Export audio WAV").clicked() {
                    export = om.audio.clone().map(|a| (a, PathBuf::from(om.export.trim())));
                }
            });
        });
        let full = ui.available_rect_before_wrap().intersect(ui.clip_rect());
        let status_h = ui.text_style_height(&egui::TextStyle::Body) * 2.0 + 10.0;
        let rect = egui::Rect::from_min_max(full.min, egui::pos2(full.max.x, (full.max.y - status_h).max(full.min.y + 16.0)));
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::BLACK);
        match (&om.info, &om.tex) {
            _ if om.opening() => {
                painter.text(rect.center(), egui::Align2::CENTER_CENTER, "opening…", egui::FontId::proportional(16.0), egui::Color32::GRAY);
            }
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
                let msg = om.error.as_deref().unwrap_or("decoding…");
                painter.text(rect.center(), egui::Align2::CENTER_CENTER, msg, egui::FontId::proportional(16.0), egui::Color32::GRAY);
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
        if external {
            om.pause();
            let p = om.file.path.clone();
            note = Some(hwvx_browser::media::open_external(&p).map(|_| format!("opened {} in your video player", p.display())));
        }
        if let Some(d) = soon {
            ctx.request_repaint_after(d);
        }
        if let Some((a, path)) = export {
            // written on a thread: a 3-minute soundtrack is 35 MB
            self.ask_then(
                move || std::fs::write(&path, a.wav_from(0.0)).map(|()| format!("saved {}", path.display())).map_err(|e| format!("{}: {e}", path.display())),
                |b, r| match r {
                    Ok(s) => b.note(s),
                    Err(e) => b.fail(e),
                },
            );
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
                let ready = !om.opening() && (om.info.is_err() || (!om.need_first && om.shown.is_some() && om.audio_rx.is_none()));
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
