//! Sound replacements in a content project (the Content tab's "Sounds"): the mod's own sound files
//! (WAV, FLAC, Ogg Vorbis, MP3, copied into its sounds/ folder) over base sounds. A new car's own
//! sounds are its entry's "sounds" ({"sound/3": "sounds/horn.ogg"}: its own pack only); any other
//! base sound is the mod's "sounds" ({"base://HW_Cars/car_7/sound/3": ...}; schemas/Mod.schema.json,
//! src/game/Port_Mods.c). The game decodes the file as the pack loads, mixes it to mono and
//! resamples it to the base sound's rate.
//!
//! The base sounds are read here from the player's own archives (xdlrs::modcheck::media), with
//! what a car pack plays each of (CarAInfo: wind, engine, rolling, gear shifts; CarA.c CarA_Init,
//! CarA_HW.c), for the list and its ▶ preview; nothing of them goes into the mod.

use crate::content::{Kind, ModProject};
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use xdlrs::modcheck::media::{Archive, RawPack, TYPE_NAMES, decode_sample, rd_u32};

/// Where the picked files go in the mod.
pub const DIR: &str = "sounds";
/// What the game decodes (src/audiodec).
pub const EXTS: [&str; 4] = ["wav", "flac", "ogg", "mp3"];

/// One sound of a base pack.
#[derive(Clone, Debug, PartialEq)]
pub struct BaseSound {
    pub index: u32,
    pub rate: u32,
    /// samples (mono) as the game plays them
    pub samples: u32,
    pub looped: bool,
    /// what plays it ("" when the pack's data doesn't say)
    pub role: String,
}

impl BaseSound {
    pub fn seconds(&self) -> f64 {
        if self.rate == 0 { 0.0 } else { self.samples as f64 / self.rate as f64 }
    }
}

/// A pack of an archive that has sounds: (type, id, "car_5", how many).
#[derive(Clone, Debug, PartialEq)]
pub struct SoundPack {
    pub typ: u32,
    pub id: u32,
    pub name: String,
    pub count: u32,
}

/// An archive of the player's data, read once for its packs' sounds.
pub struct SoundArchive {
    /// its stem ("HW_Cars"), as base:// ids name it
    pub name: String,
    pub release: &'static str,
    arc: Archive,
}

/// The archives a data folder has (their stems), for the general list.
pub fn archives(data: &Path) -> Vec<String> {
    let mut out = vec![];
    fn walk(d: &Path, depth: u32, out: &mut Vec<String>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                if depth > 0 {
                    walk(&p, depth - 1, out);
                }
            } else if p.extension().and_then(|x| x.to_str()).is_some_and(|x| ["xgc", "xpc", "xps"].contains(&x.to_ascii_lowercase().as_str())) {
                out.push(xdlrs::modcheck::media::stem(&p));
            }
        }
    }
    walk(data, 2, &mut out);
    out.sort_by_key(|s| s.to_lowercase());
    out.dedup();
    out
}

impl SoundArchive {
    /// The archive `name` (any case) of the data folder.
    pub fn find(data: &Path, name: &str) -> Result<SoundArchive, String> {
        let p = xdlrs::cars::find_archive(data, name).ok_or_else(|| format!("{name}: not in {}", data.display()))?;
        SoundArchive::open(&p)
    }

    pub fn open(path: &Path) -> Result<SoundArchive, String> {
        let bytes = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let arc = Archive::from_bytes(bytes, path)?;
        Ok(SoundArchive { name: arc.name.clone(), release: arc.release, arc })
    }

