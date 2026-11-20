//! A mod's soundtrack: the `music` section of mod.jsonc (schemas/Mod.schema.json; the game's side
//! is src/game/Port_Mods.c Port_ModsMusic and src/port/musicstream.c; docs/xdl-format/README.md
//! §8.2.2). A base song (every world's and the three front-end music packs') is replaced by
//! recorded tracks of the mod's own: one loops, a list is a playlist. Loop points come from the
//! entry, else the file's LOOPSTART / LOOPEND / LOOPLENGTH tags, else `<file>.loop.txt`, else the
//! whole file loops. The files are copied into the mod's folder (music/).

use crate::content::ModProject;
use serde_json::{Map, Value, json};
use std::path::{Path, PathBuf};

pub const SECTION: &str = "music";
/// where imported files go in the mod's folder
pub const MUSIC_DIR: &str = "music";
pub const EXTS: [&str; 4] = ["ogg", "mp3", "flac", "wav"];
/// src/audiodec's AUDIODEC_MAX_FRAMES: the packer's check decodes every audio file, and refuses
/// one longer than this (the game itself streams any length).
pub const CHECK_MAX_FRAMES: u64 = 48000 * 60 * 10;

/// A retail song: its pack, where it plays, and how to start the game there (HWVX_START).
#[derive(Clone, Debug, PartialEq)]
pub struct BaseSong {
    /// the archive's own name (ch1_l2, HW_music)
    pub archive: &'static str,
    /// world_0, music_1
    pub pack: &'static str,
    /// the archive under the data folder, without its extension
    pub path: String,
    /// Adventure, Challenge, ... Menus
    pub group: &'static str,
    pub label: String,
    /// HWVX_START for a test where it plays (empty: the title screen)
    pub start: String,
    /// HWVX_AUTOINPUT that brings the song on from there (a level's song starts after the
    /// intro box's Continue; the menus' after the title screen)
    pub input: String,
}

impl BaseSong {
    /// base://ARCHIVE/PACK/song/0, the mod's key for it
    pub fn key(&self) -> String {
        format!("base://{}/{}/song/0", self.archive, self.pack)
    }
    /// the viewer's node for its song (viewer_media.c `play`)
    pub fn viewer_node(&self) -> String {
        format!("base://{}/{}/song", self.archive, self.pack)
    }
}

