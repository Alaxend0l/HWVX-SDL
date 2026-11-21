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
    let e = canonical(&e);
    e.parent().map(Path::to_path_buf)
}

fn exe_name(name: &str) -> String {
    if cfg!(windows) { format!("{name}.exe") } else { name.to_string() }
}

/// `std::fs::canonicalize`, without Windows' `\\?\` (verbatim) prefix where the path means the
/// same without it (as the dunce crate does): a verbatim path takes no "/" and no "..", and the
/// game and the schemas' loader joined "/" onto it. Unchanged when it can't be made (the path as
/// given) or the prefix is needed (a long path, a reserved name).
pub fn canonical(p: &Path) -> PathBuf {
    let c = std::fs::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
    if cfg!(windows) { unverbatim(&c) } else { c }
}

/// `\\?\C:\x` as `C:\x`, `\\?\UNC\server\share\x` as `\\server\share\x`, when that is safe.
pub fn unverbatim(p: &Path) -> PathBuf {
    let Some(s) = p.to_str() else { return p.to_path_buf() };
    let plain = if let Some(rest) = s.strip_prefix(r"\\?\UNC\") {
        format!(r"\\{rest}")
    } else if let Some(rest) = s.strip_prefix(r"\\?\") {
        let b = rest.as_bytes();
        if b.len() < 3 || !b[0].is_ascii_alphabetic() || b[1] != b':' || b[2] != b'\\' {
            return p.to_path_buf(); // (a volume GUID path, or another kind: kept)
        }
        rest.to_string()
    } else {
        return p.to_path_buf();
    };
    // MAX_PATH, and names Windows reads differently without the prefix
    let reserved = |n: &str| {
        let stem = n.split('.').next().unwrap_or("").trim_end().to_ascii_uppercase();
        matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL" | "CONIN$" | "CONOUT$")
            || ((stem.starts_with("COM") || stem.starts_with("LPT")) && stem.len() == 4 && stem.as_bytes()[3].is_ascii_digit())
    };
    let bad_name = |n: &str| {
        n.is_empty()
            || n == "."
            || n == ".."
            || n.ends_with('.')
            || n.ends_with(' ')
            || reserved(n)
            || n.chars().any(|c| c < ' ' || "<>:\"/|?*".contains(c))
    };
    let names = if plain.starts_with(r"\\") { plain[2..].split('\\').skip(2) } else { plain[3..].split('\\').skip(0) };
    if plain.len() >= 260 || names.filter(|n| !n.is_empty()).any(bad_name) {
        return p.to_path_buf();
    }
    PathBuf::from(plain)
}

/// The packaged resources beside the program, if this is a package.
fn packaged() -> Option<PathBuf> {
    let d = exe_dir()?;
    [d.join("../Resources"), d.join("../share/hwvx-editor"), d.clone()]
        .into_iter()
        .find(|r| r.join(PROBE).is_file())
        .map(|r| canonical(&r))
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

#[cfg(test)]
mod tests {
    use super::unverbatim;
    use std::path::Path;

    #[test]
    fn verbatim_prefix_stripped_when_safe() {
        let u = |s: &str| unverbatim(Path::new(s)).to_string_lossy().into_owned();
        assert_eq!(u(r"\\?\C:\Games\HWVX Editor\schemas"), r"C:\Games\HWVX Editor\schemas");
        assert_eq!(u(r"\\?\C:\"), r"C:\");
        assert_eq!(u(r"\\?\UNC\nas\share\hwvx"), r"\\nas\share\hwvx");
        // kept: needed or not a drive path
        assert_eq!(u(r"\\?\C:\x\NUL.txt"), r"\\?\C:\x\NUL.txt");
        assert_eq!(u(r"\\?\C:\x\com1"), r"\\?\C:\x\com1");
        assert_eq!(u(r"\\?\C:\x\trailing."), r"\\?\C:\x\trailing.");
        assert_eq!(u(r"\\?\Volume{1234}\x"), r"\\?\Volume{1234}\x");
        let long = format!(r"\\?\C:\{}", "a".repeat(300));
        assert_eq!(u(&long), long);
        assert_eq!(u("/usr/local"), "/usr/local");
    }
}
