//! hwvx-decomp import [--force FILE...] [--diff] [--merge]
//!
//! Import game sources from the HWVX-GC-Decomp submodule into src/game.
//!
//! Applies only mechanical CodeWarrior -> GCC transforms. Portability fixes
//! (handles, endianness, SDK replacement) are made by hand in src/game afterwards,
//! so by default this refuses to overwrite files that already exist.
//!
//!   hwvx-decomp import            # import files that are missing
//!   hwvx-decomp import --force X  # re-import file X (discards local edits)
//!   hwvx-decomp import --diff     # print the decomp commit this was taken from
//!   hwvx-decomp import --merge    # three-way merge decomp changes since
//!                                 # DECOMP_REVISION into the patched files

use crate::util::{finditer, g, re, read, read_lossy, rsearch, sub, write, Re};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::LazyLock;

/// Files from the decomp's "game" library that are really SDK / hardware code and
/// are replaced by src/port instead of being imported.
const EXCLUDE: &[&str] = &[
    "PPCArch.c",        // PowerPC register helpers
    "OS.c",             // Dolphin OS boot
    "DebuggerDriver.c", // AMC/TRK debugger
    "Main_NGC.c",       // GameCube entry/VI/arena setup -> src/port/main_ngc.c
    "allsrc.c",         // Logitech wheel force-feedback SDK -> SDL haptics
    "THPDec.c",         // THP video decoder (PowerPC paired-single)
    "THPAudioDecode.c",
    "THPVideoDecode.c",
    "THPPlayer.c",
    "THPDraw.c",
    "THPPlay.c",
    "THPRead.c",
    "THPAudio.c",
];

static CW_PRAGMA: Re = LazyLock::new(|| {
    re(concat!(
        r"(?m)^\s*#pragma\s+(push|pop|optimization_level|optimize_for_size|use_lmw_stmw|",
        r"readonly_strings|dont_inline|auto_inline|opt_\w+|peephole|peepblock|scheduling|",
        r"fp_contract|inline_depth|force_active|section|sym)\b.*$"
    ))
});
static FIFO_DECL: Re = LazyLock::new(|| re(r"(?m)^.*PPCWGPipe\s+GXWGFifo\s*:\s*0x[0-9A-Fa-f]+\s*;\s*$"));
static FIFO_WRITE: Re = LazyLock::new(|| re(r"GXWGFifo\.(u8|s8|u16|s16|u32|s32|f32)\s*=\s*([^;]+);"));

