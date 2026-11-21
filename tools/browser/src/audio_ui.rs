//! The Audio browser (the Media tab's Audio, in the 3D view's place): every sound, song,
//! instrument sample, movie soundtrack and open mod's audio file of the data (src/audio_index.rs,
//! indexed on a thread once per data folder), searched by archive, pack and name, filtered by
//! kind and archive; ▶ plays the decoded samples with the system's player, a song in the game
//! (the pack loaded in the viewer, its sequencer), and "Open in pack" shows the item in the pack's
//! own sound view (the game's mixer, waveform). Length, rate, loop; WAV (and a song's MIDI) export.

use crate::Browser;
use crate::media_ui::View;
use eframe::egui;
use hwvx_browser::audio_index::{self, Entry, Kind};
use hwvx_browser::media::{play_file, wav_bytes};
use std::path::PathBuf;
use std::sync::mpsc::{Receiver, TryRecvError};
use std::time::{Duration, Instant};

enum Msg {
    Archive(String, Result<usize, String>),
    Done(Vec<Entry>),
}

/// An entry's samples, decoded once (off the UI thread) for its waveform, ▶ and export.
struct Decoded {
    pcm: Vec<i16>,
    rate: u32,
    /// (min, max) of ENVELOPE equal runs of the samples: the waveform drawn from it
    env: Vec<(i16, i16)>,
}

const ENVELOPE: usize = 2048;

/// What follows a decode.
enum After {
    Show,
    Play,
    Export(PathBuf),
}

pub struct AudioState {
    pub entries: Vec<Entry>,
    indexing: Option<Receiver<Msg>>,
    pub indexed_for: Option<(Option<PathBuf>, Option<PathBuf>)>,
    pub archives_read: usize,
    pub notes: Vec<String>,
    pub search: String,
    /// sound, instrument, song, movie, mod file
    pub kinds: [bool; 5],
    pub archive: String,
    pub selected: Option<usize>,
    /// the archives of the entries (sorted), made when the index is in
    archives: Vec<String>,
    /// the rows shown: for (search, kinds, archive), and their seconds
    rows: Option<((String, [bool; 5], String), Vec<usize>, f64)>,
    /// the selected entry's samples (entry index, decoded or why not), and a decode under way
    sel_pcm: Option<(usize, Result<std::sync::Arc<Decoded>, String>)>,
    decoding: Option<(usize, Receiver<Result<Decoded, String>>, After)>,
    pub export: String,
    player: Option<std::process::Child>,
    playing: Option<(usize, Instant, f64)>,
    /// a pack being loaded in the viewer, then its item opened (and played: a song)
    after_load: Option<(String, String, bool, Instant)>,
}

impl Default for AudioState {
    fn default() -> Self {
        AudioState {
            entries: Vec::new(),
            indexing: None,
            indexed_for: None,
            archives_read: 0,
            notes: Vec::new(),
            search: String::new(),
            kinds: [true; 5],
            archive: String::new(),
            selected: None,
            archives: Vec::new(),
            rows: None,
            sel_pcm: None,
            decoding: None,
            export: String::new(),
            player: None,
            playing: None,
            after_load: None,
        }
    }
}

impl Drop for AudioState {
    fn drop(&mut self) {
        if let Some(mut c) = self.player.take() {
            let _ = c.kill();
        }
    }
}

const KINDS: [Kind; 5] = [Kind::Sound, Kind::Instrument, Kind::Song, Kind::Movie, Kind::ModFile];

/// Resampled output at most this long (a few seconds' sample at a bogus rate of a few Hz asked
/// for gigabytes).
const PLAYABLE_MAX: usize = 48000 * 600;

/// For the system's player: a rate it takes (a bank sample's base-note rate can be 2920 or
/// 220942 Hz), linearly resampled to 48 kHz outside 4-96 kHz. A rate under 1 kHz (a bad
/// header) is taken as 1 kHz; the output stops at ten minutes.
pub fn playable(rate: u32, pcm: &[i16]) -> (u32, Vec<i16>) {
    if (4000..=96000).contains(&rate) || pcm.is_empty() {
        return (rate, pcm.to_vec());
    }
    let rate = rate.max(1000);
    let n = (pcm.len() as u64 * 48000 / rate as u64).clamp(1, PLAYABLE_MAX as u64) as usize;
    let step = rate as f64 / 48000.0;
    let out = (0..n)
        .map(|i| {
            let x = i as f64 * step;
            let (a, f) = (x as usize, x.fract());
            let s0 = pcm[a.min(pcm.len() - 1)] as f64;
            let s1 = pcm[(a + 1).min(pcm.len() - 1)] as f64;
            (s0 + (s1 - s0) * f).round() as i16
        })
        .collect();
    (48000, out)
}

