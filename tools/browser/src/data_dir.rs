//! The game data the editor's game reads (its --data): checked as the game checks it
//! (src/port/main_sdl.c DataIn), remembered in the editor's pref folder (editor-data.txt), and
//! suggested from the game's own launcher (sources.cfg in the game's save folder). The
//! system's file dialogs for choosing it are here too.

use std::path::{Path, PathBuf};

/// A release's root holds one of these (main_sdl.c HasData): GameCube, PC, PS2.
const MARKERS: [&str; 4] = [
    "HW_share.xgc",
    "HW_share.xpc",
    "XPS/ENGLISH/HW_share.xps",
    "HW_share.xps",
];
/// Disc images the game opens (disc.c Disc_IsImageName).
const IMAGES: [&str; 3] = ["iso", "gcm", "gcz"];
/// The editor's remembered choice, in its pref folder.
const REMEMBERED: &str = "editor-data.txt";

fn is_image(p: &Path) -> bool {
    p.extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| IMAGES.iter().any(|i| e.eq_ignore_ascii_case(i)))
}

/// `rel` (/-separated) under `dir`, any case (Port_FindNoCase).
fn exists_no_case(dir: &Path, rel: &str) -> bool {
    let mut cur = dir.to_path_buf();
    for part in rel.split('/') {
        let Ok(rd) = std::fs::read_dir(&cur) else {
            return false;
        };
        let Some(e) = rd
            .flatten()
            .find(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case(part))
        else {
            return false;
        };
        cur = e.path();
    }
    true
}

fn has_data(dir: &Path) -> bool {
    MARKERS.iter().any(|m| exists_no_case(dir, m))
}

/// What the game would read for `p`: `p` itself (extracted files or a disc image), its files/
/// folder (Dolphin's "Extract Entire Disc"), or a disc image in it. Err: why not, for the player.
pub fn check(p: &Path) -> Result<PathBuf, String> {
    if p.as_os_str().is_empty() {
        return Err("choose a folder or a disc image".into());
    }
    if p.is_file() {
        return if is_image(p) {
            plays(p).map(|_| p.to_path_buf())
        } else {
            Err(format!(
                "{} is not a disc image (.iso, .gcm or .gcz)",
                p.display()
            ))
        };
    }
    if !p.is_dir() {
        return Err(format!("{} does not exist", p.display()));
    }
    if has_data(p) {
        return Ok(p.to_path_buf());
    }
    let files = p.join("files");
    if has_data(&files) {
        return Ok(files);
    }
    // the first image the game plays; another's (a PS2 or PC .iso) says why not
    let mut why = None;
    for i in images_in(p) {
        match plays(&i) {
            Ok(()) => return Ok(i),
            Err(e) => {
                why.get_or_insert(e);
            }
        }
    }
    Err(why.unwrap_or_else(|| {
        format!(
            "no game data in {}: expected the disc's extracted files (HW_share.xgc, worlds_/, audio_/ …), \
             a files/ folder with them, or a disc image (.iso)",
            p.display()
        )
    }))
}

/// The disc images in `dir` (by extension), sorted.
pub fn images_in(dir: &Path) -> Vec<PathBuf> {
    let mut images: Vec<PathBuf> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|e| e.is_file() && is_image(e))
        .collect();
    images.sort();
    images
}

/// Whether the game reads the image `p` itself: a GameCube one, not a .gcz (disc.c refuses it).
/// A PS2 or PC .iso was once taken as game data and the game failed at every launch.
fn plays(p: &Path) -> Result<(), String> {
    let d = xdlrs::disc::Disc::open(p).map_err(|e| format!("{}: not a disc image the game reads ({e})", p.display()))?;
    match (d.release, d.kind) {
        (xdlrs::disc::Release::Gc, xdlrs::disc::Kind::GcImage) => Ok(()),
        (r, k) => Err(format!(
            "{} is the {} release's {}: the game can't read it as it is. Extract it first (File > Extract a disc…).",
            p.display(),
            r.name(),
            if k == xdlrs::disc::Kind::Gcz { ".gcz image" } else { "disc image" }
        )),
    }
}

