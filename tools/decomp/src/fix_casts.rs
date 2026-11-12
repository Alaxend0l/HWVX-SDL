//! fix-casts (formerly tools/fix_casts.py): apply PTR()/HND() at pointer<->integer
//! conversion sites reported by GCC.
//!
//! On 64-bit hosts any 32-bit integer that carries a pointer must hold a handle
//! (src/port/port.h). GCC reports every place where the decomp converts between
//! the two; this wraps the integer side in PTR() and the pointer side in HND():
//!
//!   (T *)intexpr        -> (T *)PTR(intexpr)
//!   (u32)ptrexpr        -> (u32)HND(ptrexpr)
//!   f(ptr) to int param -> f(HND(ptr))       and the reverse with PTR()
//!   x = ptr / return ptr into an integer      -> HND(...), reverse PTR(...)
//!
//! Uses GCC's SARIF diagnostics for exact source ranges. Configure a build dir
//! with -fdiagnostics-add-output=sarif (one .sarif per object file in the build
//! directory), then repeat until it reports 0 edits:
//!
//!   cmake -S . -B build-fix -G Ninja -DCMAKE_C_FLAGS=-fdiagnostics-add-output=sarif
//!   ninja -C build-fix -k 0; hwvx-decomp fix-casts build-fix
//!
//! Positions are byte offsets; GCC's columns (characters, as Python counted them)
//! are converted per line. A failed parse anywhere (Python's ValueError/IndexError)
//! skips the site.

use crate::util::*;
use fancy_regex::Regex;
use serde_json::Value;
use std::collections::{BTreeSet, HashMap};
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

type R<T> = Option<T>;

const BASIC: &str = "s8 u8 s16 u16 s32 u32 s64 u64 f32 f64 int char short long float double BOOL void";

static TYPE: LazyLock<Regex> = LazyLock::new(|| {
    re(r"\A(?:((?:(?:const|volatile|unsigned|signed)\s+)*)((?:struct|union)\s+)?(\w+)((?:\s*\*)*))\z")
});
static OPERAND: LazyLock<Regex> = LazyLock::new(|| {
    re(r"^(?:[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+[uUlL]*|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuUlL]*)")
});
static MEMBER: LazyLock<Regex> = LazyLock::new(|| re(r"^(?:->|\.)\s*[A-Za-z_]\w*"));
static WRAPPED: LazyLock<Regex> = LazyLock::new(|| re(r"^(PTR|HND)\("));

fn ch(s: &str, i: usize) -> R<u8> {
    s.as_bytes().get(i).copied()
}

fn match_close(s: &str, i: usize) -> R<usize> {
    let mut depth = 0i64;
    let mut j = i;
    while j < s.len() {
        let c = ch(s, j)?;
        if b"([{".contains(&c) {
            depth += 1;
        } else if b")]}".contains(&c) {
            depth -= 1;
            if depth == 0 {
                return Some(j);
            }
        } else if c == b'"' || c == b'\'' {
            let q = c;
            j += 1;
            while ch(s, j)? != q {
                j += if ch(s, j)? == b'\\' { 2 } else { 1 };
            }
        }
        j += 1;
    }
    None // unbalanced
}

fn skip_ws(s: &str, mut i: usize) -> usize {
    while i < s.len() && b" \t\n".contains(&s.as_bytes()[i]) {
        i += 1;
    }
    i
}

fn is_type(text: &str) -> bool {
    match TYPE.captures(text).ok().flatten() {
        None => false,
        Some(m) => {
            !g(&m, 4).is_empty()
                || !g(&m, 2).is_empty()
                || BASIC.split_whitespace().any(|b| b == g(&m, 3))
                || !g(&m, 1).is_empty()
        }
    }
}

fn tail(s: &str, i: usize) -> R<&str> {
    if i > s.len() { Some("") } else { s.get(i..) }
}