    fn raw(&self, typ: u32, id: u32) -> Result<RawPack<'_>, String> {
        let e = self.arc.entries.iter().find(|e| e.typ == typ && e.id == id).ok_or_else(|| format!("{}: no pack {}", self.name, pack_name(typ, id)))?;
        RawPack::new(self.arc.release, e.typ, self.arc.pack_bytes(e), &e.name)
    }

    /// The packs with sounds, in the archive's order.
    pub fn packs(&self) -> Vec<SoundPack> {
        let mut out: Vec<SoundPack> = self
            .arc
            .entries
            .iter()
            .filter(|e| e.typ != xdlrs::modcheck::media::TYPE_GEOMETRY)
            .filter_map(|e| {
                let pk = RawPack::new(self.arc.release, e.typ, self.arc.pack_bytes(e), &e.name).ok()?;
                let n = pk.hget("num_sounds");
                (n > 0).then(|| SoundPack { typ: e.typ, id: e.id, name: pack_name(e.typ, e.id), count: n })
            })
            .collect();
        out.dedup_by(|a, b| a.typ == b.typ && a.id == b.id);
        out
    }

    /// A pack's sounds, with what plays them where the pack's data says (car packs).
    pub fn sounds(&self, typ: u32, id: u32) -> Result<Vec<BaseSound>, String> {
        let pk = self.raw(typ, id)?;
        let roles = if typ == 0 { car_roles(&pk) } else { BTreeMap::new() };
        let n = pk.hget("num_sounds");
        let Some(&(s0, _)) = pk.sec.get("sound_data") else { return Ok(vec![]) };
        let mut out = vec![];
        for i in 0..n {
            let o = s0 + pk.hget("sfx_offset") as usize + 0x20 * i as usize;
            let len = rd_u32(pk.b, o + 4, pk.big)?;
            let lp = rd_u32(pk.b, o + 8, pk.big)?;
            let rate = rd_u32(pk.b, o + 12, pk.big)?;
            // PCM16 on GameCube and PC; the PS2's SPU-ADPCM: 28 samples a 16-byte block, less its silent first block
            let samples = if pk.release == "ps2" { (len / 16).saturating_sub(1) * 28 } else { len / 2 };
            out.push(BaseSound { index: i, rate, samples, looped: lp != 0xFFFF_FFFF, role: roles.get(&i).cloned().unwrap_or_default() });
        }
        Ok(out)
    }

    /// A sound's samples (PCM16 mono) and rate, as the game plays them.
    pub fn pcm(&self, typ: u32, id: u32, index: u32) -> Result<(Vec<i16>, u32), String> {
        let pk = self.raw(typ, id)?;
        let s = xdlrs::modcheck::media::scan_sounds(&pk)?;
        let s = s.get(index as usize).ok_or_else(|| format!("{}: no sound/{index}", pk.name))?;
        let raw = xdlrs::modcheck::media::slice(pk.b, s.ptr, (s.ptr + s.len) as i64);
        Ok((decode_sample(pk.release, raw, &format!("{}/sound/{index}", pk.name))?, s.rate))
    }
}

/// "car_5" for type 0 and id 5 (the archive's names, base:// ids' pack part).
pub fn pack_name(typ: u32, id: u32) -> String {
    format!("{}_{id}", TYPE_NAMES.get(typ as usize).copied().unwrap_or("type"))
}

/// What a car pack plays each of its own sounds as (sound index -> role), from its CarAInfo
/// (root 0; offsets of CarA.h): the wind (gears[1].soundSet), the engine (soundSets[1]; the
/// others aren't played), the rolling sound (rollSound) and the gear shifts (shiftSounds). A
/// sound the car's data refers to elsewhere is "used by the car" (its data offset); a sound in a
/// shared pack (the reference's high half) isn't this pack's.
pub fn car_roles(pk: &RawPack) -> BTreeMap<u32, String> {
    let ptrs: std::collections::BTreeSet<u32> = pk.words("pointers").into_iter().collect();
    let d = pk.section("data");
    let u32_at = |o: u32| rd_u32(d, o as usize, pk.big).ok();
    let ptr = |o: u32| if ptrs.contains(&o) { u32_at(o) } else { None };
    let relocs: BTreeMap<u32, u32> = pk.words("sound_relocs").chunks_exact(2).map(|c| (c[0], c[1])).collect();
    let mut by_off: BTreeMap<u32, String> = BTreeMap::new();
    if let Some(info) = ptr(4).and_then(ptr) {
        // wind: gears[1].soundSet, a CarASoundSet whose sample is at 0x14
        if u32_at(info + 0xD8).is_some_and(|n| n >= 2)
            && let Some(set) = ptr(info + 0xDC).and_then(|g| ptr(g + 8 + 4))
        {
            by_off.insert(set + 0x14, "wind (loop)".into());
        }
        if let Some(sets) = ptr(info + 0x11C) {
            let n = u32_at(info + 0x118).unwrap_or(0).min(16);
            for i in 0..n {
                if let Some(set) = ptr(sets + 4 * i) {
                    by_off.insert(set + 0x14, if i == 1 { "engine (loop, pitched by rpm)".into() } else { format!("engine set {i} (not played)") });
                }
            }
        }
        if let Some(set) = ptr(info + 0x60) {
            by_off.insert(set + 0x14, "rolling (loop, on a surface)".into());
        }
        // sound sets the game never reads (CarAInfo pad58, pad64)
        for o in [0x58, 0x5C, 0x64, 0x68] {
            if let Some(set) = ptr(info + o) {
                by_off.entry(set + 0x14).or_insert_with(|| "not played (an unused sound set)".into());
            }
        }
        if let Some(arr) = ptr(info + 0x114) {
            let n = u32_at(info + 0x110).unwrap_or(0).min(16);
            for i in 0..n {
                by_off.insert(arr + 4 * i, "gear shift".into());
            }
        }
    }
    let mut out: BTreeMap<u32, String> = BTreeMap::new();
    for (off, v) in &relocs {
        if v >> 16 != 0 {
            continue; // a shared pack's sound
        }
        let role = by_off.get(off).cloned().unwrap_or_else(|| format!("used by the car (0x{off:X})"));
        let e = out.entry(v & 0xFFFF).or_default();
        if !e.split(", ").any(|r| r == role) {
            if !e.is_empty() {
                e.push_str(", ");
            }
            e.push_str(&role);
        }
    }
    out
}

