//! gen-protos (formerly tools/gen_protos.py): generate src/game/include/game_protos.h from the
//! game's function definitions.
//!
//! The decomp has no shared headers: every file re-declares what it calls, and the
//! copies disagree. This collects the prototype of every non-static function
//! *definition* and puts them in one header (included by common.h); with --strip
//! it also deletes the local re-declarations so the definition is the single
//! source of truth.
//!
//! Functions whose signature uses a type only visible inside one .c file are left
//! out (callers keep their local declaration); they are listed in the header.
//!
//!   hwvx-decomp gen-protos [--strip]

use crate::util::*;
use fancy_regex::Regex;
use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

/// The header's first line names the tool that writes it.
pub const GENERATED_BY: &str = "tools/gen_protos.py";

const BUILTIN: &str = "void char short int long float double signed unsigned const volatile struct union
enum s8 u8 s16 u16 s32 u32 s64 u64 f32 f64 BOOL vu8 vu16 vu32 vs32 vf32 size_t
va_list FILE";

static BLOCK_COMMENT: LazyLock<Regex> = LazyLock::new(|| re(r"(?s)/\*.*?\*/"));
static LINE_COMMENT: LazyLock<Regex> = LazyLock::new(|| re(r"//[^\n]*"));
static T_CLOSE: LazyLock<Regex> =
    LazyLock::new(|| re(r"\}\s*(\w+)\s*(?:__attribute__\(\(.*?\)\))?\s*;"));
static T_TYPEDEF: LazyLock<Regex> = LazyLock::new(|| re(r"typedef\s+[^;{}]*?\b(\w+)\s*;"));
static T_FNPTR: LazyLock<Regex> = LazyLock::new(|| re(r"typedef\s+[^;{}]*\(\s*\*\s*(\w+)\s*\)"));
static DEF: LazyLock<Regex> = LazyLock::new(|| {
    re(r"(?m)^(?P<ret>(?:(?:const|unsigned|signed|struct|union|enum|inline|extern)\s+)*\w+[\s\*]*?)\s*\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*\{")
});
static IDENT: LazyLock<Regex> = LazyLock::new(|| re(r"[A-Za-z_]\w*"));
static TAGGED: LazyLock<Regex> = LazyLock::new(|| re(r"(?:struct|union|enum)\s+(\w+)"));
static TAGS: LazyLock<Regex> = LazyLock::new(|| re(r"\b((?:struct|union)\s+\w+)"));
static NONWORD: LazyLock<Regex> = LazyLock::new(|| re(r"\W"));
static PARAM: LazyLock<Regex> =
    LazyLock::new(|| re(r"^(.*?)(\**)\s*(\w*)\s*(\[.*\])?\s*(?=\n?\z)"));

fn strip_comments(t: &str) -> String {
    let t = sub(&BLOCK_COMMENT, t, |c| {
        c.get(0).unwrap().as_str().chars().map(|ch| if ch == '\n' { '\n' } else { ' ' }).collect()
    });
    sub(&LINE_COMMENT, &t, |_| String::new())
}

fn header_types(game: &Path, out: &Path, port_headers: &[PathBuf]) -> HashSet<String> {
    let mut types = HashSet::new();
    let mut hs = glob_ext(&game.join("include"), "h");
    hs.extend(port_headers.iter().cloned());
    for h in hs {
        if h == out {
            continue;
        }
        let t = strip_comments(&read(&h));
        for r in [&*T_CLOSE, &*T_TYPEDEF, &*T_FNPTR] {
            for c in finditer(r, &t) {
                types.insert(g(&c, 1).to_string());
            }
        }
    }
    types
}

/// (ret, name, params) for top-level function definitions.
fn definitions(text: &str) -> Vec<(String, String, String)> {
    let clean = strip_comments(text);
    let mut depth = 0i64;
    let mut tops = HashSet::new();
    for (i, c) in clean.bytes().enumerate() {
        if c == b'{' {
            if depth == 0 {
                tops.insert(i);
            }
            depth += 1;
        } else if c == b'}' {
            depth -= 1;
        }
    }
    let mut out = vec![];
    for m in finditer(&DEF, &clean) {
        let brace = m.get(0).unwrap().end() - 1;
        if !tops.contains(&brace) {
            continue;
        }
        let ret = squash(gn(&m, "ret"));
        let words: Vec<&str> = ret.split_whitespace().collect();
        if ["static", "return", "else", "if", "while", "for", "switch"].contains(&words[0]) || words.contains(&"static") {
            continue;
        }
        let name = gn(&m, "name");
        if ["if", "while", "for", "switch", "return"].contains(&name) {
            continue;
        }
        out.push((ret, name.to_string(), squash(gn(&m, "params"))));
    }
    out
}

fn idents(s: &str) -> HashSet<String> {
    finditer(&IDENT, s).iter().map(|c| g(c, 0).to_string()).collect()
}

