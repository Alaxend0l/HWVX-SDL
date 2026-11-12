//! The browser's textures and sounds (the viewer's media commands; decoding is the game's, in
//! src/port/viewer_media.c): the outline's Textures / Sounds / Song groups, the central panel's
//! texture view (zoom, pan, nearest/linear, mip level, alpha views, the texel under the pointer,
//! PNG export) and sound view (waveform with min/max per column, loop marker, play/stop through the
//! game, or here with the system's player, WAV export), the song's tempo and play/stop.

use crate::{Browser, Pending};
use eframe::egui;
use hwvx_browser::media::{Sound, Texture, play_file, wav_bytes, write_png};
use serde_json::{Value, json};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, PartialEq, Eq, Default)]
pub enum View {
    #[default]
    Scene,
    Texture,
    Sound,
    Song,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum AlphaView {
    Blend, // over a checkerboard
    Opaque,
    Alpha, // alpha alone, as grey
}

pub struct TexView {
    pub id: String,
    pub tex: Texture,
    pub level: usize,
    pub alpha: AlphaView,
    pub linear: bool,
    pub zoom: f32, // physical pixels a texel
    pub pan: egui::Vec2,
    pub fit: bool,
    handle: Option<((usize, AlphaView, bool), egui::TextureHandle)>,
}

pub struct SndView {
    pub id: String,
    pub sound: Sound,
    pub range: (usize, usize), // samples shown
    pub started: Option<Instant>,
    pub note: Option<u8>, // instruments: play at a MIDI note
}

/// The media state of the browser (one pack's listing, the item open).
#[derive(Default)]
pub struct MediaState {
    pub textures: Vec<Value>,
    pub sounds: Value,
    pub listed: bool, // both listings are in
    pub open: Option<String>,
    pub view: View,
    pub tex: Option<TexView>,
    pub snd: Option<SndView>,
    pub audio: Value, // status's "audio"
    pub song_started: Option<Instant>,
    pub local: Option<std::process::Child>,
    pub export: String,
}

impl Drop for MediaState {
    fn drop(&mut self) {
        if let Some(mut c) = self.local.take() {
            let _ = c.kill();
        }
    }
}

/// What a node id is, as the media commands take it.
pub fn media_kind(id: &str) -> Option<View> {
    let tail = id.rsplit('/').nth(1).unwrap_or("");
    if id == "song" || id.ends_with("/song") {
        Some(View::Song)
    } else if tail == "texture" {
        Some(View::Texture)
    } else if tail == "sound" || tail == "instrument" {
        Some(View::Sound)
    } else {
        None
    }
}

fn file_name(id: &str, ext: &str) -> String {
    let base: String = id.trim_start_matches("base://").chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' || c == '-' { c } else { '_' }).collect();
    format!("{base}.{ext}")
}

fn wheel(ui: &egui::Ui) -> f32 {
    ui.input(|i| {
        i.events
            .iter()
            .map(|e| match e {
                egui::Event::MouseWheel { unit, delta, .. } => match unit {
                    egui::MouseWheelUnit::Point => delta.y / 50.0,
                    egui::MouseWheelUnit::Line => delta.y,
                    egui::MouseWheelUnit::Page => delta.y * 10.0,
                },
                _ => 0.0,
            })
            .sum()
    })
}

fn tex_summary(t: &Value) -> String {
    if t["drawn"] == false {
        return "not drawn".into();
    }
    let mut s = format!("{} {}×{}", t["format"].as_str().unwrap_or("?"), t["width"], t["height"]);
    if t["levels"].as_u64().unwrap_or(1) > 1 {
        s += &format!(" ×{}", t["levels"]);
    }
    if t["palette"].is_object() {
        s += " pal";
    }
    s
}

fn sound_summary(s: &Value) -> String {
    if s["loaded"] == false {
        return "not loaded".into();
    }
    let mut out = format!("{} {} Hz {:.2} s", s["format"].as_str().unwrap_or("?"), s["rate"], s["seconds"].as_f64().unwrap_or(0.0));
    if s["loop"].is_u64() {
        out += " ↻";
    }
    out
}

impl Browser {
    /// The outline's pack changed: its media listings.
    pub(crate) fn list_media(&mut self, pack: &str) {
        self.media.textures.clear();
        self.media.sounds = Value::Null;
        self.media.listed = false;
        self.request("textures", json!({"pack": pack}), Pending::Textures(pack.to_string()));
        self.request("sounds", json!({"pack": pack}), Pending::Sounds(pack.to_string()));
    }

