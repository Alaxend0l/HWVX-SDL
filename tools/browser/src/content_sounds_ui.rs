//! The Content tab's sounds (src/content_sounds.rs): in a new car's inspector its own sounds by
//! what plays them (wind, engine, ...), each replaceable by a file the modder picks (copied into
//! the mod's sounds/ folder) and ▶ previewed, base and replacement; on the left a general list of
//! the mod's replacements of any other base sound (world, menu, items, shared), picked by
//! archive, pack and sound. The previews play with the system's player, as the media view's
//! "Play here" does (the replacement as decoded by the game's own decoders).

use crate::Browser;
use eframe::egui;
use hwvx_browser::content::{Kind, ModProject};
use hwvx_browser::content_sounds::{self as cs, BaseSound, SoundArchive, SoundPack};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::mpsc::{Receiver, TryRecvError, channel};
use std::time::Instant;

type Loaded = Result<Arc<SoundArchive>, String>;
type FileInfo = Result<(f64, u32), String>;

/// Archives kept read (each is its whole file in memory, HW_Cars tens of MB).
const KEEP_ARCHIVES: usize = 3;

#[derive(Default)]
pub(crate) struct SoundsState {
    /// the general list's selected base sound (its base:// id); content.sel set clears it
    pub sel: Option<String>,
    /// the data folder the caches below are of: another empties them
    data: Option<PathBuf>,
    /// the archives read, the last used last, and those being read (off the UI thread)
    archives: Vec<(String, Loaded)>,
    reading: HashMap<String, Receiver<Result<SoundArchive, String>>>,
    archive_names: Option<Vec<String>>,
    packs: HashMap<String, Vec<SoundPack>>,
    lists: HashMap<(String, u32, u32), Result<Vec<BaseSound>, String>>,
    /// a mod file's decoded length: (seconds, rate) or why not; those being decoded
    files: HashMap<PathBuf, FileInfo>,
    files_rx: HashMap<PathBuf, Receiver<FileInfo>>,
    /// sounds/'s unused files, as listed then (not every frame)
    unused: Option<(PathBuf, Instant, Vec<String>)>,
    // + Replace a sound: the archive and pack picked
    pick_archive: String,
    pick_pack: Option<(u32, u32)>,
    /// a file's path typed in when there is no file dialog
    typed: String,
    no_dialog: bool,
    playing: Option<std::process::Child>,
    /// HWVX_CONTENT_SOUND was looked at (once)
    args_taken: bool,
}

impl Drop for SoundsState {
    fn drop(&mut self) {
        if let Some(mut c) = self.playing.take() {
            let _ = c.kill();
        }
    }
}

fn row_text(s: &BaseSound) -> String {
    format!("{:.2} s  {} Hz{}", s.seconds(), s.rate, if s.looped { "  ↻" } else { "" })
}

fn file_info(path: &Path) -> FileInfo {
    cs::decode_file(path).map(|(pcm, rate)| (pcm.len() as f64 / rate.max(1) as f64, rate))
}

/// What a sound's row asks for.
enum RowAction {
    /// the game's sound again
    Clear,
    /// a file picked for it (snd_pick_file)
    Pick,
}

impl Browser {
    fn snd_note(&mut self, s: impl Into<String>, error: bool) {
        self.content.message = s.into();
        self.content.error = error;
    }

    /// The game data folder, the caches emptied when it changed.
    fn snd_data(&mut self) -> Option<PathBuf> {
        let d = self.testdrive_options().data;
        let s = &mut self.content.sounds;
        if s.data != d {
            s.data = d.clone();
            s.archives.clear();
            s.reading.clear();
            s.archive_names = None;
            s.packs.clear();
            s.lists.clear();
        }
        d
    }