/// A sound file decoded as the game decodes it (src/audiodec): PCM16 mono, its rate, its frames.
pub fn decode_file(path: &Path) -> Result<(Vec<i16>, u32), String> {
    let pcm = xdlrs::modcheck::audio::decode_audio(path, &path.display().to_string())?;
    Ok((pcm.mono(), pcm.rate))
}

/// A base sound's id in a mod's "sounds": base://HW_Cars/car_7/sound/3.
pub fn base_key(archive: &str, pack: &str, index: u32) -> String {
    format!("base://{archive}/{pack}/sound/{index}")
}

/// (archive, pack, index) of a base:// sound id (the release-only form "sound/gc:3" too: its index).
pub fn parse_key(key: &str) -> Option<(String, String, u32)> {
    let rest = key.strip_prefix("base://")?;
    let (archive, rest) = rest.split_once('/')?;
    let (pack, rest) = rest.split_once('/')?;
    let i = rest.strip_prefix("sound/")?;
    let i = i.rsplit(':').next()?;
    Some((archive.to_string(), pack.to_string(), i.parse().ok()?))
}

/// The file of a sound's value (a path, or {"file", "loop"}).
fn file_of(v: &Value) -> Option<String> {
    match v {
        Value::String(s) => Some(s.clone()),
        Value::Object(o) => o.get("file").and_then(Value::as_str).map(String::from),
        _ => None,
    }
}

fn set_in(m: &mut Map<String, Value>, key: &str, file: Option<&str>) {
    match file {
        Some(f) => {
            m.insert(key.to_string(), json!(f));
        }
        None => {
            m.remove(key);
        }
    }
}

impl ModProject {
    /// A new car's (or weapon's) own replacement for its sound `index`: the mod's file.
    pub fn entry_sound(&self, kind: Kind, uid: &str, index: u32) -> Option<String> {
        file_of(&self.doc[kind.section()][uid]["sounds"][format!("sound/{index}")])
    }

    /// Sets (Some: a file of the mod) or drops (None) an entry's own replacement; an empty
    /// "sounds" goes with it.
    pub fn set_entry_sound(&mut self, kind: Kind, uid: &str, index: u32, file: Option<&str>) {
        let Some(e) = self.entry_mut(kind, uid) else { return };
        let s = e.entry("sounds").or_insert_with(|| json!({}));
        if let Some(m) = s.as_object_mut() {
            set_in(m, &format!("sound/{index}"), file);
            if m.is_empty() {
                e.remove("sounds");
            }
        }
    }

    /// The mod's replacements of base sounds: (id, file), in the file's order.
    pub fn base_sounds(&self) -> Vec<(String, String)> {
        self.doc["sounds"].as_object().map(|m| m.iter().filter_map(|(k, v)| Some((k.clone(), file_of(v)?))).collect()).unwrap_or_default()
    }

    pub fn base_sound(&self, key: &str) -> Option<String> {
        file_of(&self.doc["sounds"][key])
    }

    /// Sets or drops a base sound's replacement (key: base_key); an empty "sounds" goes.
    pub fn set_base_sound(&mut self, key: &str, file: Option<&str>) {
        let Some(d) = self.doc.as_object_mut() else { return };
        let s = d.entry("sounds").or_insert_with(|| json!({}));
        if let Some(m) = s.as_object_mut() {
            set_in(m, key, file);
            if m.is_empty() {
                d.remove("sounds");
            }
        }
    }

