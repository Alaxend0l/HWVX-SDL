//! Every piece of audio in a data folder, without the game: each archive's packs' sounds (HW_Cars,
//! HW_Items, HW_share, the worlds', the interfaces', the briefings' audio_ packs...), each song
//! (MIDI, played by the game's sequencer) and its instrument bank's samples, and the movies'
//! soundtracks (src/movie.rs). Read with xdlrs::modcheck::media, as the Content tab's sound list
//! reads them, for the Audio browser (src/audio_ui.rs): its search, ▶ (the decoded samples with
//! the system's player), length, rate, loop and WAV export.

use std::path::{Path, PathBuf};
use xdlrs::modcheck::media::{Archive, RawPack, TYPE_GEOMETRY, decode_sample, rd_u32, scan_song, stem};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    /// a pack's sound effect (sound/N)
    Sound,
    /// a song's instrument bank sample (instrument/N)
    Instrument,
    /// a pack's song (MIDI): the game's sequencer plays it
    Song,
    /// a movie's soundtrack
    Movie,
    /// a mod's own sound or music file (the Content tab's sounds and Soundtrack)
    ModFile,
}

impl Kind {
    pub fn name(self) -> &'static str {
        match self {
            Kind::Sound => "sound",
            Kind::Instrument => "instrument",
            Kind::Song => "song",
            Kind::Movie => "movie",
            Kind::ModFile => "mod file",
        }
    }
}

/// One piece of audio.
#[derive(Clone, Debug)]
pub struct Entry {
    pub kind: Kind,
    /// the archive's stem ("HW_Cars"), or the movie's file name
    pub archive: String,
    /// the pack ("car_5"; "" for a movie)
    pub pack: String,
    pub typ: u32,
    pub id: u32,
    pub index: u32,
    pub release: &'static str,
    pub rate: u32,
    /// samples (mono) as the game plays them; a song: its MIDI bytes
    pub samples: u32,
    pub loop_start: Option<u32>,
    pub file: PathBuf,
    /// the stored sample's bytes in the file
    pub offset: u64,
    pub bytes: u32,
    /// lowercase: what the search matches
    pub search: String,
}

impl Entry {
    /// The node id the viewer and base:// links use (sound/3, instrument/0, song).
    pub fn node(&self) -> String {
        match self.kind {
            Kind::Sound => format!("sound/{}", self.index),
            Kind::Instrument => format!("instrument/{}", self.index),
            Kind::Song => "song".into(),
            Kind::Movie | Kind::ModFile => String::new(),
        }
    }
    pub fn id(&self) -> String {
        if matches!(self.kind, Kind::Movie | Kind::ModFile) {
            return self.archive.clone();
        }
        format!("base://{}/{}/{}", self.archive, self.pack, self.node())
    }
    pub fn seconds(&self) -> f64 {
        if self.rate == 0 || self.kind == Kind::Song { 0.0 } else { self.samples as f64 / self.rate as f64 }
    }
    /// The samples (PCM16 mono), as the game plays them.
    pub fn decode(&self) -> Result<Vec<i16>, String> {
        if self.kind == Kind::ModFile {
            return crate::content_sounds::decode_file(&self.file).map(|(pcm, _)| pcm);
        }
        if matches!(self.kind, Kind::Song | Kind::Movie) {
            return Err(format!("{} isn't a sample", self.id()));
        }
        let raw = self.raw()?;
        decode_sample(self.release, &raw, &self.id())
    }
    /// The stored bytes (a song: its standard MIDI file).
    pub fn raw(&self) -> Result<Vec<u8>, String> {
        use std::io::{Read, Seek, SeekFrom};
        let mut f = std::fs::File::open(&self.file).map_err(|e| format!("{}: {e}", self.file.display()))?;
        let mut b = vec![0u8; self.bytes as usize];
        f.seek(SeekFrom::Start(self.offset)).and_then(|_| f.read_exact(&mut b)).map_err(|e| format!("{}: {e}", self.file.display()))?;
        Ok(b)
    }
}