    /// Opens a texture, sound, instrument or song (its node id) in the central panel.
    pub(crate) fn open_media(&mut self, id: &str) {
        let Some(kind) = media_kind(id) else { return };
        let pack = self.outline.as_ref().map(|o| o.pack.clone()).unwrap_or_default();
        self.media.open = Some(id.to_string());
        self.selected = Some(id.to_string());
        self.detail = None;
        self.media.view = kind;
        match kind {
            View::Texture => {
                self.request("texture", json!({"pack": pack, "node": id}), Pending::Texture(id.to_string()));
            }
            View::Sound => {
                self.request("sound", json!({"pack": pack, "node": id}), Pending::Sound(id.to_string()));
            }
            _ => {}
        }
    }

    /// A media reply (Pending::Texture/Sound/Textures/Sounds/Play).
    pub(crate) fn media_reply(&mut self, why: Pending, r: Value) {
        match why {
            Pending::Textures(pack) => {
                if self.outline.as_ref().map(|o| o.pack.as_str()) == Some(pack.as_str()) {
                    self.media.textures = r["textures"].as_array().cloned().unwrap_or_default();
                    self.media.listed = !self.media.sounds.is_null();
                }
            }
            Pending::Sounds(pack) => {
                if self.outline.as_ref().map(|o| o.pack.as_str()) == Some(pack.as_str()) {
                    self.media.sounds = r;
                    self.media.listed = true;
                }
            }
            Pending::Texture(id) => {
                if self.media.open.as_deref() != Some(id.as_str()) {
                    return;
                }
                match self.media_reader.read(&r["blob"]).and_then(|d| Texture::from_reply(&r, &d)) {
                    Ok(tex) => {
                        let keep = self.media.tex.as_ref().map(|t| (t.alpha, t.linear));
                        self.media.export = std::env::current_dir().unwrap_or_default().join(file_name(&id, "png")).display().to_string();
                        self.media.tex = Some(TexView {
                            id,
                            tex,
                            level: 0,
                            alpha: keep.map(|k| k.0).unwrap_or(AlphaView::Blend),
                            linear: keep.map(|k| k.1).unwrap_or(false),
                            zoom: 1.0,
                            pan: egui::Vec2::ZERO,
                            fit: true,
                            handle: None,
                        });
                    }
                    Err(e) => self.fail(e),
                }
            }
            Pending::Sound(id) => {
                if self.media.open.as_deref() != Some(id.as_str()) {
                    return;
                }
                match self.media_reader.read(&r["blob"]) {
                    Ok(d) => {
                        let sound = Sound::from_reply(&r, &d);
                        self.media.export = std::env::current_dir().unwrap_or_default().join(file_name(&id, "wav")).display().to_string();
                        let n = sound.pcm.len();
                        self.media.snd = Some(SndView { id, sound, range: (0, n), started: None, note: None });
                    }
                    Err(e) => self.fail(e),
                }
            }
            Pending::Play(id) => {
                self.note(format!("playing {}", r["playing"].as_str().unwrap_or(&id)));
                let now = Some(Instant::now());
                if media_kind(&id) == Some(View::Song) {
                    self.media.song_started = now;
                } else if let Some(s) = &mut self.media.snd {
                    s.started = now;
                }
                if r["device"] == false && media_kind(&id) == Some(View::Sound) {
                    // the game has no output device: here instead
                    self.play_here();
                }
            }
            _ => {}
        }
    }

    fn play_game(&mut self) {
        let Some(id) = self.media.open.clone() else { return };
        let pack = self.outline.as_ref().map(|o| o.pack.clone()).unwrap_or_default();
        self.stop_here();
        let mut args = json!({"pack": pack, "node": id});
        if let Some(n) = self.media.snd.as_ref().and_then(|s| s.note) {
            args["note"] = json!(n);
        }
        self.request("play", args, Pending::Play(id));
    }

    fn stop_all(&mut self) {
        self.stop_here();
        self.request("stop", json!({}), Pending::Quiet);
        if let Some(s) = &mut self.media.snd {
            s.started = None;
        }
        self.media.song_started = None;
    }

