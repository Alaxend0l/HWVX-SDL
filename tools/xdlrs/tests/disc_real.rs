//! The ripper against the real game data, where this machine has it (skipped otherwise): the
//! GameCube image, as .iso and .gcz, must hold the same files, byte for byte, as Dolphin's
//! "Extract Entire Disc" of it. Compared by SHA-1 while streaming: nothing is written.
//!
//! The places can be set with XDLRS_GC_ISO, XDLRS_GC_GCZ and XDLRS_GC_FILES.

use sha1::{Digest, Sha1};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use xdlrs::disc::{Disc, Kind, Release};

fn place(var: &str, default: &str) -> Option<PathBuf> {
    let p = PathBuf::from(std::env::var(var).unwrap_or_else(|_| default.to_string()));
    p.exists().then_some(p)
}

fn gc_files() -> Option<PathBuf> {
    place("XDLRS_GC_FILES", "/home/alaxendol/Develop/HWVX-X-FILE-EDITOR/orig/gc/HWVX GC RIP/files")
}

struct Hasher(Sha1);

impl std::io::Write for Hasher {
    fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
        self.0.update(b);
        Ok(b.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// path -> (size, sha1) of every file of `d`.
fn hashes(d: &Disc) -> BTreeMap<String, (u64, String)> {
    d.entries
        .iter()
        .map(|e| {
            let mut h = Hasher(Sha1::new());
            d.copy(e, &mut h).unwrap();
            (e.path.clone(), (e.size, format!("{:x}", h.0.finalize())))
        })
        .collect()
}

fn compare(image: &Path, kind: Kind) {
    let Some(files) = gc_files() else {
        eprintln!("skipped: no extracted GameCube files");
        return;
    };
    let d = Disc::open(image).unwrap();
    assert_eq!((d.kind, d.release, d.game_id.as_deref()), (kind, Release::Gc, Some("GHWE78")));
    let folder = Disc::open(&files).unwrap();
    assert_eq!(folder.kind, Kind::Extracted);
    let (a, b) = (hashes(&d), hashes(&folder));
    assert_eq!(a.len(), b.len());
    for (p, v) in &b {
        assert_eq!(a.get(p), Some(v), "{p}");
    }
    // one file read straight from the image, as the editor does
    let (at, ipack) = xdlrs::disc::find_archive(image, "HW_IPack").unwrap();
    assert!(at.ends_with("HW_IPack.xgc"));
    assert_eq!(ipack, std::fs::read(files.join("HW_IPack.xgc")).unwrap());
}

#[test]
fn gc_iso_matches_the_extracted_files() {
    match place("XDLRS_GC_ISO", "/home/alaxendol/Develop/HWVX-SDL/temp/Hot Wheels - Velocity X.iso") {
        Some(iso) => compare(&iso, Kind::GcImage),
        None => eprintln!("skipped: no GameCube .iso"),
    }
}

#[test]
fn gc_gcz_matches_the_extracted_files() {
    match place("XDLRS_GC_GCZ", "/home/alaxendol/Develop/HWVX-SDL/temp/Hot Wheels - Velocity X.gcz") {
        Some(gcz) => compare(&gcz, Kind::Gcz),
        None => eprintln!("skipped: no GameCube .gcz"),
    }
}

/// The car stats (cars.rs) read from the image are the folder's.
#[test]
fn car_stats_from_the_image() {
    let (Some(iso), Some(files)) = (place("XDLRS_GC_ISO", "/home/alaxendol/Develop/HWVX-SDL/temp/Hot Wheels - Velocity X.iso"), gc_files()) else {
        eprintln!("skipped: no GameCube data");
        return;
    };
    let Some(root) = xdlrs::repo_root(None) else {
        eprintln!("skipped: no schemas/");
        return;
    };
    let s = xdlrs::schema::Schemas::load(&root.join("schemas")).unwrap();
    let mut a = xdlrs::cars::release(&s, &iso).unwrap();
    let mut b = xdlrs::cars::release(&s, &files).unwrap();
    a["dir"] = serde_json::Value::Null;
    b["dir"] = serde_json::Value::Null;
    assert_eq!(a, b);
}
