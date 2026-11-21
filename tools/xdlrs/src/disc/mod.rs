//! The disc ripper: Velocity X's game data from the disc a player owns, any release, read only.
//!
//! [`Disc::open`] takes a path and tells what it is ([`Kind`]) and which release ([`Release`]),
//! then lists the game's files as the port's launcher imports them, and reads or extracts them,
//! streaming from the image (images are 1.4 GB; nothing reads one whole):
//!
//! | what | release | the files (paths as extracted) |
//! |---|---|---|
//! | GameCube .iso/.gcm (NKit too), Dolphin .gcz | gc | the disc's file table: `HW_share.xgc`, `worlds_/...` (Dolphin's "Extract Entire Disc" files/) |
//! | PS2 DVD .iso | ps2 (archive version 183), ps2beta (177/179) | `XPS/ENGLISH/...` (pcdisc.c Port_ImportPS2Disc) |
//! | PC CD .iso, .cue/.bin (MODE1/2352), or the CD as a folder | pc | HWVX.cab's files flat, the movies under `FMV_/` (Port_ImportPCDisc) |
//! | a folder already extracted or installed | gc, pc, ps2, ps2beta | its files as they are |
//!
//! The port reads GameCube images itself (src/port/disc.c) and imports the PC and PS2 discs into
//! folders (src/port/pcdisc.c); this is that code in Rust, and a .gcz too (which the port refuses).
//! An extraction's folder plays as `--data`.
//!
//! The editor reads one file of any of these with [`read_file`] or [`find_archive`] (an archive
//! by name, any extension: `find_archive(path, "HW_IPack")`), the same for a folder.

pub mod cab;
pub mod gcm;
pub mod gcz;
pub mod io;
pub mod iso;

use io::{FileAt, Region, Src};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Arc;

pub type R<T> = std::result::Result<T, String>;

/// [`Disc::extract_until`]'s error when its progress callback stopped it.
pub const CANCELLED: &str = "cancelled";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Release {
    Gc,
    Pc,
    Ps2,
    /// the PS2 prototype (archive version 177/179)
    Ps2Beta,
}

impl Release {
    /// The launcher's key (sources.cfg): gc, pc, ps2, ps2beta.
    pub fn key(self) -> &'static str {
        match self {
            Release::Gc => "gc",
            Release::Pc => "pc",
            Release::Ps2 => "ps2",
            Release::Ps2Beta => "ps2beta",
        }
    }
    pub fn name(self) -> &'static str {
        match self {
            Release::Gc => "GameCube",
            Release::Pc => "PC",
            Release::Ps2 => "PlayStation 2",
            Release::Ps2Beta => "PlayStation 2 prototype",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    /// a GameCube image, .iso/.gcm (NKit or not)
    GcImage,
    /// Dolphin's compressed GameCube image
    Gcz,
    /// an ISO 9660 image (PS2 DVD, PC CD; .iso, or .cue/.bin)
    Iso,
    /// the PC CD as a folder (mounted, or copied off the disc): HWVX.cab at its top
    CdFolder,
    /// a folder of the game's files already (extracted, installed, a PS2 disc's files)
    Extracted,
}

impl Kind {
    pub fn key(self) -> &'static str {
        match self {
            Kind::GcImage => "gc-image",
            Kind::Gcz => "gcz",
            Kind::Iso => "iso9660",
            Kind::CdFolder => "cd-folder",
            Kind::Extracted => "folder",
        }
    }
    pub fn is_image(self) -> bool {
        matches!(self, Kind::GcImage | Kind::Gcz | Kind::Iso)
    }
}

#[derive(Clone, Debug)]
enum Origin {
    /// at this offset of the (2048-byte sector view of the) image
    Image(u64),
    File(PathBuf),
    /// the cabinet's file at this index
    Cab(usize),
}

/// One file of the game's data: its path as extracted ('/'-separated) and size.
#[derive(Clone, Debug)]
pub struct Entry {
    pub path: String,
    pub size: u64,
    origin: Origin,
}

impl Entry {
    pub fn name(&self) -> &str {
        self.path.rsplit('/').next().unwrap_or(&self.path)
    }
    /// The file name without its extension (as cars::find_archive matches).
    pub fn stem(&self) -> &str {
        let n = self.name();
        match n.rfind('.') {
            Some(i) if i > 0 && i < n.len() - 1 => &n[..i],
            _ => n,
        }
    }
}

