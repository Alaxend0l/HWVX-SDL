//! hwvx-decomp asm-args NAME...
//!
//! Guess a function's parameters from the decomp's assembly.
//!
//! Reports the argument registers (r3-r10, f1-f8) that the function reads before
//! writing them, scanning the function in address order (branches ignored, calls
//! clobber the volatile registers). An approximation, since arguments are usually
//! copied to saved registers or stored in the prologue. Good enough to settle "how
//! many arguments does this take" disputes between the decomp's call sites and its
//! definitions. Needs HWVX-GC-Decomp built (build/HOTWHE/asm).

use crate::util::{finditer, g, re, read_lossy, rmatch, Re};
use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

static INSN: Re = LazyLock::new(|| {
    re(r"^/\* [0-9A-F]+ [0-9A-F]+\s+(?:[0-9A-F]{2} ){4}\*/\s+(\S+)\s*(.*)(?=\n?\z)")
});
static NO_DEST: Re = LazyLock::new(|| re(r"^(st|cmp|b|mt|dcb|icb|sync|isync|crxor|crclr|psq_st|tw)"));
static REGS: Re = LazyLock::new(|| re(r"\b([rf]\d+)\b"));

fn gpr() -> Vec<String> {
    (3..11).map(|i| format!("r{i}")).collect()
}
fn fpr() -> Vec<String> {
    (1..9).map(|i| format!("f{i}")).collect()
}

fn rglob_s(dir: &Path, out: &mut Vec<PathBuf>) {
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                rglob_s(&p, out);
            } else if p.extension().and_then(|x| x.to_str()) == Some("s") {
                out.push(p);
            }
        }
    }
}

fn find(files: &[(PathBuf, String)], name: &str) -> Option<(PathBuf, Vec<String>)> {
    let head = format!(".fn {name},");
    for (f, text) in files {
        let lines: Vec<&str> = text.split('\n').collect();
        for (i, l) in lines.iter().enumerate() {
            if l.starts_with(&head) {
                let mut out = vec![];
                for l2 in &lines[i + 1..] {
                    if l2.starts_with(".endfn") {
                        break;
                    }
                    out.push(l2.to_string());
                }
                return Some((f.clone(), out));
            }
        }
    }
    None
}

fn regs(s: &str) -> Vec<String> {
    finditer(&REGS, s).iter().map(|c| g(c, 1).to_string()).collect()
}

fn analyze(lines: &[String]) -> (Vec<String>, Vec<String>) {
    let args: Vec<String> = gpr().into_iter().chain(fpr()).collect();
    let is_arg = |r: &str| args.iter().any(|a| a == r);
    let mut written: HashSet<String> = HashSet::new();
    let mut used: Vec<String> = vec![];
    let mut passed: Option<Vec<String>> = None;
    for l in lines {
        let Some(m) = rmatch(&INSN, l.trim()) else { continue };
        let op = g(&m, 1);
        let a = g(&m, 2).split('#').next().unwrap();
        let ops: Vec<&str> = if a.trim().is_empty() { vec![] } else { a.split(',').map(|x| x.trim()).collect() };
        if op == "bl" || op == "bctrl" || op == "blrl" {
            if passed.is_none() {
                passed = Some(args.iter().filter(|r| !written.contains(*r)).cloned().collect());
            }
            written.extend(args.iter().cloned());
            continue;
        }
        let dest: Option<&str> = if rmatch(&NO_DEST, op).is_some() { None } else { ops.first().copied() };
        let has_dest = dest.is_some_and(|d| !d.is_empty());
        let mut srcs: Vec<&str> = if has_dest { ops[1..].to_vec() } else { ops.clone() };
        if has_dest && (op.starts_with("stwu") || op.starts_with("stfdu")) {
            srcs.push(dest.unwrap());
        }
        for s in srcs {
            for r in regs(s) {
                if !written.contains(&r) && is_arg(&r) && !used.contains(&r) {
                    used.push(r);
                }
            }
        }
        if has_dest {
            for r in regs(dest.unwrap()) {
                written.insert(r);
            }
        }
    }
    (used, passed.unwrap_or_default())
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let asm = root.join("HWVX-GC-Decomp/build/HOTWHE/asm");
    let mut paths = vec![];
    rglob_s(&asm, &mut paths);
    paths.sort();
    let files: Vec<(PathBuf, String)> = paths.into_iter().map(|p| {
        let t = read_lossy(&p);
        (p, t)
    }).collect();
    for name in &args {
        let Some((f, lines)) = find(&files, name) else {
            println!("{name}: not found");
            continue;
        };
        let (used, passed) = analyze(&lines);
        let mut regs: Vec<String> = gpr().into_iter().filter(|r| used.contains(r)).collect();
        regs.extend(fpr().into_iter().filter(|r| used.contains(r)));
        let maybe: Vec<&str> = ["r3", "r4", "r5", "f1"]
            .into_iter()
            .filter(|r| passed.iter().any(|p| p == r) && !used.iter().any(|u| u == r))
            .collect();
        let extra = if maybe.is_empty() {
            String::new()
        } else {
            format!("   (unset before the first call, maybe passed on: {})", maybe.join(" "))
        };
        let shown = if regs.is_empty() { "(no args read)".to_string() } else { regs.join(" ") };
        println!("{}: {}   [{}]{}", name, shown, f.file_name().unwrap().to_string_lossy(), extra);
    }
    0
}