/// The choice kept from last time, if it is still there.
pub fn remembered(pref: &Path) -> Option<PathBuf> {
    let b = std::fs::read(pref.join(REMEMBERED)).ok()?;
    // only a line end (one typed in by hand) goes: a folder's name may begin or end with spaces
    let mut b = b.as_slice();
    while let [rest @ .., b'\r' | b'\n'] = b {
        b = rest;
    }
    check(&path_from_bytes(b)?).ok()
}

/// The path's bytes as kept: a Unix path as it is (its name need not be UTF-8); elsewhere UTF-8.
#[cfg(unix)]
fn path_bytes(p: &Path) -> Option<Vec<u8>> {
    Some(std::os::unix::ffi::OsStrExt::as_bytes(p.as_os_str()).to_vec())
}
#[cfg(not(unix))]
fn path_bytes(p: &Path) -> Option<Vec<u8>> {
    p.to_str().map(|s| s.as_bytes().to_vec())
}
#[cfg(unix)]
fn path_from_bytes(b: &[u8]) -> Option<PathBuf> {
    Some(PathBuf::from(<std::ffi::OsStr as std::os::unix::ffi::OsStrExt>::from_bytes(b)))
}
#[cfg(not(unix))]
fn path_from_bytes(b: &[u8]) -> Option<PathBuf> {
    std::str::from_utf8(b).ok().map(PathBuf::from)
}

pub fn remember(pref: &Path, data: &Path) -> Result<(), String> {
    // (lossy, it would name another folder next time: asked for again instead)
    let b = path_bytes(data).ok_or_else(|| format!("{}: a name that can't be written down", data.display()))?;
    std::fs::create_dir_all(pref).map_err(|e| format!("{}: {e}", pref.display()))?;
    std::fs::write(pref.join(REMEMBERED), b).map_err(|e| format!("{}: {e}", pref.join(REMEMBERED).display()))
}

/// The game's save folder (SDL_GetPrefPath("HWVX-SDL", "HotWheelsVelocityX")).
fn game_pref() -> Option<PathBuf> {
    let var = |v: &str| {
        std::env::var_os(v)
            .filter(|x| !x.is_empty())
            .map(PathBuf::from)
    };
    let base = if cfg!(target_os = "macos") {
        var("HOME").map(|h| h.join("Library/Application Support"))
    } else if cfg!(windows) {
        var("APPDATA")
    } else {
        var("XDG_DATA_HOME").or_else(|| var("HOME").map(|h| h.join(".local/share")))
    };
    base.map(|b| b.join("HWVX-SDL").join("HotWheelsVelocityX"))
}

/// The release the game's launcher plays (sources.cfg: play=, then gc=, pc=, ps2=), if it is
/// still there: what the player already showed the game.
pub fn from_launcher() -> Option<PathBuf> {
    let pref = game_pref()?;
    let cfg = std::fs::read_to_string(pref.join("sources.cfg")).ok()?;
    let get = |k: &str| {
        cfg.lines()
            .find_map(|l| l.strip_prefix(k).and_then(|r| r.strip_prefix('=')))
            .map(str::trim)
            .filter(|v| !v.is_empty())
    };
    let play = get("play").filter(|p| matches!(*p, "gc" | "pc" | "ps2"));
    play.into_iter()
        .chain(["gc", "pc", "ps2"])
        .filter_map(get)
        .find_map(|v| {
            let p = Path::new(v);
            check(&if p.is_relative() {
                pref.join(p)
            } else {
                p.to_path_buf()
            })
            .ok()
        })
}

// ---- the system's dialogs -------------------------------------------------------------------

/// Err: no dialog program (Linux: zenity or kdialog).
type Choice = Result<Option<String>, String>;

