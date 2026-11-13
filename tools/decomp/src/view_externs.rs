//! view-externs (formerly tools/view_externs.py): resolve "conflicting types"
//! between block-scope externs of one global.
//!
//! Decomp functions each declare the globals they use, inside the function, with
//! whatever type fits that function (often a struct type local to it).
//! CodeWarrior accepted different types for the same object in one file; GCC
//! does not. Each declaration is a valid *view* of the object for its function,
//! so this keeps them as views: inside the function, the declaration
//!
//!     extern Interface g_interface[];
//!
//! becomes a self-referential macro (the inner name is not re-expanded, so it
//! names the object itself)
//!
//!     #define g_interface ((Interface *)(void *)&g_interface)
//!
//! with an #undef before the function's closing brace, and the file gets one
//! file-scope declaration of the object for the macro to refer to.
//!
//! Names are taken from GCC's "conflicting types for" errors in a build log:
//!
//!   ninja -C build -k 0 > build.log 2>&1; hwvx-decomp view-externs build.log
//!
//! Function pointers and pointer-vs-integer conflicts are left alone: with 32-bit
//! handles those are real differences (see README) and need a human.

use crate::fieldify::read_text;
use crate::util::{finditer, g, re, rsearch, squash};
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

const INTS: &[&str] = &["s8", "u8", "s16", "u16", "s32", "u32", "int", "char", "short", "long", "signed", "unsigned", "BOOL"];

/// depth[i] = brace depth before text[i] (comments/strings skipped).
fn depth_map(text: &str) -> Vec<i64> {
    let b = text.as_bytes();
    let n = b.len();
    let mut depth = vec![0i64; n + 1];
    let mut d = 0i64;
    let mut i = 0;
    while i < n {
        let c = b[i];
        if b[i..].starts_with(b"/*") {
            let j = match text[i + 2..].find("*/") {
                Some(k) => i + 2 + k + 2,
                None => n,
            };
            for k in i..j {
                depth[k] = d;
            }
            i = j;
            continue;
        }
        if b[i..].starts_with(b"//") || c == b'"' || c == b'\'' {
            let j;
            if c == b'"' || c == b'\'' {
                let mut jj = i + 1;
                while jj < n && b[jj] != c && b[jj] != b'\n' {
                    jj += if b[jj] == b'\\' { 2 } else { 1 };
                }
                j = jj + 1;
            } else {
                j = match text[i..].find('\n') {
                    Some(k) => i + k,
                    None => n,
                };
            }
            for k in i..j.min(n) {
                depth[k] = d;
            }
            i = j;
            continue;
        }
        depth[i] = d;
        if c == b'{' {
            d += 1;
        } else if c == b'}' {
            d -= 1;
        }
        i += 1;
    }
    depth[n] = d;
    depth
}

/// Offset of the closing brace of the top-level block containing pos.
fn func_end(text: &str, depth: &[i64], pos: usize) -> usize {
    let b = text.as_bytes();
    let mut i = pos;
    while i < b.len() {
        if b[i] == b'}' && depth[i] == 1 {
            return i;
        }
        i += 1;
    }
    panic!("ValueError: no function end");
}

/// Offset of the start of the line where the top-level definition begins.
fn func_start(text: &str, depth: &[i64], pos: usize) -> usize {
    let b = text.as_bytes();
    let mut i = pos;
    while i > 0 && !(b[i] == b'{' && depth[i] == 0) {
        i -= 1;
    }
    // back to the start of the declarator line(s): previous ; } or blank line
    let pre = &text[..i];
    let r = |p: &str| pre.rfind(p).map(|x| x as i64).unwrap_or(-1);
    let j = r("\n\n").max(r(";").max(r("}")));
    if j >= 0 {
        text[j as usize..].find('\n').map(|k| j as usize + k + 1).unwrap_or(0)
    } else {
        0
    }
}

fn view(base: &str, stars: &str, dims: &[String], n: &str) -> String {
    let t = if !stars.is_empty() { format!("{base} {stars}") } else { base.to_string() };
    let ptr = if !stars.is_empty() { format!("{t}*") } else { format!("{t} *") };
    if dims.is_empty() {
        return format!("(*({ptr})(void *)&{n})");
    }
    let rest: String = dims[1..].concat();
    if !rest.is_empty() {
        return format!("(({t} (*){rest})(void *)&{n})");
    }
    format!("(({ptr})(void *)&{n})")
}

struct Ctx {
    headers: Vec<String>, // src/game/include/*.h except game_protos.h
}

impl Ctx {
    fn header_declares(&self, name: &str) -> bool {
        let r = re(&format!(r"(?m)^\s*extern\b[^;]*\b{name}\b"));
        self.headers.iter().any(|h| rsearch(&r, h).is_some())
    }
}