fn decl_re(names: &[String]) -> Regex {
    let alt: Vec<String> = names.iter().map(|n| escape(n)).collect();
    fancy_regex::RegexBuilder::new(&format!(
        r"(?m)^[ \t]*(?:extern[ \t]+)?(?!(?:return|else|case|goto|do|typedef)\b)(?:(?:const|struct|unsigned|signed)[ \t]+)*\w+[\w \t\*]*?\b({})[ \t]*\([^;{{}}=()]*(?:\([^;{{}}=()]*\)[^;{{}}=()]*)*\)[ \t]*;[ \t]*(?://[^\n]*)?\n",
        alt.join("|")
    ))
    .backtrack_limit(usize::MAX)
    .build()
    .expect("decl regex")
}

fn guard(fname: &str) -> String {
    let stem = match fname.rfind('.') {
        Some(i) => &fname[..i],
        None => fname,
    };
    sub(&NONWORD, stem, |_| "_".to_string())
}

fn split_params(params: &str) -> Vec<String> {
    let mut out = vec![];
    let mut depth = 0i64;
    let mut cur = String::new();
    for c in params.chars() {
        if c == ',' && depth == 0 {
            out.push(std::mem::take(&mut cur));
            continue;
        }
        depth += (c == '(' || c == '[') as i64;
        depth -= (c == ')' || c == ']') as i64;
        cur.push(c);
    }
    out.push(cur);
    out
}

fn intersects(a: &HashSet<String>, b: &HashSet<String>) -> bool {
    a.iter().any(|x| b.contains(x))
}

/// Signature with pointers to unknown types turned into void *, or None if an
/// unknown type is used by value or inside a function pointer.
fn erase(ret: &str, name: &str, params: &str, unknown: &HashSet<String>) -> Option<String> {
    let fix = |decl: &str| -> Option<String> {
        if !intersects(&idents(decl), unknown) {
            return Some(decl.trim().to_string());
        }
        if decl.contains('(') || !decl.contains('*') {
            return None;
        }
        let m = rmatch(&PARAM, decl.trim())?;
        if g(&m, 2).is_empty() {
            return None;
        }
        Some(format!("void {}{}{}", g(&m, 2), g(&m, 3), g(&m, 4)))
    };
    let mut r = ret.to_string();
    if intersects(&idents(ret), unknown) {
        if !ret.contains('*') {
            return None;
        }
        r = format!("void {}", "*".repeat(ret.matches('*').count()));
    }
    let ps: Vec<String> = if !["", "void"].contains(&params.trim()) {
        let mut v = vec![];
        for p in split_params(params) {
            v.push(fix(&p)?);
        }
        v
    } else {
        vec!["void".to_string()]
    };
    let sep = if r.ends_with('*') { "" } else { " " };
    Some(format!("{r}{sep}{name}({});", ps.join(", ")))
}

