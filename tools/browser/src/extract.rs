//! Extract a disc: the PC CD and the PS2 DVD (and a GameCube image, if the player wants files)
//! into a folder the game plays as its data, with xdlrs's ripper (xdlrs::disc). The game reads a
//! GameCube image itself; the PC's and the PS2's discs it only reads extracted (its launcher
//! imports them, src/port/pcdisc.c). This is the logic of File > Extract a disc… (src/extract_ui.rs):
//! what a disc is ([`inspect`]), where it goes ([`default_dest`], [`check_dest`]: an empty folder,
//! or an earlier extraction of the same disc), whether it fits ([`check_space`]), and the
//! extraction itself ([`run`], on a worker thread: [`Job`]), its errors in plain words.

use crate::data_dir;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, TryRecvError};
use std::sync::{Arc, Mutex};
use xdlrs::disc::{CANCELLED, Disc, Kind, Release, Step};

/// Room kept free beyond the files themselves (the file system's own blocks, the pref folder).
pub const SPACE_MARGIN: u64 = 64 << 20;

/// What a disc is, from [`Disc::open`].
#[derive(Clone, Debug)]
pub struct DiscInfo {
    pub path: PathBuf,
    pub release: Release,
    pub kind: Kind,
    /// GameCube: the game ID (GHWE78) and title
    pub game_id: Option<String>,
    pub title: Option<String>,
    /// HW_share's archive version (PS2: 183, the prototype 177/179)
    pub archive_version: Option<u32>,
    pub nkit: bool,
    pub raw_sectors: bool,
    /// the image file's size
    pub image_size: u64,
    pub files: usize,
    pub bytes: u64,
    /// the largest file (rewriting an earlier extraction's file needs it twice for a moment)
    pub largest: u64,
    /// every file's path as extracted and its size
    pub entries: Vec<(String, u64)>,
}

impl DiscInfo {
    /// "a GameCube image (NKit)", "the PC CD's .bin (raw sectors)", ...
    pub fn kind_text(&self) -> String {
        let ext = self.path.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
        let mut s = match self.kind {
            Kind::GcImage => format!(".{ext} image"),
            Kind::Gcz => "Dolphin .gcz (compressed) image".into(),
            Kind::Iso if ext == "cue" => ".cue/.bin image".into(),
            Kind::Iso => format!(".{ext} image"),
            Kind::CdFolder => "the CD's files in a folder".into(),
            Kind::Extracted => "a folder of extracted files".into(),
        };
        if self.nkit {
            s.push_str(", NKit");
        }
        if self.raw_sectors {
            s.push_str(", raw 2352-byte sectors");
        }
        s
    }

    /// The game reads this one as it is (a GameCube image; disc.c).
    pub fn plays_directly(&self) -> bool {
        self.release == Release::Gc && self.kind == Kind::GcImage
    }
}

/// The ripper's message in plain words: what went wrong first, then its details.
pub fn plain_error(raw: &str) -> String {
    let low = raw.to_ascii_lowercase();
    let head = if raw == CANCELLED {
        return "Cancelled.".into();
    } else if low.contains("a wii disc") {
        "This is a Wii disc image, not Hot Wheels: Velocity X."
    } else if low.contains("not hot wheels") || low.contains("not a disc image of velocity x") || low.contains("neither the pc cd") || low.contains("no velocity x data") {
        "This isn't a Hot Wheels: Velocity X disc (or its image is of another kind)."
    } else if low.contains("unknown version") {
        "A PS2 disc of a version the editor doesn't know (not the retail game nor the prototype)."
    } else if low.contains("compression type") || low.contains("only mszip") {
        "The PC disc's HWVX.cab uses a compression the editor can't read (only MSZIP, as on the retail CD): is it a modified copy?"
    } else if low.contains("outside the disc image") || low.contains("past its folder") || low.contains("unexpected end") || low.contains("failed to fill whole buffer") {
        "The disc image is cut short or damaged (a trimmed or unfinished copy?): rip the disc again."
    } else if low.contains("no space left") || low.contains("os error 28") || low.contains("os error 112") || low.contains("disk full") {
        "The drive ran out of space while extracting. Free some space (the files done so far stay), then extract again into the same folder."
    } else if low.contains("permission denied") || low.contains("os error 13") || low.contains("os error 5)") || low.contains("read-only") {
        "The editor may not write there: choose another folder."
    } else if low.contains("names no track file") || (low.contains(".bin") && low.contains("no such file")) {
        "The .cue's track file (.bin) is missing: keep the .bin beside the .cue."
    } else if low.contains("no such file") || low.contains("os error 2)") {
        "That file isn't there."
    } else {
        return raw.to_string();
    };
    format!("{head}\nDetails: {raw}")
}