pub struct Disc {
    pub path: PathBuf,
    pub kind: Kind,
    pub release: Release,
    /// GameCube: the game ID (GHWE78) and title
    pub game_id: Option<String>,
    pub title: Option<String>,
    /// a GameCube image in NKit's layout ("NKIT" at 0x200)
    pub nkit: bool,
    /// a CD image of raw 2352-byte sectors (.bin)
    pub raw_sectors: bool,
    /// the version in HW_share's archive header (PS2: 183 retail, 177/179 the prototype)
    pub archive_version: Option<u32>,
    /// the image's size in bytes (the file's; a .gcz's compressed size)
    pub image_size: u64,
    /// the disc's size (a .gcz's data, a .bin's 2048-byte view)
    pub disc_size: u64,
    src: Option<Src>,
    cab: Option<cab::Cab>,
    pub entries: Vec<Entry>,
}

/// The PC CD's movies (pcdisc.c: the game played them from the disc).
const MOVIES: [&str; 7] = ["thqLogo.bik", "BGlogo.bik", "hwboot.bik", "hw_intro.bik", "hw_end.bik", "hw_gal.bik", "hw_mark.bik"];

/// A file name the ripper opens as a disc image (by its extension).
pub fn is_image_name(p: &Path) -> bool {
    let ext = p.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    matches!(ext.as_str(), "iso" | "gcm" | "gcz" | "cue" | "bin")
}

/// The track file a .cue names: FILE "name" BINARY (or unquoted), relative to the .cue's folder
/// unless absolute (pcdisc.c CueTrack). The name is matched without case when the exact spelling
/// is missing, as on Windows and macOS: the retail PC .cue names IMS-HWVX.BIN, the rip ims-hwvx.bin.
fn cue_track(cue: &Path) -> R<PathBuf> {
    let text = fs::read_to_string(cue).map_err(|e| format!("{}: {e}", cue.display()))?;
    // a .cue saved by Notepad starts with a BOM
    let text = text.strip_prefix('\u{FEFF}').unwrap_or(&text);
    for line in text.lines() {
        let l = line.trim_start();
        // get(): a line starting with a multibyte character has no boundary at 4
        let Some(rest) = l.get(..4).filter(|h| h.eq_ignore_ascii_case("FILE")).map(|_| &l[4..]) else { continue };
        if !rest.starts_with([' ', '\t']) {
            continue;
        }
        let rest = rest.trim_start();
        let name = if let Some(q) = rest.strip_prefix('"') { q.split('"').next().unwrap_or("") } else { rest.split([' ', '\t']).next().unwrap_or("") };
        if name.is_empty() {
            continue;
        }
        let p = Path::new(name);
        let abs = p.is_absolute() || name.starts_with(['/', '\\']) || name.as_bytes().get(1) == Some(&b':');
        if abs {
            return Ok(p.to_path_buf());
        }
        let dir = cue.parent().unwrap_or(Path::new("."));
        let exact = dir.join(p);
        if exact.exists() {
            return Ok(exact);
        }
        return Ok(path_nocase(dir, &name.replace('\\', "/")).unwrap_or(exact));
    }
    Err(format!("{}: names no track file", cue.display()))
}

/// `name` in `dir`, matched without case (disc files keep the disc's spelling: HW_SHARE.XPS).
fn child_nocase(dir: &Path, name: &str) -> Option<PathBuf> {
    let p = dir.join(name);
    if p.exists() {
        return Some(p);
    }
    let mut names: Vec<_> = fs::read_dir(dir).ok()?.flatten().map(|e| e.file_name()).collect();
    names.sort();
    names.into_iter().find(|n| n.to_string_lossy().eq_ignore_ascii_case(name)).map(|n| dir.join(n))
}

fn path_nocase(dir: &Path, rel: &str) -> Option<PathBuf> {
    rel.split('/').try_fold(dir.to_path_buf(), |d, part| child_nocase(&d, part))
}

/// The version in an archive's header (little-endian at 8; the PS2's and PC's).
fn archive_version_le(head: &[u8]) -> Option<u32> {
    (head.len() >= 12).then(|| io::le32(head, 8))
}

fn ps2_release(version: u32) -> R<Release> {
    match version {
        183 => Ok(Release::Ps2),
        177 | 179 => Ok(Release::Ps2Beta),
        v => Err(format!("a PS2 disc of an unknown version ({v})")),
    }
}