/// (folder, archive, group, HWVX_START mode, label) of every retail world, in the game's order (the
/// MCP's progressions, as the viewer's `worlds` lists them; tests/content_music.rs): one song each
/// (world_0), as `worlds_/` holds them on every release.
const WORLDS: &[(&str, &str, &str, &str, &str)] = &[
    ("sp_/MISS_", "world1l1", "Adventure", "adventure", "world 1, level 1"),
    ("sp_/MISS_", "world1l2", "Adventure", "adventure", "world 1, level 2"),
    ("sp_/MISS_", "world1l3", "Adventure", "adventure", "world 1, level 3"),
    ("sp_/MISS_", "world2l1", "Adventure", "adventure", "world 2, level 1"),
    ("sp_/MISS_", "world2l2", "Adventure", "adventure", "world 2, level 2"),
    ("sp_/MISS_", "world2l3", "Adventure", "adventure", "world 2, level 3"),
    ("sp_/MISS_", "world3l1", "Adventure", "adventure", "world 3, level 1"),
    ("sp_/MISS_", "world3l2", "Adventure", "adventure", "world 3, level 2"),
    ("sp_/MISS_", "world3l3", "Adventure", "adventure", "world 3, level 3"),
    ("sp_/MISS_", "world4l1", "Adventure", "adventure", "world 4, level 1"),
    ("sp_/MISS_", "world4l2", "Adventure", "adventure", "world 4, level 2"),
    ("sp_/MISS_", "world4l3", "Adventure", "adventure", "world 4, level 3"),
    ("sp_/MISS_", "world5l1", "Adventure", "adventure", "world 5, level 1"),
    ("sp_/MISS_", "world5l2", "Adventure", "adventure", "world 5, level 2"),
    ("sp_/chal1_", "ch1_l2", "Challenge", "challenge", "race 1"),
    ("sp_/chal1_", "ch1_l3", "Challenge", "challenge", "race 2"),
    ("sp_/chal1_", "ch2_l1", "Challenge", "challenge", "race 3"),
    ("sp_/chal1_", "ch1_l8", "Challenge", "challenge", "race 4"),
    ("sp_/chal1_", "ch1_l4", "Challenge", "challenge", "race 5"),
    ("sp_/chal1_", "ch1_l5", "Challenge", "challenge", "race 6"),
    ("sp_/chal1_", "ch1_l6", "Challenge", "challenge", "race 7"),
    ("sp_/chal1_", "ch2_l2", "Challenge", "challenge", "race 8"),
    ("sp_/chal1_", "ch2_l4", "Challenge", "challenge", "race 9"),
    ("sp_/chal1_", "ch5_l1", "Challenge", "challenge", "race 10"),
    ("sp_/chal1_", "ch5_l2", "Challenge", "challenge", "race 11"),
    ("sp_/chal1_", "ch1_l7", "Challenge", "challenge", "race 12"),
    ("sp_/chal1_", "ch3_l2", "Challenge", "challenge", "race 13"),
    ("sp_/chal1_", "ch5_l3", "Challenge", "challenge", "race 14"),
    ("sp_/chal1_", "ch5_l5", "Challenge", "challenge", "race 15"),
    ("sp_/chal1_", "ch3_l1", "Challenge", "challenge", "race 16"),
    ("sp_/chal1_", "ch5_l4", "Challenge", "challenge", "race 17"),
    ("sp_/chal1_", "ch2_l3", "Challenge", "challenge", "race 18"),
    ("sp_/joyride_", "world1", "Joyride", "joyride", "world 1"),
    ("sp_/joyride_", "world2", "Joyride", "joyride", "world 2"),
    ("sp_/joyride_", "world3", "Joyride", "joyride", "world 3"),
    ("sp_/joyride_", "world4", "Joyride", "joyride", "world 4"),
    ("sp_/joyride_", "world5", "Joyride", "joyride", "world 5"),
    ("sp_/drag_", "race1l2", "Drag race", "drag", "track 1"),
    ("sp_/drag_", "race2l2", "Drag race", "drag", "track 2"),
    ("sp_/drag_", "race3l2", "Drag race", "drag", "track 3"),
    ("sp_/drag_", "race4l2", "Drag race", "drag", "track 4"),
    ("sp_/drag_", "race5l2", "Drag race", "drag", "track 5"),
    ("sp_/drag_", "race6l2", "Drag race", "drag", "track 6"),
    ("mp_/battle_", "bat1_l1", "Battle", "battle", "arena 1"),
    ("mp_/battle_", "bat2_l9", "Battle", "battle", "arena 2"),
    ("mp_/battle_", "bat3_l6", "Battle", "battle", "arena 3"),
    ("mp_/battle_", "bat4_l1", "Battle", "battle", "arena 4"),
    ("mp_/battle_", "bat5_l1", "Battle", "battle", "arena 5"),
    ("mp_/battle_", "bat6_l6", "Battle", "battle", "arena 6"),
    ("mp_/battle_", "bat7_l1", "Battle", "battle", "arena 7"),
    ("mp_/battle_", "bat8_l1", "Battle", "battle", "arena 8"),
    ("mp_/drag_", "race1l1", "Multi-player drag", "mpdrag", "track 1"),
    ("mp_/drag_", "race2l1", "Multi-player drag", "mpdrag", "track 2"),
    ("mp_/drag_", "race3l1", "Multi-player drag", "mpdrag", "track 3"),
    ("mp_/drag_", "race4l1", "Multi-player drag", "mpdrag", "track 4"),
    ("mp_/drag_", "race5l1", "Multi-player drag", "mpdrag", "track 5"),
    ("mp_/drag_", "race6l1", "Multi-player drag", "mpdrag", "track 6"),
];

/// The three front-end songs (HW_music): (pack, where it plays, HWVX_START and HWVX_AUTOINPUT that
/// reach it). Measured (HWVX_MUSICLOG): music_1 is the title screen and the attract demo's
/// (HW_IPack interface_1), music_2 the main menus' and the select screens' (interface_6, 3);
/// music_3 is named by screens of interface_6, 7 and 9 not reached in a scripted run.
const MENUS: &[(&str, &str, &str, &str)] = &[
    ("music_1", "Title screen and attract demo", "", ""),
    ("music_2", "Main menus and select screens", "", "700:start,+400:start"),
    ("music_3", "Other front-end screens", "", ""),
];