fn fname(p: &Path) -> String {
    p.file_name().unwrap().to_string_lossy().into_owned()
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let strip = args.iter().any(|a| a == "--strip");
    let game = root.join("src/game");
    let out_path = game.join("include/game_protos.h");
    let port_headers = vec![root.join("src/port/port.h"), root.join("src/port/sdk.h"), root.join("src/port/gx.h")];
    let mut known = header_types(&game, &out_path, &port_headers);
    known.extend(BUILTIN.split_whitespace().map(String::from));

    let mut protos: HashMap<String, (String, String)> = HashMap::new();
    let mut skipped: Vec<String> = vec![];
    let mut erased: HashMap<String, (String, String)> = HashMap::new();
    let mut plain_ret: HashSet<String> = HashSet::new();
    let cfiles = glob_ext(&game, "c");
    for f in &cfiles {
        let fnm = fname(f);
        for (ret, name, params) in definitions(&read(f)) {
            let both = format!("{ret} {params}");
            let tagged: HashSet<String> = finditer(&TAGGED, &both).iter().map(|c| g(c, 1).to_string()).collect();
            let unknown: HashSet<String> = idents(&both)
                .into_iter()
                .filter(|w| {
                    w.as_bytes()[0].is_ascii_uppercase() && !known.contains(w) && !tagged.contains(w) && *w != name
                })
                .collect();
            let sig = format!("{ret} {name}({});", if params.is_empty() { "void" } else { &params });
            if !unknown.is_empty() {
                let esig = erase(&ret, &name, &params, &unknown);
                if esig.is_some() && !protos.contains_key(&name) && !erased.contains_key(&name) {
                    erased.insert(name.clone(), (esig.unwrap(), fnm.clone()));
                    if !intersects(&idents(&ret), &unknown) {
                        plain_ret.insert(name.clone());
                    }
                } else {
                    let mut u: Vec<&String> = unknown.iter().collect();
                    u.sort();
                    let u: Vec<&str> = u.iter().map(|s| s.as_str()).collect();
                    skipped.push(format!("{name} ({fnm}: {})", u.join(" ")));
                }
            } else if protos.contains_key(&name) && protos[&name].0 != sig {
                skipped.push(format!("{name} (defined twice: {}, {fnm})", protos[&name].1));
                protos.remove(&name);
            } else {
                protos.insert(name.clone(), (sig, fnm.clone()));
            }
        }
    }

    let mut lines: Vec<String> = vec![
        format!("/* Generated by {GENERATED_BY} from the game's function definitions."),
        " * Do not edit by hand; fix the definition and regenerate. */".into(),
        "#ifndef GAME_PROTOS_H".into(),
        "#define GAME_PROTOS_H".into(),
        "".into(),
    ];
    let mut tags = BTreeSet::new();
    for (sig, _) in protos.values().chain(erased.values()) {
        for c in finditer(&TAGS, sig) {
            tags.insert(g(&c, 1).to_string());
        }
    }
    lines.extend(tags.iter().map(|t| format!("{t};")));
    lines.push("".into());
    let mut cur: Option<String> = None;
    let mut ps: Vec<(&String, &(String, String))> = protos.iter().collect();
    ps.sort_by(|a, b| (&a.1.1, a.0).cmp(&(&b.1.1, b.0)));
    for (_, (sig, f)) in ps {
        if cur.as_ref() != Some(f) {
            lines.push(format!("\n/* {f} */"));
            cur = Some(f.clone());
        }
        lines.push(sig.clone());
    }
    lines.extend([
        "".into(),
        "/* Signatures using a type local to their file, with pointers to it as void *.".into(),
        " * Hidden from that file and from files declaring them with their own copy of".into(),
        " * the type (HWVX_SRC_<file> is defined per source by CMake). */".into(),
    ]);
    // Files that declare an erased function themselves (with their own copy
    // of the type) keep that declaration and don't see the erased one.
    let mut declared: HashMap<String, BTreeSet<String>> = erased.keys().map(|n| (n.clone(), BTreeSet::new())).collect();
    if !erased.is_empty() {
        let mut names: Vec<String> = erased.keys().cloned().collect();
        names.sort();
        let dre = decl_re(&names);
        for f in &cfiles {
            let text = read(f);
            for m in finditer(&dre, &text) {
                let n = g(&m, 1);
                if !(strip && plain_ret.contains(n)) {
                    declared.get_mut(n).unwrap().insert(fname(f));
                }
            }
        }
    }
    let mut cur: Option<String> = None;
    let mut es: Vec<(&String, &(String, String))> = erased.iter().collect();
    es.sort_by(|a, b| (&a.1.1, a.0).cmp(&(&b.1.1, b.0)));
    for (name, (sig, f)) in es {
        let mut files = declared[name].clone();
        files.insert(f.clone());
        let cond = files.iter().map(|x| format!("!defined(HWVX_SRC_{})", guard(x))).collect::<Vec<_>>().join(" && ");
        if cur.as_ref() != Some(&cond) {
            if cur.is_some() {
                lines.push("#endif".into());
            }
            lines.push(format!("#if {cond}"));
            cur = Some(cond);
        }
        lines.push(sig.clone());
    }
    if cur.is_some() {
        lines.push("#endif".into());
    }
    lines.push("".into());
    lines.push("/* Not declared here (signature uses a file-local type):".into());
    let mut sk = skipped.clone();
    sk.sort();
    lines.extend(sk.iter().map(|s| format!(" *   {s}")));
    lines.extend([" */".into(), "".into(), "#endif".into(), "".into()]);
    write(&out_path, &lines.join("\n"));
    eprintln!("{} prototypes, {} with void * params, {} skipped", protos.len(), erased.len(), skipped.len());

    if strip {
        // Local declarations of erased functions only add value where the
        // return type was erased (callers use the struct it points to); the
        // void * parameters accept whatever the call sites pass. The defining
        // file doesn't see the erased prototype, so it keeps its own.
        let mut pnames: Vec<String> = protos.keys().cloned().collect();
        pnames.sort();
        let decl = decl_re(&pnames);
        let mut files = cfiles.clone();
        files.extend(glob_ext(&game.join("include"), "h"));
        for f in files {
            if f == out_path || fname(&f) == "data_gen.c" {
                continue;
            }
            let src = read(&f);
            let mut out = sub(&decl, &src, |_| String::new());
            let fnm = fname(&f);
            let mut others: Vec<String> = plain_ret.iter().filter(|n| erased[*n].1 != fnm).cloned().collect();
            if !others.is_empty() {
                others.sort();
                out = sub(&decl_re(&others), &out, |_| String::new());
            }
            if out != src {
                write(&f, &out);
            }
        }
    }
    0
}