/// The archives under a data folder (any case, a few levels down), sorted.
pub fn archive_files(data: &Path) -> Vec<PathBuf> {
    fn walk(d: &Path, depth: u32, out: &mut Vec<PathBuf>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                if depth > 0 {
                    walk(&p, depth - 1, out);
                }
            } else if p.extension().and_then(|x| x.to_str()).is_some_and(|x| ["xgc", "xpc", "xps"].contains(&x.to_ascii_lowercase().as_str())) {
                out.push(p);
            }
        }
    }
    let mut out = Vec::new();
    walk(data, 6, &mut out); // the PS2's worlds: XPS/ENGLISH/WORLDS_/SP_/CHAL1_/...
    out.sort_by_key(|p| p.to_string_lossy().to_lowercase());
    out
}

fn search_text(e: &Entry) -> String {
    format!("{} {} {} {} {}", e.archive, e.pack, e.node(), e.kind.name(), e.id()).to_lowercase()
}

/// An archive's audio. Err: an archive this reader doesn't take (the PS2 prototype's).
pub fn index_archive(path: &Path) -> Result<Vec<Entry>, String> {
    let bytes = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let arc = Archive::from_bytes(bytes, path)?;
    let name = stem(path);
    let mut out = Vec::new();
    let mut seen = std::collections::HashSet::new();
    for e in &arc.entries {
        if e.typ == TYPE_GEOMETRY || !seen.insert((e.typ, e.id)) {
            continue;
        }
        let Ok(pk) = RawPack::new(arc.release, e.typ, arc.pack_bytes(e), &e.name) else { continue };
        let base = e.start as u64;
        let mk = |kind, index, rate, samples, loop_start, off: usize, n: usize| {
            let mut en = Entry {
                kind,
                archive: name.clone(),
                pack: e.name.clone(),
                typ: e.typ,
                id: e.id,
                index,
                release: arc.release,
                rate,
                samples,
                loop_start,
                file: path.to_path_buf(),
                offset: base + off as u64,
                bytes: n as u32,
                search: String::new(),
            };
            en.search = search_text(&en);
            en
        };
        if let Some(&(s0, _)) = pk.sec.get("sound_data") {
            for i in 0..pk.hget("num_sounds") as usize {
                let o = s0 + pk.hget("sfx_offset") as usize + 0x20 * i;
                let (Ok(ptr), Ok(len), Ok(lp), Ok(rate)) = (rd_u32(pk.b, o, pk.big), rd_u32(pk.b, o + 4, pk.big), rd_u32(pk.b, o + 8, pk.big), rd_u32(pk.b, o + 12, pk.big)) else { break };
                let samples = sample_count(arc.release, len);
                let lp = (lp != u32::MAX && lp < samples).then_some(lp);
                out.push(mk(Kind::Sound, i as u32, rate, samples, lp, s0 + ptr as usize, len as usize));
            }
        }
        if let Ok((song, bank)) = scan_song(&pk) {
            if let Some((at, n)) = song {
                out.push(mk(Kind::Song, 0, 0, n.max(0) as u32, None, at, n.max(0) as usize));
            }
            if let Some((ents, data)) = bank {
                let bo = data - (0x20 + 0x20 * ents.len());
                for b in &ents {
                    let q = bo + 0x20 + 0x20 * b.k;
                    let lp = rd_u32(pk.b, q + 8, pk.big).unwrap_or(u32::MAX);
                    let samples = sample_count(arc.release, b.len as u32);
                    let lp = (lp != u32::MAX && lp < samples).then_some(lp);
                    out.push(mk(Kind::Instrument, b.k as u32, b.rate, samples, lp, data + b.off, b.len));
                }
            }
        }
    }
    Ok(out)
}

/// Samples (mono) of `bytes` stored: PCM16, or the PS2's SPU-ADPCM (28 a 16-byte block, less
/// its silent first block).
fn sample_count(release: &str, bytes: u32) -> u32 {
    if release == "ps2" { (bytes / 16).saturating_sub(1) * 28 } else { bytes / 2 }
}

/// A movie's soundtrack as an entry (its length from the movie's description).
pub fn movie_entry(m: &crate::movie::MovieFile, info: &crate::movie::Info) -> Entry {
    let mut e = Entry {
        kind: Kind::Movie,
        archive: m.name.clone(),
        pack: String::new(),
        typ: 0,
        id: 0,
        index: 0,
        release: "",
        rate: info.rate,
        samples: (info.seconds * info.rate as f64) as u32,
        loop_start: None,
        file: m.path.clone(),
        offset: 0,
        bytes: 0,
        search: String::new(),
    };
    e.search = format!("{} movie {}", m.name, m.kind.name()).to_lowercase();
    e
}