/// Every retail song, in the order the list shows them: the menus first, then each mode's worlds.
pub fn base_songs() -> Vec<BaseSong> {
    let mut v: Vec<BaseSong> = MENUS
        .iter()
        .map(|&(pack, what, start, input)| BaseSong { archive: "HW_music", pack, path: "HW_music".into(), group: "Menus", label: format!("{what}  ({pack})"), start: start.into(), input: input.into() })
        .collect();
    for &(folder, archive, group, mode, what) in WORLDS {
        let path = format!("worlds_/{folder}/{archive}");
        v.push(BaseSong { archive, pack: "world_0", path, group, label: format!("{group} {what}  ({archive})"), start: format!("race:mode={mode},world={archive}"), input: "+240:a,+60:a".into() });
    }
    v
}

/// The retail songs the data has no world for: their keys. The PC has no multi-player drag
/// worlds (race1l1..race6l1): 54 of the 60 songs, where the GameCube and the PS2 have all.
pub fn songs_missing(info: &crate::content::DataInfo) -> Vec<String> {
    base_songs().into_iter().filter(|s| !info.has_archive(s.archive)).map(|s| s.key()).collect()
}

/// What the editor says of a song the data has no world for.
pub fn not_on_release(info: &crate::content::DataInfo) -> String {
    format!("not on this release ({})", info.release_name())
}

/// The song a key names (with or without its /song/0).
pub fn song_for(key: &str) -> Option<BaseSong> {
    let k = canonical(key);
    base_songs().into_iter().find(|s| s.key() == k)
}

/// A key with its /song/0.
pub fn canonical(key: &str) -> String {
    if key.ends_with("/song/0") { key.to_string() } else { format!("{key}/song/0") }
}

/// One recorded track of a song (the schema's musicTrack).
#[derive(Clone, Debug, PartialEq, Default)]
pub struct Track {
    pub file: String,
    pub intro: Option<String>,
    pub loop_start: Option<u64>,
    pub loop_end: Option<u64>,
    /// `loop: false`: played once, then silence
    pub once: bool,
    pub volume: Option<f64>,
}

impl Track {
    pub fn new(file: &str) -> Track {
        Track { file: file.into(), ..Default::default() }
    }

    pub fn from_value(v: &Value) -> Option<Track> {
        match v {
            Value::String(s) => Some(Track::new(s)),
            Value::Object(o) => Some(Track {
                file: o.get("file")?.as_str()?.into(),
                intro: o.get("intro").and_then(Value::as_str).map(String::from),
                loop_start: o.get("loopStart").and_then(Value::as_u64),
                loop_end: o.get("loopEnd").and_then(Value::as_u64),
                once: o.get("loop") == Some(&Value::Bool(false)),
                volume: o.get("volume").and_then(Value::as_f64),
            }),
            _ => None,
        }
    }

    /// The file's name alone when nothing else is set, else the object.
    pub fn to_value(&self) -> Value {
        let mut o = Map::new();
        o.insert("file".into(), json!(self.file));
        if let Some(i) = self.intro.as_ref().filter(|i| !i.is_empty()) {
            o.insert("intro".into(), json!(i));
        }
        if let Some(s) = self.loop_start {
            o.insert("loopStart".into(), json!(s));
        }
        if let Some(e) = self.loop_end.filter(|&e| e >= 1) {
            o.insert("loopEnd".into(), json!(e));
        }
        if self.once {
            o.insert("loop".into(), json!(false));
        }
        if let Some(v) = self.volume.filter(|v| (v - 1.0).abs() > 1e-9) {
            let v = (v.clamp(0.0, 4.0) * 100.0).round() / 100.0;
            o.insert("volume".into(), if v.fract() == 0.0 { json!(v as i64) } else { json!(v) });
        }
        if o.len() == 1 { json!(self.file) } else { Value::Object(o) }
    }
}

/// A name the schema takes (letters, digits, _ . / space -, a lower-case audio extension).
pub fn valid_file_name(f: &str) -> bool {
    let Some((stem, ext)) = f.rsplit_once('.') else { return false };
    !stem.is_empty()
        && EXTS.contains(&ext)
        && stem.chars().all(|c| c.is_ascii_alphanumeric() || "_./ -".contains(c))
        && !f.starts_with('/')
        && !f.split('/').any(|p| p == ".." || p.is_empty())
}