/// Every file under `dir` (sorted, depth first) as (path under `prefix`, size, file).
fn walk_dir(dir: &Path, prefix: &str, out: &mut Vec<Entry>, depth: u32) -> R<()> {
    if depth > 16 {
        return Ok(());
    }
    let mut items: Vec<_> = fs::read_dir(dir).map_err(|e| format!("{}: {e}", dir.display()))?.flatten().collect();
    items.sort_by_key(|e| e.file_name());
    for e in items {
        let name = e.file_name().to_string_lossy().into_owned();
        let p = e.path();
        let rel = if prefix.is_empty() { name.clone() } else { format!("{prefix}/{name}") };
        let Ok(md) = fs::metadata(&p) else { continue };
        if md.is_dir() {
            walk_dir(&p, &rel, out, depth + 1)?;
        } else if md.is_file() {
            out.push(Entry { path: rel, size: md.len(), origin: Origin::File(p) });
        }
    }
    Ok(())
}

impl Disc {
    fn new(path: &Path, kind: Kind, release: Release) -> Disc {
        Disc {
            path: path.to_path_buf(),
            kind,
            release,
            game_id: None,
            title: None,
            nkit: false,
            raw_sectors: false,
            archive_version: None,
            image_size: 0,
            disc_size: 0,
            src: None,
            cab: None,
            entries: vec![],
        }
    }

    /// What `path` is (an image or a folder), its release and its files.
    pub fn open(path: &Path) -> R<Disc> {
        let md = fs::metadata(path).map_err(|e| format!("{}: {e}", path.display()))?;
        if md.is_dir() {
            return Disc::open_dir(path);
        }
        let ext = path.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
        let image = if ext == "cue" { cue_track(path)? } else { path.to_path_buf() };
        let file: Src = Arc::new(FileAt::open(&image).map_err(|e| format!("{}: {e}", image.display()))?);
        let image_size = file.len();
        let mut magic = [0u8; 4];
        let _ = file.read_at(0, &mut magic);
        let mut d = if u32::from_le_bytes(magic) == gcz::MAGIC {
            let g = gcz::Gcz::open(file).map_err(|e| format!("{}: {e}", path.display()))?;
            Disc::open_gc(path, Arc::new(g), Kind::Gcz)?
        } else if gcm::is_gc(&*file) {
            Disc::open_gc(path, file, Kind::GcImage)?
        } else {
            let raw = io::is_raw_cd(&*file);
            let src: Src = if raw { Arc::new(io::RawSectors { inner: file }) } else { file };
            if !iso::is_iso(&*src) {
                let mut h = [0u8; 4];
                let wii = src.read_at(0x18, &mut h).is_ok() && u32::from_be_bytes(h) == gcm::WII_MAGIC;
                return Err(format!(
                    "{}: {}",
                    path.display(),
                    if wii { "a Wii disc image, not a GameCube one" } else { "not a disc image of Velocity X (GameCube .iso/.gcm/.gcz, PS2 .iso, PC .iso or .cue/.bin)" }
                ));
            }
            let mut d = Disc::open_iso(path, src)?;
            d.raw_sectors = raw;
            d
        };
        one_spelling(&d.entries).map_err(|e| format!("{}: {e}", path.display()))?;
        d.image_size = image_size;
        Ok(d)
    }

    fn open_gc(path: &Path, src: Src, kind: Kind) -> R<Disc> {
        let (h, files) = gcm::read(&*src).map_err(|e| format!("{}: {e}", path.display()))?;
        if !files.iter().any(|f| f.path.eq_ignore_ascii_case("HW_share.xgc")) {
            return Err(format!("{}: a GameCube disc ({}, {}) without HW_share.xgc: not Hot Wheels Velocity X", path.display(), h.id, h.title));
        }
        let mut d = Disc::new(path, kind, Release::Gc);
        d.game_id = Some(h.id);
        d.title = Some(h.title);
        d.nkit = h.nkit;
        d.disc_size = src.len();
        d.entries = files.into_iter().map(|f| Entry { path: f.path, size: f.size, origin: Origin::Image(f.offset) }).collect();
        d.src = Some(src);
        Ok(d)
    }