/// End of the operand of a cast starting at s[i].
fn unary_end(s: &str, i: usize) -> R<usize> {
    let i = skip_ws(s, i);
    let c = ch(s, i)?;
    let mut k;
    if b"*-!~&+".contains(&c) {
        let two = s.get(i..(i + 2).min(s.len())).unwrap_or("");
        if two == "++" || two == "--" {
            return unary_end(s, i + 2);
        }
        return unary_end(s, i + 1);
    }
    if c == b'(' {
        let j = match_close(s, i)?;
        // (type)operand: a cast, keep going; otherwise a parenthesized expr
        if is_type(s.get(i + 1..j)?.trim()) {
            return unary_end(s, j + 1);
        }
        k = j + 1;
    } else if c == b'"' {
        k = s[i + 1..].find('"')? + i + 1 + 1;
    } else {
        let m = rmatch(&OPERAND, tail(s, i)?)?;
        k = i + m.get(0).unwrap().end();
        if &s[i..k] == "sizeof" {
            k = skip_ws(s, k);
            return Some(match_close(s, k)? + 1);
        }
    }
    loop {
        let k2 = skip_ws(s, k);
        if k2 < s.len() && b"([".contains(&s.as_bytes()[k2]) {
            k = match_close(s, k2)? + 1;
            continue;
        }
        if let Some(m) = rmatch(&MEMBER, tail(s, k2)?) {
            k = k2 + m.get(0).unwrap().end();
            continue;
        }
        let two = s.get(k2.min(s.len())..(k2 + 2).min(s.len())).unwrap_or("");
        if two == "++" || two == "--" {
            k = k2 + 2;
            continue;
        }
        return Some(k);
    }
}

/// End of an assignment-expression starting at s[i] (stops at , ) ; }).
fn expr_end(s: &str, i: usize) -> R<usize> {
    let mut depth = 0i64;
    let mut j = i;
    while j < s.len() {
        let c = ch(s, j)?;
        if b"([{".contains(&c) {
            depth += 1;
        } else if b")]}".contains(&c) {
            if depth == 0 {
                return Some(j);
            }
            depth -= 1;
        } else if (c == b',' || c == b';') && depth == 0 {
            return Some(j);
        } else if c == b'"' || c == b'\'' {
            let q = c;
            j += 1;
            while ch(s, j)? != q {
                j += if ch(s, j)? == b'\\' { 2 } else { 1 };
            }
        }
        j += 1;
    }
    Some(j)
}

/// Byte offsets of line starts (Python's line_offsets, in bytes).
fn line_offsets(s: &str) -> Vec<usize> {
    let mut offs = vec![0];
    for (i, c) in s.bytes().enumerate() {
        if c == b'\n' {
            offs.push(i + 1);
        }
    }
    offs
}

/// Byte position of character column `col` (1-based) of line `line` (1-based):
/// Python's offs[line - 1] + col - 1 in characters. Past the end of the text the
/// result keeps counting (so it orders and fails like Python's index).
fn position(s: &str, offs: &[usize], line: i64, col: i64) -> R<usize> {
    let l = py_index(offs.len(), line - 1)?;
    let start = offs[l];
    let n = col - 1;
    if n < 0 {
        // a negative column: Python would step back characters
        let mut p = start;
        for _ in 0..(-n) {
            if p == 0 {
                return None;
            }
            p -= 1;
            while !s.is_char_boundary(p) {
                p -= 1;
            }
        }
        return Some(p);
    }
    let mut p = start;
    let mut left = n as usize;
    for c in s[start..].chars() {
        if left == 0 {
            return Some(p);
        }
        p += c.len_utf8();
        left -= 1;
    }
    Some(p + left)
}

/// Python list indexing with negative wrap.
fn py_index(len: usize, i: i64) -> R<usize> {
    let j = if i < 0 { len as i64 + i } else { i };
    if j < 0 || j as usize >= len { None } else { Some(j as usize) }
}

/// (start, end, wrapper) or None.
fn plan(src: &str, offs: &[usize], region: &Value, msg: &str) -> R<(usize, usize, &'static str)> {
    let int = |k: &str| region.get(k).and_then(|v| v.as_i64());
    let start_line = int("startLine")?;
    let pos = position(src, offs, start_line, int("startColumn")?)?;
    let end_line = int("endLine").unwrap_or(start_line);
    let rend = match int("endColumn") {
        Some(c) => Some(position(src, offs, end_line, c)?),
        None => None,
    };
    if msg.contains("cast to pointer from integer") || msg.contains("cast from pointer to integer") {
        let w = if msg.contains("to pointer") { "PTR" } else { "HND" };
        if ch(src, pos)? == b'(' {
            let close = match_close(src, pos)?;
            let start = skip_ws(src, close + 1);
            return Some((start, unary_end(src, start)?, w));
        }
        // Inside macro arguments GCC reports the macro name; which argument
        // is at fault can't be told from here, so leave it for a human.
        return None;
    }
    let kind = if msg.contains("makes pointer from integer") {
        "PTR"
    } else if msg.contains("makes integer from pointer") {
        "HND"
    } else {
        return None;
    };
    let start;
    if msg.starts_with("passing argument") {
        let rend = rend?;
        if rend <= pos {
            return None;
        }
        return Some((pos, rend, kind));
    }
    if msg.starts_with("assignment") || msg.starts_with("initialization") {
        let eq = if ch(src, pos)? != b'=' { src.get(pos..)?.find('=')? + pos } else { pos };
        if src[pos..eq].contains('\n') || ch(src, eq + 1)? == b'=' {
            return None;
        }
        start = skip_ws(src, eq + 1);
    } else if msg.starts_with("returning") {
        let mut s0 = pos;
        if src.get(s0..).map(|t| t.starts_with("return")).unwrap_or(false) {
            s0 = skip_ws(src, s0 + 6);
        }
        start = s0;
    } else {
        return None;
    }
    let mut end = expr_end(src, start)?;
    while end > start && end - 1 < src.len() && b" \t\n".contains(&src.as_bytes()[end - 1]) {
        end -= 1;
    }
    Some((start, end, kind))
}