/// A file's name made into one the schema takes: other characters become _, the extension
/// lower case. None if it isn't an audio file the game plays.
pub fn safe_file_name(name: &str) -> Option<String> {
    let (stem, ext) = name.rsplit_once('.')?;
    let ext = ext.to_ascii_lowercase();
    if !EXTS.contains(&ext.as_str()) {
        return None;
    }
    let stem: String = stem.chars().map(|c| if c.is_ascii_alphanumeric() || "_- ".contains(c) { c } else { '_' }).collect();
    let stem = stem.trim().trim_matches('.');
    Some(format!("{}.{ext}", if stem.is_empty() { "track" } else { stem }))
}

impl ModProject {
    /// The songs the mod replaces: (key as written, its tracks), in the file's order.
    pub fn music(&self) -> Vec<(String, Vec<Track>)> {
        self.doc[SECTION]
            .as_object()
            .map(|m| {
                m.iter()
                    .map(|(k, v)| {
                        let t = match v {
                            Value::Array(a) => a.iter().filter_map(Track::from_value).collect(),
                            v => Track::from_value(v).into_iter().collect(),
                        };
                        (k.clone(), t)
                    })
                    .collect()
            })
            .unwrap_or_default()
    }

    /// The key a song is written under (with or without /song/0), if the mod has it.
    pub fn music_key(&self, key: &str) -> Option<String> {
        let k = canonical(key);
        self.doc[SECTION].as_object()?.keys().find(|x| canonical(x) == k).cloned()
    }

    pub fn music_tracks(&self, key: &str) -> Vec<Track> {
        let Some(k) = self.music_key(key) else { return vec![] };
        self.music().into_iter().find(|(x, _)| *x == k).map(|x| x.1).unwrap_or_default()
    }

    /// Writes a song's tracks: one is the track itself, more a playlist, none drops the song
    /// (and the section with its last song).
    pub fn set_music(&mut self, key: &str, tracks: &[Track]) {
        let k = self.music_key(key).unwrap_or_else(|| canonical(key));
        if tracks.is_empty() {
            if let Some(m) = self.doc.get_mut(SECTION).and_then(Value::as_object_mut) {
                m.remove(&k);
                if m.is_empty() {
                    self.doc.as_object_mut().unwrap().remove(SECTION);
                }
            }
            return;
        }
        if !self.doc[SECTION].is_object() {
            self.doc[SECTION] = json!({});
        }
        self.doc[SECTION][&k] = if tracks.len() == 1 { tracks[0].to_value() } else { Value::Array(tracks.iter().map(Track::to_value).collect()) };
    }

    /// Copies an audio file into the mod's music/ folder: its path in the mod. The same file
    /// imported again is the one already there; another file of the same name gets a number.
    pub fn import_music(&self, src: &Path) -> Result<String, String> {
        let name = src.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        let safe = safe_file_name(&name).ok_or_else(|| format!("{name}: not a file the game streams (OGG, MP3, FLAC or WAV)"))?;
        let data = std::fs::read(src).map_err(|e| format!("{}: {e}", src.display()))?;
        let dir = self.dir.join(MUSIC_DIR);
        std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        let (stem, ext) = safe.rsplit_once('.').unwrap();
        for n in 1.. {
            let file = if n == 1 { safe.clone() } else { format!("{stem}-{n}.{ext}") };
            let to = dir.join(&file);
            match std::fs::read(&to) {
                Ok(have) if have == data => return Ok(format!("{MUSIC_DIR}/{file}")),
                Ok(_) => continue,
                Err(_) => {
                    std::fs::write(&to, &data).map_err(|e| format!("{}: {e}", to.display()))?;
                    // a sidecar of loop points goes with it
                    let side = PathBuf::from(format!("{}.loop.txt", src.display()));
                    if let Ok(t) = std::fs::read(&side) {
                        let _ = std::fs::write(dir.join(format!("{file}.loop.txt")), t);
                    }
                    return Ok(format!("{MUSIC_DIR}/{file}"));
                }
            }
        }
        unreachable!()
    }

    /// Audio files in music/ no song uses (left behind by edits).
    pub fn music_unused(&self) -> Vec<String> {
        let used: Vec<String> = self.music().into_iter().flat_map(|(_, t)| t.into_iter().flat_map(|t| std::iter::once(t.file).chain(t.intro))).collect();
        let mut out = vec![];
        if let Ok(rd) = std::fs::read_dir(self.dir.join(MUSIC_DIR)) {
            for e in rd.flatten() {
                let f = format!("{MUSIC_DIR}/{}", e.file_name().to_string_lossy());
                if safe_file_name(&f).is_some() && !f.ends_with(".loop.txt") && !used.contains(&f) {
                    out.push(f);
                }
            }
        }
        out.sort();
        out
    }