fn envelope(pcm: &[i16]) -> Vec<(i16, i16)> {
    if pcm.is_empty() {
        return vec![];
    }
    let n = ENVELOPE.min(pcm.len());
    (0..n)
        .map(|c| {
            let s = &pcm[c * pcm.len() / n..((c + 1) * pcm.len() / n).max(c * pcm.len() / n + 1)];
            (*s.iter().min().unwrap(), *s.iter().max().unwrap())
        })
        .collect()
}

/// An entry's samples and rate (a mod file's from its own decode: once).
fn decode(e: &Entry) -> Result<Decoded, String> {
    let (pcm, rate) = if e.kind == Kind::ModFile { hwvx_browser::content_sounds::decode_file(&e.file)? } else { (e.decode()?, e.rate) };
    Ok(Decoded { env: envelope(&pcm), pcm, rate })
}

fn fmt_len(e: &Entry) -> String {
    match e.kind {
        Kind::Song => format!("{} bytes", e.samples),
        _ if e.rate == 0 => "?".into(),
        _ => format!("{:.2} s", e.seconds()),
    }
}

impl Browser {
    /// The index, (re)made on a thread when the data folder or the open mod changes.
    pub(crate) fn audio_refresh(&mut self, force: bool) {
        let data = self.data_folder();
        let mod_dir = self.content.proj.as_ref().map(|p| p.dir.clone());
        let key = (data.clone(), mod_dir.clone());
        if !force && self.audio.indexed_for.as_ref() == Some(&key) {
            return;
        }
        self.audio.indexed_for = Some(key);
        self.audio.entries.clear();
        self.audio.notes.clear();
        self.audio.archives_read = 0;
        self.audio.selected = None;
        self.audio.sel_pcm = None;
        self.audio.decoding = None;
        self.audio.rows = None;
        self.audio.archives.clear();
        let (tx, rx) = std::sync::mpsc::channel();
        let c = self.ctx.clone();
        std::thread::spawn(move || {
            let mut all = Vec::new();
            if let Some(d) = &data {
                all = audio_index::index_all(d, |f, r| {
                    let _ = tx.send(Msg::Archive(f.display().to_string(), r));
                    c.request_repaint();
                });
                for m in hwvx_browser::movie::find_movies(d) {
                    if let Ok(info) = hwvx_browser::movie::probe(&m) {
                        if info.channels > 0 {
                            all.push(audio_index::movie_entry(&m, &info));
                        }
                    }
                }
            }
            if let Some(m) = &mod_dir {
                all.extend(audio_index::mod_files(m));
            }
            let _ = tx.send(Msg::Done(all));
            c.request_repaint();
        });
        self.audio.indexing = Some(rx);
    }

    fn audio_poll(&mut self) {
        let Some(rx) = &self.audio.indexing else { return };
        loop {
            let m = match rx.try_recv() {
                Ok(m) => m,
                Err(TryRecvError::Empty) => return,
                Err(TryRecvError::Disconnected) => {
                    // the indexer crashed on an archive: what it read before is lost with it
                    self.audio.indexing = None;
                    self.audio.notes.push("the index stopped (a crash reading the data): Rescan to try again".into());
                    return;
                }
            };
            match m {
                Msg::Archive(f, r) => {
                    self.audio.archives_read += 1;
                    if let Err(e) = r {
                        self.audio.notes.push(format!("{f}: {e}"));
                    }
                }
                Msg::Done(v) => {
                    let mut archives: Vec<String> = v.iter().filter(|e| !matches!(e.kind, Kind::Movie | Kind::ModFile)).map(|e| e.archive.clone()).collect();
                    archives.sort_by_key(|a| a.to_lowercase());
                    archives.dedup();
                    self.audio.archives = archives;
                    self.audio.rows = None;
                    self.audio.entries = v;
                    self.audio.indexing = None;
                    return;
                }
            }
        }
    }