    fn open_iso(path: &Path, src: Src) -> R<Disc> {
        let s = &*src;
        let root = iso::root(s).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut d;
        if let Some(c) = iso::find(s, &root, "HWVX.cab").filter(|r| !r.dir) {
            d = Disc::new(path, Kind::Iso, Release::Pc);
            let region: Src = Arc::new(Region { inner: src.clone(), base: c.offset(), len: c.size as u64 });
            let cab = cab::Cab::open(region).map_err(|e| format!("{}: {e}", path.display()))?;
            d.entries = cab_entries(&cab);
            d.cab = Some(cab);
            if let Some(fmv) = iso::find(s, &root, "FMV_").filter(|r| r.dir) {
                for m in MOVIES {
                    if let Some(r) = iso::find(s, &fmv, m).filter(|r| !r.dir) {
                        d.entries.push(Entry { path: format!("FMV_/{m}"), size: r.size as u64, origin: Origin::Image(r.offset()) });
                    }
                }
            }
        } else if let Some(eng) = iso::find_path(s, &root, "XPS/ENGLISH").filter(|r| r.dir) {
            let share = iso::find(s, &eng, "HW_share.xps").filter(|r| !r.dir && r.size >= 12);
            let Some(share) = share else {
                return Err(format!("{}: no XPS/ENGLISH/HW_share.xps: not a Hot Wheels Velocity X PS2 disc", path.display()));
            };
            let mut head = [0u8; 12];
            s.read_at(share.offset(), &mut head).map_err(|e| format!("{}: {e}", path.display()))?;
            let v = io::le32(&head, 8);
            d = Disc::new(path, Kind::Iso, ps2_release(v).map_err(|e| format!("{}: {e}", path.display()))?);
            d.archive_version = Some(v);
            d.entries = iso::walk(s, &eng, "XPS/ENGLISH")
                .map_err(|e| format!("{}: {e}", path.display()))?
                .into_iter()
                .map(|(p, r)| Entry { path: p, size: r.size as u64, origin: Origin::Image(r.offset()) })
                .collect();
        } else {
            return Err(format!("{}: an ISO 9660 image, but neither the PC CD (HWVX.cab) nor a PS2 disc (XPS/ENGLISH)", path.display()));
        }
        d.disc_size = src.len();
        d.src = Some(src);
        Ok(d)
    }

    fn open_dir(dir: &Path) -> R<Disc> {
        if let Some(c) = child_nocase(dir, "HWVX.cab").filter(|p| p.is_file()) {
            let mut d = Disc::new(dir, Kind::CdFolder, Release::Pc);
            let file: Src = Arc::new(FileAt::open(&c).map_err(|e| format!("{}: {e}", c.display()))?);
            let cab = cab::Cab::open(file).map_err(|e| format!("{}: {e}", c.display()))?;
            d.entries = cab_entries(&cab);
            d.cab = Some(cab);
            if let Some(fmv) = child_nocase(dir, "FMV_").filter(|p| p.is_dir()) {
                for m in MOVIES {
                    if let Some(p) = child_nocase(&fmv, m).filter(|p| p.is_file()) {
                        let size = fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
                        d.entries.push(Entry { path: format!("FMV_/{m}"), size, origin: Origin::File(p) });
                    }
                }
            }
            one_spelling(&d.entries).map_err(|e| format!("{}: {e}", c.display()))?;
            return Ok(d);
        }
        let version_of = |p: &Path| -> Option<u32> {
            let mut f = fs::File::open(p).ok()?;
            let mut h = [0u8; 12];
            std::io::Read::read_exact(&mut f, &mut h).ok()?;
            archive_version_le(&h)
        };
        // a PS2 disc's files: XPS/ENGLISH (or that folder itself)
        for (rel, base) in [("XPS/ENGLISH", dir.to_path_buf()), ("", dir.to_path_buf())] {
            let eng = if rel.is_empty() { Some(base.clone()) } else { path_nocase(&base, rel) };
            if let Some(eng) = eng
                && let Some(share) = child_nocase(&eng, "HW_share.xps").filter(|p| p.is_file())
                && let Some(v) = version_of(&share)
            {
                let mut d = Disc::new(dir, Kind::Extracted, ps2_release(v).map_err(|e| format!("{}: {e}", dir.display()))?);
                d.archive_version = Some(v);
                walk_dir(&eng, "XPS/ENGLISH", &mut d.entries, 0)?;
                return Ok(d);
            }
        }
        for base in [dir.to_path_buf(), dir.join("files")] {
            if child_nocase(&base, "HW_share.xgc").is_some_and(|p| p.is_file()) {
                let mut d = Disc::new(dir, Kind::Extracted, Release::Gc);
                walk_dir(&base, "", &mut d.entries, 0)?;
                return Ok(d);
            }
        }
        if let Some(share) = child_nocase(dir, "HW_share.xpc").filter(|p| p.is_file()) {
            let mut d = Disc::new(dir, Kind::Extracted, Release::Pc);
            d.archive_version = version_of(&share);
            walk_dir(dir, "", &mut d.entries, 0)?;
            return Ok(d);
        }
        Err(format!(
            "{}: no Velocity X data there (HW_share.xgc, files/HW_share.xgc, HW_share.xpc, XPS/ENGLISH/HW_share.xps, or the PC CD's HWVX.cab)",
            dir.display()
        ))
    }