    /// What's wrong with the section, as the game and the packer would see it.
    pub fn music_problems(&self) -> Vec<String> {
        let mut out = vec![];
        for (k, tracks) in self.music() {
            let name = song_for(&k).map(|s| s.label).unwrap_or_else(|| k.clone());
            if song_for(&k).is_none() {
                out.push(format!("{k}: no retail song has this id"));
            }
            if tracks.is_empty() {
                out.push(format!("{name}: no track"));
            }
            for t in &tracks {
                for f in std::iter::once(&t.file).chain(t.intro.iter()) {
                    if !valid_file_name(f) {
                        out.push(format!("{name}: {f}: a name of letters, digits, _ . / - and spaces, ending .ogg .mp3 .flac or .wav"));
                    } else if !self.dir.join(f).is_file() {
                        out.push(format!("{name}: {f} isn't in the mod's folder"));
                    }
                }
                if let (Some(s), Some(e)) = (t.loop_start, t.loop_end) {
                    if e <= s {
                        out.push(format!("{name}: {}: the loop ends before it starts ({s}..{e}): the whole file loops", t.file));
                    }
                }
            }
        }
        out
    }
}

// ---- the files ---------------------------------------------------------------------------------

/// What the game reads of a file's loop: LOOPSTART / LOOPEND / LOOPLENGTH in its first 256 KB
/// (Vorbis comments, ID3 TXXX: musicstream.c TagValue), else in `<file>.loop.txt`.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct LoopTags {
    pub start: Option<u64>,
    pub end: Option<u64>,
    /// "tags", ".loop.txt" or ""
    pub from: &'static str,
}

fn tag_value(buf: &[u8], key: &str) -> Option<u64> {
    let k = key.as_bytes();
    let n = buf.len();
    let mut i = 0;
    while i + k.len() + 1 < n {
        if buf[i..i + k.len()].eq_ignore_ascii_case(k) && (buf[i + k.len()] == b'=' || buf[i + k.len()] == 0) {
            let mut j = i + k.len() + 1;
            while j < n && buf[j] == 0 {
                j += 1;
            }
            let (mut v, mut digits) = (0u64, 0);
            while j < n && buf[j].is_ascii_digit() && digits < 18 {
                v = v * 10 + (buf[j] - b'0') as u64;
                j += 1;
                digits += 1;
            }
            if digits > 0 {
                return Some(v);
            }
        }
        i += 1;
    }
    None
}

fn tags_from(path: &Path, max: usize, start: &mut Option<u64>, end: &mut Option<u64>) {
    use std::io::Read;
    let Ok(f) = std::fs::File::open(path) else { return };
    let mut buf = vec![];
    if f.take(max as u64).read_to_end(&mut buf).is_err() || buf.is_empty() {
        return;
    }
    if start.is_none() {
        *start = tag_value(&buf, "LOOPSTART");
    }
    if end.is_none() {
        *end = tag_value(&buf, "LOOPEND");
        if end.is_none() {
            if let (Some(s), Some(l)) = (*start, tag_value(&buf, "LOOPLENGTH").filter(|&l| l > 0)) {
                *end = Some(s + l);
            }
        }
    }
}

pub fn loop_tags(path: &Path) -> LoopTags {
    let (mut s, mut e) = (None, None);
    tags_from(path, 256 * 1024, &mut s, &mut e);
    let mut from = if s.is_some() || e.is_some() { "tags" } else { "" };
    if s.is_none() || e.is_none() {
        let (s0, e0) = (s, e);
        tags_from(&PathBuf::from(format!("{}.loop.txt", path.display())), 4096, &mut s, &mut e);
        if (s, e) != (s0, e0) {
            from = ".loop.txt";
        }
    }
    LoopTags { start: s, end: e, from }
}

/// The loop the game plays: (start, end) in the file's frames, end None for the file's end.
pub fn effective_loop(t: &Track, tags: &LoopTags, frames: u64) -> (u64, Option<u64>) {
    let s = t.loop_start.or(tags.start).unwrap_or(0);
    let e = t.loop_end.or(tags.end).filter(|&e| e > s && e < frames);
    (if s < frames { s } else { 0 }, e)
}

/// A decoded file: interleaved 16-bit samples (the port's own decoders, src/audiodec, linked in
/// through xdlrs).
pub struct Decoded {
    pub data: Vec<i16>,
    pub channels: usize,
    pub rate: u32,
}

