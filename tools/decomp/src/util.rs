//! Helpers shared by the ports: Python `re` semantics on top of fancy_regex, and
//! byte-offset text scanning. All offsets are byte offsets into UTF-8 text; the
//! sources are ASCII where the scanners look, so these match Python's char offsets.
//!
//! Regex notes for the ports (Python `re` -> fancy_regex):
//! - `re.match(p, s)` -> `rmatch` (anchored at the start of `s`, like Python).
//! - `re.fullmatch(p, s)` -> `rfull`.
//! - Python's `$` without re.M also matches before a final "\n": write `(?=\n?\z)`.
//! - With re.M put `(?m)` in front; with re.S `(?s)`.

#![allow(dead_code)]

use fancy_regex::{Captures, Regex};
use std::sync::LazyLock;

/// Compiled with the backtrack limit lifted (Python's `re` has none), see `re_big`.
pub fn re(p: &str) -> Regex {
    re_big(p)
}

/// A lazily compiled regex: `static R: Re = lazy(r"...");` doesn't work with closures
/// capturing nothing, so use `static R: LazyLock<Regex> = LazyLock::new(|| re(r"..."))`.
pub type Re = LazyLock<Regex>;

/// Python re.match: the pattern anchored at the start of `s`.
pub fn rmatch<'t>(r: &Regex, s: &'t str) -> Option<Captures<'t>> {
    let c = r.captures(s).ok()??;
    if c.get(0).unwrap().start() == 0 {
        Some(c)
    } else {
        // the leftmost match isn't at 0; an anchored try is needed only if the
        // pattern could also match at 0 later in the order, which leftmost search
        // rules out: a match at 0 would have been found first.
        None
    }
}

/// Python re.search.
pub fn rsearch<'t>(r: &Regex, s: &'t str) -> Option<Captures<'t>> {
    r.captures(s).ok()?
}

pub fn is_match(r: &Regex, s: &str) -> bool {
    r.is_match(s).unwrap_or(false)
}

/// All captures, left to right (Python re.finditer).
pub fn finditer<'r, 't>(r: &'r Regex, s: &'t str) -> Vec<Captures<'t>> {
    r.captures_iter(s).filter_map(|c| c.ok()).collect()
}

/// Python re.sub with a function.
pub fn sub(r: &Regex, s: &str, mut f: impl FnMut(&Captures) -> String) -> String {
    let mut out = String::with_capacity(s.len());
    let mut last = 0;
    for c in r.captures_iter(s) {
        let c = c.expect("regex");
        let m = c.get(0).unwrap();
        out.push_str(&s[last..m.start()]);
        out.push_str(&f(&c));
        last = m.end();
    }
    out.push_str(&s[last..]);
    out
}

/// Group text or "" (Python's m.group(n) or '' for unmatched optional groups
/// where the port needs a string).
pub fn g<'t>(c: &Captures<'t>, i: usize) -> &'t str {
    c.get(i).map(|m| m.as_str()).unwrap_or("")
}

pub fn gn<'t>(c: &Captures<'t>, n: &str) -> &'t str {
    c.name(n).map(|m| m.as_str()).unwrap_or("")
}

pub fn escape(s: &str) -> String {
    fancy_regex::escape(s).into_owned()
}

/// Byte at i (0 past the end).
#[inline]
pub fn at(s: &str, i: usize) -> u8 {
    *s.as_bytes().get(i).unwrap_or(&0)
}

/// Python str.isalnum() for the character whose UTF-8 encoding covers byte i.
pub fn is_alnum_at(s: &str, i: usize) -> bool {
    let b = s.as_bytes()[i];
    if b < 0x80 {
        return b.is_ascii_alphanumeric();
    }
    let mut st = i;
    while !s.is_char_boundary(st) {
        st -= 1;
    }
    s[st..].chars().next().map(|c| c.is_alphanumeric()).unwrap_or(false)
}

/// Largest char boundary <= i.
pub fn floor_cb(s: &str, mut i: usize) -> usize {
    if i >= s.len() {
        return s.len();
    }
    while !s.is_char_boundary(i) {
        i -= 1;
    }
    i
}

/// Python's " ".join(s.split()).
pub fn squash(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// Python's str.count("\n", 0, end).
pub fn count_nl(s: &str, start: usize, end: usize) -> usize {
    s.as_bytes()[start..end.min(s.len())].iter().filter(|&&b| b == b'\n').count()
}

pub fn read(p: impl AsRef<std::path::Path>) -> String {
    let p = p.as_ref();
    std::fs::read_to_string(p).unwrap_or_else(|e| panic!("{}: {e}", p.display()))
}

/// Python read_text(errors="replace").
pub fn read_lossy(p: impl AsRef<std::path::Path>) -> String {
    let p = p.as_ref();
    let b = std::fs::read(p).unwrap_or_else(|e| panic!("{}: {e}", p.display()));
    String::from_utf8_lossy(&b).into_owned()
}

pub fn write(p: impl AsRef<std::path::Path>, s: &str) {
    let p = p.as_ref();
    std::fs::write(p, s).unwrap_or_else(|e| panic!("{}: {e}", p.display()));
}

/// Sorted glob of one directory by extension (Python sorted(dir.glob("*.ext"))).
pub fn glob_ext(dir: &std::path::Path, ext: &str) -> Vec<std::path::PathBuf> {
    let mut v: Vec<_> = std::fs::read_dir(dir)
        .map(|r| {
            r.filter_map(|e| e.ok().map(|e| e.path()))
                .filter(|p| p.extension().and_then(|e| e.to_str()) == Some(ext) && p.is_file())
                .collect()
        })
        .unwrap_or_default();
    v.sort();
    v
}

/// A regex with fancy_regex's backtrack limit lifted (Python's `re` has none): for
/// the patterns with lookarounds over long name alternations.
pub fn re_big(p: &str) -> Regex {
    fancy_regex::RegexBuilder::new(p)
        .backtrack_limit(usize::MAX)
        .build()
        .unwrap_or_else(|e| panic!("bad regex {p}: {e}"))
}