    /// Every frame: the index's progress, and a pack loaded for an item it opens.
    pub(crate) fn audio_tick(&mut self) {
        self.audio_poll();
        if let Some((pack, node, play, t0)) = self.audio.after_load.clone() {
            let here = self.outline.as_ref().is_some_and(|o| o.pack.eq_ignore_ascii_case(&pack));
            if here && self.media.listed {
                self.audio.after_load = None;
                self.open_media(&node);
                if play {
                    self.play_game();
                }
            } else if t0.elapsed() > Duration::from_secs(90) {
                self.audio.after_load = None;
            }
        }
        if let Some((_, t0, len)) = self.audio.playing {
            if t0.elapsed().as_secs_f64() > len + 0.2 {
                self.audio.playing = None;
            }
        }
        self.audio_decoded();
    }

    /// A decode's result, when it is in: kept for the entry, then what was asked of it.
    fn audio_decoded(&mut self) {
        let Some((i, rx, _)) = &self.audio.decoding else { return };
        let i = *i;
        let r = match rx.try_recv() {
            Ok(r) => r.map(std::sync::Arc::new),
            Err(TryRecvError::Empty) => return self.ctx.request_repaint_after(Duration::from_millis(50)),
            Err(TryRecvError::Disconnected) => Err("its decoder stopped (a crash: damaged data?)".into()),
        };
        let Some((_, _, after)) = self.audio.decoding.take() else { return };
        self.audio.sel_pcm = Some((i, r.clone()));
        match (after, r) {
            (After::Show, _) => {}
            (_, Err(err)) => self.fail(err),
            (After::Play, Ok(d)) => self.audio_play_decoded(i, &d),
            (After::Export(path), Ok(d)) => self.audio_write(&path, Ok(wav_bytes(d.rate, &d.pcm))),
        }
    }

    /// The entry's samples decoded on a thread, then `after` (one decode at a time: a later
    /// one takes its place).
    fn audio_decode(&mut self, i: usize, after: After) {
        if let Some((k, Ok(d))) = &self.audio.sel_pcm {
            if *k == i {
                let d = d.clone();
                match after {
                    After::Show => {}
                    After::Play => self.audio_play_decoded(i, &d),
                    After::Export(path) => self.audio_write(&path, Ok(wav_bytes(d.rate, &d.pcm))),
                }
                return;
            }
        }
        let e = self.audio.entries[i].clone();
        let (tx, rx) = std::sync::mpsc::channel();
        let ctx = self.ctx.clone();
        std::thread::spawn(move || {
            let _ = tx.send(decode(&e));
            ctx.request_repaint();
        });
        self.audio.decoding = Some((i, rx, after));
    }

    fn audio_play_decoded(&mut self, i: usize, d: &Decoded) {
        self.audio_stop();
        let id = self.audio.entries[i].id();
        let (r, pcm) = playable(d.rate, &d.pcm);
        let dir = hwvx_browser::resources::default_pref();
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("audio.wav");
        if let Err(err) = std::fs::write(&path, wav_bytes(r, &pcm)) {
            return self.fail(format!("{}: {err}", path.display()));
        }
        match play_file(&path) {
            Ok(c) => {
                self.audio.player = Some(c);
                self.audio.playing = Some((i, Instant::now(), pcm.len() as f64 / r.max(1) as f64));
                self.note(format!("playing {id} with the system's player{}", if r != d.rate { " (resampled to 48 kHz)" } else { "" }));
            }
            Err(err) => self.fail(err),
        }
    }

    fn audio_write(&mut self, path: &std::path::Path, bytes: Result<Vec<u8>, String>) {
        match bytes.and_then(|b| std::fs::write(path, b).map_err(|err| format!("{}: {err}", path.display()))) {
            Ok(()) => self.note(format!("saved {}", path.display())),
            Err(err) => self.fail(err),
        }
    }

    fn audio_stop(&mut self) {
        if let Some(mut c) = self.audio.player.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
        self.audio.playing = None;
    }

    fn audio_play(&mut self, i: usize) {
        self.audio_stop();
        let e = self.audio.entries[i].clone();
        match e.kind {
            Kind::Song => return self.audio_in_pack(i, true),
            Kind::Movie => {
                let Some(m) = hwvx_browser::movie::find_movies(&self.data_folder().unwrap_or_default()).into_iter().find(|m| m.path == e.file) else { return };
                self.movies.tab = true;
                self.open_movie(m, true);
                return;
            }
            _ => {}
        }
        self.audio_decode(i, After::Play);
    }