impl Decoded {
    pub fn frames(&self) -> u64 {
        (self.data.len() / self.channels.max(1)) as u64
    }
    pub fn seconds(&self) -> f64 {
        self.frames() as f64 / self.rate.max(1) as f64
    }
}

pub fn decode(path: &Path) -> Result<Decoded, String> {
    let p = xdlrs::modcheck::audio::decode_audio(path, &path.display().to_string())?;
    Ok(Decoded { data: p.data, channels: p.channels.max(1), rate: p.rate })
}

/// A 16-bit WAV of interleaved samples.
pub fn wav(rate: u32, channels: usize, data: &[i16]) -> Vec<u8> {
    let ch = channels.max(1) as u32;
    let bytes = (data.len() * 2) as u32;
    let mut v = Vec::with_capacity(44 + bytes as usize);
    v.extend_from_slice(b"RIFF");
    v.extend_from_slice(&(36 + bytes).to_le_bytes());
    v.extend_from_slice(b"WAVEfmt ");
    v.extend_from_slice(&16u32.to_le_bytes());
    v.extend_from_slice(&1u16.to_le_bytes());
    v.extend_from_slice(&(ch as u16).to_le_bytes());
    v.extend_from_slice(&rate.to_le_bytes());
    v.extend_from_slice(&(rate * ch * 2).to_le_bytes());
    v.extend_from_slice(&((ch * 2) as u16).to_le_bytes());
    v.extend_from_slice(&16u16.to_le_bytes());
    v.extend_from_slice(b"data");
    v.extend_from_slice(&bytes.to_le_bytes());
    for s in data {
        v.extend_from_slice(&s.to_le_bytes());
    }
    v
}

/// The loop's seam to listen to: `secs` before the loop's end, then `secs` from its start
/// (twice round), as the stream plays it.
pub fn seam(d: &Decoded, start: u64, end: Option<u64>, secs: f64) -> Vec<i16> {
    let ch = d.channels;
    let n = d.frames();
    let end = end.unwrap_or(n).min(n);
    let w = ((secs * d.rate as f64) as u64).max(1);
    let a = end.saturating_sub(w).max(start);
    let b = (start + w).min(end);
    let piece = |x: u64, y: u64| d.data[(x as usize * ch)..(y as usize * ch)].to_vec();
    let mut out = piece(a, end);
    out.extend(piece(start, b));
    out
}