    /// The decoded samples with the system's player (a WAV next to the program).
    fn play_here(&mut self) {
        self.stop_here();
        let Some(s) = &self.media.snd else { return };
        let dir = std::env::current_exe().ok().and_then(|e| e.parent().map(|d| d.join("viewer-pref"))).unwrap_or_else(|| "viewer-pref".into());
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("play.wav");
        let rate = s.sound.rate;
        if let Err(e) = std::fs::write(&path, wav_bytes(rate, &s.sound.pcm)) {
            return self.fail(format!("{}: {e}", path.display()));
        }
        match play_file(&path) {
            Ok(c) => {
                self.media.local = Some(c);
                if let Some(s) = &mut self.media.snd {
                    s.started = Some(Instant::now());
                }
                self.note("playing here (once, the loop isn't repeated)".into());
            }
            Err(e) => self.fail(e),
        }
    }

    fn stop_here(&mut self) {
        if let Some(mut c) = self.media.local.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    // ---- the outline's groups ------------------------------------------------------------------

    pub(crate) fn media_outline(&mut self, ui: &mut egui::Ui, clicked: &mut Option<String>) {
        let selected = self.selected.clone();
        let textures = &self.media.textures;
        if !textures.is_empty() {
            let own: Vec<&Value> = textures.iter().filter(|t| t["shared"] != true).collect();
            egui::CollapsingHeader::new(format!("Textures ({})", own.len())).id_salt(("group", "Textures")).default_open(false).show(ui, |ui| {
                let mut row = |ui: &mut egui::Ui, t: &Value, label: String| {
                    let id = t["id"].as_str().unwrap_or("");
                    let r = ui.selectable_label(selected.as_deref() == Some(id), egui::RichText::new(format!("{label}  ·  {}", tex_summary(t))));
                    let users = t["usedBy"].as_array().map(|u| u.iter().filter_map(Value::as_str).collect::<Vec<_>>().join("\n")).unwrap_or_default();
                    let r = r.on_hover_text(format!("{id}  ({})\nsource {}{}\n{}", t["textureId"], t["source"].as_str().unwrap_or("?"), if users.is_empty() { String::new() } else { format!("\nused by\n{users}") }, t["flags"]));
                    if r.clicked() {
                        *clicked = Some(id.to_string());
                    }
                };
                for t in &own {
                    row(ui, t, t["id"].as_str().unwrap_or("").to_string());
                }
                // each shared pack once (in the order they come first; not only neighbours)
                let mut packs: Vec<String> = Vec::new();
                for p in textures.iter().filter(|t| t["shared"] == true).filter_map(|t| t["pack"].as_str()) {
                    if !packs.iter().any(|x| x == p) {
                        packs.push(p.to_string());
                    }
                }
                for p in packs {
                    let items: Vec<&Value> = textures.iter().filter(|t| t["shared"] == true && t["pack"].as_str() == Some(p.as_str())).collect();
                    egui::CollapsingHeader::new(format!("{p} ({} used)", items.len())).id_salt(("shared", p.as_str())).show(ui, |ui| {
                        for t in items {
                            row(ui, t, format!("texture/{}", t["index"]));
                        }
                    });
                }
            });
        }
        // borrowed, not copied every frame
        let sounds: &[Value] = self.media.sounds["sounds"].as_array().map(Vec::as_slice).unwrap_or(&[]);
        if !sounds.is_empty() {
            egui::CollapsingHeader::new(format!("Sounds ({})", sounds.len())).id_salt(("group", "Sounds")).default_open(false).show(ui, |ui| {
                for s in sounds {
                    let id = s["id"].as_str().unwrap_or("");
                    let r = ui.selectable_label(selected.as_deref() == Some(id), format!("{id}  ·  {}", sound_summary(s)));
                    let r = r.on_hover_text(format!("{} samples, loop {}, volume {}, plays as {}", s["length"], s["loop"], s["volume"], s["plays"].as_str().unwrap_or("?")));
                    if r.clicked() {
                        *clicked = Some(id.to_string());
                    }
                }
            });
        }
        let song = &self.media.sounds["song"];
        if song.is_object() {
            let insts: &[Value] = self.media.sounds["instruments"].as_array().map(Vec::as_slice).unwrap_or(&[]);
            egui::CollapsingHeader::new("Song").id_salt(("group", "Song")).default_open(true).show(ui, |ui| {
                let r = ui.selectable_label(selected.as_deref() == Some("song"), format!("song  ·  {} {:.0} bpm, {} ticks", song["kind"].as_str().unwrap_or(""), song["bpm"].as_f64().unwrap_or(0.0), song["ticks"]));
                if r.clicked() {
                    *clicked = Some("song".into());
                }
                egui::CollapsingHeader::new(format!("Instruments ({})", insts.len())).id_salt(("group", "Instruments")).show(ui, |ui| {
                    for s in insts {
                        let id = s["id"].as_str().unwrap_or("");
                        let what = if s["drum"] == true { format!("drum {}", s["lo"]) } else { format!("prog {} {}-{}", s["program"], s["lo"], s["hi"]) };
                        let r = ui.selectable_label(selected.as_deref() == Some(id), format!("{id}  ·  {what}  ·  {}", sound_summary(s)));
                        let r = r.on_hover_text(format!("envelope {}", s["envelope"]));
                        if r.clicked() {
                            *clicked = Some(id.to_string());
                        }
                    }
                });
            });
        }
    }

    // ---- properties ------------------------------------------------------------------------------

    /// The open item's listing entry, in the right-hand panel; true if it showed one.
    pub(crate) fn media_properties(&mut self, ui: &mut egui::Ui) -> bool {
        let Some(id) = self.media.open.clone() else { return false };
        if self.selected.as_deref() != Some(id.as_str()) {
            return false;
        }
        let entry = match media_kind(&id) {
            Some(View::Texture) => self.media.textures.iter().find(|t| t["id"].as_str() == Some(id.as_str())).cloned(),
            Some(View::Sound) => ["sounds", "instruments"]
                .iter()
                .flat_map(|k| self.media.sounds[*k].as_array().into_iter().flatten())
                .find(|t| t["id"].as_str() == Some(id.as_str()))
                .cloned(),
            Some(View::Song) => Some(self.media.sounds["song"].clone()),
            _ => None,
        };
        ui.heading(&id);
        let Some(entry) = entry else {
            ui.label("…");
            return true;
        };
        let mut nav = None;
        egui::ScrollArea::vertical().id_salt("mprops").auto_shrink([false, false]).show(ui, |ui| {
            egui::Grid::new("mfields").num_columns(2).striped(true).spacing([12.0, 4.0]).show(ui, |ui| {
                for (k, v) in entry.as_object().into_iter().flatten() {
                    if k == "usedBy" || k == "id" {
                        continue;
                    }
                    ui.label(k);
                    match v {
                        Value::Object(m) => {
                            ui.vertical(|ui| {
                                for (a, b) in m {
                                    ui.label(format!("{a}: {b}"));
                                }
                            });
                        }
                        _ => {
                            ui.label(v.to_string());
                        }
                    }
                    ui.end_row();
                }
            });
            if let Some(users) = entry["usedBy"].as_array() {
                ui.separator();
                ui.label(format!("Used by ({})", users.len()));
                for u in users.iter().filter_map(Value::as_str) {
                    if ui.link(u).clicked() {
                        nav = Some(u.to_string());
                    }
                }
            }
        });
        if let Some(n) = nav {
            self.media.view = View::Scene;
            self.follow(&n);
        }
        true
    }

    /// The textures a node uses (their usedBy lists it), as links.
    pub(crate) fn textures_of(&mut self, ui: &mut egui::Ui, node: &str) {
        let used: Vec<(String, String)> = self
            .media
            .textures
            .iter()
            .filter(|t| t["usedBy"].as_array().is_some_and(|u| u.iter().any(|x| x.as_str() == Some(node))))
            .map(|t| (t["id"].as_str().unwrap_or("").to_string(), tex_summary(t)))
            .collect();
        if used.is_empty() {
            return;
        }
        ui.separator();
        ui.label(format!("Textures ({})", used.len()));
        for (id, s) in used {
            ui.horizontal(|ui| {
                if ui.link(&id).clicked() {
                    let ctx = self.ctx.clone();
                    self.reveal(&ctx, &id);
                    self.open_media(&id);
                }
                ui.label(egui::RichText::new(s).weak());
            });
        }
    }

    // ---- the central panel -------------------------------------------------------------------

    /// The tabs over the central panel: the 3D view, or the open media item.
    pub(crate) fn view_tabs(&mut self, ui: &mut egui::Ui) {
        let Some(open) = self.media.open.clone() else { return };
        let kind = media_kind(&open).unwrap_or(View::Scene);
        ui.horizontal(|ui| {
            ui.selectable_value(&mut self.media.view, View::Scene, "3D view");
            ui.selectable_value(&mut self.media.view, kind, open.as_str());
        });
    }

    pub(crate) fn media_panel(&mut self, ui: &mut egui::Ui) {
        match self.media.view {
            View::Texture => self.texture_panel(ui),
            View::Sound => self.sound_panel(ui),
            View::Song => self.song_panel(ui),
            View::Scene => {}
        }
    }

    fn texture_panel(&mut self, ui: &mut egui::Ui) {
        let ctx = ui.ctx().clone();
        let ppp = ctx.pixels_per_point();
        let Some(tv) = &mut self.media.tex else {
            ui.centered_and_justified(|ui| ui.spinner());
            return;
        };
        if Some(tv.id.as_str()) != self.media.open.as_deref() {
            ui.centered_and_justified(|ui| ui.spinner());
            return;
        }
        let mut export = false;
        ui.horizontal(|ui| {
            egui::ComboBox::from_id_salt("level")
                .selected_text(format!("level {}", tv.tex.levels[tv.level].level))
                .show_ui(ui, |ui| {
                    for (i, l) in tv.tex.levels.iter().enumerate() {
                        ui.selectable_value(&mut tv.level, i, format!("level {}  {}×{}", l.level, l.width, l.height));
                    }
                });
            ui.separator();
            ui.selectable_value(&mut tv.alpha, AlphaView::Blend, "RGBA").on_hover_text("over a checkerboard");
            ui.selectable_value(&mut tv.alpha, AlphaView::Opaque, "RGB");
            ui.selectable_value(&mut tv.alpha, AlphaView::Alpha, "Alpha");
            ui.separator();
            ui.checkbox(&mut tv.linear, "linear").on_hover_text("off: nearest texel");
            if ui.button("Fit").clicked() {
                tv.fit = true;
            }
            if ui.button("1:1").clicked() {
                tv.fit = false;
                tv.zoom = 1.0;
                tv.pan = egui::Vec2::ZERO;
            }
            ui.separator();
            ui.add(egui::TextEdit::singleline(&mut self.media.export).desired_width(240.0));
            export = ui.button("Export PNG").on_hover_text("this level, as decoded").clicked();
        });
        let lv = &tv.tex.levels[tv.level];
        let (w, h) = (lv.width as f32, lv.height as f32);
        let status_h = ui.text_style_height(&egui::TextStyle::Body) + 6.0;
        let full = ui.available_rect_before_wrap();
        let rect = egui::Rect::from_min_max(full.min, egui::pos2(full.max.x, (full.max.y - status_h).max(full.min.y + 16.0)));
        let resp = ui.interact(rect, egui::Id::unique("hwvx-texture"), egui::Sense::click_and_drag());
        if tv.fit {
            tv.zoom = ((rect.width() - 16.0) / w).min((rect.height() - 16.0) / h).max(0.01) * ppp;
            tv.pan = egui::Vec2::ZERO;
        }
        let size = egui::vec2(w, h) * tv.zoom / ppp;
        let mut img = egui::Rect::from_center_size(rect.center() + tv.pan, size);
        // wheel: zoom around the pointer; drag: pan
        if resp.hovered() {
            let steps = wheel(ui);
            if steps != 0.0 {
                let pos = resp.hover_pos().unwrap_or(rect.center());
                let k = 1.15f32.powf(steps);
                let new_zoom = (tv.zoom * k).clamp(0.05, 256.0);
                let k = new_zoom / tv.zoom;
                // the point under the pointer stays
                let c = rect.center() + tv.pan;
                tv.pan += (pos - c) * (1.0 - k);
                tv.zoom = new_zoom;
                tv.fit = false;
                img = egui::Rect::from_center_size(rect.center() + tv.pan, egui::vec2(w, h) * tv.zoom / ppp);
            }
        }
        if resp.dragged() {
            tv.pan += resp.drag_delta();
            tv.fit = false;
            img = img.translate(resp.drag_delta());
        }
        if resp.double_clicked() {
            tv.fit = true;
        }
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::from_rgb(0x30, 0x34, 0x3C));
        // the checkerboard, under the visible part of the image
        let vis = img.intersect(rect);
        if tv.alpha == AlphaView::Blend && vis.is_positive() {
            let cell = 8.0;
            let (x0, y0) = (((vis.min.x - img.min.x) / cell).floor(), ((vis.min.y - img.min.y) / cell).floor());
            let mut y = y0;
            while img.min.y + y * cell < vis.max.y {
                let mut x = x0;
                while img.min.x + x * cell < vis.max.x {
                    let r = egui::Rect::from_min_size(egui::pos2(img.min.x + x * cell, img.min.y + y * cell), egui::vec2(cell, cell)).intersect(vis);
                    let c = if (x as i64 + y as i64) % 2 == 0 { 0x99 } else { 0x66 };
                    painter.rect_filled(r, 0.0, egui::Color32::from_gray(c));
                    x += 1.0;
                }
                y += 1.0;
            }
        }
        let key = (tv.level, tv.alpha, tv.linear);
        if tv.handle.as_ref().is_none_or(|(k, _)| *k != key) {
            let px: Vec<u8> = match tv.alpha {
                AlphaView::Blend => lv.rgba.clone(),
                AlphaView::Opaque => lv.rgba.chunks_exact(4).flat_map(|p| [p[0], p[1], p[2], 255]).collect(),
                AlphaView::Alpha => lv.rgba.chunks_exact(4).flat_map(|p| [p[3], p[3], p[3], 255]).collect(),
            };
            let ci = egui::ColorImage::from_rgba_unmultiplied([lv.width as usize, lv.height as usize], &px);
            let opts = if tv.linear { egui::TextureOptions::LINEAR } else { egui::TextureOptions::NEAREST };
            tv.handle = Some((key, ctx.load_texture("media-texture", ci, opts)));
        }
        if let Some((_, t)) = &tv.handle {
            painter.image(t.id(), img, egui::Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)), egui::Color32::WHITE);
        }
        painter.rect_stroke(img, 0.0, egui::Stroke::new(1.0, egui::Color32::from_gray(0x80)), egui::StrokeKind::Outside);
        // the texel under the pointer
        let mut line = format!(
            "{}  ·  {} {}×{}  ·  level {} {}×{}  ·  {:.0}%",
            tv.id,
            tv.tex.info["format"].as_str().unwrap_or("?"),
            tv.tex.info["width"],
            tv.tex.info["height"],
            lv.level,
            lv.width,
            lv.height,
            tv.zoom * 100.0
        );
        if let Some(a) = tv.tex.info["alpha"].as_str() {
            line += &format!("  ·  alpha {a}");
        }
        if let Some(p) = resp.hover_pos().filter(|p| img.contains(*p)) {
            let tx = (((p.x - img.min.x) / img.width()) * w).floor().clamp(0.0, w - 1.0) as usize;
            let ty = (((p.y - img.min.y) / img.height()) * h).floor().clamp(0.0, h - 1.0) as usize;
            let o = (ty * lv.width as usize + tx) * 4;
            let c = &lv.rgba[o..o + 4];
            line += &format!("  ·  ({tx}, {ty})  RGBA {} {} {} {}  #{:02X}{:02X}{:02X}{:02X}", c[0], c[1], c[2], c[3], c[0], c[1], c[2], c[3]);
        }
        let status = egui::Rect::from_min_max(egui::pos2(full.min.x, rect.max.y), full.max);
        ui.scope_builder(egui::UiBuilder::new().max_rect(status), |ui| ui.label(egui::RichText::new(line).weak().small()));
        if export {
            let path = std::path::PathBuf::from(self.media.export.trim());
            let tv = self.media.tex.as_ref().unwrap();
            let lv = &tv.tex.levels[tv.level];
            match write_png(&path, lv.width, lv.height, &lv.rgba) {
                Ok(()) => self.note(format!("saved {}", path.display())),
                Err(e) => self.fail(e),
            }
        }
    }

    fn sound_panel(&mut self, ui: &mut egui::Ui) {
        let ready = self.media.snd.as_ref().is_some_and(|s| Some(s.id.as_str()) == self.media.open.as_deref());
        if !ready {
            ui.centered_and_justified(|ui| ui.spinner());
            return;
        }
        let (mut play, mut here, mut stop, mut export) = (false, false, false, false);
        let device = self.media.audio["device"].as_bool().unwrap_or(true);
        let instrument = self.media.snd.as_ref().unwrap().id.contains("instrument/");
        ui.horizontal(|ui| {
            play = ui.button("▶ Play").on_hover_text("through the game's own mixer").clicked();
            here = ui.button("▶ Play here").on_hover_text("the decoded samples with the system's player (once)").clicked();
            stop = ui.button("■ Stop").clicked();
            if instrument {
                let s = self.media.snd.as_mut().unwrap();
                let mut at = s.note.is_some();
                ui.checkbox(&mut at, "at note");
                if at {
                    let mut n = s.note.unwrap_or(60);
                    ui.add(egui::DragValue::new(&mut n).range(0..=127));
                    s.note = Some(n);
                } else {
                    s.note = None;
                }
            }
            if !device {
                ui.colored_label(egui::Color32::LIGHT_RED, "the game has no audio device: Play plays here");
            }
            ui.separator();
            ui.add(egui::TextEdit::singleline(&mut self.media.export).desired_width(240.0));
            export = ui.button("Export WAV").clicked();
        });
        if play {
            self.play_game();
        }
        if here {
            self.play_here();
        }
        if stop {
            self.stop_all();
        }
        let s = self.media.snd.as_mut().unwrap();
        let snd = &s.sound;
        let n = snd.pcm.len().max(1);
        let status_h = ui.text_style_height(&egui::TextStyle::Body) + 6.0;
        let full = ui.available_rect_before_wrap();
        let rect = egui::Rect::from_min_max(full.min, egui::pos2(full.max.x, (full.max.y - status_h).max(full.min.y + 16.0)));
        let resp = ui.interact(rect, egui::Id::unique("hwvx-wave"), egui::Sense::click_and_drag());
        let (mut a, mut b) = s.range;
        let x_of = |i: usize, a: usize, b: usize| rect.min.x + (i as f32 - a as f32) / (b - a).max(1) as f32 * rect.width();
        // wheel zooms time around the pointer, drag pans, double-click shows it all
        if resp.hovered() {
            let steps = wheel(ui);
            if steps != 0.0 {
                let p = resp.hover_pos().map(|p| ((p.x - rect.min.x) / rect.width()).clamp(0.0, 1.0)).unwrap_or(0.5);
                let at = a as f32 + p * (b - a) as f32;
                let span = ((b - a) as f32 * 1.2f32.powf(-steps)).clamp(32.0, n as f32);
                a = (at - p * span).max(0.0) as usize;
                b = (a + span as usize).min(n);
                a = b.saturating_sub(span as usize);
            }
        }
        if resp.dragged() {
            let d = (-resp.drag_delta().x / rect.width() * (b - a) as f32) as i64;
            let span = b - a;
            a = (a as i64 + d).clamp(0, (n - span) as i64) as usize;
            b = a + span;
        }
        if resp.double_clicked() {
            a = 0;
            b = n;
        }
        s.range = (a, b);
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::from_rgb(0x20, 0x23, 0x28));
        let mid = rect.center().y;
        let half = rect.height() * 0.45;
        painter.line_segment([egui::pos2(rect.min.x, mid), egui::pos2(rect.max.x, mid)], egui::Stroke::new(1.0, egui::Color32::from_gray(0x50)));
        let cols = rect.width().max(1.0) as usize;
        let wave = egui::Color32::from_rgb(0x6C, 0xB4, 0xF0);
        for (c, (lo, hi)) in snd.peaks(a, b, cols).into_iter().enumerate() {
            let x = rect.min.x + c as f32 + 0.5;
            let y0 = mid - hi as f32 / 32768.0 * half;
            let y1 = mid - lo as f32 / 32768.0 * half;
            painter.line_segment([egui::pos2(x, y0), egui::pos2(x, y1.max(y0 + 1.0))], egui::Stroke::new(1.0, wave));
        }
        if let Some(l) = snd.loop_start {
            let x = x_of(l as usize, a, b);
            if x >= rect.min.x && x <= rect.max.x {
                let c = egui::Color32::from_rgb(0xF0, 0xA0, 0x30);
                painter.line_segment([egui::pos2(x, rect.min.y), egui::pos2(x, rect.max.y)], egui::Stroke::new(1.5, c));
                painter.text(egui::pos2(x + 3.0, rect.min.y + 2.0), egui::Align2::LEFT_TOP, "loop", egui::FontId::proportional(12.0), c);
            }
            let x = x_of(n, a, b);
            if x >= rect.min.x && x <= rect.max.x {
                painter.line_segment([egui::pos2(x - 1.0, rect.min.y), egui::pos2(x - 1.0, rect.max.y)], egui::Stroke::new(1.0, egui::Color32::from_rgb(0xF0, 0xA0, 0x30)));
            }
        }
        // where playing is, from when it started (the loop repeats in the game)
        if let Some(t0) = s.started {
            let pos = (t0.elapsed().as_secs_f64() * snd.rate as f64) as usize;
            let pos = match snd.loop_start {
                Some(l) if pos >= n && (l as usize) < n => l as usize + (pos - n) % (n - l as usize),
                _ => pos,
            };
            if pos < n {
                let x = x_of(pos, a, b);
                painter.line_segment([egui::pos2(x, rect.min.y), egui::pos2(x, rect.max.y)], egui::Stroke::new(1.0, egui::Color32::WHITE));
                ui.ctx().request_repaint_after(Duration::from_millis(16));
            } else {
                s.started = None;
            }
        }
        let mut line = format!(
            "{}  ·  {}  ·  {} Hz  ·  {} samples, {:.3} s  ·  loop {}  ·  showing {}..{}",
            s.id,
            snd.info["format"].as_str().unwrap_or("?"),
            snd.rate,
            n,
            n as f64 / snd.rate.max(1) as f64,
            snd.loop_start.map(|l| format!("from {l}")).unwrap_or_else(|| "none".into()),
            a,
            b
        );
        if let Some(p) = resp.hover_pos() {
            let i = (a as f32 + (p.x - rect.min.x) / rect.width() * (b - a) as f32) as usize;
            if i < n {
                line += &format!("  ·  sample {i} ({:.3} s) = {}", i as f64 / snd.rate.max(1) as f64, snd.pcm[i]);
            }
        }
        let status = egui::Rect::from_min_max(egui::pos2(full.min.x, rect.max.y), full.max);
        ui.scope_builder(egui::UiBuilder::new().max_rect(status), |ui| ui.label(egui::RichText::new(line).weak().small()));
        if export {
            let path = std::path::PathBuf::from(self.media.export.trim());
            let s = &self.media.snd.as_ref().unwrap().sound;
            match std::fs::write(&path, wav_bytes(s.rate, &s.pcm)) {
                Ok(()) => self.note(format!("saved {}", path.display())),
                Err(e) => self.fail(format!("{}: {e}", path.display())),
            }
        }
    }

    fn song_panel(&mut self, ui: &mut egui::Ui) {
        let song = self.media.sounds["song"].clone();
        let (mut play, mut stop) = (false, false);
        ui.horizontal(|ui| {
            play = ui.button("▶ Play").on_hover_text("the game's sequencer and the bank's samples").clicked();
            stop = ui.button("■ Stop").clicked();
        });
        if play {
            self.play_game();
        }
        if stop {
            self.stop_all();
        }
        ui.separator();
        egui::Grid::new("song").num_columns(2).striped(true).show(ui, |ui| {
            for k in ["kind", "bpm", "tempo", "tempoChanges", "division", "ticks", "events", "notes", "tracks", "channels", "markers", "instruments"] {
                ui.label(k);
                ui.label(song[k].to_string());
                ui.end_row();
            }
        });
        let playing = self.media.audio["song"] == true;
        let tick = self.media.audio["tick"].as_u64().unwrap_or(0);
        let ticks = song["ticks"].as_u64().unwrap_or(1).max(1);
        ui.separator();
        ui.add(egui::ProgressBar::new((tick % ticks) as f32 / ticks as f32).text(if playing { format!("playing: tick {tick} of {ticks}") } else { "stopped".into() }));
        if playing {
            ui.ctx().request_repaint_after(Duration::from_millis(250));
        }
    }
}
