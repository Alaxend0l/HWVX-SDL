//! Where the editor finds what it needs at run time, packaged or in the checkout.
//!
//! Resources (the folder holding schemas/, which the level tools read as their root):
//!   1. $HWVX_EDITOR_RESOURCES
//!   2. beside the program: ../Resources (a macOS .app), ../share/hwvx-editor (Linux), or the
//!      program's own folder (Windows, a plain folder)
//!   3. the checkout the program was built in (dev: the first folder above it with schemas/)
//! The game (hwvx) and hwvx-x: beside the program first (a package bundles them there), then
//! the checkout's build/hwvx and target/release/hwvx-x, then (hwvx-x) the PATH.
//! A package keeps its scratch (viewer-pref) in the user's own folder, never inside itself.

use std::path::{Path, PathBuf};

const PROBE: &str = "schemas/Trigger.schema.json";

fn exe_dir() -> Option<PathBuf> {
    let e = std::env::current_exe().ok()?;
    let e = std::fs::canonicalize(&e).unwrap_or(e);
    e.parent().map(Path::to_path_buf)
}

fn exe_name(name: &str) -> String {
    if cfg!(windows) { format!("{name}.exe") } else { name.to_string() }
}

/// The packaged resources beside the program, if this is a package.
fn packaged() -> Option<PathBuf> {
    let d = exe_dir()?;
    [d.join("../Resources"), d.join("../share/hwvx-editor"), d.clone()]
        .into_iter()
        .find(|r| r.join(PROBE).is_file())
        .map(|r| std::fs::canonicalize(&r).unwrap_or(r))
}

/// 1 and 2 only: the resources given or packaged (they win over a --game's checkout).
pub fn given() -> Option<PathBuf> {
    std::env::var_os("HWVX_EDITOR_RESOURCES").filter(|r| !r.is_empty()).map(PathBuf::from).or_else(packaged)
}

/// The resources folder (schemas/ in it), by the order above.
pub fn root() -> Option<PathBuf> {
    given().or_else(|| exe_dir()?.ancestors().find(|d| d.join(PROBE).is_file()).map(Path::to_path_buf))
}

/// tools/ under the resources (the level tools take its parent as their root; it needn't exist).
pub fn tools() -> Option<PathBuf> {
    root().map(|r| r.join("tools"))
}

/// The game: beside the program, else <checkout>/build/hwvx above it.
pub fn game() -> Option<PathBuf> {
    let name = exe_name("hwvx");
    let d = exe_dir()?;
    let p = d.join(&name);
    if p.is_file() {
        return Some(p);
    }
    d.ancestors().map(|a| a.join("build").join(&name)).find(|p| p.is_file())
}

/// hwvx-x beside the program (a package); None: xdlrs looks in the checkout, then the PATH.
pub fn hwvx_x() -> Option<PathBuf> {
    exe_dir().map(|d| d.join(exe_name("hwvx-x"))).filter(|p| p.is_file())
}

/// The editor's own scratch (the viewer's pref folder, test drives): beside the program in a
/// checkout; a package's lives in the user's folder (Application Support, $XDG_DATA_HOME,
/// %APPDATA%), as HWVX-Editor/viewer-pref.
pub fn default_pref() -> PathBuf {
    if packaged().is_some() {
        let home = |v: &str| std::env::var_os(v).filter(|x| !x.is_empty()).map(PathBuf::from);
        let base = if cfg!(target_os = "macos") {
            home("HOME").map(|h| h.join("Library/Application Support/HWVX-Editor"))
        } else if cfg!(windows) {
            home("APPDATA").map(|h| h.join("HWVX-Editor"))
        } else {
            home("XDG_DATA_HOME").or_else(|| home("HOME").map(|h| h.join(".local/share"))).map(|h| h.join("hwvx-editor"))
        };
        if let Some(b) = base {
            return b.join("viewer-pref");
        }
    }
    exe_dir().map(|d| d.join("viewer-pref")).unwrap_or_else(|| "viewer-pref".into())
}