/// A test tone: (seconds, Hz) pieces, a sine at `amp`, stereo 16-bit at `rate`.
pub fn tone(rate: u32, pieces: &[(f64, f64)], amp: f64) -> Vec<i16> {
    let mut out = vec![];
    let mut phase = 0.0f64;
    for &(secs, hz) in pieces {
        for _ in 0..(secs * rate as f64) as usize {
            let s = (phase.sin() * amp * 32767.0) as i16;
            out.push(s);
            out.push(s);
            phase += 2.0 * std::f64::consts::PI * hz / rate as f64;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("hwvx-music-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    fn schema() -> Value {
        serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap()
    }

    #[test]
    fn every_song_once_with_a_valid_key() {
        let songs = base_songs();
        assert_eq!(songs.len(), 60, "57 worlds and the three front-end songs");
        let re = regex_like_key;
        for s in &songs {
            assert!(re(&s.key()), "{}", s.key());
            assert_eq!(songs.iter().filter(|x| x.key() == s.key()).count(), 1, "{}", s.key());
            assert_eq!(song_for(&s.key().replace("/song/0", "")).as_ref(), Some(s));
        }
        assert_eq!(song_for("base://ch1_l2/world_0/song/0").unwrap().start, "race:mode=challenge,world=ch1_l2");
    }

    /// With each release's data (HWVX_TEST_DATA: the GameCube's, HWVX_TEST_PC_DATA,
    /// HWVX_TEST_PS2_DATA; each skipped without): the songs it has no world for. The PC lacks the
    /// six multi-player drag worlds; the others have every one.
    #[test]
    fn songs_per_release() {
        for (var, release, missing) in [("HWVX_TEST_DATA", "gc", 0), ("HWVX_TEST_PC_DATA", "pc", 6), ("HWVX_TEST_PS2_DATA", "ps2", 0)] {
            let Some(data) = std::env::var_os(var).map(PathBuf::from) else {
                eprintln!("{var} not set: skipped");
                continue;
            };
            let info = crate::content::DataInfo::read(&data).unwrap();
            assert_eq!(info.release, release, "{var}");
            let m = songs_missing(&info);
            assert_eq!(m.len(), missing, "{var}: {m:?}");
            assert!(m.iter().all(|k| song_for(k).is_some_and(|s| s.start.starts_with("race:mode=mpdrag,"))), "{m:?}");
            if missing > 0 {
                assert_eq!(not_on_release(&info), "not on this release (PC)");
            }
        }
    }

    /// the schema's propertyNames pattern, ^base://[A-Za-z0-9_]+/[a-z_]+_[0-9]+(/song/0)?$
    fn regex_like_key(k: &str) -> bool {
        let Some(rest) = k.strip_prefix("base://") else { return false };
        let rest = rest.strip_suffix("/song/0").unwrap_or(rest);
        let Some((a, p)) = rest.split_once('/') else { return false };
        let Some((t, n)) = p.rsplit_once('_') else { return false };
        !a.is_empty() && a.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') && !t.is_empty() && t.chars().all(|c| c.is_ascii_lowercase() || c == '_') && !n.is_empty() && n.chars().all(|c| c.is_ascii_digit())
    }

    #[test]
    fn tracks_round_trip() {
        assert_eq!(Track::new("music/a.ogg").to_value(), json!("music/a.ogg"));
        let t = Track { file: "music/a.ogg".into(), intro: Some("music/i.ogg".into()), loop_start: Some(10), loop_end: Some(20), once: true, volume: Some(0.5) };
        let v = t.to_value();
        assert_eq!(v, json!({"file": "music/a.ogg", "intro": "music/i.ogg", "loopStart": 10, "loopEnd": 20, "loop": false, "volume": 0.5}));
        assert_eq!(Track::from_value(&v), Some(t));
        assert_eq!(Track { volume: Some(1.0), ..Track::new("x.wav") }.to_value(), json!("x.wav"), "volume 1 is the default");
        assert_eq!(Track { volume: Some(2.0), ..Track::new("x.wav") }.to_value(), json!({"file": "x.wav", "volume": 2}));
    }

    #[test]
    fn names() {
        assert_eq!(safe_file_name("My Song (final).OGG").as_deref(), Some("My Song _final_.ogg"));
        assert_eq!(safe_file_name("über.flac").as_deref(), Some("_ber.flac"));
        assert_eq!(safe_file_name("notes.txt"), None);
        assert!(valid_file_name("music/My Song _final_.ogg"));
        assert!(!valid_file_name("../x.ogg") && !valid_file_name("/x.ogg") && !valid_file_name("x.OGG") && !valid_file_name("a(b).wav"));
    }

    #[test]
    fn tags_as_the_game_reads_them() {
        let d = scratch("tags");
        std::fs::create_dir_all(&d).unwrap();
        // a Vorbis comment and an ID3 TXXX-like key\0value, anywhere in the first 256 KB
        let f = d.join("a.ogg");
        std::fs::write(&f, b"OggS....\x0b\x00\x00\x00loopstart=88200....LOOPLENGTH=176400").unwrap();
        assert_eq!(loop_tags(&f), LoopTags { start: Some(88200), end: Some(264600), from: "tags" });
        let g = d.join("b.mp3");
        std::fs::write(&g, b"ID3....TXXX\x00\x00\x00LOOPSTART\x00\x0012\x00LOOPEND\x00999").unwrap();
        assert_eq!(loop_tags(&g), LoopTags { start: Some(12), end: Some(999), from: "tags" });
        let w = d.join("c.wav");
        std::fs::write(&w, wav(44100, 2, &tone(44100, &[(0.1, 440.0)], 0.3))).unwrap();
        assert_eq!(loop_tags(&w).from, "");
        std::fs::write(d.join("c.wav.loop.txt"), "LOOPSTART=100\nLOOPEND=4000\n").unwrap();
        assert_eq!(loop_tags(&w), LoopTags { start: Some(100), end: Some(4000), from: ".loop.txt" });
        let t = Track { loop_end: Some(3000), ..Track::new("c.wav") };
        assert_eq!(effective_loop(&t, &loop_tags(&w), 4410), (100, Some(3000)), "the entry's own first");
        assert_eq!(effective_loop(&Track::new("c.wav"), &LoopTags::default(), 4410), (0, None));
        let _ = std::fs::remove_dir_all(d);
    }

    #[test]
    fn decode_and_seam() {
        let d = scratch("decode");
        std::fs::create_dir_all(&d).unwrap();
        let f = d.join("t.wav");
        std::fs::write(&f, wav(22050, 2, &tone(22050, &[(1.0, 300.0), (2.0, 440.0)], 0.4))).unwrap();
        let a = decode(&f).unwrap();
        assert_eq!((a.rate, a.channels, a.frames()), (22050, 2, 66150));
        let s = seam(&a, 22050, Some(44100), 0.5);
        assert_eq!(s.len(), 2 * (11025 + 11025));
        assert_eq!(&s[..2 * 11025], &a.data[2 * 33075..2 * 44100]);
        assert_eq!(&s[2 * 11025..], &a.data[2 * 22050..2 * 33075]);
        let _ = std::fs::remove_dir_all(d);
    }

    /// The section as the editor writes it: songs, playlists, imported files; schema-valid; the
    /// file's other members and comments kept; a song dropped with its last track.
    #[test]
    fn music_section_written_and_schema_valid() {
        let d = scratch("music");
        let src = scratch("music-src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("Menu Theme!.WAV"), wav(44100, 2, &tone(44100, &[(0.5, 440.0)], 0.3))).unwrap();
        std::fs::write(src.join("b.wav"), wav(44100, 2, &tone(44100, &[(0.5, 660.0)], 0.3))).unwrap();
        std::fs::write(src.join("b.wav.loop.txt"), "LOOPSTART=10\n").unwrap();
        let mut p = ModProject::create(&d, "Music Mod", "").unwrap();
        let a = p.import_music(&src.join("Menu Theme!.WAV")).unwrap();
        assert_eq!(a, "music/Menu Theme_.wav");
        assert_eq!(p.import_music(&src.join("Menu Theme!.WAV")).unwrap(), a, "the same file again: the one there");
        let b = p.import_music(&src.join("b.wav")).unwrap();
        assert!(d.join("music/b.wav.loop.txt").is_file(), "its loop sidecar goes with it");
        assert!(p.import_music(&src.join("b.wav.loop.txt")).is_err());
        let menu = base_songs()[0].key();
        p.set_music(&menu, &[Track { loop_start: Some(1000), loop_end: Some(20000), ..Track::new(&a) }]);
        p.set_music("base://ch1_l2/world_0/song/0", &[Track::new(&a), Track { intro: Some(b.clone()), volume: Some(0.8), ..Track::new(&b) }]);
        p.set_music("base://world1/world_0/song/0", &[Track { once: true, ..Track::new(&b) }]);
        assert!(p.music_problems().is_empty(), "{:?}", p.music_problems());
        p.save().unwrap();
        let q = ModProject::open(&d).unwrap();
        assert_eq!(q.doc["music"][&menu], json!({"file": a, "loopStart": 1000, "loopEnd": 20000}));
        assert_eq!(q.doc["music"]["base://ch1_l2/world_0/song/0"], json!([a, {"file": b, "intro": b, "volume": 0.8}]));
        assert_eq!(q.music_tracks("base://ch1_l2/world_0").len(), 2, "found without its /song/0");
        let msgs = xdlrs::validate::MiniValidator::new(&schema()).errors(&q.doc);
        assert!(msgs.is_empty(), "{msgs:?}");
        // a hand-written key without /song/0 is edited in place, not doubled
        let mut q = q;
        q.doc["music"]["base://world2/world_0"] = json!("music/b.wav");
        q.set_music("base://world2/world_0/song/0", &[Track::new(&a)]);
        assert_eq!(q.doc["music"]["base://world2/world_0"], json!(a));
        assert!(q.doc["music"].get("base://world2/world_0/song/0").is_none());
        // dropping every song drops the section
        for (k, _) in q.music() {
            q.set_music(&k, &[]);
        }
        assert!(q.doc.get("music").is_none());
        assert_eq!(q.music_unused(), vec![a.clone(), b.clone()]);
        // problems: a missing file, a bad name, an unknown song, a backwards loop
        q.doc["music"] = json!({"base://nope/world_0": "music/gone.ogg", "base://world3/world_0": {"file": "x(1).ogg", "loopStart": 9, "loopEnd": 3}});
        let pr = q.music_problems();
        assert_eq!(pr.len(), 4, "{pr:?}");
        let _ = std::fs::remove_dir_all(d);
        let _ = std::fs::remove_dir_all(src);
    }
}
