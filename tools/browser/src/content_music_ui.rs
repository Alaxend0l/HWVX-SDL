//! The Content tab's Soundtrack (src/content_music.rs): the songs the mod replaces in the left
//! panel; a song's playlist in the inspector (files copied into the mod's music/ folder, their
//! order, intro, loop points, volume), a preview of a file or its loop's seam with the system's
//! player, the base song through the viewer for comparison, and a test in the game where the
//! song plays (the mod run from a copy of its folder, HWVX_MUSICLOG on).

use crate::{Browser, Pending};
use eframe::egui;
use hwvx_browser::content_music::{self as cm, BaseSong, LoopTags, Track};
use hwvx_browser::testdrive::{Prepared, TestDrive};
use serde_json::json;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, Sender, channel};
use std::time::Instant;

/// What the inspector shows of a file: decoded once, in the background.
#[derive(Clone)]
pub(crate) struct FileInfo {
    rate: u32,
    channels: usize,
    frames: u64,
    tags: LoopTags,
}

#[derive(Default)]
pub(crate) struct MusicState {
    /// the song shown in the inspector (its key, base://...)
    pub sel: Option<String>,
    /// + Replace a song: the filter and the song picked
    filter: String,
    pick: Option<String>,
    /// the path field (for when there is no file dialog)
    path: String,
    info: HashMap<PathBuf, Result<FileInfo, String>>,
    asked: HashSet<PathBuf>,
    info_rx: Option<Receiver<(PathBuf, Result<FileInfo, String>)>>,
    info_tx: Option<Sender<(PathBuf, Result<FileInfo, String>)>>,
    preview_rx: Option<Receiver<Result<PathBuf, String>>>,
    player: Option<std::process::Child>,
    /// a base song to play once the viewer's pack list is in
    want_base: Option<String>,
}

impl Drop for MusicState {
    fn drop(&mut self) {
        if let Some(mut c) = self.player.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }
}

fn info_of(path: &Path) -> Result<FileInfo, String> {
    let d = cm::decode(path)?;
    Ok(FileInfo { rate: d.rate, channels: d.channels, frames: d.frames(), tags: cm::loop_tags(path) })
}

fn secs(frames: u64, rate: u32) -> String {
    let s = frames as f64 / rate.max(1) as f64;
    format!("{}:{:06.3}", (s / 60.0) as u64, s % 60.0)
}