/// What `path` is, as the extraction sees it. A folder of files already extracted is refused
/// (the game plays it as it is).
pub fn inspect(path: &Path) -> Result<DiscInfo, String> {
    if path.as_os_str().is_empty() {
        return Err("Choose a disc image (.iso, .gcm, .gcz, .cue or .bin).".into());
    }
    let d = Disc::open(path).map_err(|e| plain_error(&e))?;
    if d.kind == Kind::Extracted {
        return Err(format!(
            "{} holds the {} release's files already: nothing to extract. Choose it as the game data as it is (File > Game data…).",
            path.display(),
            d.release.name()
        ));
    }
    Ok(DiscInfo {
        path: path.to_path_buf(),
        release: d.release,
        kind: d.kind,
        game_id: d.game_id.clone(),
        title: d.title.clone(),
        archive_version: d.archive_version,
        nkit: d.nkit,
        raw_sectors: d.raw_sectors,
        image_size: d.image_size,
        files: d.entries.len(),
        bytes: d.total_size(),
        largest: d.entries.iter().map(|e| e.size).max().unwrap_or(0),
        entries: d.entries.iter().map(|e| (e.path.clone(), e.size)).collect(),
    })
}

/// Where an extraction goes unless the player says otherwise: game-data/<release> beside the
/// editor's pref folder (the packaged editor's: ~/.local/share/hwvx-editor/game-data/ps2), made absolute.
pub fn default_dest(pref: &Path, release: Release) -> PathBuf {
    let base = pref.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(pref);
    let base = std::path::absolute(base).unwrap_or_else(|_| base.to_path_buf());
    base.join("game-data").join(release.key())
}

/// The destination folder, as found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Dest {
    /// missing or empty (empty folders in it are fine)
    Fresh,
    /// an earlier extraction of this disc, finished or not: its complete files and their bytes
    /// (and the .part files of one stopped)
    Previous { files: usize, bytes: u64, parts: usize },
}

/// Files a system leaves in any folder: not the player's.
fn ignorable(name: &str) -> bool {
    matches!(name.to_ascii_lowercase().as_str(), ".ds_store" | "thumbs.db" | "desktop.ini" | ".directory")
}

/// What [`walk`] hands each file to.
type Visit<'a> = dyn FnMut(&str, Option<u64>) -> Result<(), String> + 'a;