    /// An archive, read on a thread of its own (None while it is).
    fn snd_archive(&mut self, name: &str) -> Option<Loaded> {
        let Some(data) = self.snd_data() else { return Some(Err("the game data isn't known (File > Game data…)".to_string())) };
        let ctx = self.ctx.clone();
        let s = &mut self.content.sounds;
        if let Some(rx) = s.reading.get(name) {
            let r = match rx.try_recv() {
                Ok(r) => r.map(Arc::new),
                Err(TryRecvError::Empty) => return None,
                Err(TryRecvError::Disconnected) => Err(format!("{name}: its reader stopped (a crash: a damaged archive?)")),
            };
            s.reading.remove(name);
            if s.archives.len() >= KEEP_ARCHIVES {
                let _ = s.archives.remove(0); // the least recently used
            }
            s.archives.push((name.to_string(), r));
        }
        if let Some(i) = s.archives.iter().position(|(n, _)| n == name) {
            let e = s.archives.remove(i);
            let r = e.1.clone();
            s.archives.push(e);
            return Some(r);
        }
        let (tx, rx) = channel();
        let n = name.to_string();
        std::thread::spawn(move || {
            let _ = tx.send(SoundArchive::find(&data, &n));
            ctx.request_repaint();
        });
        s.reading.insert(name.to_string(), rx);
        None
    }

    fn snd_list(&mut self, archive: &str, typ: u32, id: u32) -> Option<Result<Vec<BaseSound>, String>> {
        self.snd_data();
        let k = (archive.to_string(), typ, id);
        if let Some(l) = self.content.sounds.lists.get(&k) {
            return Some(l.clone());
        }
        let r = self.snd_archive(archive)?.and_then(|a| a.sounds(typ, id));
        self.content.sounds.lists.insert(k, r.clone());
        Some(r)
    }

    fn snd_packs(&mut self, archive: &str) -> Option<Vec<SoundPack>> {
        self.snd_data();
        if let Some(p) = self.content.sounds.packs.get(archive) {
            return Some(p.clone());
        }
        let p = self.snd_archive(archive)?.map(|a| a.packs()).unwrap_or_default();
        self.content.sounds.packs.insert(archive.to_string(), p.clone());
        Some(p)
    }

    /// A mod file's length as the game decodes it (None while it is decoded, off the UI thread).
    fn snd_file_info(&mut self, rel: &str) -> Option<FileInfo> {
        let path = self.content.proj.as_ref()?.dir.join(rel);
        let s = &mut self.content.sounds;
        if let Some(r) = s.files.get(&path) {
            return Some(r.clone());
        }
        if let Some(rx) = s.files_rx.get(&path) {
            let r = match rx.try_recv() {
                Ok(r) => r,
                Err(TryRecvError::Empty) => return None,
                Err(TryRecvError::Disconnected) => Err("its decoder stopped (a crash)".into()),
            };
            s.files_rx.remove(&path);
            s.files.insert(path, r.clone());
            return Some(r);
        }
        let (tx, rx) = channel();
        let ctx = self.ctx.clone();
        let p = path.clone();
        std::thread::spawn(move || {
            let _ = tx.send(file_info(&p));
            ctx.request_repaint();
        });
        s.files_rx.insert(path, rx);
        None
    }