/// Audio files, in the system's dialog (several at once); empty when cancelled or there is none.
fn choose_audio() -> Vec<String> {
    use std::process::Command;
    let out = if cfg!(target_os = "macos") {
        Command::new("osascript")
            .args(["-e", "set fs to choose file with prompt \"Music for the mod (OGG, MP3, FLAC, WAV)\" with multiple selections allowed", "-e", "set out to \"\"", "-e", "repeat with f in fs", "-e", "set out to out & POSIX path of f & linefeed", "-e", "end repeat", "-e", "out"])
            .output()
    } else if cfg!(windows) {
        xdlrs::command("powershell")
            .args(["-NoProfile", "-Command", "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Multiselect = $true; $d.Filter = 'Music (*.ogg;*.mp3;*.flac;*.wav)|*.ogg;*.mp3;*.flac;*.wav'; if ($d.ShowDialog() -eq 'OK') { $d.FileNames -join \"`n\" }"])
            .output()
    } else {
        Command::new("zenity")
            .args(["--file-selection", "--multiple", "--separator=\n", "--title=Music for the mod", "--file-filter=Music | *.ogg *.mp3 *.flac *.wav *.OGG *.MP3 *.FLAC *.WAV", "--file-filter=All files | *"])
            .output()
            .or_else(|_| Command::new("kdialog").args(["--getopenfilename", ".", "*.ogg *.mp3 *.flac *.wav|Music", "--multiple", "--separate-output"]).output())
    };
    match out {
        Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout).lines().map(str::trim).filter(|l| !l.is_empty()).map(String::from).collect(),
        _ => vec![],
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

impl Browser {
    fn music_note(&mut self, s: impl Into<String>, error: bool) {
        self.content.message = s.into();
        self.content.error = error;
    }

    fn music_tick(&mut self) {
        let m = &mut self.content.music;
        if let Some(rx) = &m.info_rx {
            while let Ok((p, r)) = rx.try_recv() {
                m.info.insert(p, r);
            }
        }
        if let Some(rx) = &m.preview_rx {
            if let Ok(r) = rx.try_recv() {
                m.preview_rx = None;
                match r.and_then(|p| hwvx_browser::media::play_file(&p)) {
                    Ok(c) => {
                        m.player = Some(c);
                        self.note("playing the preview with the system's player".into());
                    }
                    Err(e) => self.fail(e),
                }
            } else {
                self.ctx.request_repaint_after(std::time::Duration::from_millis(100));
            }
        }
        if let Some(key) = self.content.music.want_base.clone() {
            if !self.packs.is_empty() {
                self.content.music.want_base = None;
                self.music_play_base(&key);
            }
        }
    }

    /// The decoded file's numbers (None while it is being read; asked for once).
    fn music_info(&mut self, file: &str) -> Option<Result<FileInfo, String>> {
        let dir = self.content.proj.as_ref()?.dir.clone();
        let path = dir.join(file);
        let m = &mut self.content.music;
        if let Some(r) = m.info.get(&path) {
            return Some(r.clone());
        }
        if m.asked.insert(path.clone()) {
            if m.info_tx.is_none() {
                let (tx, rx) = channel();
                m.info_tx = Some(tx);
                m.info_rx = Some(rx);
            }
            let tx = m.info_tx.clone().unwrap();
            let ctx = self.ctx.clone();
            std::thread::spawn(move || {
                let r = info_of(&path);
                let _ = tx.send((path, r));
                ctx.request_repaint();
            });
        }
        None
    }

    fn music_forget(&mut self, file: &str) {
        if let Some(p) = self.content.proj.as_ref().map(|p| p.dir.join(file)) {
            self.content.music.info.remove(&p);
            self.content.music.asked.remove(&p);
        }
    }

    fn music_stop_preview(&mut self) {
        if let Some(mut c) = self.content.music.player.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    /// A file of the mod (whole, or its loop's seam) to the system's player, decoded here.
    fn music_preview(&mut self, file: &str, seam: Option<(u64, Option<u64>)>) {
        let Some(path) = self.content.proj.as_ref().map(|p| p.dir.join(file)) else { return };
        self.music_stop_preview();
        let out = hwvx_browser::resources::default_pref().join("music-preview.wav");
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let r = (|| {
                let d = cm::decode(&path)?;
                let data = match seam {
                    Some((s, e)) => cm::seam(&d, s, e, 3.0).repeat(2),
                    None => d.data.clone(),
                };
                let _ = std::fs::create_dir_all(out.parent().unwrap());
                std::fs::write(&out, cm::wav(d.rate, d.channels, &data)).map_err(|e| format!("{}: {e}", out.display()))?;
                Ok(out)
            })();
            let _ = tx.send(r);
        });
        self.content.music.preview_rx = Some(rx);
        self.note(format!("decoding {file}…"));
    }

    /// The retail song through the viewer: its pack loaded, its song played by the game's own
    /// sequencer (viewer_media.c `play`).
    fn music_play_base(&mut self, key: &str) {
        let Some(song) = cm::song_for(key) else { return };
        if self.viewer.is_none() {
            return self.fail("the viewer isn't running: the base song plays through it".into());
        }
        if self.packs.is_empty() {
            self.content.music.want_base = Some(key.to_string());
            self.request("packs", json!({}), Pending::Packs);
            return;
        }
        let want = format!("{}/{}", song.archive, song.pack);
        let Some(p) = self.packs.iter().find(|p| p.pack.eq_ignore_ascii_case(&want)) else {
            return self.fail(format!("{want} isn't in the game data"));
        };
        let (archive, type_name, id) = (p.archive.clone(), p.type_name.clone(), p.id);
        self.music_stop_preview();
        self.load(&archive, &type_name, id);
        let node = song.viewer_node();
        self.request("play", json!({"node": node}), Pending::Play(node.clone()));
    }

    /// Files into the mod's music/ folder, added to the song's playlist.
    fn music_add_files(&mut self, key: &str, files: &[String]) {
        let Some(p) = self.content.proj.as_mut() else { return };
        let mut tracks = p.music_tracks(key);
        let mut msgs = vec![];
        let mut err = false;
        for f in files {
            match p.import_music(Path::new(f.trim())) {
                Ok(rel) => {
                    msgs.push(format!("added {rel}"));
                    tracks.push(Track::new(&rel));
                }
                Err(e) => {
                    msgs.push(e);
                    err = true;
                }
            }
        }
        p.set_music(key, &tracks);
        self.music_note(msgs.join("; "), err);
    }

    /// The game started where the song plays, the mod run from a copy of its folder.
    fn music_test(&mut self, song: &BaseSong) {
        self.content_save();
        let Some(p) = self.content.proj.as_ref() else { return };
        if p.dirty() {
            return;
        }
        let mut o = self.testdrive_options();
        o.env.push(("HWVX_MUSICLOG".into(), "1".into()));
        if !song.input.is_empty() {
            o.env.push(("HWVX_AUTOINPUT".into(), song.input.clone()));
        }
        let mods = o.scratch.join("content-mods");
        let _ = std::fs::remove_dir_all(&mods);
        let dest = mods.join(p.dir.file_name().unwrap_or_default());
        if let Err(e) = copy_dir(&p.dir, &dest) {
            return self.music_note(format!("the mod could not be copied for the test: {e}"), true);
        }
        let prepared = Prepared { start: song.start.clone(), mods, level: PathBuf::new(), cached: false, tool_ms: 0.0, ms: 0.0 };
        if let Some(d) = self.level.drive.as_mut() {
            d.stop();
        }
        self.music_stop_preview();
        match TestDrive::launch(&o, &prepared, Instant::now()) {
            Ok(d) => {
                self.level.drive = Some(d);
                let log = o.scratch.join("testdrive.log");
                self.music_note(format!("starting the game ({}): its music log in {}", if song.start.is_empty() { "the title screen" } else { &song.start }, log.display()), false);
            }
            Err(e) => self.music_note(e, true),
        }
    }

    /// The left panel's Soundtrack list (after the cars and weapons).
    pub(crate) fn music_panel(&mut self, ui: &mut egui::Ui) {
        self.music_tick();
        let Some(p) = self.content.proj.as_ref() else { return };
        let music = p.music();
        let songs = cm::base_songs();
        // a soundtrack-only mod opens on its first song
        let entries = p.doc["cars"].as_object().map_or(0, |m| m.len()) + p.doc["items"].as_object().map_or(0, |m| m.len());
        if entries == 0 && self.content.sel.is_none() && self.content.music.sel.is_none() {
            self.content.music.sel = music.first().map(|(k, _)| cm::canonical(k));
        }
        ui.horizontal(|ui| {
            ui.strong(format!("Soundtrack ({})", music.len()));
            ui.menu_button("+ Replace a song", |ui| {
                let m = &mut self.content.music;
                ui.label("the game's song to replace");
                ui.add(egui::TextEdit::singleline(&mut m.filter).hint_text("filter: challenge, ch1_l2, menu…").desired_width(220.0));
                let f = m.filter.to_lowercase();
                egui::ScrollArea::vertical().max_height(320.0).show(ui, |ui| {
                    let mut group = "";
                    for s in songs.iter().filter(|s| f.is_empty() || s.label.to_lowercase().contains(&f) || s.group.to_lowercase().contains(&f)) {
                        if s.group != group {
                            group = s.group;
                            ui.label(egui::RichText::new(group).small().strong());
                        }
                        let have = music.iter().any(|(k, _)| cm::canonical(k) == s.key());
                        let text = if have { format!("{}  ✔", s.label) } else { s.label.clone() };
                        if ui.selectable_label(m.pick.as_deref() == Some(&s.key()), text).on_hover_text(s.key()).clicked() {
                            m.pick = Some(s.key());
                        }
                    }
                });
                ui.separator();
                let picked = m.pick.clone();
                if ui.add_enabled(picked.is_some(), egui::Button::new("Choose files…")).on_hover_text("OGG, MP3, FLAC or WAV: copied into the mod's music/ folder").clicked() {
                    let key = picked.unwrap();
                    let files = choose_audio();
                    if !files.is_empty() {
                        self.music_add_files(&key, &files);
                    }
                    self.content.music.sel = Some(key);
                    self.content.sel = None;
                    ui.close();
                }
            });
        });
        for (k, tracks) in music {
            let label = cm::song_for(&k).map(|s| s.label).unwrap_or_else(|| k.clone());
            let n = if tracks.len() == 1 { tracks[0].file.rsplit('/').next().unwrap_or("").to_string() } else { format!("a playlist of {}", tracks.len()) };
            let selected = self.content.music.sel.as_deref().is_some_and(|s| cm::canonical(s) == cm::canonical(&k));
            if ui.selectable_label(selected, format!("♪ {label}  →  {n}")).on_hover_text(&k).clicked() {
                self.content.music.sel = Some(cm::canonical(&k));
                self.content.sel = None;
            }
        }
        ui.add_space(6.0);
    }

    /// The right panel for the song selected in the Soundtrack list.
    pub(crate) fn music_inspector(&mut self, ui: &mut egui::Ui) {
        self.music_tick();
        let Some(key) = self.content.music.sel.clone() else { return };
        let Some(proj) = self.content.proj.as_ref() else { return };
        let song = cm::song_for(&key);
        let mut tracks = proj.music_tracks(&key);
        let before = tracks.clone();
        let files: Vec<String> = {
            let mut v = proj.music_unused();
            v.extend(tracks.iter().flat_map(|t| std::iter::once(t.file.clone()).chain(t.intro.clone())));
            v.sort();
            v.dedup();
            v
        };
        ui.heading("Song");
        ui.label(egui::RichText::new(song.as_ref().map(|s| s.label.clone()).unwrap_or_else(|| "not a retail song".into())).strong());
        ui.label(egui::RichText::new(&key).small().weak());
        if let Some(s) = &song {
            ui.label(egui::RichText::new(if s.group == "Menus" { "the front end's music (HW_music)".to_string() } else { format!("the {} world {}'s song", s.group.to_lowercase(), s.archive) }).small());
        }
        ui.horizontal_wrapped(|ui| {
            if let Some(s) = song.clone() {
                let busy = self.test_drive_busy();
                let b = egui::Button::new(egui::RichText::new("▶ Test in game").strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
                let tip = if s.start.is_empty() { "the game from its title screen, with the mod (saved first)".to_string() } else { format!("the game started where the song plays ({}), with the mod (saved first)", s.start) };
                if ui.add_enabled(!busy && !tracks.is_empty(), b).on_hover_text(tip).clicked() {
                    self.music_test(&s);
                }
                if ui.add_enabled(self.viewer.is_some(), egui::Button::new("▶ Base song")).on_hover_text("the game's own song, through the viewer (its pack loaded)").clicked() {
                    self.music_play_base(&key);
                }
            }
            if ui.button("■ Stop").on_hover_text("the preview and the viewer's song").clicked() {
                self.music_stop_preview();
                self.request("stop", json!({}), Pending::Quiet);
            }
        });
        ui.separator();
        ui.horizontal(|ui| {
            ui.strong(if tracks.len() > 1 { format!("Playlist ({} tracks, each played through in turn)", tracks.len()) } else { "Track (it loops)".to_string() });
        });
        let mut action: Option<(usize, &str)> = None;
        let mut preview: Option<(String, Option<(u64, Option<u64>)>)> = None;
        let n = tracks.len();
        egui::ScrollArea::vertical().id_salt("music_tracks").show(ui, |ui| {
            for i in 0..n {
                let file = tracks[i].file.clone();
                let info = self.music_info(&file);
                egui::Frame::group(ui.style()).show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new(format!("{}. {}", i + 1, file.strip_prefix("music/").unwrap_or(&file))).strong());
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui.small_button("✖").on_hover_text("off the playlist (the file stays in the folder)").clicked() {
                                action = Some((i, "remove"));
                            }
                            if ui.add_enabled(i + 1 < n, egui::Button::new("⏷").small()).on_hover_text("later").clicked() {
                                action = Some((i, "down"));
                            }
                            if ui.add_enabled(i > 0, egui::Button::new("⏶").small()).on_hover_text("earlier").clicked() {
                                action = Some((i, "up"));
                            }
                        });
                    });
                    let t = &mut tracks[i];
                    match &info {
                        None => {
                            ui.label(egui::RichText::new("reading…").weak());
                        }
                        Some(Err(e)) => {
                            ui.colored_label(egui::Color32::LIGHT_RED, format!("can't be decoded: {e}"));
                        }
                        Some(Ok(fi)) => {
                            ui.label(egui::RichText::new(format!("{} Hz, {}, {} ({} frames)", fi.rate, if fi.channels == 1 { "mono".to_string() } else { format!("{} channels", fi.channels) }, secs(fi.frames, fi.rate), fi.frames)).small());
                            if fi.frames as f64 / fi.rate.max(1) as f64 * 48000.0 > cm::CHECK_MAX_FRAMES as f64 {
                                ui.colored_label(egui::Color32::YELLOW, "longer than ten minutes: the game streams it, but Save as mod's check can't decode it");
                            }
                            let (ls, le) = cm::effective_loop(t, &fi.tags, fi.frames);
                            let playlist = n > 1;
                            ui.horizontal(|ui| {
                                if ui.button("▶ Play").on_hover_text("the file with the system's player").clicked() {
                                    preview = Some((file.clone(), None));
                                }
                                if !playlist && ui.add_enabled(!t.once, egui::Button::new("▶ Loop seam")).on_hover_text("3 s before the loop's end, then 3 s from its start, twice: as the game loops it").clicked() {
                                    preview = Some((file.clone(), Some((ls, le))));
                                }
                            });
                            // in a playlist a track plays from its start to its loop end, then the
                            // next one starts (musicstream.c): no loop of its own
                            let mut looped = !t.once;
                            if !playlist {
                                ui.checkbox(&mut looped, "loops (else played once, then silence)");
                                t.once = !looped;
                            }
                            if looped || playlist {
                                let tag = match (fi.tags.start, fi.tags.end) {
                                    (None, None) => "the file has no loop tags".to_string(),
                                    (s, e) => format!("its {}: {}..{}", fi.tags.from, s.map(|x| x.to_string()).unwrap_or("0".into()), e.map(|x| x.to_string()).unwrap_or("end".into())),
                                };
                                egui::Grid::new(("music_loop", i)).num_columns(3).show(ui, |ui| {
                                    let rows = [("loop start", &mut t.loop_start, ls), (if playlist { "ends at" } else { "loop end" }, &mut t.loop_end, le.unwrap_or(fi.frames))];
                                    for (name, v, dflt) in rows.into_iter().skip(playlist as usize) {
                                        let mut on = v.is_some();
                                        ui.checkbox(&mut on, name).on_hover_text("ticked: the mod's own (frames of the file); else the file's tags, its .loop.txt, or the whole file");
                                        let mut x = v.unwrap_or(dflt);
                                        ui.add_enabled(on, egui::DragValue::new(&mut x).range(0..=fi.frames).speed((fi.rate / 100).max(1) as f64).suffix(" frames"));
                                        ui.label(egui::RichText::new(secs(x, fi.rate)).small());
                                        ui.end_row();
                                        *v = if on { Some(x) } else { None };
                                    }
                                });
                                let (from, then) = if playlist { (0, ", then the next track") } else { (ls, "") };
                                ui.label(egui::RichText::new(format!("plays {}..{}{then} ({tag})", secs(from, fi.rate), le.map(|e| secs(e, fi.rate)).unwrap_or("the end".into()))).small().weak());
                            }
                        }
                    }
                    ui.horizontal(|ui| {
                        ui.label("intro");
                        let cur = t.intro.clone().unwrap_or_default();
                        egui::ComboBox::from_id_salt(("music_intro", i)).selected_text(if cur.is_empty() { "none".to_string() } else { cur.strip_prefix("music/").unwrap_or(&cur).to_string() }).show_ui(ui, |ui| {
                            let mut v = cur.clone();
                            ui.selectable_value(&mut v, String::new(), "none");
                            for f in files.iter().filter(|f| **f != file) {
                                ui.selectable_value(&mut v, f.clone(), f.strip_prefix("music/").unwrap_or(f));
                            }
                            if v != cur {
                                t.intro = Some(v).filter(|v| !v.is_empty());
                            }
                        })
                        .response
                        .on_hover_text("a file of the mod played once before this track, flowing straight into it");
                    });
                    ui.horizontal(|ui| {
                        ui.label("volume");
                        let mut v = t.volume.unwrap_or(1.0);
                        if ui.add(egui::Slider::new(&mut v, 0.0..=4.0).step_by(0.05)).on_hover_text("1: a track mastered at about -17 dB RMS plays at the game's own music level").changed() {
                            t.volume = Some(v);
                        }
                    });
                });
            }
            ui.horizontal_wrapped(|ui| {
                if ui.button("+ Add files…").on_hover_text("OGG, MP3, FLAC or WAV: copied into the mod's music/ folder (you can also drop files on the window)").clicked() {
                    let f = choose_audio();
                    if !f.is_empty() {
                        action = Some((usize::MAX, "added"));
                        self.music_add_files(&key, &f);
                    }
                }
                let unused: Vec<String> = files.iter().filter(|f| !tracks.iter().any(|t| t.file == **f)).cloned().collect();
                if !unused.is_empty() {
                    ui.menu_button("+ A file of the mod", |ui| {
                        for f in unused {
                            if ui.button(f.strip_prefix("music/").unwrap_or(&f)).clicked() {
                                tracks.push(Track::new(&f));
                                ui.close();
                            }
                        }
                    });
                }
            });
            ui.horizontal(|ui| {
                ui.add(egui::TextEdit::singleline(&mut self.content.music.path).hint_text("or a file's path").desired_width(200.0));
                if ui.add_enabled(!self.content.music.path.trim().is_empty(), egui::Button::new("Add")).clicked() {
                    let f = std::mem::take(&mut self.content.music.path);
                    action = Some((usize::MAX, "added"));
                    self.music_add_files(&key, &[f]);
                }
            });
        });
        // files dropped on the window go to this song
        let dropped: Vec<String> = ui.ctx().input(|i| i.raw.dropped_files.iter().map(|f| f.path().display().to_string()).filter(|p| !p.is_empty()).collect());
        if !dropped.is_empty() {
            self.music_add_files(&key, &dropped);
            return;
        }
        if let Some((f, seam)) = preview {
            self.music_preview(&f, seam);
        }
        match action {
            Some((_, "added")) => return, // (the playlist was written with the new files)
            Some((i, "remove")) => {
                let f = tracks.remove(i).file;
                self.music_forget(&f);
            }
            Some((i, "up")) => tracks.swap(i, i - 1),
            Some((i, "down")) => tracks.swap(i, i + 1),
            _ => {}
        }
        if tracks != before {
            if let Some(p) = self.content.proj.as_mut() {
                p.set_music(&key, &tracks);
            }
        }
        ui.separator();
        let problems = self.content.proj.as_ref().map(|p| p.music_problems()).unwrap_or_default();
        for pr in &problems {
            ui.colored_label(egui::Color32::LIGHT_RED, pr);
        }
        ui.horizontal(|ui| {
            if ui.button("🗑 Keep the game's song").on_hover_text("the mod no longer replaces it (its files stay in music/)").clicked() {
                if let Some(p) = self.content.proj.as_mut() {
                    p.set_music(&key, &[]);
                }
                self.content.music.sel = None;
            }
        });
        ui.label(egui::RichText::new("Save as mod packs music/ with the rest: the check refuses only the game's own audio.").small().weak());
    }
}
