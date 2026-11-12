//! fieldify (formerly tools/fieldify.py): turn pointer fields in game structs into
//! 4-byte handles (src/port/port.h).
//!
//! Pass 1 - declarations, inside every struct/union body:
//!     T *name;              -> PTR32(T) name;
//!     T *name[N];           -> PTR32(T) name[N];
//!     R (*name)(A);         -> FPTR32(R, (A)) name;
//!   `T **name` becomes PTR32(T *) and is listed for review: what it points at is
//!   usually an array of 4-byte slots, which needs a human decision.
//!
//! Pass 2 - accesses `x->name` / `x.name` of converted fields:
//!     x->name = v           -> SETP(x->name, v)
//!     x->name               -> GETP(x->name)
//!   Address-of, ++/--/+=, and designated initializers are left alone and listed.
//!   Field names that are pointers in one struct and not in another are rewritten
//!   anyway; the compiler rejects GETP on a non-PTR32 member, and
//!   `hwvx-decomp fieldify --revert build.log` undoes exactly those sites.
//!
//!   hwvx-decomp fieldify src/game/*.c src/game/include/*.h
//!
//! After a decomp merge, rewrite only the merged files (names still come from all):
//!
//!   hwvx-decomp fieldify --only src/game/A.c,src/game/B.c src/game/*.c src/game/include/*.h
//!
//! Idempotent: existing PTR32 fields are picked up and already wrapped accesses
//! are left alone, so it can be rerun after merging new decomp code. The sites to
//! review go to fieldify_review.txt in the current directory.

use crate::util::{count_nl, g, gn, is_alnum_at, is_match, re, rmatch};
use fancy_regex::Regex;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

const TYPE: &str = r"(?:(?:const|volatile|unsigned|signed|struct|union|enum)\s+)*\w+";

static FIELD: LazyLock<Regex> = LazyLock::new(|| {
    re(&format!(r"^(?P<ind>[ \t]*)(?P<base>{TYPE})\s*(?P<decls>\*[^;]*?);(?P<rest>.*)(?=\n?\z)"))
});
static FNPTR: LazyLock<Regex> = LazyLock::new(|| {
    re(&format!(
        r"^(?P<ind>[ \t]*)(?P<ret>{TYPE}\s*\**)\s*\(\s*\*\s*(?P<name>\w+)\s*(?P<arr>(?:\[[^\]]*\])*)\s*\)\s*\((?P<args>[^;]*)\)\s*;(?P<rest>.*)(?=\n?\z)"
    ))
});
static DECL: LazyLock<Regex> = LazyLock::new(|| {
    re(r"^\s*(?P<stars>\*+)\s*(?:const\s+)?(?P<name>\w+)\s*(?P<arr>(?:\[[^\]]*\])*)\s*(?=\n?\z)")
});
static OPEN: LazyLock<Regex> =
    LazyLock::new(|| re(r"\b(struct|union)\b(\s+\w+)?\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?\{"));