    /// Every sound file the mod refers to (its "sounds" and its entries').
    pub fn sound_files(&self) -> Vec<String> {
        let mut out: Vec<String> = self.base_sounds().into_iter().map(|x| x.1).collect();
        for kind in [Kind::Car, Kind::Weapon] {
            for (_, e) in self.entries(kind) {
                out.extend(e["sounds"].as_object().into_iter().flatten().filter_map(|(_, v)| file_of(v)));
            }
        }
        out.sort();
        out.dedup();
        out
    }

    /// Files in sounds/ nothing refers to any more (removing them keeps them out of the package).
    pub fn unused_sound_files(&self) -> Vec<String> {
        let used = self.sound_files();
        let mut out: Vec<String> = std::fs::read_dir(self.dir.join(DIR))
            .into_iter()
            .flatten()
            .flatten()
            .filter(|e| e.path().is_file())
            .map(|e| format!("{DIR}/{}", e.file_name().to_string_lossy()))
            .filter(|f| !used.contains(f))
            .collect();
        out.sort();
        out
    }

    /// The mod's own copy of a sound file the modder picked: sounds/<its name> (another name
    /// when one of other content is there). Ok: the path in the mod.
    pub fn import_sound(&self, src: &Path) -> Result<String, String> {
        let ext = src.extension().and_then(|x| x.to_str()).map(str::to_ascii_lowercase).unwrap_or_default();
        if !EXTS.contains(&ext.as_str()) {
            return Err(format!("{}: the game plays WAV, FLAC, Ogg Vorbis and MP3 files", src.display()));
        }
        let bytes = std::fs::read(src).map_err(|e| format!("{}: {e}", src.display()))?;
        let stem: String = src
            .file_stem()
            .map(|s| s.to_string_lossy().chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' }).collect())
            .unwrap_or_default();
        let stem = if stem.is_empty() { "sound".to_string() } else { stem };
        let dir = self.dir.join(DIR);
        std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        for n in 0..1000 {
            let name = if n == 0 { format!("{stem}.{ext}") } else { format!("{stem}-{n}.{ext}") };
            let to: PathBuf = dir.join(&name);
            match std::fs::read(&to) {
                Ok(b) if b == bytes => return Ok(format!("{DIR}/{name}")),
                Ok(_) => continue,
                Err(_) => {
                    std::fs::write(&to, &bytes).map_err(|e| format!("{}: {e}", to.display()))?;
                    return Ok(format!("{DIR}/{name}"));
                }
            }
        }
        Err(format!("{}: too many files named {stem}", dir.display()))
    }
}

/// A sound file, in the system's dialog (Ok(None): cancelled). The prompt reaches the dialog as
/// an argument or an environment variable, never inside a script's text.
pub fn choose_sound(prompt: &str) -> Result<Option<String>, String> {
    use std::process::Command;
    let on_path = |name: &str| std::env::var_os("PATH").is_some_and(|p| std::env::split_paths(&p).any(|d| d.join(name).is_file()));
    let mut cmd = if cfg!(target_os = "macos") {
        let mut c = Command::new("osascript");
        c.args(["-e", "on run argv", "-e", "POSIX path of (choose file with prompt (item 1 of argv))", "-e", "end run", prompt]);
        c
    } else if cfg!(windows) {
        let mut c = xdlrs::command("powershell");
        c.env("HWVX_PROMPT", prompt).args([
            "-NoProfile",
            "-Command",
            "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = $env:HWVX_PROMPT; $d.Filter = 'Sound (*.wav;*.flac;*.ogg;*.mp3)|*.wav;*.flac;*.ogg;*.mp3|All files (*.*)|*.*'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }",
        ]);
        c
    } else if on_path("zenity") {
        let mut c = Command::new("zenity");
        c.args(["--file-selection", &format!("--title={prompt}"), "--file-filter=Sound | *.wav *.flac *.ogg *.mp3 *.WAV *.FLAC *.OGG *.MP3", "--file-filter=All files | *"]);
        c
    } else if on_path("kdialog") {
        let mut c = Command::new("kdialog");
        c.args(["--title", prompt, "--getopenfilename", ".", "*.wav *.flac *.ogg *.mp3|Sound"]);
        c
    } else {
        return Err("no file dialog here (install zenity or kdialog): type the path instead".into());
    };
    let out = cmd.output().map_err(|e| e.to_string())?;
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    Ok((out.status.success() && !s.is_empty()).then_some(s))
}

/// A test tone as a WAV file's bytes (PCM16 mono): `hz` for `seconds` at `rate`, never game audio.
pub fn tone_wav(hz: f64, seconds: f64, rate: u32) -> Vec<u8> {
    let n = (seconds * rate as f64) as usize;
    let pcm: Vec<i16> = (0..n).map(|i| (12000.0 * (2.0 * std::f64::consts::PI * hz * i as f64 / rate as f64).sin()) as i16).collect();
    crate::media::wav_bytes(rate, &pcm)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("hwvx-content-sounds-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    fn schema_errors(p: &ModProject) -> Vec<String> {
        let schema: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../schemas/Mod.schema.json")).unwrap()).unwrap();
        xdlrs::validate::MiniValidator::new(&schema).errors(&ModProject::open(&p.dir).unwrap().doc)
    }

    #[test]
    fn keys() {
        assert_eq!(base_key("HW_Cars", "car_7", 3), "base://HW_Cars/car_7/sound/3");
        assert_eq!(parse_key("base://HW_Cars/car_7/sound/3"), Some(("HW_Cars".into(), "car_7".into(), 3)));
        assert_eq!(parse_key("base://HW_share/share_0/sound/gc:12"), Some(("HW_share".into(), "share_0".into(), 12)));
        assert_eq!(parse_key("base://HW_Cars/car_7/texture/3"), None);
        assert_eq!(pack_name(0, 5), "car_5");
        assert_eq!(pack_name(8, 0), "share_0");
    }

    /// A tone imported, set on a new car and on a base sound, saved, reopened, schema-valid;
    /// dropping them leaves no empty "sounds".
    #[test]
    fn replacements_roundtrip_and_match_the_schema() {
        let d = scratch("roundtrip");
        let src = scratch("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("My Horn!.wav"), tone_wav(880.0, 0.5, 22050)).unwrap();
        std::fs::write(src.join("notes.txt"), b"x").unwrap();
        let mut p = ModProject::create(&d, "Sound Mod", "").unwrap();
        let car = p.add(Kind::Car, 7, "Loud");
        assert!(p.import_sound(&src.join("notes.txt")).is_err(), "not audio");
        let f = p.import_sound(&src.join("My Horn!.wav")).unwrap();
        assert_eq!(f, "sounds/My_Horn_.wav");
        assert_eq!(p.import_sound(&src.join("My Horn!.wav")).unwrap(), f, "the same file again: the same copy");
        std::fs::write(src.join("My Horn!.wav"), tone_wav(440.0, 0.5, 22050)).unwrap();
        assert_eq!(p.import_sound(&src.join("My Horn!.wav")).unwrap(), "sounds/My_Horn_-1.wav", "another file of that name");
        p.set_entry_sound(Kind::Car, &car, 1, Some(&f));
        let key = base_key("HW_share", "share_0", 4);
        p.set_base_sound(&key, Some("sounds/My_Horn_-1.wav"));
        p.save().unwrap();
        assert!(schema_errors(&p).is_empty(), "{:?}", schema_errors(&p));
        let q = ModProject::open(&d).unwrap();
        assert_eq!(q.doc["cars"][&car]["sounds"]["sound/1"], json!(f));
        assert_eq!(q.entry_sound(Kind::Car, &car, 1), Some(f.clone()));
        assert_eq!(q.entry_sound(Kind::Car, &car, 2), None);
        assert_eq!(q.base_sounds(), vec![(key.clone(), "sounds/My_Horn_-1.wav".to_string())]);
        assert!(q.unused_sound_files().is_empty());
        let (pcm, rate) = decode_file(&d.join(&f)).unwrap();
        assert_eq!((pcm.len(), rate), (11025, 22050), "decoded as the game decodes it");
        let mut q = q;
        q.set_entry_sound(Kind::Car, &car, 1, None);
        q.set_base_sound(&key, None);
        q.save().unwrap();
        let r = ModProject::open(&d).unwrap();
        assert!(r.doc["cars"][&car].get("sounds").is_none() && r.doc.get("sounds").is_none(), "empty sections go");
        assert_eq!(r.unused_sound_files(), vec!["sounds/My_Horn_-1.wav".to_string(), f.clone()]);
        let _ = std::fs::remove_dir_all(d);
        let _ = std::fs::remove_dir_all(src);
    }

    /// The packer's check (xdlrs modcheck, as Save as mod runs it) takes an entry's own sounds.
    #[test]
    fn the_packer_reads_entry_sounds() {
        let d = scratch("manifest");
        let src = scratch("manifest-src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("tone.wav"), tone_wav(1000.0, 0.25, 32000)).unwrap();
        let mut p = ModProject::create(&d, "Pack Mod", "").unwrap();
        let car = p.add(Kind::Car, 3, "C");
        let f = p.import_sound(&src.join("tone.wav")).unwrap();
        p.set_entry_sound(Kind::Car, &car, 0, Some(&f));
        p.save().unwrap();
        let m = xdlrs::modcheck::manifest::Manifest::load(&d).unwrap();
        assert!(m.errors.is_empty(), "{:?}", m.errors);
        assert_eq!(m.sounds.len(), 1);
        assert_eq!(m.sounds[0].1, "base://HW_Cars/car_3/sound/0");
        let _ = std::fs::remove_dir_all(d);
        let _ = std::fs::remove_dir_all(src);
    }

    /// The mod of the game run (docs-content-sounds.md, "Testing"): HWVX_SOUND_TEST_MODS=DIR
    /// writes DIR/soundtest, a new car made from car 7 whose engine (sound/1) is a loud 2 kHz tone.
    #[test]
    fn game_run_mod() {
        let Some(out) = std::env::var_os("HWVX_SOUND_TEST_MODS") else { return };
        let d = Path::new(&out).join("soundtest");
        let _ = std::fs::remove_dir_all(&d);
        let src = scratch("game-run-src");
        std::fs::create_dir_all(&src).unwrap();
        let rate = 22050;
        let n = rate as usize;
        let pcm: Vec<i16> = (0..n).map(|i| (30000.0 * (2.0 * std::f64::consts::PI * 2000.0 * i as f64 / rate as f64).sin()) as i16).collect();
        std::fs::write(src.join("tone.wav"), crate::media::wav_bytes(rate, &pcm)).unwrap();
        let mut p = ModProject::create(&d, "Sound test", "").unwrap();
        let uid = p.add(Kind::Car, 7, "Tone Car");
        let f = p.import_sound(&src.join("tone.wav")).unwrap();
        p.set_entry_sound(Kind::Car, &uid, 1, Some(&f));
        p.save().unwrap();
        assert!(schema_errors(&p).is_empty());
        std::fs::write(Path::new(&out).join("soundtest.uid"), &uid).unwrap();
        eprintln!("{} {uid}", d.display());
        let _ = std::fs::remove_dir_all(src);
    }

    /// Save as mod's own call (content_ui.rs content_pack: the no-game-data check and packer)
    /// on a mod with a new car's sound and a base sound, both tones. HWVX_TEST_DATA and
    /// HWVX_TEST_OUT (a folder with room for the data's index, about 100 MB) set it going.
    #[test]
    fn save_as_mod_packs_sounds() {
        let (Some(data), Some(out)) = (std::env::var_os("HWVX_TEST_DATA"), std::env::var_os("HWVX_TEST_OUT")) else { return };
        let out = Path::new(&out);
        let d = out.join("pack-test-mod");
        let _ = std::fs::remove_dir_all(&d);
        let src = scratch("pack-src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("engine.wav"), tone_wav(300.0, 1.0, 22050)).unwrap();
        std::fs::write(src.join("horn.wav"), tone_wav(1200.0, 0.4, 22050)).unwrap();
        let mut p = ModProject::create(&d, "Pack Sounds", "").unwrap();
        let car = p.add(Kind::Car, 7, "Hum");
        let f = p.import_sound(&src.join("engine.wav")).unwrap();
        p.set_entry_sound(Kind::Car, &car, 1, Some(&f));
        let g = p.import_sound(&src.join("horn.wav")).unwrap();
        p.set_base_sound(&base_key("HW_share", "share_1", 3), Some(&g));
        p.save().unwrap();
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let xmod = out.join("pack-test.xmod");
        let line = xdlrs::level_share::check_and_pack(&root, &d, &xmod, None, Some(Path::new(&data)), &out.join("base-cache")).map_err(|e| xdlrs::level::top_message(&e)).unwrap();
        eprintln!("{line}");
        assert!(line.contains("3 files") && line.contains("no game data found"), "{line}");
        let _ = std::fs::remove_dir_all(src);
    }

    /// With the GameCube's data (HWVX_TEST_DATA) and the PC's or the PS2's (HWVX_TEST_PC_DATA,
    /// HWVX_TEST_PS2_DATA; each skipped without): a car sound's index means the same on every
    /// release. Every car pack has as many sounds on each, and each index plays the same role
    /// (CarAInfo: wind, engine, rolling, gear shifts, ...), so `sound/N` (the GameCube's N)
    /// replaces the same sound everywhere. Lengths may differ (the releases' own recordings).
    #[test]
    fn car_sound_roles_per_release() {
        let Some(gc) = std::env::var_os("HWVX_TEST_DATA") else { return };
        let gc = SoundArchive::find(Path::new(&gc), "HW_Cars").unwrap();
        // "used by the car (0x99434)": its data offset differs by release, not what it is
        let role = |r: &str| r.split(", ").map(|x| x.split(" (0x").next().unwrap_or(x)).collect::<Vec<_>>().join(", ");
        let roles = |a: &SoundArchive, id: u32| a.sounds(0, id).unwrap().into_iter().map(|s| (s.index, role(&s.role), s.looped)).collect::<Vec<_>>();
        let cars: Vec<u32> = gc.packs().iter().filter(|p| p.typ == 0).map(|p| p.id).collect();
        for var in ["HWVX_TEST_PC_DATA", "HWVX_TEST_PS2_DATA"] {
            let Some(data) = std::env::var_os(var) else {
                eprintln!("{var} not set: skipped");
                continue;
            };
            let other = SoundArchive::find(Path::new(&data), "HW_Cars").unwrap();
            let theirs: Vec<u32> = other.packs().iter().filter(|p| p.typ == 0).map(|p| p.id).collect();
            assert_eq!(theirs, cars, "{var}: the same car packs have sounds");
            let mut sounds = 0;
            for &id in &cars {
                let (a, b) = (roles(&gc, id), roles(&other, id));
                eprintln!("{var} car_{id}: {} sounds; GameCube {:?}", b.len(), a);
                assert_eq!(b, a, "{var}: car_{id}'s sounds by index: their roles");
                sounds += a.len();
            }
            eprintln!("{var}: {} car packs, {sounds} sounds, every index the same role", cars.len());
            // the shared sounds' packs: as many sounds each (HW_share: 15; HW_Items: 15 packs)
            for name in ["HW_share", "HW_Items"] {
                let a: Vec<(String, u32)> = SoundArchive::find(Path::new(&std::env::var_os("HWVX_TEST_DATA").unwrap()), name).unwrap().packs().into_iter().map(|p| (p.name, p.count)).collect();
                let b: Vec<(String, u32)> = SoundArchive::find(Path::new(&data), name).unwrap().packs().into_iter().map(|p| (p.name, p.count)).collect();
                assert_eq!(b, a, "{var} {name}: its packs' sound counts");
            }
        }
    }

    /// With the GameCube data (HWVX_TEST_DATA): a car pack's sounds and what plays them.
    #[test]
    fn car_sounds_of_the_data() {
        let Some(data) = std::env::var_os("HWVX_TEST_DATA") else { return };
        let a = SoundArchive::find(Path::new(&data), "HW_Cars").unwrap();
        let cars: Vec<u32> = a.packs().iter().filter(|p| p.typ == 0).map(|p| p.id).collect();
        assert!(cars.len() >= 33, "{cars:?}");
        for id in cars {
            let s = a.sounds(0, id).unwrap();
            for x in &s {
                eprintln!("car_{id} sound/{}: {} Hz {:.2} s{} {}", x.index, x.rate, x.seconds(), if x.looped { " loop" } else { "" }, x.role);
            }
            assert!(s.iter().any(|x| x.role.contains("engine (loop")), "car_{id}: an engine");
            let (pcm, rate) = a.pcm(0, id, 0).unwrap();
            assert_eq!(pcm.len() as u32, s[0].samples);
            assert_eq!(rate, s[0].rate);
        }
        let packs = a.packs();
        assert!(packs.iter().any(|p| p.name == "car_7"));
        let share = SoundArchive::find(Path::new(&data), "HW_share").unwrap();
        for p in share.packs() {
            eprintln!("HW_share {} {} sounds", p.name, p.count);
        }
        assert!(archives(Path::new(&data)).iter().any(|a| a == "HW_share"));
    }
}