    fn snd_stop(&mut self) {
        if let Some(mut c) = self.content.sounds.playing.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    fn snd_play(&mut self, pcm: Result<(Vec<i16>, u32), String>, what: &str) {
        self.snd_stop();
        let r = pcm.and_then(|(pcm, rate)| {
            let dir = hwvx_browser::resources::default_pref();
            let _ = std::fs::create_dir_all(&dir);
            let path = dir.join("content-sound.wav");
            std::fs::write(&path, hwvx_browser::media::wav_bytes(rate, &pcm)).map_err(|e| format!("{}: {e}", path.display()))?;
            hwvx_browser::media::play_file(&path)
        });
        match r {
            Ok(c) => {
                self.content.sounds.playing = Some(c);
                self.snd_note(format!("playing {what} (once)"), false);
            }
            Err(e) => self.snd_note(e, true),
        }
    }

    fn snd_play_base(&mut self, archive: &str, typ: u32, id: u32, index: u32) {
        match self.snd_archive(archive) {
            Some(a) => {
                let pcm = a.and_then(|a| a.pcm(typ, id, index));
                self.snd_play(pcm, &format!("{archive}/{}/sound/{index}", cs::pack_name(typ, id)));
            }
            None => self.snd_note(format!("reading {archive}…: ▶ again once it is read"), false),
        }
    }

    fn snd_play_file(&mut self, rel: &str) {
        let Some(dir) = self.content.proj.as_ref().map(|p| p.dir.clone()) else { return };
        self.snd_play(cs::decode_file(&dir.join(rel)), rel);
    }

    /// A file the modder picks (its dialog, its copy into the mod and its decode off the UI
    /// thread), then `apply` with its path in the mod.
    fn snd_pick_file(&mut self, apply: impl FnOnce(&mut ModProject, &str) + 'static) {
        let Some(dir) = self.content.proj.as_ref().map(|p| p.dir.clone()) else { return };
        let typed = self.content.sounds.typed.trim().to_string();
        let to = dir.clone();
        self.ask_then(
            move || {
                let (src, no_dialog) = match cs::choose_sound("A sound file (WAV, FLAC, Ogg Vorbis, MP3)") {
                    Ok(Some(f)) => (f, false),
                    Ok(None) => return (None, false),
                    Err(e) if typed.is_empty() => return (Some(Err(e)), true),
                    Err(_) => (typed, true),
                };
                let r = cs::import_sound(&to, Path::new(&src)).and_then(|rel| {
                    let info = file_info(&to.join(&rel)).map_err(|e| format!("{rel}: the game can't decode it: {e}"))?;
                    Ok((rel, info))
                });
                (Some(r), no_dialog)
            },
            move |b, (r, no_dialog)| {
                b.content.sounds.no_dialog |= no_dialog;
                match r {
                    None => {}
                    Some(Ok((rel, info))) => {
                        let path = dir.join(&rel);
                        b.content.sounds.files_rx.remove(&path);
                        b.content.sounds.files.insert(path, Ok(info));
                        b.content.sounds.unused = None;
                        let Some(p) = b.content.proj.as_mut().filter(|p| p.dir == dir) else { return };
                        apply(p, &rel);
                        b.snd_note(format!("copied into the mod as {rel}"), false);
                    }
                    Some(Err(e)) => b.snd_note(e, true),
                }
            },
        );
    }

    /// One base sound and its replacement: ▶ both, Replace…, ✕.
    fn snd_row(&mut self, ui: &mut egui::Ui, archive: &str, typ: u32, id: u32, s: &BaseSound, cur: Option<String>) -> Option<RowAction> {
        let mut out = None;
        ui.horizontal(|ui| {
            if ui.small_button("▶").on_hover_text("the game's sound").clicked() {
                self.snd_play_base(archive, typ, id, s.index);
            }
            let role = if s.role.is_empty() { String::new() } else { format!("  {}", s.role) };
            ui.label(egui::RichText::new(format!("sound/{}{role}", s.index)).strong());
            ui.label(egui::RichText::new(row_text(s)).weak());
        });
        ui.horizontal(|ui| {
            ui.add_space(22.0);
            match &cur {
                Some(f) => {
                    if ui.small_button("▶").on_hover_text("the mod's sound, as the game decodes it").clicked() {
                        self.snd_play_file(f);
                    }
                    let info = match self.snd_file_info(f) {
                        Some(Ok((sec, rate))) => format!("{sec:.2} s  {rate} Hz (plays at {} Hz)", s.rate),
                        Some(Err(e)) => format!("can't be decoded: {e}"),
                        None => "reading…".into(),
                    };
                    ui.label(format!("→ {f}")).on_hover_text(info.clone());
                    ui.label(egui::RichText::new(info.split("  ").next().unwrap_or("")).weak().small());
                    if ui.small_button("✕").on_hover_text("the game's sound again").clicked() {
                        out = Some(RowAction::Clear);
                    }
                }
                None => {
                    ui.label(egui::RichText::new("the game's").weak());
                }
            }
            if ui.small_button(if cur.is_some() { "Change…" } else { "Replace…" }).on_hover_text("a WAV, FLAC, Ogg Vorbis or MP3 file: copied into the mod's sounds/").clicked() {
                out = Some(RowAction::Pick);
            }
        });
        out
    }

    fn snd_no_dialog_field(&mut self, ui: &mut egui::Ui) {
        if self.content.sounds.no_dialog {
            ui.horizontal(|ui| {
                ui.label("file");
                ui.add(egui::TextEdit::singleline(&mut self.content.sounds.typed).hint_text("the path of a sound file (no file dialog here)"));
            });
        }
    }

    /// The car inspector's "Sounds": the new car's own sounds (its base car's pack, by what
    /// plays them), each replaceable for this car only (the entry's "sounds").
    pub(crate) fn content_sounds_car(&mut self, ui: &mut egui::Ui, uid: &str) {
        let Some(base) = self.content.proj.as_ref().and_then(|p| p.doc["cars"][uid]["extends"].as_str().and_then(|x| Kind::Car.base_number(x))) else { return };
        let n = self.content.proj.as_ref().map(|p| p.doc["cars"][uid]["sounds"].as_object().map(|m| m.len()).unwrap_or(0)).unwrap_or(0);
        egui::CollapsingHeader::new(format!("Sounds{}", if n > 0 { format!("  ({n} replaced)") } else { String::new() })).id_salt("content_car_sounds").default_open(n > 0).show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("This car's own: a replacement plays in this car only, never in the car it is made from.").weak().small());
                if ui.small_button("⏹").on_hover_text("stop the preview").clicked() {
                    self.snd_stop();
                }
            });
            self.snd_no_dialog_field(ui);
            let list = match self.snd_list("HW_Cars", 0, base) {
                Some(Ok(l)) => l,
                Some(Err(e)) => {
                    ui.colored_label(egui::Color32::LIGHT_RED, e);
                    return;
                }
                None => {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label("reading HW_Cars…");
                    });
                    return;
                }
            };
            if list.is_empty() {
                ui.label("the car's pack has no sounds of its own");
            }
            for s in &list {
                let cur = self.content.proj.as_ref().and_then(|p| p.entry_sound(Kind::Car, uid, s.index));
                match self.snd_row(ui, "HW_Cars", 0, base, s, cur) {
                    Some(RowAction::Clear) => {
                        if let Some(p) = self.content.proj.as_mut() {
                            p.set_entry_sound(Kind::Car, uid, s.index, None);
                        }
                        self.content.sounds.unused = None;
                    }
                    Some(RowAction::Pick) => {
                        let (uid, index) = (uid.to_string(), s.index);
                        self.snd_pick_file(move |p, rel| p.set_entry_sound(Kind::Car, &uid, index, Some(rel)));
                    }
                    None => {}
                }
            }
            ui.label(egui::RichText::new("Horns, crashes, skids and pickups are the shared packs' (Sounds on the left: HW_share). The game mixes a file to mono and resamples it to the sound's rate; its loop tag, else the whole file when the game's sound loops.").weak().small());
        });
    }

    /// sounds/'s files nothing refers to: listed again every 2 s, or after an edit here.
    fn snd_unused(&mut self) -> Vec<String> {
        let Some(p) = self.content.proj.as_ref() else { return vec![] };
        let fresh = self.content.sounds.unused.as_ref().is_some_and(|(d, t, _)| *d == p.dir && t.elapsed().as_secs() < 2);
        if !fresh {
            self.content.sounds.unused = Some((p.dir.clone(), Instant::now(), p.unused_sound_files()));
        }
        self.content.sounds.unused.as_ref().map(|u| u.2.clone()).unwrap_or_default()
    }

    /// The left panel's general "Sounds": the mod's replacements of base sounds, and + Replace a sound.
    pub(crate) fn content_sounds_panel(&mut self, ui: &mut egui::Ui) {
        if self.content.proj.is_none() {
            return;
        }
        // HWVX_CONTENT_SOUND=base://ARCHIVE/PACK/sound/N: that sound in the inspector at start (for --app-shot)
        if !self.content.sounds.args_taken {
            self.content.sounds.args_taken = true;
            if let Some(k) = std::env::var("HWVX_CONTENT_SOUND").ok().filter(|k| cs::parse_key(k).is_some()) {
                self.content.sounds.sel = Some(k);
                self.content.sel = None;
            }
        }
        if self.content.sel.is_some() {
            self.content.sounds.sel = None;
        }
        let list = self.content.proj.as_ref().map(|p| p.base_sounds()).unwrap_or_default();
        ui.horizontal(|ui| {
            ui.strong(format!("Sounds ({})", list.len()));
            ui.menu_button("+ Replace a sound", |ui| self.snd_picker(ui));
        });
        for (key, file) in list {
            let selected = self.content.sounds.sel.as_deref() == Some(key.as_str());
            let short = key.strip_prefix("base://").unwrap_or(&key).to_string();
            if ui.selectable_label(selected, format!("{short}   ({})", file.rsplit('/').next().unwrap_or(&file))).on_hover_text(&key).clicked() {
                self.content.sounds.sel = Some(key);
                self.content.sel = None;
            }
        }
        let unused = self.snd_unused();
        if !unused.is_empty() {
            ui.horizontal(|ui| {
                ui.label(egui::RichText::new(format!("{} file(s) in sounds/ unused", unused.len())).weak()).on_hover_text(unused.join("\n"));
                if ui.small_button("remove").on_hover_text("delete them from the mod's folder (else they go into the package)").clicked() {
                    let dir = self.content.proj.as_ref().map(|p| p.dir.clone()).unwrap_or_default();
                    for f in &unused {
                        let _ = std::fs::remove_file(dir.join(f));
                    }
                    self.content.sounds.files.clear();
                    self.content.sounds.unused = None;
                }
            });
        }
        ui.add_space(6.0);
    }

    /// + Replace a sound: archive, pack, then one of its sounds (it opens in the inspector).
    fn snd_picker(&mut self, ui: &mut egui::Ui) {
        let Some(data) = self.snd_data() else {
            ui.label("the game data isn't known (File > Game data…)");
            return;
        };
        let archives = self.content.sounds.archive_names.get_or_insert_with(|| cs::archives(&data)).clone();
        if self.content.sounds.pick_archive.is_empty() {
            self.content.sounds.pick_archive = archives.iter().find(|a| a.eq_ignore_ascii_case("HW_share")).or(archives.first()).cloned().unwrap_or_default();
        }
        let mut arch = self.content.sounds.pick_archive.clone();
        ui.label("archive");
        egui::ComboBox::from_id_salt("content_snd_archive").selected_text(&arch).height(380.0).show_ui(ui, |ui| {
            for a in &archives {
                ui.selectable_value(&mut arch, a.clone(), a);
            }
        });
        if arch != self.content.sounds.pick_archive {
            self.content.sounds.pick_archive = arch.clone();
            self.content.sounds.pick_pack = None;
        }
        let Some(packs) = self.snd_packs(&arch) else {
            ui.horizontal(|ui| {
                ui.spinner();
                ui.label(format!("reading {arch}…"));
            });
            return;
        };
        if packs.is_empty() {
            ui.label(egui::RichText::new("no pack of it has sounds").weak());
            return;
        }
        let mut pick = self.content.sounds.pick_pack.filter(|p| packs.iter().any(|x| (x.typ, x.id) == *p)).unwrap_or((packs[0].typ, packs[0].id));
        let label = |p: &SoundPack| format!("{}  ({} sounds)", p.name, p.count);
        ui.label("pack");
        let shown = packs.iter().find(|p| (p.typ, p.id) == pick).map(label).unwrap_or_default();
        egui::ComboBox::from_id_salt("content_snd_pack").selected_text(shown).height(380.0).show_ui(ui, |ui| {
            for p in &packs {
                ui.selectable_value(&mut pick, (p.typ, p.id), label(p));
            }
        });
        self.content.sounds.pick_pack = Some(pick);
        let sounds = match self.snd_list(&arch, pick.0, pick.1) {
            Some(Ok(s)) => s,
            Some(Err(e)) => {
                ui.colored_label(egui::Color32::LIGHT_RED, e);
                return;
            }
            None => return,
        };
        ui.label("sound (▶ to hear it, its name to pick it)");
        egui::ScrollArea::vertical().max_height(320.0).show(ui, |ui| {
            for s in &sounds {
                ui.horizontal(|ui| {
                    if ui.small_button("▶").clicked() {
                        self.snd_play_base(&arch, pick.0, pick.1, s.index);
                    }
                    let role = if s.role.is_empty() { String::new() } else { format!("  {}", s.role) };
                    if ui.selectable_label(false, format!("sound/{}{role}   {}", s.index, row_text(s))).clicked() {
                        self.content.sounds.sel = Some(cs::base_key(&arch, &cs::pack_name(pick.0, pick.1), s.index));
                        self.content.sel = None;
                        ui.close();
                    }
                });
            }
        });
    }

    /// The inspector for the general list's selected sound; false when none is (content.sel wins).
    pub(crate) fn content_sounds_inspector(&mut self, ui: &mut egui::Ui) -> bool {
        if self.content.sel.is_some() {
            self.content.sounds.sel = None;
            return false;
        }
        let Some(key) = self.content.sounds.sel.clone() else { return false };
        ui.heading("Sound");
        ui.label(egui::RichText::new(&key).small().weak());
        let Some((archive, pack, index)) = cs::parse_key(&key) else {
            ui.label("not a base sound's id");
            return true;
        };
        let reading = |ui: &mut egui::Ui| {
            ui.horizontal(|ui| {
                ui.spinner();
                ui.label(format!("reading {archive}…"));
            });
        };
        let Some(packs) = self.snd_packs(&archive) else {
            reading(ui);
            return true;
        };
        let Some(sp) = packs.iter().find(|p| p.name == pack).cloned() else {
            ui.colored_label(egui::Color32::LIGHT_RED, format!("{archive} has no {pack} with sounds in this game data"));
            return true;
        };
        let list = match self.snd_list(&archive, sp.typ, sp.id) {
            Some(Ok(l)) => l,
            Some(Err(e)) => {
                ui.colored_label(egui::Color32::LIGHT_RED, e);
                return true;
            }
            None => {
                reading(ui);
                return true;
            }
        };
        let Some(s) = list.iter().find(|s| s.index == index).cloned() else {
            ui.colored_label(egui::Color32::LIGHT_RED, format!("{pack} has {} sounds", list.len()));
            return true;
        };
        ui.label(egui::RichText::new("Every pack that loads it plays the mod's file instead: the base pack's, and the new cars made from it (their own Sounds win).").weak().small());
        self.snd_no_dialog_field(ui);
        ui.separator();
        let cur = self.content.proj.as_ref().and_then(|p| p.base_sound(&key));
        match self.snd_row(ui, &archive, sp.typ, sp.id, &s, cur.clone()) {
            Some(RowAction::Clear) => {
                if let Some(p) = self.content.proj.as_mut() {
                    p.set_base_sound(&key, None);
                }
                self.content.sounds.unused = None;
            }
            Some(RowAction::Pick) => {
                let k = key.clone();
                self.snd_pick_file(move |p, rel| p.set_base_sound(&k, Some(rel)));
            }
            None => {}
        }
        ui.separator();
        ui.horizontal(|ui| {
            let busy = self.test_drive_busy();
            // a car pack's sound: a race in that car; else in the mod's first new car, or car 0
            let car = (archive.eq_ignore_ascii_case("HW_Cars") && sp.typ == 0).then(|| sp.id.to_string()).or_else(|| self.content.proj.as_ref().and_then(|p| p.entries(Kind::Car).first().map(|x| x.0.clone()))).unwrap_or_else(|| "0".into());
            let b = egui::Button::new(egui::RichText::new("▶ Drive with it").strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
            if ui.add_enabled(cur.is_some() && !busy, b).on_hover_text(format!("a Challenge race in car {car} with the mod as it is now (saved first)")).clicked() {
                self.content_test(Kind::Car, &car);
            }
            if ui.button("⏹ Stop").on_hover_text("stop the preview").clicked() {
                self.snd_stop();
            }
        });
        true
    }
}