    /// The bytes of all the files.
    pub fn total_size(&self) -> u64 {
        self.entries.iter().map(|e| e.size).sum()
    }

    /// A file by its path (without case, '/' or '\\'), or by its name alone when no path matches.
    pub fn find(&self, name: &str) -> Option<&Entry> {
        let want = name.replace('\\', "/");
        let want = want.trim_start_matches('/');
        self.entries
            .iter()
            .find(|e| e.path.eq_ignore_ascii_case(want))
            .or_else(|| (!want.contains('/')).then(|| self.entries.iter().find(|e| e.name().eq_ignore_ascii_case(want))).flatten())
    }

    /// An archive by its name without extension (any case; "HW_IPack" finds HW_IPack.xgc,
    /// .xpc, .xps): the one nearest the top.
    pub fn find_archive(&self, stem: &str) -> Option<&Entry> {
        self.entries.iter().filter(|e| e.stem().eq_ignore_ascii_case(stem)).min_by_key(|e| e.path.matches('/').count())
    }

    /// The files `only` picks (none: all). A pattern with '/' matches the whole path, one without
    /// the name or the path; `*` and `?`, without case.
    pub fn select(&self, only: &[String]) -> Vec<&Entry> {
        self.entries.iter().filter(|e| only.is_empty() || only.iter().any(|p| matches(p, e))).collect()
    }

    /// A file's bytes (read whole: for the packs; a movie is better copied with [`Disc::copy`]).
    pub fn read(&self, name: &str) -> R<Vec<u8>> {
        let e = self.find(name).ok_or_else(|| format!("{}: no {name}", self.path.display()))?;
        self.read_entry(e)
    }

    pub fn read_entry(&self, e: &Entry) -> R<Vec<u8>> {
        if let Origin::Cab(i) = e.origin {
            let cab = self.cab.as_ref().expect("a cabinet entry without its cabinet");
            return cab.read_file(&cab.files[i]);
        }
        // the size is the image's word for it: checked against the image before it is allocated
        if let Origin::Image(off) = e.origin {
            self.inside(e, off)?;
        }
        let mut out = Vec::with_capacity(e.size as usize);
        self.copy(e, &mut out)?;
        Ok(out)
    }

    fn inside(&self, e: &Entry, off: u64) -> R<&Src> {
        let src = self.src.as_ref().expect("an image entry without its image");
        if off > src.len() || e.size > src.len() - off {
            return Err(format!("{} lies outside the disc image (a trimmed image?)", e.path));
        }
        Ok(src)
    }

    /// A file's bytes written to `w`, a megabyte at a time.
    pub fn copy(&self, e: &Entry, w: &mut dyn Write) -> R<()> {
        match &e.origin {
            Origin::Image(off) => {
                let src = self.inside(e, *off)?;
                io::copy_range(&**src, *off, e.size, w).map_err(|x| format!("{}: {x}", e.path))
            }
            Origin::File(p) => {
                let mut f = fs::File::open(p).map_err(|x| format!("{}: {x}", p.display()))?;
                std::io::copy(&mut f, w).map(|_| ()).map_err(|x| format!("{}: {x}", p.display()))
            }
            Origin::Cab(_) => {
                let b = self.read_entry(e)?;
                w.write_all(&b).map_err(|x| format!("{}: {x}", e.path))
            }
        }
    }

    /// Extract the files `only` picks (none: all) under `out`, in the layout the launcher
    /// imports (an extraction plays as `--data out`). Each file is written as NAME.part and
    /// renamed when complete; the image is never written. `progress` sees each finished file.
    pub fn extract(&self, out: &Path, only: &[String], progress: &mut dyn FnMut(&Entry)) -> R<(usize, u64)> {
        self.extract_until(out, only, &mut |e| {
            progress(e);
            true
        })
    }