fn run(cmd: &mut std::process::Command) -> Choice {
    let out = cmd.output().map_err(|e| e.to_string())?;
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    Ok((out.status.success() && !s.is_empty()).then_some(s))
}

fn on_path(name: &str) -> bool {
    std::env::var_os("PATH")
        .is_some_and(|p| std::env::split_paths(&p).any(|d| d.join(name).is_file()))
}

const NO_DIALOG: &str = "no file dialog here (install zenity or kdialog): type the path instead";

/// A folder, in the system's dialog. The prompt reaches the dialog as an argument or an
/// environment variable, never inside a script's text.
pub fn choose_folder(prompt: &str) -> Choice {
    let r = if cfg!(target_os = "macos") {
        run(std::process::Command::new("osascript").args([
            "-e",
            "on run argv",
            "-e",
            "POSIX path of (choose folder with prompt (item 1 of argv))",
            "-e",
            "end run",
            prompt,
        ]))
    } else if cfg!(windows) {
        run(xdlrs::command("powershell").env("HWVX_PROMPT", prompt).args([
            "-NoProfile",
            "-Command",
            "[Console]::OutputEncoding = [Text.Encoding]::UTF8; Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.FolderBrowserDialog; $d.Description = $env:HWVX_PROMPT; if ($d.ShowDialog() -eq 'OK') { $d.SelectedPath }",
        ]))
    } else if on_path("zenity") {
        run(std::process::Command::new("zenity").args([
            "--file-selection",
            "--directory",
            &format!("--title={prompt}"),
        ]))
    } else if on_path("kdialog") {
        run(std::process::Command::new("kdialog").args([
            "--title",
            prompt,
            "--getexistingdirectory",
            ".",
        ]))
    } else {
        Err(NO_DIALOG.into())
    };
    r.map(|c| {
        c.map(|s| {
            if s.len() > 1 {
                s.trim_end_matches(['/', '\\']).to_string()
            } else {
                s
            }
        })
    })
}