fn game_objects(decomp: &Path) -> Vec<String> {
    let src = read(decomp.join("configure.py"));
    let m = rsearch(&re(r#"(?s)"lib": "game".*?"objects": \[(.*?)\]"#), &src).expect("game lib in configure.py");
    finditer(&re(r#"Object\(\w+, "([^"]+)""#), g(&m, 1)).iter().map(|c| g(c, 1).to_string()).collect()
}

fn transform(text: &str) -> String {
    let t = sub(&CW_PRAGMA, text, |_| String::new());
    let t = sub(&FIFO_DECL, &t, |_| String::new());
    sub(&FIFO_WRITE, &t, |c| format!("GXWGFifo_{}({});", g(c, 1), g(c, 2)))
}

fn show(decomp: &Path, rev: &str, rel: &str) -> Option<String> {
    let r = Command::new("git").arg("-C").arg(decomp).arg("show").arg(format!("{rev}:{rel}")).output().ok()?;
    if r.status.success() { Some(String::from_utf8_lossy(&r.stdout).into_owned()) } else { None }
}

fn rel_to(p: &Path, root: &Path) -> String {
    p.strip_prefix(root).unwrap_or(p).display().to_string()
}

/// Apply decomp changes old..new to the local copies. The local file is
/// "ours", the old decomp file the base: local rewrites survive, new decomp
/// code arrives unrewritten (rerun the tools afterwards). Conflicts are left
/// as markers.
fn merge(root: &Path, decomp: &Path, files: &[(String, PathBuf)], old: &str, new: &str) -> (Vec<PathBuf>, Vec<PathBuf>) {
    let (mut conflicts, mut changed) = (vec![], vec![]);
    let tmp = root.join("build/import-merge-tmp");
    for (rel, dst) in files {
        let base = show(decomp, old, rel);
        let theirs = show(decomp, new, rel);
        let Some(theirs) = theirs else { continue };
        if base.as_deref() == Some(theirs.as_str()) {
            continue;
        }
        let theirs = transform(&theirs);
        if !dst.exists() {
            write(dst, &theirs);
            println!("new      {}", rel_to(dst, root));
            changed.push(dst.clone());
            continue;
        }
        std::fs::create_dir_all(&tmp).expect("temp dir");
        let (b, t) = (tmp.join("base"), tmp.join("theirs"));
        write(&b, &transform(base.as_deref().unwrap_or("")));
        write(&t, &theirs);
        let short = |s: &str| s.chars().take(10).collect::<String>();
        use std::io::Write;
        std::io::stdout().flush().ok();
        let st = Command::new("git")
            .args(["merge-file", "-L", "port", "-L", &short(old), "-L", &short(new)])
            .arg(dst)
            .arg(&b)
            .arg(&t)
            .status()
            .expect("git merge-file");
        let _ = std::fs::remove_dir_all(&tmp);
        changed.push(dst.clone());
        // Python's returncode: the exit status, or minus the signal
        let code = st.code().unwrap_or(-1);
        let status = if code == 0 { "merged".to_string() } else { format!("CONFLICT ({code})") };
        if code != 0 {
            conflicts.push(dst.clone());
        }
        println!("{:8} {}", status, rel_to(dst, root));
    }
    (conflicts, changed)
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let (mut force, mut diff, mut do_merge) = (vec![], false, false);
    let mut in_force = false;
    for a in &args {
        match a.as_str() {
            "--force" => in_force = true,
            "--diff" => {
                diff = true;
                in_force = false
            }
            "--merge" => {
                do_merge = true;
                in_force = false
            }
            s if in_force && !s.starts_with('-') => force.push(s.to_string()),
            s => {
                eprintln!("usage: hwvx-decomp import [--force [FORCE ...]] [--diff] [--merge]\nunrecognized argument: {s}");
                return 2;
            }
        }
    }
    let decomp = root.join("HWVX-GC-Decomp");
    let dest = root.join("src").join("game");
    let rev = Command::new("git").arg("-C").arg(&decomp).args(["rev-parse", "HEAD"]).output().expect("git");
    let rev = String::from_utf8_lossy(&rev.stdout).trim().to_string();
    if diff {
        println!("{rev}");
        return 0;
    }
    std::fs::create_dir_all(dest.join("include")).expect("src/game/include");

    let mut files: Vec<(String, PathBuf)> = game_objects(&decomp)
        .into_iter()
        .filter(|f| !EXCLUDE.contains(&f.as_str()))
        .map(|f| (format!("src/{f}"), dest.join(&f)))
        .collect();
    // directory order, like Python's Path.glob
    if let Ok(rd) = std::fs::read_dir(decomp.join("include")) {
        for e in rd.flatten() {
            let p = e.path();
            if p.extension().and_then(|x| x.to_str()) == Some("h") {
                let n = p.file_name().unwrap().to_string_lossy().into_owned();
                files.push((format!("include/{n}"), dest.join("include").join(&n)));
            }
        }
    }
    if do_merge {
        let old = read(dest.join("DECOMP_REVISION")).trim().to_string();
        let (conflicts, changed) = merge(root, &decomp, &files, &old, &rev);
        write(dest.join("DECOMP_REVISION"), &format!("{rev}\n"));
        if !changed.is_empty() {
            println!(
                "then rewrite the merged files (see README, Updating from the decomp):\n  hwvx-decomp fieldify --only {} src/game/*.c src/game/include/*.h",
                changed.iter().map(|c| rel_to(c, root)).collect::<Vec<_>>().join(",")
            );
        }
        if !conflicts.is_empty() {
            eprintln!("{} file(s) with conflicts", conflicts.len());
            return 1;
        }
        return 0;
    }
    let mut n = 0;
    for (rel, dst) in &files {
        let name = dst.file_name().unwrap().to_string_lossy();
        if dst.exists() && !force.iter().any(|f| *f == name) {
            continue;
        }
        write(dst, &transform(&read_lossy(decomp.join(rel))));
        n += 1;
    }
    write(dest.join("DECOMP_REVISION"), &format!("{rev}\n"));
    eprintln!("imported {} files from decomp {}", n, &rev[..rev.len().min(10)]);
    0
}