    /// [`Disc::extract`], stopped when `progress` returns false (after that file): Err([`CANCELLED`]).
    /// The files done stay; a cabinet file being written stays as NAME.part.
    pub fn extract_until(&self, out: &Path, only: &[String], progress: &mut dyn FnMut(&Entry) -> bool) -> R<(usize, u64)> {
        let picked = self.select(only);
        fs::create_dir_all(out).map_err(|e| format!("{}: {e}", out.display()))?;
        let (mut files, mut bytes) = (0usize, 0u64);
        let mut cab_picked: Vec<&Entry> = vec![];
        for e in picked {
            if matches!(e.origin, Origin::Cab(_)) {
                cab_picked.push(e);
                continue;
            }
            let mut w = Part::create(out, &e.path)?;
            self.copy(e, &mut w.f).map_err(|x| format!("{x} (writing {})", w.part.display()))?;
            w.finish()?;
            if !progress(e) {
                return Err(CANCELLED.into());
            }
            files += 1;
            bytes += e.size;
        }
        if let Some(cab) = &self.cab {
            // a folder decompressed once for all its files
            for (fi, _) in cab.folders.iter().enumerate() {
                let mut want: Vec<(&Entry, &cab::CabFile, Option<Part>, u64)> = cab_picked
                    .iter()
                    .filter_map(|e| match e.origin {
                        Origin::Cab(i) if cab.files[i].folder as usize == fi => Some((*e, &cab.files[i], None, 0)),
                        _ => None,
                    })
                    .collect();
                if want.is_empty() {
                    continue;
                }
                let mut left = want.len();
                let mut stopped = false;
                for w in want.iter_mut().filter(|w| w.1.size == 0) {
                    Part::create(out, &w.0.path)?.finish()?;
                    if !progress(w.0) {
                        return Err(CANCELLED.into());
                    }
                    left -= 1;
                    w.3 = u64::MAX;
                }
                if left > 0 {
                    cab.stream(fi, &mut |pos, block| {
                        for w in want.iter_mut() {
                            if w.3 == u64::MAX {
                                continue;
                            }
                            if let Some(r) = cab::overlap(w.1, pos, block.len()) {
                                if w.2.is_none() {
                                    w.2 = Some(Part::create(out, &w.0.path)?);
                                }
                                let p = w.2.as_mut().unwrap();
                                p.f.write_all(&block[r.clone()]).map_err(|x| format!("{}: {x}", p.part.display()))?;
                                w.3 += r.len() as u64;
                                if w.3 == w.1.size as u64 {
                                    w.2.take().unwrap().finish()?;
                                    w.3 = u64::MAX;
                                    left -= 1;
                                    if !progress(w.0) {
                                        stopped = true;
                                        return Ok(false);
                                    }
                                }
                            }
                        }
                        Ok(left > 0)
                    })?;
                }
                if stopped {
                    return Err(CANCELLED.into());
                }
                if let Some(w) = want.iter().find(|w| w.3 != u64::MAX) {
                    return Err(format!("HWVX.cab: {} runs past its folder's data", w.1.name));
                }
                files += want.len();
                bytes += want.iter().map(|w| w.1.size as u64).sum::<u64>();
            }
        }
        Ok((files, bytes))
    }
}

impl std::fmt::Debug for Disc {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Disc({}, {}, {}, {} files)", self.path.display(), self.kind.key(), self.release.key(), self.entries.len())
    }
}

/// The image's files as one file system without case sees them (Windows', macOS's): two names
/// differing only in case, or a file where another's folder is, would land on one another.
fn one_spelling(entries: &[Entry]) -> R<()> {
    let mut files = std::collections::HashSet::new();
    let mut dirs = std::collections::HashSet::new();
    for e in entries {
        let low = e.path.to_lowercase();
        let mut at = 0;
        while let Some(i) = low[at..].find('/') {
            dirs.insert(low[..at + i].to_string());
            at += i + 1;
        }
        if !files.insert(low) {
            return Err(format!("two files named {} but for case (a damaged image?)", e.path));
        }
    }
    match files.iter().find(|f| dirs.contains(*f)) {
        Some(f) => Err(format!("{f} is both a file and a folder (a damaged image?)")),
        None => Ok(()),
    }
}