/// Entries matching a search: every word in the entry's text, and the kind's filter.
pub fn matches(e: &Entry, words: &str, kinds: &[Kind]) -> bool {
    (kinds.is_empty() || kinds.contains(&e.kind)) && words.to_lowercase().split_whitespace().all(|w| e.search.contains(w))
}

/// Every archive's audio, in turn, with each archive's result (`progress` is called after each).
pub fn index_all(data: &Path, mut progress: impl FnMut(&Path, Result<usize, String>)) -> Vec<Entry> {
    let mut all = Vec::new();
    for f in archive_files(data) {
        match index_archive(&f) {
            Ok(v) => {
                progress(&f, Ok(v.len()));
                all.extend(v);
            }
            Err(e) => progress(&f, Err(e)),
        }
    }
    all
}

/// A mod file's (rate, samples), decoded once per version of it (each re-index decoded every
/// file whole again).
fn mod_file_length(p: &Path) -> (u32, u32) {
    type Seen = std::collections::HashMap<PathBuf, (Option<std::time::SystemTime>, u64, (u32, u32))>;
    static SEEN: std::sync::Mutex<Option<Seen>> = std::sync::Mutex::new(None);
    let stamp = std::fs::metadata(p).map(|m| (m.modified().ok(), m.len())).unwrap_or((None, 0));
    let mut seen = SEEN.lock().unwrap_or_else(|e| e.into_inner());
    let seen = seen.get_or_insert_with(Default::default);
    if let Some(&(t, n, v)) = seen.get(p) {
        if (t, n) == stamp {
            return v;
        }
    }
    let v = crate::content_sounds::decode_file(p).map(|(pcm, r)| (r, pcm.len() as u32)).unwrap_or((0, 0));
    seen.insert(p.to_path_buf(), (stamp.0, stamp.1, v));
    v
}

/// A mod's own audio files (WAV, FLAC, Ogg Vorbis, MP3 anywhere in its folder: sounds/, music/),
/// decoded as the game decodes them (src/audiodec, xdlrs::modcheck::audio) for their rate and
/// length.
pub fn mod_files(dir: &Path) -> Vec<Entry> {
    fn walk(root: &Path, d: &Path, depth: u32, out: &mut Vec<Entry>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        let mut ents: Vec<_> = rd.flatten().map(|e| e.path()).collect();
        ents.sort();
        for p in ents {
            if p.is_dir() {
                if depth > 0 {
                    walk(root, &p, depth - 1, out);
                }
                continue;
            }
            let ext = p.extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_default();
            if !crate::content_sounds::EXTS.contains(&ext.as_str()) {
                continue;
            }
            let name = p.strip_prefix(root).unwrap_or(&p).to_string_lossy().replace('\\', "/");
            let (rate, samples) = mod_file_length(&p);
            let mut e = Entry {
                kind: Kind::ModFile,
                archive: name.clone(),
                pack: String::new(),
                typ: 0,
                id: 0,
                index: 0,
                release: "",
                rate,
                samples,
                loop_start: None,
                file: p.clone(),
                offset: 0,
                bytes: 0,
                search: String::new(),
            };
            e.search = format!("{name} mod file").to_lowercase();
            out.push(e);
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, 3, &mut out);
    out
}

/// The viewer's pack (its `packs` listing: archive, type, id) an entry is in: the archive matched
/// by its file's stem (any case, any folder the viewer names it with), the type by name.
pub fn viewer_pack<'a>(e: &Entry, packs: impl IntoIterator<Item = (&'a str, &'a str, u64)>) -> Option<(&'a str, &'a str, u64)> {
    let tn = xdlrs::modcheck::media::TYPE_NAMES.get(e.typ as usize).copied().unwrap_or("");
    packs.into_iter().find(|(archive, typ, id)| {
        let arch = archive.rsplit(['/', '\\']).next().unwrap_or(archive);
        let arch = arch.rsplit_once('.').map(|(s, _)| s).unwrap_or(arch);
        arch.eq_ignore_ascii_case(&e.archive) && *typ == tn && *id == e.id as u64
    })
}
