//! Save as mod: the one place the level tools call the no-game-data check ([`crate::modcheck`],
//! tools/xdl_mod.py's port): the index of the player's own data made once into the cache, then
//! the mod folder checked against it and packed, as tools/xdl_level.py `mod --xmod` does with
//! the Python. No Python runs.

use crate::error::{Error, Result};
use sha1::Digest as _;
use sha2::Sha256;
use std::path::{Path, PathBuf};

fn lerr(m: impl Into<String>) -> Error {
    Error::level(m)
}

fn hexs(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn tail(s: &str, n: usize) -> String {
    let c: Vec<char> = s.chars().collect();
    c[c.len().saturating_sub(n)..].iter().collect()
}

/// Every file under root (directories walked, symlinked ones not followed), sorted as Python
/// sorts Paths (by their parts).
fn files_under(root: &Path) -> Vec<PathBuf> {
    let mut out = vec![];
    let mut stack = vec![root.to_path_buf()];
    while let Some(d) = stack.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        for e in rd.flatten() {
            let p = e.path();
            let ft = e.file_type().ok();
            if ft.is_some_and(|t| t.is_dir()) {
                stack.push(p);
            } else if p.is_file() {
                out.push(p);
            }
        }
    }
    out.sort_by(|a, b| a.components().cmp(b.components()));
    out
}

/// The index the check packs against: `index`, or one made from `data` once into the cache
/// (keyed as xdl_level.py keys it: the folder, its files' names, sizes and times, and the
/// indexer: here this crate's sources where the Python hashes xdl_mod.py, so a newer indexer
/// makes a new index; the .npz format is the Python's, either reads the other's).
fn mod_index(root: &Path, index: Option<&Path>, data: Option<&Path>, cache: &Path) -> Result<PathBuf> {
    if let Some(i) = index {
        return Ok(i.to_path_buf());
    }
    let Some(data) = data else {
        return Err(lerr("a .xmod needs the no-game-data check: --index INDEX.npz or --data DATA"));
    };
    let mut fh = Sha256::new();
    fh.update(b"xdlrs modcheck\0");
    fh.update(crate::level::TOOL_ID.as_bytes());
    fh.update(b"\0");
    let files_hash = hexs(&fh.finalize());
    let droot = std::fs::canonicalize(data).unwrap_or_else(|_| data.to_path_buf());
    let mut h = sha1::Sha1::new();
    h.update(droot.display().to_string().as_bytes());
    h.update(b"\0");
    h.update(files_hash.as_bytes());
    for f in files_under(&droot) {
        let Ok(m) = std::fs::metadata(&f) else { continue };
        let ns = m.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_nanos()).unwrap_or(0);
        let rel = f.strip_prefix(&droot).unwrap_or(&f).display().to_string();
        h.update(format!("{rel}\0{}\0{ns}\0", m.len()).as_bytes());
    }
    let key = &hexs(&h.finalize())[..12];
    let idx = cache.join(format!("mod-index-{key}.npz"));
    if !idx.exists() {
        std::fs::create_dir_all(cache).map_err(|e| Error::io(format!("{}: {e}", cache.display())))?;
        let o = crate::modcheck::IndexOptions { archives: None, no_geometry: false, jobs: 4 };
        crate::modcheck::build_index(&[data.to_path_buf()], &idx, &o, root).map_err(|e| lerr(format!("the index: {}", tail(e.0.trim(), 400))))?;
    }
    Ok(idx)
}

/// The mod folder `mod_dir` checked for game data and packed into `xmod` (modcheck, as
/// tools/xdl_mod.py pack, against the player's own data's index). Ok: the packer's last line; Err (Kind::Level): why
/// it wasn't packed.
pub fn check_and_pack(root: &Path, mod_dir: &Path, xmod: &Path, index: Option<&Path>, data: Option<&Path>, cache: &Path) -> Result<String> {
    let idx = mod_index(root, index, data, cache)?;
    let opt = crate::modcheck::Options { root: Some(root.to_path_buf()), ..Default::default() };
    let r = crate::modcheck::check_and_pack(mod_dir, &idx, Some(xmod), &opt);
    let text = format!("{}{}", r.stdout, r.stderr);
    let text = text.trim();
    if r.code != 0 {
        return Err(lerr(format!("the package: {}", tail(text, 1500))));
    }
    Ok(match text.lines().last() {
        Some(l) if !text.is_empty() => l.to_string(),
        _ => format!("{}: packed", xmod.display()),
    })
}