/// Each file under `dir` to `visit` (its size; None: not a plain file, a link or a device),
/// depth first in name order; `visit`'s Err ends the walk there. Links are not followed: the
/// destination's own folders only (a link to `/` once walked the whole drive).
fn walk(dir: &Path, rel: &str, visit: &mut Visit, depth: u32) -> Result<(), String> {
    if depth > 16 {
        return Ok(());
    }
    let rd = std::fs::read_dir(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    let mut items: Vec<_> = rd.flatten().collect();
    items.sort_by_key(|e| e.file_name());
    for e in items {
        let name = e.file_name().to_string_lossy().into_owned();
        let r = if rel.is_empty() { name.clone() } else { format!("{rel}/{name}") };
        let Ok(ft) = e.file_type() else { continue };
        if ft.is_dir() {
            walk(&e.path(), &r, visit, depth + 1)?;
        } else if !ignorable(&name) {
            visit(&r, ft.is_file().then(|| e.metadata().map(|m| m.len()).unwrap_or(0)))?;
        }
    }
    Ok(())
}

/// Whether the disc may go into `dest`: a new or empty folder, or one holding only this disc's
/// files (an earlier extraction, perhaps stopped). Anything else is refused: the extraction
/// would mix with other files. The first file that isn't the disc's decides (`/` is refused at
/// once, not walked).
pub fn check_dest(dest: &Path, info: &DiscInfo) -> Result<Dest, String> {
    if dest.as_os_str().is_empty() {
        return Err("Choose a folder to extract into.".into());
    }
    if dest.is_file() {
        return Err(format!("{} is a file: choose a folder.", dest.display()));
    }
    if !dest.exists() {
        return Ok(Dest::Fresh);
    }
    let want: HashMap<String, u64> = info.entries.iter().map(|(p, s)| (p.to_ascii_lowercase(), *s)).collect();
    let (mut files, mut bytes, mut parts) = (0usize, 0u64, 0usize);
    walk(dest, "", &mut |rel, size| {
        let (name, part) = match rel.strip_suffix(".part") {
            Some(n) => (n, true),
            None => (rel, false),
        };
        let (Some(size), Some(&want_size)) = (size, want.get(&name.to_ascii_lowercase())) else {
            return Err(format!(
                "{} already holds other files ({rel}, …). Choose an empty folder, or the folder an earlier extraction of this disc went to.",
                dest.display()
            ));
        };
        if part {
            parts += 1;
        } else if size == want_size {
            files += 1;
            bytes += size;
        } else {
            return Err(format!(
                "{} holds {rel} of another size ({size} bytes, this disc's is {want_size}): another disc's files? Choose an empty folder.",
                dest.display()
            ));
        }
        Ok(())
    }, 0)?;
    Ok(if files + parts == 0 { Dest::Fresh } else { Dest::Previous { files, bytes, parts } })
}

/// The bytes the extraction still needs on the drive: the disc's files, less an earlier
/// extraction's (written again, each beside its old copy until done: the largest file's room).
pub fn space_needed(info: &DiscInfo, dest: &Dest) -> u64 {
    match dest {
        Dest::Fresh => info.bytes,
        Dest::Previous { bytes, .. } => info.bytes.saturating_sub(*bytes) + info.largest.min(*bytes),
    }
}

/// "1.9 GB", "640 MB", "12 KB"
pub fn human(b: u64) -> String {
    let f = b as f64;
    if f >= 1e12 {
        format!("{:.1} TB", f / 1e12)
    } else if f >= 1e9 {
        format!("{:.1} GB", f / 1e9)
    } else if f >= 1e6 {
        format!("{:.0} MB", f / 1e6)
    } else if f >= 1e3 {
        format!("{:.0} KB", f / 1e3)
    } else {
        format!("{b} bytes")
    }
}

/// Refuses an extraction the drive hasn't room for (`free`: None, unknown: let it try).
pub fn check_space(needed: u64, free: Option<u64>, what: &str) -> Result<(), String> {
    match free {
        Some(free) if free < needed.saturating_add(SPACE_MARGIN) => Err(format!(
            "Not enough free space: {what} need {}, and the drive has {} free. Free some space, or choose a folder on another drive.",
            human(needed + SPACE_MARGIN),
            human(free)
        )),
        _ => Ok(()),
    }
}

/// The free bytes on the drive `path` would be on (its nearest existing folder).
pub fn free_space(path: &Path) -> Option<u64> {
    let mut p = path.to_path_buf();
    while !p.exists() {
        p = p.parent()?.to_path_buf();
        if p.as_os_str().is_empty() {
            p = std::env::current_dir().ok()?;
        }
    }
    free_at(&p)
}

#[cfg(unix)]
fn free_at(p: &Path) -> Option<u64> {
    use std::os::unix::ffi::OsStrExt;
    let c = std::ffi::CString::new(p.as_os_str().as_bytes()).ok()?;
    let mut s: libc::statvfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::statvfs(c.as_ptr(), &mut s) } != 0 {
        return None;
    }
    Some(s.f_bavail as u64 * s.f_frsize as u64)
}

#[cfg(windows)]
fn free_at(p: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;
    let w: Vec<u16> = p.as_os_str().encode_wide().chain(Some(0)).collect();
    let mut free = 0u64;
    let ok = unsafe { windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW(w.as_ptr(), &mut free, std::ptr::null_mut(), std::ptr::null_mut()) };
    (ok != 0).then_some(free)
}

#[cfg(not(any(unix, windows)))]
fn free_at(_: &Path) -> Option<u64> {
    None
}

/// An extraction's destination, checked: what is there, the bytes it needs, the drive's free bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Plan {
    pub found: Dest,
    pub needed: u64,
    pub free: Option<u64>,
}

/// [`check_dest`] and [`check_space`] (with the drive's free space, or `free` when given: tests).
pub fn plan(dest: &Path, info: &DiscInfo, free: Option<Option<u64>>) -> Result<Plan, String> {
    let found = check_dest(dest, info)?;
    let needed = space_needed(info, &found);
    let free = free.unwrap_or_else(|| free_space(dest));
    check_space(needed, free, &what(info.release))?;
    Ok(Plan { found, needed, free })
}

/// "the PS2 disc's files"
pub fn what(release: Release) -> String {
    format!("the {} disc's files", release.name())
}

