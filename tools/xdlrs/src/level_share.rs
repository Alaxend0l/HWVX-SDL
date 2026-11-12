//! Save as mod: the one place the level tools call the no-game-data check (tools/xdl_mod.py).
//!
//! [`check_and_pack`] still runs the Python (`xdl_mod.py index` once per data folder into the
//! cache, then `xdl_mod.py pack`), exactly as tools/xdl_level.py `mod --xmod` does; the Rust port
//! of xdl_mod (stage 3, the `modcheck` module) replaces this function's body. Nothing else in
//! the crate or the editor runs Python.

use crate::error::{Error, Result};
use sha1::Digest as _;
use sha2::Sha256;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

fn lerr(m: impl Into<String>) -> Error {
    Error::level(m)
}

fn python() -> String {
    std::env::var("HWVX_PYTHON").ok().filter(|p| !p.is_empty()).unwrap_or_else(|| "python3".into())
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

/// The index tools/xdl_mod.py packs against: `index`, or one made from `data` once into the
/// cache (keyed as xdl_level.py keys it: the folder, its files' names, sizes and times, and
/// xdl_mod.py itself; the Python and this share the cache).
fn mod_index(root: &Path, index: Option<&Path>, data: Option<&Path>, cache: &Path) -> Result<PathBuf> {
    if let Some(i) = index {
        return Ok(i.to_path_buf());
    }
    let Some(data) = data else {
        return Err(lerr("a .xmod needs the no-game-data check: --index INDEX.npz or --data DATA"));
    };
    let tool = root.join("tools").join("xdl_mod.py");
    let mut fh = Sha256::new();
    fh.update(b"xdl_mod.py\0");
    match std::fs::read(&tool) {
        Ok(b) => fh.update(&b),
        Err(_) => fh.update(b"-"),
    }
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
        let r = Command::new(python())
            .arg(&tool)
            .arg("index")
            .arg(data)
            .arg("-o")
            .arg(&idx)
            .stdin(Stdio::null())
            .output()
            .map_err(|e| lerr(format!("xdl_mod.py index: {e}")))?;
        if !r.status.success() {
            let t = if r.stderr.is_empty() { &r.stdout } else { &r.stderr };
            return Err(lerr(format!("xdl_mod.py index: {}", tail(String::from_utf8_lossy(t).trim(), 400))));
        }
    }
    Ok(idx)
}

/// The mod folder `mod_dir` checked for game data and packed into `xmod` (tools/xdl_mod.py pack
/// against the player's own data's index). Ok: the packer's last line; Err (Kind::Level): why
/// it wasn't packed.
pub fn check_and_pack(root: &Path, mod_dir: &Path, xmod: &Path, index: Option<&Path>, data: Option<&Path>, cache: &Path) -> Result<String> {
    let idx = mod_index(root, index, data, cache)?;
    let r = Command::new(python())
        .arg(root.join("tools").join("xdl_mod.py"))
        .arg("pack")
        .arg(mod_dir)
        .arg("-o")
        .arg(xmod)
        .arg("--index")
        .arg(&idx)
        .stdin(Stdio::null())
        .output()
        .map_err(|e| lerr(format!("the package: {e}")))?;
    let text = format!("{}{}", String::from_utf8_lossy(&r.stdout), String::from_utf8_lossy(&r.stderr));
    let text = text.trim();
    if !r.status.success() {
        return Err(lerr(format!("the package: {}", tail(text, 1500))));
    }
    Ok(match text.lines().last() {
        Some(l) if !text.is_empty() => l.to_string(),
        _ => format!("{}: packed", xmod.display()),
    })
}