fn is_word(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

/// Python's re.match(r"\w\w", src[e - 1:e + 1]) for 0 < e < len(src).
fn word_pair(src: &str, e: usize) -> bool {
    if e == 0 {
        // src[-1:1]: the last character only when the text is one character long
        return false;
    }
    if !src.is_char_boundary(e) {
        return false;
    }
    let before = src[..e].chars().next_back();
    let after = src[e..].chars().next();
    matches!((before, after), (Some(a), Some(b)) if is_word(a) && is_word(b))
}

fn sarif_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        let Ok(ft) = e.file_type() else { continue };
        if ft.is_dir() {
            sarif_files(&p, out);
        } else if p.file_name().and_then(|n| n.to_str()).map(|n| n.ends_with(".sarif")).unwrap_or(false) {
            out.push(p);
        }
    }
}

pub fn run(_root: &Path, args: Vec<String>) -> i32 {
    let Some(dir) = args.first() else {
        eprintln!("usage: hwvx-decomp fix-casts BUILD_DIR");
        return 2;
    };
    let mut sarifs = vec![];
    sarif_files(Path::new(dir), &mut sarifs);
    sarifs.sort();
    let mut order: Vec<String> = vec![];
    let mut edits: HashMap<String, Vec<(Value, String)>> = HashMap::new();
    for sarif in sarifs {
        let Ok(bytes) = std::fs::read(&sarif) else { continue };
        let Ok(text) = String::from_utf8(bytes) else { continue };
        let Ok(d) = serde_json::from_str::<Value>(&text) else { continue };
        for run in d.get("runs").and_then(|v| v.as_array()).cloned().unwrap_or_default() {
            for r in run.get("results").and_then(|v| v.as_array()).cloned().unwrap_or_default() {
                if r.get("level").and_then(|v| v.as_str()) != Some("error") {
                    continue;
                }
                let loc = &r["locations"][0]["physicalLocation"];
                let path = loc["artifactLocation"]["uri"].as_str().expect("uri").replace("file://", "");
                let msg = r["message"]["text"].as_str().expect("message").to_string();
                if !edits.contains_key(&path) {
                    order.push(path.clone());
                }
                edits.entry(path).or_default().push((loc["region"].clone(), msg));
            }
        }
    }
    let mut total = 0;
    for path in order {
        if !path.contains("/src/game/") {
            continue;
        }
        let errs = &edits[&path];
        let mut src = read(&path);
        let offs = line_offsets(&src);
        let mut planned: BTreeSet<(usize, usize, &'static str)> = BTreeSet::new();
        for (region, msg) in errs {
            let mut p = plan(&src, &offs, region, msg);
            // A span ending inside an identifier means GCC gave a macro token
            // range, not the expression; leave those for a human.
            if let Some(q) = p {
                if q.1 < src.len() && word_pair(&src, q.1) {
                    p = None;
                }
            }
            if let Some(q) = p {
                if q.1 > q.0 {
                    let span = src.get(q.0.min(src.len())..q.1.min(src.len())).unwrap_or("");
                    if !is_match(&WRAPPED, span) {
                        planned.insert(q);
                    }
                }
            }
        }
        // Non-overlapping only; the next round picks up the rest.
        let mut chosen: Vec<(usize, usize, &str)> = vec![];
        for p in planned {
            if let Some(last) = chosen.last() {
                if p.0 < last.1 {
                    continue;
                }
            }
            chosen.push(p);
        }
        for &(start, end, w) in chosen.iter().rev() {
            let (s, e) = (start.min(src.len()), end.min(src.len()));
            src = format!("{}{w}({}){}", &src[..s], &src[s..e], &src[e..]);
        }
        if !chosen.is_empty() {
            write(&path, &src);
            total += chosen.len();
        }
    }
    println!("{total} edits");
    0
}