/// How far an extraction is.
#[derive(Clone, Debug, Default)]
pub struct Progress {
    pub files: usize,
    pub bytes: u64,
    pub files_total: usize,
    pub bytes_total: u64,
    /// the last file done
    pub last: String,
    /// not begun: an extraction stopped before (its window closed) is still letting go of its file
    pub waiting: bool,
}

impl Progress {
    pub fn fraction(&self) -> f32 {
        if self.bytes_total == 0 {
            if self.files_total == 0 { 0.0 } else { self.files as f32 / self.files_total as f32 }
        } else {
            (self.bytes as f64 / self.bytes_total as f64) as f32
        }
    }
}

/// Extracts the disc at `image` into `dest` (checked again: the folder, the free space), each
/// file as NAME.part until whole, then checks the result as the game would (data_dir::check).
/// Ok: the game data path. Err: in plain words; [`CANCELLED`] when `cancel` was set (looked at
/// each megabyte). `wake`: as the bytes go.
pub fn run(image: &Path, dest: &Path, progress: &Mutex<Progress>, cancel: &AtomicBool, wake: &dyn Fn()) -> Result<PathBuf, String> {
    let info = inspect(image)?;
    plan(dest, &info, None)?;
    let disc = Disc::open(image).map_err(|e| plain_error(&e))?;
    {
        let mut p = progress.lock().unwrap();
        *p = Progress { files_total: info.files, bytes_total: info.bytes, ..Default::default() };
    }
    let r = disc.extract_steps(dest, &[], &mut |s| {
        let mut p = progress.lock().unwrap();
        match s {
            Step::Bytes(n) => p.bytes += n,
            Step::Done(e) => {
                p.files += 1;
                p.last = e.path.clone();
            }
        }
        drop(p);
        wake();
        !cancel.load(Ordering::Relaxed)
    });
    match r {
        Err(e) if e == CANCELLED => return Err(CANCELLED.into()),
        Err(e) => return Err(plain_error(&e)),
        Ok(_) => {}
    }
    data_dir::check(dest).map_err(|e| format!("The files were extracted, but the game wouldn't find its data there: {e}"))
}

/// One extraction at a time, the thread's whole run: one stopped by its window closing finishes
/// its megabyte (and lets go of its .part) before the next, perhaps into the same folder, begins.
static ONE: Mutex<()> = Mutex::new(());

/// [`run`] on a worker thread.
pub struct Job {
    pub image: PathBuf,
    pub dest: PathBuf,
    pub progress: Arc<Mutex<Progress>>,
    cancel: Arc<AtomicBool>,
    rx: Receiver<Result<PathBuf, String>>,
}

impl Job {
    /// `wake` is called as the bytes go and at the end (the window's repaint).
    pub fn start(image: &Path, dest: &Path, wake: impl Fn() + Send + 'static) -> Job {
        let progress = Arc::new(Mutex::new(Progress::default()));
        let cancel = Arc::new(AtomicBool::new(false));
        let (tx, rx) = std::sync::mpsc::channel();
        let (i, d, p, c) = (image.to_path_buf(), dest.to_path_buf(), progress.clone(), cancel.clone());
        std::thread::spawn(move || {
            let _one = match ONE.try_lock() {
                Ok(g) => g,
                Err(std::sync::TryLockError::Poisoned(g)) => g.into_inner(),
                Err(_) => {
                    p.lock().unwrap().waiting = true;
                    wake();
                    ONE.lock().unwrap_or_else(|e| e.into_inner())
                }
            };
            let r = if c.load(Ordering::Relaxed) { Err(CANCELLED.into()) } else { run(&i, &d, &p, &c, &wake) };
            let _ = tx.send(r);
            wake();
        });
        Job { image: image.to_path_buf(), dest: dest.to_path_buf(), progress, cancel, rx }
    }

    pub fn cancel(&self) {
        self.cancel.store(true, Ordering::Relaxed);
    }

    pub fn cancelling(&self) -> bool {
        self.cancel.load(Ordering::Relaxed)
    }

    /// The result, once the thread is done.
    pub fn poll(&self) -> Option<Result<PathBuf, String>> {
        match self.rx.try_recv() {
            Ok(r) => Some(r),
            Err(TryRecvError::Empty) => None,
            Err(TryRecvError::Disconnected) => Some(Err("the extraction stopped unexpectedly".into())),
        }
    }

    pub fn snapshot(&self) -> Progress {
        self.progress.lock().unwrap().clone()
    }
}

impl Drop for Job {
    /// A window closed (or the editor quit) mid-way stops it within a megabyte (the next waits: ONE).
    fn drop(&mut self) {
        self.cancel();
    }
}