fn process(ctx: &Ctx, path: &Path, names: &BTreeSet<String>, report: &mut Vec<String>) -> usize {
    let mut text = read_text(path);
    let fname = path.file_name().unwrap().to_string_lossy().into_owned();
    let mut edits: Vec<(usize, usize, String)> = vec![];
    let mut undefs: Vec<(usize, BTreeSet<String>)> = vec![]; // function end offset -> names (insertion order)
    let mut first_func: Vec<(String, usize)> = vec![]; // name -> start of first function using a view
    let depth = depth_map(&text);
    for name in names {
        let decl = re(&format!(
            r"(?m)^([ \t]*)extern\s+((?:(?:const|volatile|struct|union|enum|unsigned|signed)\s+)*\w+)\s*(\**)\s*\b{name}\s*((?:\[[^\]]*\]\s*)*);[ \t]*(?:(?://|/\*)[^\n]*)?$"
        ));
        let found: Vec<_> = finditer(&decl, &text).into_iter().filter(|m| depth[m.get(0).unwrap().start()] > 0).collect();
        if found.is_empty() {
            continue;
        }
        let mut kinds = BTreeSet::new();
        for m in &found {
            let k = if !g(m, 3).is_empty() || !g(m, 4).is_empty() {
                "ptr"
            } else if g(m, 2).split_whitespace().any(|w| INTS.contains(&w)) {
                "int"
            } else {
                "other"
            };
            kinds.insert(k);
        }
        if kinds.contains("int") && kinds.contains("ptr") && !found.iter().all(|m| !g(m, 4).is_empty()) {
            report.push(format!("{fname}: {name}: pointer and integer views, fix by hand"));
            continue;
        }
        let dim_re = re(r"\[[^\]]*\]");
        for m in &found {
            let dims: Vec<String> = finditer(&dim_re, g(m, 4)).iter().map(|d| g(d, 0).to_string()).collect();
            let v = view(&squash(g(m, 2)), g(m, 3), &dims, name);
            let m0 = m.get(0).unwrap();
            edits.push((m0.start(), m0.end(), format!("{}#define {name} {v}", g(m, 1))));
            let end = func_end(&text, &depth, m0.start());
            match undefs.iter_mut().find(|u| u.0 == end) {
                Some(u) => {
                    u.1.insert(name.clone());
                }
                None => undefs.push((end, BTreeSet::from([name.clone()]))),
            }
            let fs = func_start(&text, &depth, m0.start());
            match first_func.iter_mut().find(|f| &f.0 == name) {
                Some(f) => f.1 = f.1.min(fs),
                None => first_func.push((name.clone(), fs)),
            }
        }
    }
    if edits.is_empty() {
        return 0;
    }
    for (end, ns) in &undefs {
        edits.push((*end, *end, ns.iter().map(|n| format!("#undef {n}\n")).collect()));
    }
    // a file-scope declaration for the macros to refer to
    for (name, fs) in &first_func {
        let fs = *fs;
        let before = &text[..fs];
        let r = re(&format!(r"(?m)^[^\s#/*][^;{{}}()]*\b{name}\b\s*(\[[^\]]*\]\s*)*(=|;)"));
        if rsearch(&r, before).is_some() || ctx.header_declares(name) {
            continue;
        }
        let lr = re(&format!(r"(?m)^((?:static\s+)?[A-Za-z_][\w \*]*?\b{name}\s*(?:\[[^\]]*\]\s*)*)(?:=[^;]*)?;"));
        let later = rsearch(&lr, &text[fs..]);
        let d;
        match later {
            Some(l) if depth[fs + l.get(0).unwrap().start()] == 0 && !g(&l, 1).contains("static") => {
                let g1 = g(&l, 1).trim();
                let ext = re(r"^extern\s+");
                let stripped = match crate::util::rmatch(&ext, g1) {
                    Some(m) => g1[m.get(0).unwrap().end()..].to_string(),
                    None => g1.to_string(),
                };
                d = format!("extern {stripped};");
            }
            Some(l) if depth[fs + l.get(0).unwrap().start()] == 0 => {
                report.push(format!("{fname}: {name}: static defined after first use, fix by hand"));
                continue;
            }
            _ => d = format!("extern u8 {name}[];  /* typed per function below */"),
        }
        edits.push((fs, fs, d + "\n"));
    }
    // sorted(..., reverse=True) is stable: ties keep their order
    edits.sort_by(|a, b| (b.0, b.1).cmp(&(a.0, a.1)));
    let n = edits.len();
    for (s, e, r) in edits {
        text = format!("{}{}{}", &text[..s], r, &text[e..]);
    }
    crate::util::write(path, &text);
    n
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let game = root.join("src").join("game");
    let protos = game.join("include").join("game_protos.h");
    let log = read_text(Path::new(&args[0]));
    let funcs: std::collections::HashSet<String> =
        finditer(&re(r"\b(\w+)\("), &read_text(&protos)).iter().map(|m| g(m, 1).to_string()).collect();
    let mut per_file: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    let err = re("src/game/(\\w+\\.c):\\d+:\\d+: error: conflicting types for \u{2018}(\\w+)\u{2019}");
    for m in finditer(&err, &log) {
        let n = g(&m, 2);
        if !funcs.contains(n) {
            per_file.entry(g(&m, 1).to_string()).or_default().insert(n.to_string());
        }
    }
    let mut headers = vec![];
    for h in crate::util::glob_ext(&game.join("include"), "h") {
        if h == protos {
            continue;
        }
        headers.push(read_text(&h));
    }
    let ctx = Ctx { headers };
    let mut report = vec![];
    let mut total = 0;
    for (f, ns) in &per_file {
        total += process(&ctx, &game.join(f), ns, &mut report);
    }
    for r in &report {
        println!("{r}");
    }
    eprintln!("{total} edits in {} files", per_file.len());
    0
}