/// A disc image (.iso, .gcm, .gcz; the PC CD's .cue/.bin, for extracting), in the system's dialog.
pub fn choose_image(prompt: &str) -> Choice {
    if cfg!(target_os = "macos") {
        run(std::process::Command::new("osascript").args([
            "-e",
            "on run argv",
            "-e",
            "POSIX path of (choose file with prompt (item 1 of argv))",
            "-e",
            "end run",
            prompt,
        ]))
    } else if cfg!(windows) {
        run(xdlrs::command("powershell").env("HWVX_PROMPT", prompt).args([
            "-NoProfile",
            "-Command",
            "[Console]::OutputEncoding = [Text.Encoding]::UTF8; Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = $env:HWVX_PROMPT; $d.Filter = 'Disc image (*.iso;*.gcm;*.gcz;*.cue;*.bin)|*.iso;*.gcm;*.gcz;*.cue;*.bin|All files (*.*)|*.*'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }",
        ]))
    } else if on_path("zenity") {
        run(std::process::Command::new("zenity").args([
            "--file-selection",
            &format!("--title={prompt}"),
            "--file-filter=Disc image | *.iso *.gcm *.gcz *.cue *.bin *.ISO *.GCM *.GCZ *.CUE *.BIN",
            "--file-filter=All files | *",
        ]))
    } else if on_path("kdialog") {
        run(std::process::Command::new("kdialog").args([
            "--title",
            prompt,
            "--getopenfilename",
            ".",
            "*.iso *.gcm *.gcz *.cue *.bin *.ISO *.GCM *.GCZ *.CUE *.BIN|Disc image",
        ]))
    } else {
        Err(NO_DIALOG.into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let d =
            std::env::temp_dir().join(format!("hwvx-data-dir-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn finds_what_the_game_finds() {
        let d = scratch("finds");
        assert!(check(&d).is_err(), "an empty folder");
        assert!(check(&d.join("missing")).is_err());
        // a files/ folder (any case of the marker)
        std::fs::create_dir_all(d.join("files")).unwrap();
        std::fs::write(d.join("files/hw_share.XGC"), b"").unwrap();
        assert_eq!(check(&d).unwrap(), d.join("files"));
        assert_eq!(check(&d.join("files")).unwrap(), d.join("files"));
        // a PS2 root
        let p = scratch("ps2");
        std::fs::create_dir_all(p.join("XPS/ENGLISH")).unwrap();
        std::fs::write(p.join("XPS/ENGLISH/HW_share.xps"), b"").unwrap();
        assert_eq!(check(&p).unwrap(), p);
        // a disc image in the folder, or chosen itself; another file is refused
        use xdlrs::disc::test_images::{Node, gamecube, iso9660};
        let i = scratch("image");
        std::fs::write(i.join("game.ISO"), gamecube("GHWE78", &[("HW_share.xgc", b"s")])).unwrap();
        std::fs::write(i.join("notes.txt"), b"").unwrap();
        assert_eq!(check(&i).unwrap(), i.join("game.ISO"));
        assert_eq!(check(&i.join("game.ISO")).unwrap(), i.join("game.ISO"));
        assert!(check(&i.join("notes.txt")).is_err());
        // a PS2 .iso (or anything else named .iso) is not game data: extracted first
        let o = scratch("ps2-image");
        let mut share = vec![0u8; 64];
        share[8..12].copy_from_slice(&183u32.to_le_bytes());
        let ps2 = iso9660(&[Node::Dir("XPS", vec![Node::Dir("ENGLISH", vec![Node::File("HW_share.xps", &share)])])]);
        std::fs::write(o.join("ps2.iso"), ps2).unwrap();
        assert!(check(&o).unwrap_err().contains("Extract it first"));
        assert!(check(&o.join("ps2.iso")).unwrap_err().contains("PlayStation 2"));
        std::fs::write(o.join("empty.iso"), b"").unwrap();
        assert!(check(&o.join("empty.iso")).is_err());
        for x in [d, p, i, o] {
            let _ = std::fs::remove_dir_all(x);
        }
    }

    #[test]
    fn remembers_only_what_is_still_there() {
        let pref = scratch("pref");
        let data = scratch("data");
        assert_eq!(remembered(&pref), None);
        std::fs::write(data.join("HW_share.xgc"), b"").unwrap();
        remember(&pref, &data).unwrap();
        assert_eq!(remembered(&pref), Some(data.clone()));
        std::fs::remove_file(data.join("HW_share.xgc")).unwrap();
        assert_eq!(remembered(&pref), None, "the data was moved away");
        // spaces at either end are the folder's own; a line end typed by hand is not
        let spaced = scratch(" spaced ");
        std::fs::write(spaced.join("HW_share.xgc"), b"").unwrap();
        remember(&pref, &spaced).unwrap();
        assert_eq!(remembered(&pref), Some(spaced.clone()));
        let mut typed = std::fs::read(pref.join(REMEMBERED)).unwrap();
        typed.extend_from_slice(b"\r\n");
        std::fs::write(pref.join(REMEMBERED), typed).unwrap();
        assert_eq!(remembered(&pref), Some(spaced.clone()));
        // a name that isn't UTF-8 (Linux allows one) comes back as it was
        #[cfg(target_os = "linux")]
        {
            use std::os::unix::ffi::OsStrExt;
            let odd = std::env::temp_dir().join(std::ffi::OsStr::from_bytes(b"hwvx-data-dir-\xff"));
            std::fs::create_dir_all(&odd).unwrap();
            std::fs::write(odd.join("HW_share.xgc"), b"").unwrap();
            remember(&pref, &odd).unwrap();
            assert_eq!(remembered(&pref), Some(odd.clone()));
            let _ = std::fs::remove_dir_all(odd);
        }
        for x in [pref, data, spaced] {
            let _ = std::fs::remove_dir_all(x);
        }
    }
}