static EXISTING: LazyLock<Regex> = LazyLock::new(|| {
    re(r"(?m)^[ \t]*F?PTR32\((?:[^()]|\([^()]*\))*\)\s*(\w+)\s*(\[[^;]*\])?\s*;")
});
static NONCODE: LazyLock<Regex> = LazyLock::new(|| {
    re(r#"(?s)/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'"#)
});
static WRAPPED: LazyLock<Regex> = LazyLock::new(|| re(r"(GETP|SETP)\(\s*(?=\n?\z)"));
static CAST_PTR: LazyLock<Regex> = LazyLock::new(|| re(&format!(r"^(?:\s*{TYPE}[\s\*]*)\z")));
static CAST_SCALAR: LazyLock<Regex> = LazyLock::new(|| {
    re(r"^(?:\s*(?:const\s+)?(?:s8|u8|s16|u16|s32|u32|f32|f64|int|char|float|double)\s*)\z")
});
const SKIP_BASES: &[&str] = &["return", "typedef", "else", "case", "goto", "PTR32", "FPTR32"];

/// Python's errors: ValueError (caught where the Python catches it) vs anything else.
#[derive(Debug)]
pub enum Unbalanced {
    Value,
}

/// Python str.isspace() for the regex `\s` on one char.
fn py_space(c: char) -> bool {
    c.is_whitespace() || ('\x1c'..='\x1f').contains(&c)
}

/// Index past leading `\s*` of s[i..].
fn skip_space(s: &str, i: usize) -> usize {
    let mut k = i;
    for c in s[i..].chars() {
        if !py_space(c) {
            break;
        }
        k += c.len_utf8();
    }
    k
}

/// Python read_text(): UTF-8 with universal newlines.
pub fn read_text(p: &Path) -> String {
    let s = std::fs::read_to_string(p).unwrap_or_else(|e| panic!("{}: {e}", p.display()));
    universal(s)
}

pub fn universal(s: String) -> String {
    if s.contains('\r') {
        s.replace("\r\n", "\n").replace('\r', "\n")
    } else {
        s
    }
}

/// Byte at a Python index that may be negative (s[-1] is the last).
fn at_py(s: &str, j: isize) -> u8 {
    let b = s.as_bytes();
    if j < 0 {
        b[(b.len() as isize + j) as usize]
    } else {
        b[j as usize]
    }
}

fn starts_py(s: &str, pat: &str, j: isize) -> bool {
    let st = if j < 0 { (s.len() as isize + j).max(0) as usize } else { j as usize };
    st <= s.len() && s.as_bytes()[st..].starts_with(pat.as_bytes())
}

/// Matching closer of the bracket at s[i] (comments and string literals skipped).
pub fn match_close(s: &str, i: usize) -> Result<usize, Unbalanced> {
    let n = s.len() as isize;
    let mut depth = 0i64;
    let mut j = i as isize;
    while j < n {
        let c = at_py(s, j);
        if starts_py(s, "/*", j) {
            let from = (if j < 0 { n + j } else { j }) as usize + 2;
            match s.get(from..).and_then(|t| t.find("*/")) {
                Some(k) => j = (from + k + 2) as isize,
                None => return Err(Unbalanced::Value), // str.index: ValueError
            }
            continue;
        }
        if starts_py(s, "//", j) {
            let from = (if j < 0 { n + j } else { j }) as usize;
            j = match s[from..].find('\n') {
                Some(k) => (from + k) as isize,
                None => -1,
            };
            continue;
        }
        if c == b'(' || c == b'[' || c == b'{' {
            depth += 1;
        } else if c == b')' || c == b']' || c == b'}' {
            depth -= 1;
            if depth == 0 {
                return Ok(if j < 0 { (n + j) as usize } else { j as usize });
            }
        } else if c == b'"' || c == b'\'' {
            let q = c;
            j += 1;
            loop {
                let jj = if j < 0 { n + j } else { j };
                if jj >= n || jj < 0 {
                    panic!("IndexError: string index out of range");
                }
                let cj = s.as_bytes()[jj as usize];
                if cj == q {
                    break;
                }
                j += if cj == b'\\' { 2 } else { 1 };
            }
        }
        j += 1;
    }
    Err(Unbalanced::Value)
}

/// s[j] is a closing bracket; the index of its opener.
fn match_open(s: &str, j: usize) -> usize {
    let b = s.as_bytes();
    let mut depth = 0i64;
    let mut i = j as isize;
    while i >= 0 {
        let c = b[i as usize];
        if c == b')' || c == b']' || c == b'}' {
            depth += 1;
        } else if c == b'(' || c == b'[' || c == b'{' {
            depth -= 1;
            if depth == 0 {
                return i as usize;
            }
        }
        i -= 1;
    }
    panic!("ValueError: unbalanced");
}

// ---- pass 1 ---------------------------------------------------------------

#[derive(Default)]
pub struct Field {
    array: HashSet<bool>,
    files: HashMap<String, HashSet<bool>>,
}

pub type Fields = (Vec<String>, HashMap<String, Field>);

fn note_field(fields: &mut Fields, name: &str, is_array: bool, where_: &str) {
    if !fields.1.contains_key(name) {
        fields.0.push(name.to_string());
        fields.1.insert(name.to_string(), Field::default());
    }
    let f = fields.1.get_mut(name).unwrap();
    f.array.insert(is_array);
    let file = where_.split(':').next().unwrap_or("").to_string();
    f.files.entry(file).or_default().insert(is_array);
}

fn convert_line(line: &str, fields: &mut Fields, review: &mut Vec<String>, where_: &str) -> String {
    if let Some(m) = rmatch(&FNPTR, line) {
        let args = gn(&m, "args").trim();
        let args = if args.is_empty() { "void" } else { args };
        let ret = crate::util::squash(gn(&m, "ret"));
        note_field(fields, gn(&m, "name"), !gn(&m, "arr").is_empty(), where_);
        return format!(
            "{}FPTR32({}, ({})) {}{};{}",
            gn(&m, "ind"),
            ret,
            args,
            gn(&m, "name"),
            gn(&m, "arr"),
            gn(&m, "rest")
        );
    }
    let m = match rmatch(&FIELD, line) {
        Some(m) => m,
        None => return line.to_string(),
    };
    if SKIP_BASES.contains(&gn(&m, "base").split_whitespace().next().unwrap_or("")) {
        return line.to_string();
    }
    let base = crate::util::squash(gn(&m, "base"));
    let mut outs = vec![];
    for d in gn(&m, "decls").split(',') {
        match rmatch(&DECL, d) {
            Some(dm) => outs.push((gn(&dm, "stars").len(), gn(&dm, "name").to_string(), gn(&dm, "arr").to_string())),
            None => return line.to_string(),
        }
    }
    let ind = gn(&m, "ind");
    let mut lines: Vec<String> = vec![];
    for (nstars, name, arr) in outs {
        if nstars == 0 {
            lines.push(format!("{ind}{base} {name}{arr};"));
            continue;
        }
        let pointee = if nstars > 1 { format!("{base} {}", "*".repeat(nstars - 1)) } else { base.clone() };
        if nstars > 1 {
            review.push(format!("{where_}: {name}: {base} {} field -> PTR32({pointee})", "*".repeat(nstars)));
        }
        note_field(fields, &name, !arr.is_empty(), where_);
        lines.push(format!("{ind}PTR32({}) {name}{arr};", pointee.trim()));
    }
    lines.last_mut().unwrap().push_str(gn(&m, "rest"));
    lines.join("\n")
}

fn note_existing(text: &str, fields: &mut Fields, fname: &str) {
    for m in crate::util::finditer(&EXISTING, text) {
        note_field(fields, g(&m, 1), !g(&m, 2).is_empty(), fname);
    }
}

/// Python's s[:-1] (drop the last character).
fn drop_last(s: &str) -> &str {
    match s.char_indices().last() {
        Some((i, _)) => &s[..i],
        None => s,
    }
}

fn convert_decls(text: &str, fields: &mut Fields, review: &mut Vec<String>, fname: &str) -> String {
    let mut spans: Vec<(usize, usize, usize)> = vec![]; // (body start, close, match start)
    for m in crate::util::finditer(&OPEN, text) {
        let m0 = m.get(0).unwrap();
        let end = match match_close(text, m0.end() - 1) {
            Ok(e) => e,
            Err(_) => continue,
        };
        if let Some(last) = spans.last() {
            if m0.start() < last.1 {
                continue; // nested: handled with the enclosing body
            }
        }
        spans.push((m0.end(), end, m0.start()));
    }
    let mut out = String::with_capacity(text.len() + 1024);
    let mut last = 0;
    for &(s, e, _) in &spans {
        out.push_str(&text[last..s]);
        let body = &text[s..e];
        let line0 = count_nl(text, 0, s) + 1;
        if !body.contains('\n') && body.contains(';') {
            // one-line struct: `struct { u32 a; T *p; }`
            let parts: Vec<&str> = body.split(';').collect();
            let mut conv: Vec<String> = vec![];
            for p in &parts[..parts.len() - 1] {
                if p.trim().is_empty() {
                    conv.push(p.to_string());
                } else {
                    let c = convert_line(&format!("{p};"), fields, review, &format!("{fname}:{line0}"));
                    conv.push(drop_last(&c).to_string());
                }
            }
            conv.push(parts[parts.len() - 1].to_string());
            out.push_str(&conv.join(";"));
            last = e;
            continue;
        }
        let new: Vec<String> = body
            .split('\n')
            .enumerate()
            .map(|(k, l)| convert_line(l, fields, review, &format!("{fname}:{}", line0 + k)))
            .collect();
        out.push_str(&new.join("\n"));
        last = e;
    }
    out.push_str(&text[last..]);
    out
}

// ---- pass 2 ---------------------------------------------------------------

fn is_word_at(s: &str, i: usize) -> bool {
    at(s, i) == b'_' || is_alnum_at(s, i)
}

use crate::util::at;

/// s[i] is the first char of '->' or '.'; where its operand begins.
fn postfix_start(s: &str, i: usize) -> usize {
    let b = s.as_bytes();
    let mut j = i as isize - 1;
    loop {
        while j >= 0 && matches!(b[j as usize], b' ' | b'\t' | b'\n') {
            j -= 1;
        }
        if j < 0 {
            return 0;
        }
        let ju = j as usize;
        if b[ju] == b')' || b[ju] == b']' {
            j = match_open(s, ju) as isize - 1;
            // a call or subscript continues the operand; a cast does not:
            // treat (expr) as the start unless an identifier precedes it
            let mut k = j;
            while k >= 0 && matches!(b[k as usize], b' ' | b'\t') {
                k -= 1;
            }
            if k >= 0 && b[k as usize] == b')' {
                // (T *)(expr): the group before is a cast, not a call
                let o = match_open(s, k as usize);
                let inner = &s[o + 1..k as usize];
                if is_match(&CAST_PTR, inner) && inner.contains('*') || is_match(&CAST_SCALAR, inner) {
                    return (j + 1) as usize;
                }
            }
            if k >= 0 && (is_alnum_at(s, k as usize) || matches!(b[k as usize], b'_' | b')' | b']')) {
                j = k;
                continue;
            }
            return (j + 1) as usize;
        }
        if is_word_at(s, ju) {
            while j >= 0 && is_word_at(s, j as usize) {
                j -= 1;
            }
            let mut k = j;
            while k >= 0 && matches!(b[k as usize], b' ' | b'\t') {
                k -= 1;
            }
            if k >= 1 && b[k as usize - 1] == b'-' && b[k as usize] == b'>' {
                j = k - 2;
                continue;
            }
            if k >= 0 && b[k as usize] == b'.' && !(k >= 1 && b[k as usize - 1].is_ascii_digit()) {
                j = k - 1;
                continue;
            }
            return (j + 1) as usize;
        }
        return (j + 1) as usize;
    }
}

/// If s[j..] starts with `= rhs` (not ==): (rhs start, rhs end).
fn assignment_rhs(s: &str, j: usize) -> Option<(usize, usize)> {
    let b = s.as_bytes();
    let k0 = skip_space(s, j);
    if at(s, k0) != b'=' || at(s, k0 + 1) == b'=' {
        return None;
    }
    let k = k0 + 1;
    let mut depth = 0i64;
    let mut e = k;
    while e < b.len() {
        let c = b[e];
        if matches!(c, b'(' | b'[' | b'{') {
            depth += 1;
        } else if matches!(c, b')' | b']' | b'}') {
            if depth == 0 {
                break;
            }
            depth -= 1;
        } else if (c == b';' || c == b',') && depth == 0 {
            break;
        } else if c == b'"' || c == b'\'' {
            let q = c;
            e += 1;
            loop {
                let ce = *b.get(e).expect("IndexError: string index out of range");
                if ce == q {
                    break;
                }
                e += if ce == b'\\' { 2 } else { 1 };
            }
        }
        e += 1;
    }
    Some((k, e))
}

/// Start/end offsets of comments and string/char literals.
fn noncode_spans(text: &str) -> Vec<(usize, usize)> {
    crate::util::finditer(&NONCODE, text).iter().map(|m| (m.get(0).unwrap().start(), m.get(0).unwrap().end())).collect()
}

fn in_spans(spans: &[(usize, usize)], i: usize) -> bool {
    // bisect_right(spans, (i, inf)) - 1: the last span starting at or before i
    let k = spans.partition_point(|&(s, _)| s <= i);
    k > 0 && spans[k - 1].0 <= i && i < spans[k - 1].1
}

/// `re.match(r"\s*(->|\.(?!\d)|\[)", s[end:])`
fn continues_at(s: &str, end: usize) -> bool {
    let k = skip_space(s, end);
    let b = s.as_bytes();
    match at(s, k) {
        b'-' => at(s, k + 1) == b'>',
        b'.' => !(k + 1 < b.len() && is_digit_at(s, k + 1)),
        b'[' => true,
        _ => false,
    }
}

/// Python `\d` on the char at byte i.
fn is_digit_at(s: &str, i: usize) -> bool {
    let b = s.as_bytes()[i];
    if b < 0x80 {
        return b.is_ascii_digit();
    }
    s[crate::util::floor_cb(s, i)..].chars().next().map(|c| c.is_numeric() && c.to_digit(10).is_some()).unwrap_or(false)
}

/// `re.match(r"\s*(\+\+|--|[-+*/|&^]=|<<=|>>=)", s[end:])`
fn arith_at(s: &str, end: usize) -> bool {
    let k = skip_space(s, end);
    let t = &s.as_bytes()[k..];
    t.starts_with(b"++")
        || t.starts_with(b"--")
        || (t.len() >= 2 && b"-+*/|&^".contains(&t[0]) && t[1] == b'=')
        || t.starts_with(b"<<=")
        || t.starts_with(b">>=")
}

struct Acc {
    names: Regex,
}

impl Acc {
    /// The regex `(->|(?<![\w.])\.|(?<=[\w\)\]])\.)\s*(NAMES)\b`, finditer from `from`:
    /// a '.' matches unless a '.' precedes it. Returns (op start, match end, name).
    fn next(&self, text: &str, from: usize) -> Option<(usize, usize, String)> {
        let b = text.as_bytes();
        let mut p = from;
        while p < b.len() {
            let oplen = if b[p] == b'-' && at(text, p + 1) == b'>' {
                2
            } else if b[p] == b'.' && !(p > 0 && b[p - 1] == b'.') {
                1
            } else {
                0
            };
            if oplen > 0 {
                if let Some(m) = rmatch(&self.names, &text[p + oplen..]) {
                    let end = p + oplen + m.get(0).unwrap().end();
                    return Some((p, end, g(&m, 1).to_string()));
                }
            }
            p += 1;
        }
        None
    }
}

fn rewrite_accesses(
    text: &str,
    fields: &Fields,
    review: &mut Vec<String>,
    fname: &str,
    line_base: usize,
    acc: &Option<Acc>,
) -> String {
    let acc = match acc {
        Some(a) => a,
        None => return text.to_string(),
    };
    let mut text = text.to_string();
    let mut changed = true;
    while changed {
        changed = false;
        // Left to right, so a->b->c becomes GETP(GETP(a->b)->c).
        let noncode = noncode_spans(&text);
        let mut from = 0;
        while let Some((op_i, mend, name)) = acc.next(&text, from) {
            from = mend;
            if in_spans(&noncode, op_i) {
                continue;
            }
            let b = text.as_bytes();
            // already wrapped by an earlier rewrite
            {
                let ps = postfix_start(&text, op_i);
                let w0 = crate::util::floor_cb(&text, ps.saturating_sub(12));
                let w0 = if ps < 12 { 0 } else { w0 };
                if crate::util::rsearch(&WRAPPED, &text[w0..ps]).is_some() {
                    continue;
                }
            }
            let mut k = op_i as isize - 1;
            while k >= 0 && matches!(b[k as usize], b' ' | b'\t' | b'\n') {
                k -= 1;
            }
            if b[op_i] == b'.' && k >= 0 && matches!(b[k as usize], b'{' | b',') {
                let line = line_base + count_nl(&text, 0, op_i) + 1;
                review.push(format!("{fname}:{line}: designated initializer .{name}"));
                continue;
            }
            let start = postfix_start(&text, op_i);
            let mut end = mend;
            // Array or not: this file's own declarations win, then headers,
            // then all; if still mixed, a following [ means array indexing.
            let f = &fields.1[&name];
            let mut kinds: HashSet<bool> = match f.files.get(fname) {
                Some(s) if !s.is_empty() => s.clone(),
                _ => {
                    let mut u = HashSet::new();
                    for (kf, v) in &f.files {
                        if kf.ends_with(".h") {
                            u.extend(v.iter().copied());
                        }
                    }
                    if u.is_empty() { f.array.clone() } else { u }
                }
            };
            if kinds.len() > 1 {
                let k2 = skip_space(&text, end);
                kinds = HashSet::from([at(&text, k2) == b'[']);
            }
            if kinds.contains(&true) {
                let mut e = end;
                loop {
                    let mut e2 = e;
                    while e2 < b.len() && matches!(b[e2], b' ' | b'\t') {
                        e2 += 1;
                    }
                    if e2 < b.len() && b[e2] == b'[' {
                        e = match_close(&text, e2).expect("ValueError: unbalanced") + 1;
                    } else {
                        break;
                    }
                }
                end = e;
            }
            // address-of / inc-dec / compound assignment: leave for a human
            let mut p = start as isize - 1;
            while p >= 0 && matches!(b[p as usize], b' ' | b'\t') {
                p -= 1;
            }
            let line = line_base + count_nl(&text, 0, op_i) + 1;
            // & and prefix ++ bind to the whole postfix chain; they only
            // concern this field if the chain ends here.
            let continues = continues_at(&text, end);
            if !continues && p >= 0 && b[p as usize] == b'&' && !(p >= 1 && b[p as usize - 1] == b'&') {
                review.push(format!("{fname}:{line}: address of {name}"));
                continue;
            }
            let incdec = p >= 1 && {
                let t = &b[p as usize - 1..=p as usize];
                t == b"++" || t == b"--"
            };
            if arith_at(&text, end) || (!continues && incdec) {
                review.push(format!("{fname}:{line}: arithmetic update of {name}"));
                continue;
            }
            let expr = text[start..end].to_string();
            let rhs = assignment_rhs(&text, end);
            // `*x->f = v` and `x->f[i] = v` store through the pointer
            let deref = p >= 0
                && b[p as usize] == b'*'
                && !continues
                && (p == 0 || b" \t\n(=!&|,;{}".contains(&b[p as usize - 1]));
            if let (Some((r0, r1)), false) = (rhs, deref) {
                let new = format!("SETP({expr}, {})", text[r0..r1].trim());
                text = format!("{}{}{}", &text[..start], new, &text[r1..]);
            } else {
                text = format!("{}GETP({expr}){}", &text[..start], &text[end..]);
            }
            changed = true;
            break;
        }
    }
    text
}

fn rewrite_accesses_file(text: &str, fields: &Fields, review: &mut Vec<String>, fname: &str, acc: &Option<Acc>) -> String {
    // Don't rewrite inside struct bodies (declarations).
    let mut spans = vec![];
    for m in crate::util::finditer(&OPEN, text) {
        let e = m.get(0).unwrap().end();
        if let Ok(c) = match_close(text, e - 1) {
            spans.push((e, c));
        }
    }
    spans.sort();
    let mut merged: Vec<(usize, usize)> = vec![];
    for (s, e) in spans {
        if let Some(l) = merged.last() {
            if s < l.1 {
                continue;
            }
        }
        merged.push((s, e));
    }
    let mut pieces = String::with_capacity(text.len() + 4096);
    let mut last = 0;
    for (s, e) in merged {
        pieces.push_str(&rewrite_accesses(&text[last..s], fields, review, fname, count_nl(text, 0, last), acc));
        pieces.push_str(&text[s..e]);
        last = e;
    }
    pieces.push_str(&rewrite_accesses(&text[last..], fields, review, fname, count_nl(text, 0, last), acc));
    pieces
}

fn file_name(p: &str) -> String {
    Path::new(p).file_name().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()
}

fn resolve(p: &str) -> PathBuf {
    let pb = PathBuf::from(p);
    pb.canonicalize().unwrap_or_else(|_| std::env::current_dir().unwrap().join(pb))
}

pub fn run(_root: &Path, args: Vec<String>) -> i32 {
    let mut args = args;
    if args.first().map(|s| s.as_str()) == Some("--revert") {
        revert(&args[1]);
        return 0;
    }
    let mut only: Option<HashSet<PathBuf>> = None;
    if args.first().map(|s| s.as_str()) == Some("--only") {
        only = Some(args[1].split(',').map(resolve).collect());
        args.drain(..2);
    }
    let mut fields: Fields = (vec![], HashMap::new());
    let mut review: Vec<String> = vec![];
    let mut order: Vec<String> = vec![];
    let mut texts: HashMap<String, String> = HashMap::new();
    for p in &args {
        if !texts.contains_key(p) {
            order.push(p.clone());
            texts.insert(p.clone(), read_text(Path::new(p)));
        }
    }
    for p in &args {
        // Fields converted by an earlier run still need their new accesses
        // rewritten (e.g. code merged in from the decomp since).
        let name = file_name(p);
        note_existing(&texts[p], &mut fields, &name);
        let t = convert_decls(&texts[p], &mut fields, &mut review, &name);
        texts.insert(p.clone(), t);
    }
    // sorted(..., key=len, reverse=True): longest first, ties in insertion order
    let mut names: Vec<&String> = fields.0.iter().collect();
    names.sort_by(|a, b| b.chars().count().cmp(&a.chars().count()));
    let acc = if names.is_empty() {
        None
    } else {
        let alt: Vec<String> = names.iter().map(|n| crate::util::escape(n)).collect();
        Some(Acc { names: re(&format!(r"^\s*({})\b", alt.join("|"))) })
    };
    for p in &args {
        let out = rewrite_accesses_file(&texts[p], &fields, &mut review, &file_name(p), &acc);
        texts.insert(p.clone(), out);
    }
    for p in &order {
        if only.as_ref().is_none_or(|o| o.contains(&resolve(p))) {
            crate::util::write(p, &texts[p]);
        }
    }
    let mut seen = HashSet::new();
    review.retain(|r| seen.insert(r.clone()));
    crate::util::write("fieldify_review.txt", &(review.join("\n") + "\n"));
    eprintln!(
        "{} pointer field names converted; {} sites to review (fieldify_review.txt)",
        fields.0.len(),
        review.len()
    );
    0
}

/// Undo GETP/SETP calls that GCC rejected because the member isn't a PTR32
/// (a same-named field that is a plain value or native pointer in that struct).
///
/// Reads a text build log; errors inside the macro are attributed to the use
/// site through the following "in expansion of macro" note.
fn revert(log: &str) {
    let bad = [
        "request for member",
        "has no member named \u{2018}_t\u{2019}",
        "has no member named \u{2018}h\u{2019}",
        "is a pointer; did you mean",
    ];
    let err = re(r"^(/\S+?\.[ch]):(\d+):(\d+): error: (.*)(?=\n?\z)");
    let note = re("^(/\\S+?\\.[ch]):(\\d+):(\\d+): note: in expansion of macro \u{2018}(GETP|SETP)\u{2019}");
    // Notes run from the outermost expansion to the innermost; the innermost
    // GETP/SETP is the one applied to the wrong member.
    let mut sites: HashSet<(String, usize, usize, String)> = HashSet::new();
    let mut pending = false;
    let mut last: Option<(String, usize, usize, String)> = None;
    let raw = std::fs::read(log).unwrap_or_else(|e| panic!("{log}: {e}"));
    let text = universal(String::from_utf8_lossy(&raw).into_owned());
    for line in text.split_inclusive('\n') {
        let line = line.strip_suffix('\n').unwrap_or(line);
        if let Some(m) = rmatch(&err, line) {
            if let Some(l) = last.take() {
                sites.insert(l);
            }
            pending = bad.iter().any(|b| g(&m, 4).contains(b));
            continue;
        }
        if let Some(m) = rmatch(&note, line) {
            if pending && g(&m, 1).contains("/src/game/") {
                last = Some((g(&m, 1).to_string(), g(&m, 2).parse().unwrap(), g(&m, 3).parse().unwrap(), g(&m, 4).to_string()));
            }
        }
    }
    if let Some(l) = last.take() {
        sites.insert(l);
    }
    let mut by_file: Vec<(String, Vec<(usize, usize, String)>)> = vec![];
    for (f, l, c, mac) in sites {
        match by_file.iter_mut().find(|x| x.0 == f) {
            Some(x) => x.1.push((l, c, mac)),
            None => by_file.push((f, vec![(l, c, mac)])),
        }
    }
    let mut n = 0;
    for (path, lst) in by_file {
        let mut src = read_text(Path::new(&path));
        let mut offs = vec![0usize];
        offs.extend(src.match_indices('\n').map(|(i, _)| i + 1));
        // A column counts characters from the line start (Python indexes by char).
        let pos_of = |src: &str, l: usize, c: usize| -> usize {
            let st = offs[l - 1];
            let mut it = src[st..].char_indices();
            let mut p = src.len() + (c - 1); // past the end: never a match
            let mut left = c - 1;
            loop {
                match it.next() {
                    Some((i, _)) if left == 0 => {
                        p = st + i;
                        break;
                    }
                    Some(_) => left -= 1,
                    None => {
                        if left == 0 {
                            p = src.len();
                        }
                        break;
                    }
                }
            }
            p
        };
        let mut items: Vec<(usize, usize, String)> = lst.into_iter().map(|(l, c, mac)| (pos_of(&src, l, c), l, mac)).collect();
        // right to left so earlier offsets stay valid
        items.sort_by(|a, b| b.0.cmp(&a.0));
        for (pos, _l, mac) in items {
            let head = format!("{mac}(");
            if !(pos <= src.len() && src.as_bytes()[pos..].starts_with(head.as_bytes())) {
                continue;
            }
            let o = pos + mac.len();
            let close = match_close(&src, o).expect("ValueError: unbalanced");
            let inner = &src[o + 1..close];
            let rep = if mac == "GETP" {
                inner.to_string()
            } else {
                let mut depth = 0i64;
                let mut k = None;
                for (kk, ch) in inner.char_indices() {
                    k = Some((kk, kk + ch.len_utf8()));
                    if matches!(ch, '(' | '[' | '{') {
                        depth += 1;
                    } else if matches!(ch, ')' | ']' | '}') {
                        depth -= 1;
                    } else if ch == ',' && depth == 0 {
                        break;
                    }
                }
                // no top-level comma: Python's loop leaves k on the last character
                let (k, after) = k.expect("NameError: k");
                format!("{} = {}", &inner[..k], inner[after..].trim())
            };
            src = format!("{}{}{}", &src[..pos], rep, &src[close + 1..]);
            n += 1;
        }
        crate::util::write(&path, &src);
    }
    println!("reverted {n} accesses");
}