    /// Loads the entry's pack in the viewer and opens the item there (its own view: the game's
    /// mixer or sequencer).
    fn audio_in_pack(&mut self, i: usize, play: bool) {
        let e = self.audio.entries[i].clone();
        let key = audio_index::viewer_pack(&e, self.packs.iter().map(|p| (p.archive.as_str(), p.type_name.as_str(), p.id))).map(|(a, t, i)| (a.to_string(), t.to_string(), i));
        let found = key.and_then(|(a, t, i)| self.packs.iter().find(|p| p.archive == a && p.type_name == t && p.id == i).cloned());
        let Some(p) = found else {
            return self.fail(format!("{}/{}: not in the viewer's pack list", e.archive, e.pack));
        };
        self.movies.tab = false;
        self.world.tab_worlds = false;
        self.content.tab = false;
        self.level.tab = false;
        let here = self.outline.as_ref().is_some_and(|o| o.pack.eq_ignore_ascii_case(&p.pack));
        if !here {
            self.load(&p.archive, &p.type_name, p.id);
        }
        self.audio.after_load = Some((p.pack.clone(), e.node(), play, Instant::now()));
    }

    /// The left panel's part of the Audio browser: what is indexed, and the button to it.
    pub(crate) fn audio_side(&mut self, ui: &mut egui::Ui) {
        self.audio_refresh(false);
        ui.label("Every sound, song, instrument sample, movie soundtrack and an open mod's audio file of the data.");
        if self.audio.indexing.is_some() {
            ui.horizontal(|ui| {
                ui.spinner();
                ui.label(format!("reading the archives: {}", self.audio.archives_read));
            });
        } else {
            let count = |k: Kind| self.audio.entries.iter().filter(|e| e.kind == k).count();
            egui::Grid::new("audio-counts").num_columns(2).show(ui, |ui| {
                for k in KINDS {
                    ui.label(k.name());
                    ui.label(count(k).to_string());
                    ui.end_row();
                }
            });
        }
        if ui.button("Open the Audio browser").clicked() {
            self.media.view = View::Audio;
        }
        if ui.button("Rescan").clicked() {
            self.audio_refresh(true);
        }
        for n in &self.audio.notes {
            ui.label(egui::RichText::new(n).weak().small());
        }
    }

    pub(crate) fn audio_panel(&mut self, ui: &mut egui::Ui) {
        self.audio_refresh(false);
        ui.horizontal(|ui| {
            ui.label("Search");
            ui.add(egui::TextEdit::singleline(&mut self.audio.search).hint_text("archive, pack, sound/3 ...").desired_width(220.0));
            for (k, on) in KINDS.iter().zip(self.audio.kinds.iter_mut()) {
                ui.checkbox(on, k.name());
            }
            egui::ComboBox::from_id_salt("audio-archive").selected_text(if self.audio.archive.is_empty() { "every archive" } else { &self.audio.archive }).show_ui(ui, |ui| {
                ui.selectable_value(&mut self.audio.archive, String::new(), "every archive");
                for a in &self.audio.archives {
                    ui.selectable_value(&mut self.audio.archive, a.clone(), a);
                }
            });
            if ui.button("■ Stop").clicked() {
                self.audio_stop();
                self.request("stop", serde_json::json!({}), crate::Pending::Quiet);
            }
        });
        if self.audio.indexing.is_some() {
            ui.horizontal(|ui| {
                ui.spinner();
                ui.label(format!("reading the archives: {}", self.audio.archives_read));
            });
            ui.ctx().request_repaint_after(Duration::from_millis(100));
            return;
        }
        // filtered again only when the search, the kinds or the archive changed
        let key = (self.audio.search.clone(), self.audio.kinds, self.audio.archive.clone());
        if self.audio.rows.as_ref().is_none_or(|r| r.0 != key) {
            let kinds: Vec<Kind> = KINDS.iter().zip(self.audio.kinds).filter(|(_, on)| *on).map(|(k, _)| *k).collect();
            let rows: Vec<usize> = self
                .audio
                .entries
                .iter()
                .enumerate()
                .filter(|(_, e)| audio_index::matches(e, &self.audio.search, &kinds) && (self.audio.archive.is_empty() || e.archive == self.audio.archive))
                .map(|(i, _)| i)
                .collect();
            let total: f64 = rows.iter().map(|&i| self.audio.entries[i].seconds()).sum();
            self.audio.rows = Some((key, rows, total));
        }
        let Some((_, rows, total)) = &self.audio.rows else { return };
        ui.label(egui::RichText::new(format!("{} of {} items  ·  {:.0} s of audio", rows.len(), self.audio.entries.len(), total)).weak());
        let detail_h = 120.0;
        let table_h = (ui.available_height() - detail_h).max(80.0);
        let (mut play, mut open, mut select) = (None, None, None);
        let playing = self.audio.playing.map(|p| p.0);
        egui::ScrollArea::vertical().id_salt("audio-rows").max_height(table_h).auto_shrink([false, false]).show_rows(ui, 20.0, rows.len(), |ui, range| {
            egui::Grid::new("audio-grid").num_columns(7).striped(true).min_col_width(40.0).show(ui, |ui| {
                for &i in &rows[range] {
                    let e = &self.audio.entries[i];
                    let label = if playing == Some(i) { "■" } else { "▶" };
                    let tip = match e.kind {
                        Kind::Song => "load the pack and play the song in the game (its sequencer)",
                        Kind::Movie => "open the movie and play it",
                        _ => "the decoded samples, with the system's player",
                    };
                    if ui.small_button(label).on_hover_text(tip).clicked() {
                        play = Some(i);
                    }
                    let sel = self.audio.selected == Some(i);
                    let name = match e.kind {
                        Kind::Movie | Kind::ModFile => e.archive.clone(),
                        _ => format!("{}  {}  {}", e.archive, e.pack, e.node()),
                    };
                    if ui.selectable_label(sel, egui::RichText::new(name).monospace()).clicked() {
                        select = Some(i);
                    }
                    ui.label(e.kind.name());
                    ui.label(fmt_len(e));
                    ui.label(if e.rate > 0 { format!("{} Hz", e.rate) } else { String::new() });
                    ui.label(e.loop_start.map(|l| format!("↻ {l}")).unwrap_or_default());
                    if matches!(e.kind, Kind::Sound | Kind::Instrument | Kind::Song) {
                        if ui.small_button("Open in pack").on_hover_text("load its pack in the viewer and open it there").clicked() {
                            open = Some(i);
                        }
                    } else {
                        ui.label("");
                    }
                    ui.end_row();
                }
            });
        });
        if let Some(i) = play {
            if playing == Some(i) {
                self.audio_stop();
            } else {
                self.audio_play(i);
            }
        }
        if let Some(i) = open {
            self.audio_in_pack(i, false);
        }
        if let Some(i) = select {
            self.audio_select(i);
        }
        ui.separator();
        self.audio_detail(ui);
        if self.audio.playing.is_some() {
            ui.ctx().request_repaint_after(Duration::from_millis(30));
        }
    }