fn cab_entries(cab: &cab::Cab) -> Vec<Entry> {
    cab.files
        .iter()
        .enumerate()
        .filter(|(_, f)| (f.folder as usize) < cab.folders.len())
        .map(|(i, f)| Entry { path: f.name.clone(), size: f.size as u64, origin: Origin::Cab(i) })
        .collect()
}

/// A file being written: NAME.part, renamed to NAME when complete.
struct Part {
    f: std::io::BufWriter<fs::File>,
    part: PathBuf,
    dest: PathBuf,
}

impl Part {
    fn create(out: &Path, rel: &str) -> R<Part> {
        let mut dest = out.to_path_buf();
        for c in rel.split('/') {
            if !gcm::safe_name(c) {
                return Err(format!("{rel}: not a safe path"));
            }
            dest.push(c);
        }
        if let Some(p) = dest.parent() {
            fs::create_dir_all(p).map_err(|e| format!("{}: {e}", p.display()))?;
        }
        let mut part = dest.clone().into_os_string();
        part.push(".part");
        let part = PathBuf::from(part);
        let f = fs::File::create(&part).map_err(|e| format!("{}: {e}", part.display()))?;
        Ok(Part { f: std::io::BufWriter::with_capacity(1 << 20, f), part, dest })
    }
    fn finish(self) -> R<()> {
        let f = self.f.into_inner().map_err(|e| format!("{}: {e}", self.part.display()))?;
        f.sync_all().ok();
        drop(f);
        fs::rename(&self.part, &self.dest).map_err(|e| format!("{}: {e}", self.dest.display()))
    }
}

/// `*` and `?` matching, without case.
pub fn glob(p: &str, s: &str) -> bool {
    let (p, s): (Vec<char>, Vec<char>) = (p.to_lowercase().chars().collect(), s.to_lowercase().chars().collect());
    let (mut i, mut j, mut star, mut mark) = (0, 0, None, 0);
    while j < s.len() {
        if i < p.len() && (p[i] == '?' || p[i] == s[j]) {
            i += 1;
            j += 1;
        } else if i < p.len() && p[i] == '*' {
            star = Some(i);
            mark = j;
            i += 1;
        } else if let Some(st) = star {
            i = st + 1;
            mark += 1;
            j = mark;
        } else {
            return false;
        }
    }
    while i < p.len() && p[i] == '*' {
        i += 1;
    }
    i == p.len()
}

fn matches(pat: &str, e: &Entry) -> bool {
    let pat = pat.replace('\\', "/");
    if pat.contains('/') { glob(pat.trim_start_matches('/'), &e.path) } else { glob(&pat, e.name()) || glob(&pat, &e.path) }
}

/// One file of the game's data at `path` (a disc image of any release, or a folder), by its
/// path or name ([`Disc::find`]).
pub fn read_file(path: &Path, name: &str) -> R<Vec<u8>> {
    Disc::open(path)?.read(name)
}

/// An archive by name without extension ("HW_IPack", any case) at `path`: a folder (searched a
/// few levels down, cars::find_archive) or a disc image of any release. Returns where it is (for
/// messages; inside an image: IMAGE/its path) and its bytes.
pub fn find_archive(path: &Path, stem: &str) -> R<(PathBuf, Vec<u8>)> {
    if path.is_dir()
        && let Some(p) = crate::cars::find_archive(path, stem)
    {
        let b = fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))?;
        return Ok((p, b));
    }
    // a folder without it: the PC CD's (its archives are in HWVX.cab), or none
    let d = match Disc::open(path) {
        Err(_) if path.is_dir() => return Err(format!("{}: no {stem}", path.display())),
        d => d?,
    };
    let e = d.find_archive(stem).ok_or_else(|| format!("{}: no {stem}", path.display()))?;
    Ok((path.join(&e.path), d.read_entry(e)?))
}

#[cfg(test)]
mod tests;

/// Tiny disc images built in memory, for tests (this crate's, and the editor's with the
/// `test-utils` feature): a GameCube image, an ISO 9660 image (a PS2 DVD's or the PC CD's tree),
/// its raw 2352-byte sectors (.bin), and a sample HWVX.cab.
#[cfg(any(test, feature = "test-utils"))]
pub mod test_images {
    pub use super::cab::tests::sample as cab_sample;
    pub use super::gcm::tests::make as gamecube;
    pub use super::iso::tests::{Node, make as iso9660, to_raw};
}