    fn audio_select(&mut self, i: usize) {
        self.audio.selected = Some(i);
        let e = &self.audio.entries[i];
        let ext = if e.kind == Kind::Song { "mid" } else { "wav" };
        let base: String = e.id().trim_start_matches("base://").chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' || c == '-' { c } else { '_' }).collect();
        self.audio.export = std::env::current_dir().unwrap_or_default().join(format!("{base}.{ext}")).display().to_string();
    }

    fn audio_detail(&mut self, ui: &mut egui::Ui) {
        let Some(i) = self.audio.selected else {
            ui.weak("select an item for its waveform and export");
            return;
        };
        let e = self.audio.entries[i].clone();
        // its samples decoded once, off the UI thread (unless a decode of it is under way)
        let sample = matches!(e.kind, Kind::Sound | Kind::Instrument | Kind::ModFile);
        if sample && self.audio.sel_pcm.as_ref().is_none_or(|(k, _)| *k != i) && self.audio.decoding.is_none() {
            self.audio_decode(i, After::Show);
        }
        let mut export = false;
        ui.horizontal(|ui| {
            ui.strong(e.id());
            ui.label(format!("{}  ·  {}  ·  {}{}", e.kind.name(), fmt_len(&e), if e.rate > 0 { format!("{} Hz", e.rate) } else { "-".into() }, e.loop_start.map(|l| format!("  ·  loop from {l}")).unwrap_or_default()));
            if e.kind != Kind::Movie {
                ui.add(egui::TextEdit::singleline(&mut self.audio.export).desired_width(260.0));
                export = ui.button(if e.kind == Kind::Song { "Export MIDI" } else { "Export WAV" }).clicked();
            }
        });
        let rect = ui.available_rect_before_wrap();
        let rect = egui::Rect::from_min_size(rect.min, egui::vec2(rect.width(), rect.height().clamp(30.0, 90.0)));
        ui.allocate_rect(rect, egui::Sense::hover());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::from_rgb(0x20, 0x23, 0x28));
        match &self.audio.sel_pcm {
            Some((k, Ok(d))) if *k == i && !d.env.is_empty() => {
                let env = &d.env;
                let (mid, half) = (rect.center().y, rect.height() * 0.45);
                let cols = rect.width().max(1.0) as usize;
                let wave = egui::Color32::from_rgb(0x6C, 0xB4, 0xF0);
                for c in 0..cols {
                    let a = (c * env.len() / cols).min(env.len() - 1);
                    let b = ((c + 1) * env.len() / cols).clamp(a + 1, env.len());
                    let (lo, hi) = env[a..b].iter().fold((i16::MAX, i16::MIN), |(lo, hi), &(l, h)| (lo.min(l), hi.max(h)));
                    let x = rect.min.x + c as f32 + 0.5;
                    let (y0, y1) = (mid - hi as f32 / 32768.0 * half, mid - lo as f32 / 32768.0 * half);
                    painter.line_segment([egui::pos2(x, y0), egui::pos2(x, y1.max(y0 + 1.0))], egui::Stroke::new(1.0, wave));
                }
                if let Some(l) = e.loop_start {
                    let x = rect.min.x + l as f32 / d.pcm.len() as f32 * rect.width();
                    painter.line_segment([egui::pos2(x, rect.min.y), egui::pos2(x, rect.max.y)], egui::Stroke::new(1.5, egui::Color32::from_rgb(0xF0, 0xA0, 0x30)));
                }
                if let Some((p, t0, len)) = self.audio.playing {
                    if p == i && len > 0.0 {
                        let x = rect.min.x + (t0.elapsed().as_secs_f64() / len).min(1.0) as f32 * rect.width();
                        painter.line_segment([egui::pos2(x, rect.min.y), egui::pos2(x, rect.max.y)], egui::Stroke::new(1.0, egui::Color32::WHITE));
                    }
                }
            }
            Some((k, Err(err))) if *k == i => {
                painter.text(rect.center(), egui::Align2::CENTER_CENTER, err, egui::FontId::proportional(13.0), egui::Color32::LIGHT_RED);
            }
            _ => {
                let msg = match e.kind {
                    Kind::Song => "a MIDI song: ▶ plays it in the game (its sequencer and the pack's bank)",
                    Kind::Movie => "a movie's soundtrack: ▶ opens the movie",
                    _ if self.audio.decoding.is_some() => "decoding…",
                    _ => "",
                };
                painter.text(rect.center(), egui::Align2::CENTER_CENTER, msg, egui::FontId::proportional(13.0), egui::Color32::GRAY);
            }
        }
        if export {
            let path = PathBuf::from(self.audio.export.trim());
            if e.kind == Kind::Song {
                self.audio_write(&path, e.raw());
            } else {
                self.audio_decode(i, After::Export(path));
            }
        }
    }

    /// --audio SEARCH: ready (true) once the index is in.
    pub(crate) fn audio_script(&mut self) -> bool {
        let Some(s) = self.args.audio.clone() else { return true };
        if self.media.view != View::Audio {
            self.movies.tab = true;
            self.movies.audio_tab = true;
            self.audio.search = s;
            self.media.view = View::Audio;
            self.audio_refresh(false);
        }
        if self.audio.indexing.is_some() {
            return false;
        }
        if self.audio.selected.is_none() {
            let kinds: Vec<Kind> = KINDS.to_vec();
            if let Some(i) = self.audio.entries.iter().position(|e| audio_index::matches(e, &self.audio.search, &kinds) && e.kind == Kind::Sound) {
                self.audio_select(i);
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A bad rate of a few Hz: no gigabytes, a ten-minute cap at most.
    #[test]
    fn playable_is_bounded() {
        let pcm = vec![100i16; 48000];
        assert!(playable(2, &pcm).1.len() <= PLAYABLE_MAX);
        assert!(playable(0, &pcm).1.len() <= PLAYABLE_MAX);
        assert_eq!(playable(22050, &pcm).1.len(), pcm.len());
        assert_eq!(playable(2000, &[1, 2]).0, 48000);
    }

    #[test]
    fn envelope_keeps_the_peaks() {
        let mut pcm = vec![0i16; 100_000];
        pcm[54_321] = 30000;
        pcm[7] = -30000;
        let env = envelope(&pcm);
        assert_eq!(env.len(), ENVELOPE);
        assert!(env.iter().any(|e| e.1 == 30000) && env.iter().any(|e| e.0 == -30000));
        assert_eq!(envelope(&[5, -5]), vec![(5, 5), (-5, -5)]);
        assert!(envelope(&[]).is_empty());
    }
}
