//! Rewrite byte-offset struct accesses into field accesses, using the struct
//! layouts clang computes from the headers.
//!
//!   hwvx-decomp offsets-to-fields LAYOUTS FILE FUNC [VAR=Type ...] [--rename A=B ...]
//!
//! LAYOUTS: output of `clang -Xclang -fdump-record-layouts-complete
//! -Xclang -fdump-record-layouts` on a file including the headers.
//!
//! In function FUNC of FILE the variables given are retyped (their u8 * / void *
//! declarations, parameters included, become Type *); variables the function
//! already declares as pointers to a known struct are typed from their
//! declarations. Then every `BASE + 0xNN`
//! whose BASE has a known struct-pointer type is rewritten according to what
//! surrounds it:
//!
//!     *(T *)(BASE + 0xNN)          -> BASE->field
//!     SLOT_LOAD(T, (BASE + 0xNN))  -> GETP(BASE->field)       (PTR32 fields)
//!     SLOT_STORE((BASE + 0xNN), v) -> SETP(BASE->field, v)
//!     f(..., BASE + 0xNN, ...)     -> &BASE->field            (a field starting there)
//!     BASE[0xNN]                   -> BASE->field             (byte fields; BASE one
//!                                                              of the variables given)
//!     M2C_FIELD(BASE, T *, 0xNN)   -> BASE->field
//!
//! BASE may carry a (u8 *) / (char *) cast and parentheses. The types of
//! expressions are worked out from the layouts: variables, GETP(x->f) of PTR32
//! fields, chains of ->, . and [i]. Rounds repeat until nothing changes, so
//! nested accesses resolve inside out. A site is rewritten only when a field
//! starts at the offset with the access's size; the rest are listed.
//!
//! Afterwards build and run `hwvx-decomp check-ptr-offsets`: a leftover `BASE + 0xNN`
//! on a retyped variable now steps whole structs.
//!
//! A line-for-line port of tools/offsets_to_fields.py (same output, quirks included).

use crate::util::{escape, finditer, g, is_alnum_at, re, rmatch, rsearch, Re};
use std::collections::{HashMap, HashSet};
use std::path::Path;

fn scalar(t: &str) -> Option<i64> {
    Some(match t {
        "u8" | "s8" | "char" => 1,
        "u16" | "s16" => 2,
        "u32" | "s32" | "f32" | "int" | "BOOL" => 4,
        "u64" | "s64" | "f64" => 8,
        _ => return None,
    })
}

fn access(t: &str) -> Option<i64> {
    Some(match t {
        "u8" | "s8" | "char" => 1,
        "u16" | "s16" => 2,
        "u32" | "s32" | "f32" | "GXColor" => 4,
        "f64" | "s64" | "u64" => 8,
        "Vector4" | "Vector" | "Quaternion" => 16,
        "Matrix4" => 64,
        _ => return None,
    })
}

const FLOATS: &[&str] = &["f32", "f64"];
const INTS: &[&str] = &["u8", "s8", "char", "u16", "s16", "u32", "s32", "u64", "s64", "int", "BOOL"];

/// Python int(s, 0) for the literals these patterns capture.
fn int0(s: &str) -> i64 {
    if let Some(h) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        return i64::from_str_radix(h, 16).expect("int");
    }
    if s.len() > 1 && s.starts_with('0') && !s.bytes().all(|b| b == b'0') {
        panic!("ValueError: invalid literal for int() with base 0: {s:?}");
    }
    s.parse().expect("int")
}

// ---- layouts -------------------------------------------------------------

struct Node {
    off: i64,
    ty: String,
    name: Option<String>,
    kids: Vec<usize>,
    depth: i64,
    size: Option<i64>,
}

struct Rec {
    n: Vec<Node>,
    r: HashMap<String, usize>,
}

static L_HEAD: Re = Re::new(|| re(r"^\s*0 \| (struct|union) (.+)(?=\n?\z)"));
static L_SIZEOF: Re = Re::new(|| re(r"\[sizeof=(\d+)"));
static L_FIELD: Re = Re::new(|| re(r"^\s*(\d+)(?::\d+-\d+)? \|( +)(.+)(?=\n?\z)"));
static IDENT: Re = Re::new(|| re(r"^[A-Za-z_]\w*(?=\n?\z)"));

fn parse_layouts(path: &str) -> Rec {
    let text = crate::util::read(path);
    let lines: Vec<&str> = text.split('\n').collect();
    let mut rc = Rec { n: Vec::new(), r: HashMap::new() };
    let mut i = 0;
    while i < lines.len() {
        let m = rmatch(&L_HEAD, lines[i]);
        if let Some(m) = m.filter(|_| i > 0 && lines[i - 1].starts_with("*** Dumping AST Record Layout")) {
            let rname = g(&m, 2).trim().to_string();
            let root = rc.n.len();
            rc.n.push(Node { off: 0, ty: format!("{} {}", g(&m, 1), rname), name: None, kids: vec![], depth: -1, size: None });
            let mut stack = vec![root];
            i += 1;
            while i < lines.len() && !lines[i].starts_with("*** Dumping") {
                let l = lines[i];
                if let Some(sm) = rsearch(&L_SIZEOF, l) {
                    rc.n[root].size = Some(g(&sm, 1).parse().unwrap());
                    break;
                }
                if let Some(fm) = rmatch(&L_FIELD, l) {
                    let off: i64 = g(&fm, 1).parse().unwrap();
                    let depth = (g(&fm, 2).len() as i64 - 3).div_euclid(2);
                    let body = g(&fm, 3).trim();
                    let parts: Vec<&str> = match body.rfind(' ') {
                        Some(p) => vec![&body[..p], &body[p + 1..]],
                        None => vec![body],
                    };
                    let (ftype, fname) = if parts.len() == 2
                        && IDENT.is_match(parts[1]).unwrap()
                        && !parts[0].ends_with("struct")
                        && !parts[0].ends_with("union")
                        && !parts[1].contains("(unnamed")
                        && !parts[1].contains("(anonymous")
                    {
                        (parts[0].to_string(), Some(parts[1].to_string()))
                    } else {
                        (body.to_string(), None)
                    };
                    let node = rc.n.len();
                    rc.n.push(Node { off, ty: ftype, name: fname, kids: vec![], depth, size: None });
                    while stack.len() > 1 && rc.n[*stack.last().unwrap()].depth >= depth {
                        stack.pop();
                    }
                    let top = *stack.last().unwrap();
                    rc.n[top].kids.push(node);
                    stack.push(node);
                }
                i += 1;
            }
            let short = rname.rsplit("::").next().unwrap().to_string();
            rc.r.insert(rname, root);
            rc.r.entry(short).or_insert(root);
        }
        i += 1;
    }
    rc
}

static PTR32_T: Re = Re::new(|| re(r"^(.*?)\s*\*\s*\[0\](?=\n?\z)"));

impl Rec {
    fn ptr32_target(&self, node: usize) -> Option<String> {
        let n = &self.n[node];
        let kids = &n.kids;
        if n.ty.starts_with("union")
            && kids.len() == 2
            && self.n[kids[0]].name.as_deref() == Some("h")
            && self.n[kids[1]].name.as_deref() == Some("_t")
        {
            if let Some(m) = rmatch(&PTR32_T, &self.n[kids[1]].ty) {
                return Some(g(&m, 1).trim().to_string());
            }
        }
        None
    }

    fn rec_key(&self, t: &str) -> Option<String> {
        let t = t.trim();
        let alt = t.replacen("struct ", "", 1).replacen("union ", "", 1);
        for k in [t, alt.trim()] {
            if self.r.contains_key(k) {
                return Some(k.to_string());
            }
        }
        None
    }

    fn root(&self, k: &str) -> usize {
        self.r[k]
    }

    fn type_size(&self, t: &str) -> Option<i64> {
        static UN: Re = Re::new(|| re(r"^union .*\(unnamed(?: union)? at [^)]*\)\[(\d+)\](?=\n?\z)"));
        static ARR: Re = Re::new(|| re(r"^(.*)\[(\d+)\](?=\n?\z)"));
        let t = t.trim();
        if let Some(m) = rmatch(&UN, t) {
            return Some(4 * g(&m, 1).parse::<i64>().unwrap());
        }
        if let Some(m) = rmatch(&ARR, t) {
            let s = self.type_size(g(&m, 1))?;
            return Some(s * g(&m, 2).parse::<i64>().unwrap());
        }
        if let Some(s) = scalar(t) {
            return Some(s);
        }
        if let Some(k) = self.rec_key(t) {
            if let Some(s) = self.n[self.r[&k]].size.filter(|&s| s != 0) {
                return Some(s);
            }
        }
        None
    }

    fn node_size(&self, n: usize) -> Option<i64> {
        if self.ptr32_target(n).is_some() {
            return Some(4);
        }
        let node = &self.n[n];
        let mut s = self.type_size(&node.ty);
        if s.is_none() {
            if let Some(k) = self.rec_key(&node.ty) {
                s = self.n[self.r[&k]].size;
            }
        }
        if s.is_none() && !node.kids.is_empty() {
            let mut ends = vec![];
            for &k in &node.kids {
                let ks = self.node_size(k)?;
                ends.push(self.n[k].off - node.off + ks);
            }
            s = ends.into_iter().max();
        }
        s
    }

    fn node_record(&self, n: usize) -> Option<String> {
        static TRAIL: Re = Re::new(|| re(r"\[\d+\](?=\n?\z)"));
        let t = crate::util::sub(&TRAIL, &self.n[n].ty, |_| String::new());
        self.rec_key(&t)
    }

    fn leaf_type(&self, n: usize) -> String {
        if self.ptr32_target(n).is_some() {
            return "u32".into();
        }
        static TRAIL: Re = Re::new(|| re(r"\[\d+\](?=\n?\z)"));
        crate::util::sub(&TRAIL, &self.n[n].ty, |_| String::new()).trim().to_string()
    }

    /// Field path at offset `off`. mode: "scalar" (size), "ptr" (a PTR32), "addr".
    fn find_path(&self, node: usize, base: i64, off: i64, size: i64, mode: &str) -> Option<(Vec<Option<String>>, usize)> {
        static ARR: Re = Re::new(|| re(r"^(.*)\[(\d+)\](?=\n?\z)"));
        let mut cands = vec![];
        for &k in &self.n[node].kids {
            let Some(ks) = self.node_size(k) else { continue };
            let rel = self.n[k].off - base;
            if !(rel <= off && off < rel + ks) {
                continue;
            }
            cands.push((k, rel, ks));
        }
        for (k, rel, ks) in cands {
            let kn = &self.n[k];
            let name = kn.name.clone();
            if self.ptr32_target(k).is_some() {
                if off == rel && (mode == "ptr" || mode == "addr") || (off == rel && mode == "scalar" && size == 4) {
                    return Some((vec![name], k));
                }
                continue;
            }
            if mode == "addr" && off == rel && name.as_deref().is_some_and(|s| !s.is_empty()) {
                return Some((vec![name], k));
            }
            if let Some(am) = rmatch(&ARR, &kn.ty) {
                let et = g(&am, 1).trim().to_string();
                let es = match self.type_size(&et) {
                    Some(es) if es != 0 => es,
                    _ => continue,
                };
                let idx = (off - rel).div_euclid(es);
                let inner = off - rel - idx * es;
                let ix = Some(format!("[{idx}]"));
                if scalar(&et).is_some() || et.ends_with('*') {
                    if inner == 0 && (mode == "scalar" && es == size || mode == "addr") {
                        return Some((vec![name, ix], k));
                    }
                    continue;
                }
                if let Some(ek) = self.rec_key(&et) {
                    if inner == 0 && mode == "addr" {
                        return Some((vec![name, ix], k));
                    }
                    if inner == 0 && mode == "scalar" && es == size {
                        return Some((vec![name, ix], k));
                    }
                    if let Some(r) = self.find_path(self.r[&ek], 0, inner, size, mode) {
                        let mut p = vec![name, ix];
                        p.extend(r.0);
                        return Some((p, r.1));
                    }
                }
                continue;
            }
            if !kn.kids.is_empty() {
                let named = name.as_deref().is_some_and(|s| !s.is_empty());
                if mode == "scalar" && off == rel && ks == size && named && self.rec_key(&kn.ty).is_none() && access(&kn.ty).is_some() {
                    return Some((vec![name], k));
                }
                let last = kn.ty.split_whitespace().last().unwrap_or("");
                if mode == "scalar" && off == rel && ks == size && named && matches!(last, "Vector4" | "Quaternion" | "Matrix4" | "GXColor") {
                    return Some((vec![name], k));
                }
                if let Some(r) = self.find_path(k, base, off, size, mode) {
                    let mut p = if named { vec![name] } else { vec![] };
                    p.extend(r.0);
                    return Some((p, r.1));
                }
                continue;
            }
            if mode == "scalar" && off == rel && ks == size {
                return Some((vec![name], k));
            }
        }
        None
    }

    /// txt for an access of type acc to field node, or None if the types clash.
    fn access_text(&self, txt: String, acc: &str, node: usize, after: &str, before: &str) -> Option<String> {
        static W1: Re = Re::new(|| re(r"^\s*(=(?!=)|\+=|-=|\|=|&=|\^=|<<=|>>=|\+\+|--)"));
        static W2: Re = Re::new(|| re(r"(\+\+|--)\s*(?=\n?\z)"));
        static W3: Re = Re::new(|| re(r"&\s*(?=\n?\z)"));
        let ft = self.leaf_type(node);
        if acc == ft || matches!(acc, "Vector4" | "Quaternion" | "Matrix4" | "GXColor" | "Vector") {
            return Some(txt);
        }
        if FLOATS.contains(&acc) != FLOATS.contains(&ft.as_str()) {
            return None;
        }
        if INTS.contains(&acc) && INTS.contains(&ft.as_str()) {
            let write = rmatch(&W1, after).is_some() || rsearch(&W2, before).is_some() || rsearch(&W3, before).is_some();
            if write {
                return Some(txt);
            }
            return Some(format!("(({acc}){txt})"));
        }
        None
    }
}

fn join(base: &str, path: &[Option<String>]) -> String {
    static J: Re = Re::new(|| re(r"^(?:[&*]|\(\w[\w\s]*\*\))"));
    let mut out = if rmatch(&J, base).is_some() { format!("({base})") } else { base.to_string() };
    let mut first = true;
    for p in path {
        let p = p.as_deref().expect("AttributeError: 'NoneType' object has no attribute 'startswith'");
        if p.starts_with('[') {
            out += p;
        } else {
            out += if first { "->" } else { "." };
            out += p;
            first = false;
        }
    }
    out
}

// ---- expression typing ---------------------------------------------------

fn strip_parens(e: &str) -> String {
    let mut e = e.trim();
    while e.starts_with('(') && mtch(e, 0) == e.len() as i64 - 1 {
        e = e[1..e.len() - 1].trim();
    }
    e.to_string()
}

/// Python match(s, i): the bracket closing s[i] ( and [ counted together), or -1.
fn mtch(s: &str, i: usize) -> i64 {
    let b = s.as_bytes();
    assert!(matches!(b[i], b'(' | b'['), "KeyError");
    let mut depth = 0;
    for (j, &c) in b.iter().enumerate().skip(i) {
        if c == b'(' || c == b'[' {
            depth += 1;
        } else if c == b')' || c == b']' {
            depth -= 1;
            if depth == 0 {
                return j as i64;
            }
        }
    }
    -1
}

static CASTS: Re = Re::new(|| re(r"^\((?:const\s+)?(?:u8|s8|char|void|struct \w+|\w+)\s*\*\)\s*"));

fn is_digit_at(s: &str, i: usize) -> bool {
    let b = s.as_bytes()[i];
    if b < 0x80 {
        return b.is_ascii_digit();
    }
    let st = crate::util::floor_cb(s, i);
    s[st..].chars().next().is_some_and(|c| c.is_numeric())
}

/// Split e at its last top-level ->, . or [...]: (prefix, op, rest).
fn split_last(e: &str) -> Option<(String, &'static str, String)> {
    let b = e.as_bytes();
    let n = b.len() as i64;
    let mut depth = 0;
    let mut i = n - 1;
    while i >= 0 {
        let c = b[i as usize];
        if c == b')' || c == b']' {
            if c == b']' && depth == 0 {
                let mut d = 0;
                let mut j = i;
                while j >= 0 {
                    if b[j as usize] == b']' {
                        d += 1;
                    } else if b[j as usize] == b'[' {
                        d -= 1;
                        if d == 0 {
                            break;
                        }
                    }
                    j -= 1;
                }
                if j > 0 {
                    let (j, i) = (j as usize, i as usize);
                    return Some((e[..j].to_string(), "[", e[j + 1..i].to_string()));
                }
                return None;
            }
            depth += 1;
        } else if c == b'(' || c == b'[' {
            depth -= 1;
        } else if depth == 0 {
            let iu = i as usize;
            // e[i-1:i+1] == '->' (empty slice at i == 0)
            if i >= 1 && &b[iu - 1..iu + 1] == b"->" {
                return Some((e[..iu - 1].to_string(), "->", e[iu + 1..].to_string()));
            }
            // e[i-1] wraps to the last character at i == 0
            let prev = if i >= 1 { iu - 1 } else { (n - 1) as usize };
            if c == b'.' && !is_digit_at(e, prev) {
                return Some((e[..iu].to_string(), ".", e[iu + 1..].to_string()));
            }
        }
        i -= 1;
    }
    None
}

enum Lv {
    Node(usize),
    Rec(String),
}

struct Typer<'a> {
    r: &'a Rec,
    vars: HashMap<String, String>,
    byte_vars: HashSet<String>,
}

impl Typer<'_> {
    fn declared(&self, e: &str) -> bool {
        self.vars.contains_key(e) && !self.byte_vars.contains(e)
    }

    /// Record R if e has type R *.
    fn ptr(&self, e: &str) -> Option<String> {
        static GETP: Re = Re::new(|| re(r"^GETP\((.*)\)(?=\n?\z)"));
        static AMP: Re = Re::new(|| re(r"^&(.*)(?=\n?\z)"));
        static CLEAN: Re = Re::new(|| re(r"[()*]|const"));
        let e = strip_parens(e);
        if let Some(m) = rmatch(&CASTS, &e) {
            let g0 = g(&m, 0);
            let sq = g0.replace(' ', "");
            if !matches!(sq.as_str(), "(u8*)" | "(s8*)" | "(char*)" | "(void*)") {
                let t = crate::util::sub(&CLEAN, g0, |_| String::new());
                if let Some(k) = self.r.rec_key(t.trim()) {
                    return Some(k);
                }
            }
        }
        if let Some(v) = self.vars.get(&e) {
            return Some(v.clone());
        }
        if let Some(m) = rmatch(&GETP, &e) {
            if mtch(&e, 4) == e.len() as i64 - 1 {
                if let Some(Lv::Node(n)) = self.lval(g(&m, 1)) {
                    if let Some(t) = self.r.ptr32_target(n).filter(|t| !t.is_empty()) {
                        return self.r.rec_key(&t);
                    }
                }
            }
        }
        if let Some(m) = rmatch(&AMP, &e) {
            match self.lval(g(&m, 1)) {
                Some(Lv::Node(n)) => {
                    let nn = &self.r.n[n];
                    return if nn.kids.is_empty() || self.r.rec_key(&nn.ty).is_some() { self.r.node_record(n) } else { None };
                }
                Some(Lv::Rec(s)) => return Some(s),
                None => {}
            }
        }
        None
    }

    /// Field node (or record name for a whole element) an lvalue denotes.
    fn lval(&self, e: &str) -> Option<Lv> {
        static ARRT: Re = Re::new(|| re(r"\[\d+\](?=\n?\z)"));
        let e = strip_parens(e);
        let (pre, op, rest) = split_last(&e)?;
        match op {
            "[" => {
                if let Some(r) = self.ptr(&pre) {
                    return Some(Lv::Rec(r));
                }
                if let Some(Lv::Node(n)) = self.lval(&pre) {
                    if rsearch(&ARRT, &self.r.n[n].ty).is_some() {
                        return self.r.node_record(n).map(Lv::Rec);
                    }
                }
                None
            }
            "->" => {
                let r = self.ptr(&pre)?;
                self.field(self.r.root(&r), &rest).map(Lv::Node)
            }
            "." => match self.lval(&pre) {
                Some(Lv::Rec(n)) => self.field(self.r.root(&n), &rest).map(Lv::Node),
                Some(Lv::Node(n)) => {
                    if let Some(k) = self.r.rec_key(&self.r.n[n].ty) {
                        return self.field(self.r.root(&k), &rest).map(Lv::Node);
                    }
                    self.field(n, &rest).map(Lv::Node)
                }
                None => None,
            },
            _ => None,
        }
    }

    fn field(&self, node: usize, name: &str) -> Option<usize> {
        for &k in &self.r.n[node].kids {
            let kn = &self.r.n[k];
            if kn.name.as_deref() == Some(name) {
                return Some(k);
            }
            if kn.name.is_none() && !kn.kids.is_empty() {
                if let Some(r) = self.field(k, name) {
                    return Some(r);
                }
            }
        }
        None
    }
}

// ---- rewriting -------------------------------------------------------------

/// Start index of the operand ending at s[end-1] (identifier chain, calls).
fn left_operand(s: &str, end: usize) -> usize {
    static CAST: Re = Re::new(|| re(r"\((?:u8|s8|char) \*\)\s*(?=\n?\z)"));
    let b = s.as_bytes();
    let mut i = end;
    while i > 0 && b[i - 1] == b' ' {
        i -= 1;
    }
    let mut j = i as i64;
    while j > 0 {
        let ju = j as usize;
        let c = b[ju - 1];
        if (c == b')' || c == b']') && ju < i && (is_alnum_at(s, ju) || b[ju] == b'_') {
            break;
        }
        if c == b')' || c == b']' {
            let mut d = 0;
            let mut k = ju as i64 - 1;
            while k >= 0 {
                let ck = b[k as usize];
                if ck == b')' || ck == b']' {
                    d += 1;
                } else if ck == b'(' || ck == b'[' {
                    d -= 1;
                    if d == 0 {
                        break;
                    }
                }
                k -= 1;
            }
            j = k;
            continue;
        }
        if is_alnum_at(s, ju - 1) || c == b'_' {
            j -= 1;
            continue;
        }
        if c == b'>' && ju >= 2 && b[ju - 2] == b'-' {
            j -= 2;
            continue;
        }
        if c == b'.' {
            j -= 1;
            continue;
        }
        break;
    }
    // unbalanced text: Python would go on with j == -1 (s[:-1]); not reachable on code that compiles
    let mut j = j.max(0) as usize;
    if let Some(m) = rsearch(&CAST, &s[..j]) {
        j = m.get(0).unwrap().start();
    }
    j
}

static CAST_PRE: Re = Re::new(|| re(r"^\((?:u8|s8|char) \*\)\s*"));

fn uncast(s: &str) -> String {
    crate::util::sub(&CAST_PRE, s, |_| String::new())
}

/// BASE + i * sizeof(*BASE) -> (&BASE[i]).
fn rewrite_strides(body: String, typer: &Typer) -> (String, bool) {
    static STRIDE: Re = Re::new(|| re(r"\s*\+\s*([A-Za-z_][\w.\->\[\]]*|\([^()]*\))\s*\*\s*(0x[0-9A-Fa-f]+|\d+)\b"));
    let mut changed = false;
    let mut out = body;
    let ms: Vec<(usize, usize, String, String)> = finditer(&STRIDE, &out)
        .iter()
        .map(|m| (m.get(0).unwrap().start(), m.get(0).unwrap().end(), g(m, 1).to_string(), g(m, 2).to_string()))
        .collect();
    for (ms_, me, g1, g2) in ms.into_iter().rev() {
        let st = left_operand(&out, ms_);
        let base = strip_parens(&uncast(&strip_parens(&out[st..ms_])));
        if base.is_empty() {
            continue;
        }
        let Some(rec) = typer.ptr(&base) else { continue };
        if typer.r.n[typer.r.root(&rec)].size != Some(int0(&g2)) {
            continue;
        }
        out = format!("{}(&{}[{}]){}", &out[..st], base, g1, &out[me..]);
        changed = true;
    }
    (out, changed)
}

/// (&E)->f -> E.f
fn tidy(body: String) -> String {
    let mut out = body;
    loop {
        let mut i = out.find("(&").map(|x| x as i64).unwrap_or(-1);
        let mut found = false;
        while i >= 0 {
            let iu = i as usize;
            let j = mtch(&out, iu);
            if j > 0 && out.get(j as usize + 1..j as usize + 3) == Some("->") {
                let ju = j as usize;
                out = format!("{}{}.{}", &out[..iu], &out[iu + 2..ju], &out[ju + 3..]);
                found = true;
                break;
            }
            i = out[iu + 1..].find("(&").map(|x| (x + iu + 1) as i64).unwrap_or(-1);
        }
        if !found {
            return out;
        }
    }
}

fn count(s: &str, c: char) -> i64 {
    s.matches(c).count() as i64
}

/// Index past n closing parens at the start of `after` (spaces allowed), or None.
fn closers(after: &str, n: i64) -> Option<usize> {
    let b = after.as_bytes();
    let mut k = 0;
    for _ in 0..n.max(0) {
        while k < b.len() && b[k] == b' ' {
            k += 1;
        }
        if k < b.len() && b[k] == b')' {
            k += 1;
        } else {
            return None;
        }
    }
    Some(k)
}

fn split_args(s: &str) -> Vec<String> {
    let mut parts = vec![];
    let mut depth = 0;
    let mut cur = String::new();
    for c in s.chars() {
        if c == '(' || c == '[' {
            depth += 1;
        } else if c == ')' || c == ']' {
            depth -= 1;
        }
        if c == ',' && depth == 0 {
            parts.push(std::mem::take(&mut cur));
        } else {
            cur.push(c);
        }
    }
    parts.push(cur);
    parts
}

fn rewrite(body: String, typer: &Typer) -> (String, bool) {
    static OFFRE: Re = Re::new(|| re(r"\s*\+\s*(0x[0-9A-Fa-f]+|\d+)\b"));
    static DECLCAST: Re = Re::new(|| re(r"^\(*\((?:u8|s8|char) \*\)"));
    static DEREF: Re = Re::new(|| re(r"\*\(\s*(\w+)\s*\*\s*\)\s*\(\s*(\(\s*)*(?=\n?\z)"));
    static SLOAD: Re = Re::new(|| re(r"SLOT_LOAD\(\s*[^,()]+?(?:\([^()]*\))?\s*,\s*(\(\s*)*(?=\n?\z)"));
    static SSTORE: Re = Re::new(|| re(r"SLOT_STORE\(\s*(\(\s*)*(?=\n?\z)"));
    static SSTORE_A: Re = Re::new(|| re(r"^(\s*\))*\s*,"));
    static MBEF: Re = Re::new(|| re(r"(?:(?<![*\s])\(|,|=|\breturn)\s*(?=\n?\z)"));
    static MBEF_NO: Re = Re::new(|| re(r"\*\s*\(\s*(?=\n?\z)"));
    static MAFT: Re = Re::new(|| re(r"^\s*(?:,|;|\)(?!\s*(?:\[|\.|->)))"));
    static IDX: Re = Re::new(|| re(r"\[\s*(0x[0-9A-Fa-f]+|\d+)\s*\]"));
    static M2C: Re = Re::new(|| re(r"M2C_FIELD\("));
    static TM: Re = Re::new(|| re(r"^(\w+)\s*\*(?=\n?\z)"));
    let r = typer.r;
    let (body, mut changed) = rewrite_strides(body, typer);
    let mut out = body;
    let sites: Vec<(usize, usize, String)> =
        finditer(&OFFRE, &out).iter().map(|m| (m.get(0).unwrap().start(), m.get(0).unwrap().end(), g(m, 1).to_string())).collect();
    for (ms_, me, g1) in sites.into_iter().rev() {
        let st = left_operand(&out, ms_);
        let base_txt = out[st..ms_].to_string();
        let base = strip_parens(&uncast(&strip_parens(&base_txt)));
        if base.is_empty() || is_digit_at(&base, 0) {
            continue;
        }
        if typer.declared(&base) && rmatch(&DECLCAST, base_txt.trim()).is_none() {
            continue;
        }
        let Some(rec) = typer.ptr(&base) else { continue };
        let rroot = r.root(&rec);
        let off = int0(&g1);
        let before = out[..st].to_string();
        let after = out[me..].to_string();
        // *(T *)( [ ( ] BASE + OFF [ ) ] )
        if let Some(mb) = rsearch(&DEREF, &before) {
            let mbs = mb.get(0).unwrap().start();
            let tail = &before[mbs..];
            let nopen = count(tail, '(') - count(tail, ')');
            let k = closers(&after, nopen);
            let acc = g(&mb, 1).to_string();
            let size = access(&acc);
            let p = size.and_then(|size| r.find_path(rroot, 0, off, size, "scalar"));
            if let (Some(p), Some(k)) = (p, k) {
                let mut txt = join(&base, &p.0);
                if r.ptr32_target(p.1).is_some() {
                    txt += ".h";
                }
                if let Some(txt) = r.access_text(txt, &acc, p.1, &after[k..], &out[..mbs]) {
                    out = format!("{}{}{}", &out[..mbs], txt, &after[k..]);
                    changed = true;
                    continue;
                }
            }
        }
        // SLOT_LOAD(T, ( BASE + OFF ))
        if let Some(mb) = rsearch(&SLOAD, &before) {
            let mbs = mb.get(0).unwrap().start();
            let tail = &before[mbs..];
            let nopen = count(tail, '(') - count(tail, ')');
            let p = r.find_path(rroot, 0, off, 4, "ptr");
            let k = closers(&after, nopen);
            if let (Some(p), Some(k)) = (p, k) {
                if r.ptr32_target(p.1).is_some() {
                    out = format!("{}GETP({}){}", &out[..mbs], join(&base, &p.0), &after[k..]);
                    changed = true;
                    continue;
                }
            }
        }
        // SLOT_STORE(( BASE + OFF ), v)
        if let Some(mb) = rsearch(&SSTORE, &before) {
            let mbs = mb.get(0).unwrap().start();
            let tail = &before[mbs..];
            let nopen = count(tail, '(') - count(tail, ')') - 1;
            let p = r.find_path(rroot, 0, off, 4, "ptr");
            let ma = rmatch(&SSTORE_A, &after);
            if let (Some(p), Some(ma)) = (p, ma) {
                let g0 = ma.get(0).unwrap();
                if r.ptr32_target(p.1).is_some() && count(g0.as_str(), ')') == nopen {
                    out = format!("{}SETP({},{}", &out[..mbs], join(&base, &p.0), &after[g0.end()..]);
                    changed = true;
                    continue;
                }
            }
        }
        // address: an argument / initializer / operand ending there
        let mut mbef = rsearch(&MBEF, &before).is_some();
        if rsearch(&MBEF_NO, &before).is_some() {
            mbef = false;
        }
        let maft = rmatch(&MAFT, &after).is_some();
        if mbef && maft {
            if let Some(p) = r.find_path(rroot, 0, off, 0, "addr") {
                out = format!("{}&{}{}", &out[..st], join(&base, &p.0), after);
                changed = true;
                continue;
            }
        }
    }
    // BASE[0xNN] and M2C_FIELD(BASE, T *, 0xNN)
    let idx: Vec<(usize, usize, String)> =
        finditer(&IDX, &out).iter().map(|m| (m.get(0).unwrap().start(), m.get(0).unwrap().end(), g(m, 1).to_string())).collect();
    for (ms_, me, g1) in idx.into_iter().rev() {
        let st = left_operand(&out, ms_);
        let base = strip_parens(&out[st..ms_]);
        if !typer.byte_vars.contains(&base) {
            continue;
        }
        let Some(rec) = typer.ptr(&base) else { continue };
        if let Some(p) = r.find_path(r.root(&rec), 0, int0(&g1), 1, "scalar") {
            if let Some(txt) = r.access_text(join(&base, &p.0), "u8", p.1, &out[me..], &out[..st]) {
                out = format!("{}{}{}", &out[..st], txt, &out[me..]);
                changed = true;
            }
        }
    }
    let m2c: Vec<(usize, usize)> = finditer(&M2C, &out).iter().map(|m| (m.get(0).unwrap().start(), m.get(0).unwrap().end())).collect();
    for (ms_, me) in m2c.into_iter().rev() {
        let e = mtch(&out, me - 1);
        assert!(e >= 0, "unbalanced M2C_FIELD");
        let e = e as usize;
        let parts: Vec<String> = split_args(&out[me..e]).iter().map(|p| p.trim().to_string()).collect();
        if parts.len() != 3 {
            continue;
        }
        let base = strip_parens(&uncast(&parts[0]));
        let rec = typer.ptr(&base);
        let tm = rmatch(&TM, &parts[1]);
        let (Some(rec), Some(tm)) = (rec, tm) else { continue };
        let acc = g(&tm, 1).to_string();
        let size = access(&acc);
        let p = size.and_then(|size| r.find_path(r.root(&rec), 0, int0(&parts[2]), size, "scalar"));
        if let Some(p) = p {
            let mut txt = join(&base, &p.0);
            if r.ptr32_target(p.1).is_some() {
                txt += ".h";
            }
            if let Some(txt) = r.access_text(txt, &acc, p.1, &out[e + 1..], &out[..ms_]) {
                out = format!("{}{}{}", &out[..ms_], txt, &out[e + 1..]);
                changed = true;
            }
        }
    }
    (out, changed)
}

fn func_span(src: &str, name: &str) -> (usize, usize) {
    let fre = re(&format!(r"(?m)^(?![ \t])[^\n;{{}}]*\b{}\s*\([^;{{]*\)\s*\{{", escape(name)));
    let Some(m) = rsearch(&fre, src) else {
        eprintln!("function not found: {name}");
        std::process::exit(1);
    };
    let m = m.get(0).unwrap();
    let b = src.as_bytes();
    let mut depth = 0;
    let mut j = m.end() - 1;
    while j < b.len() {
        if b[j] == b'{' {
            depth += 1;
        } else if b[j] == b'}' {
            depth -= 1;
            if depth == 0 {
                return (m.start(), j + 1);
            }
        }
        j += 1;
    }
    eprintln!("unbalanced: {name}");
    std::process::exit(1);
}

pub fn run(_root: &Path, mut args: Vec<String>) -> i32 {
    static PDECL: Re = Re::new(|| {
        re(r"(?m)(?:^\s*|[,(;{]\s*)(?:const\s+)?(?:struct\s+)?([A-Za-z_]\w*)\s*\*\s*([A-Za-z_]\w*)\s*[,;=)]")
    });
    static LEFT: Re = Re::new(|| re(r"\+ 0x[0-9A-Fa-f]+|M2C_FIELD|\[0x[0-9A-Fa-f]+\]"));
    let mut renames: Vec<(String, String)> = vec![];
    while let Some(i) = args.iter().position(|a| a == "--rename") {
        let (a, b) = args[i + 1].split_once('=').expect("A=B");
        renames.push((a.to_string(), b.to_string()));
        args.drain(i..i + 2);
    }
    if args.len() < 3 {
        eprintln!("usage: hwvx-decomp offsets-to-fields LAYOUTS FILE FUNC [VAR=Type ...] [--rename A=B ...]");
        return 2;
    }
    let (layouts, path, fname) = (args[0].clone(), args[1].clone(), args[2].clone());
    // dict(): insertion order, a repeated key keeps its first position and last value
    let mut varmap: Vec<(String, String)> = vec![];
    for a in &args[3..] {
        let (v, t) = a.split_once('=').expect("VAR=Type");
        if let Some(e) = varmap.iter_mut().find(|e| e.0 == v) {
            e.1 = t.to_string();
        } else {
            varmap.push((v.to_string(), t.to_string()));
        }
    }
    let r = parse_layouts(&layouts);
    let mut src = crate::util::read(&path);
    let (a, b) = func_span(&src, &fname);
    let mut body = src[a..b].to_string();
    let mut vars: HashMap<String, String> = HashMap::new();
    for (v, t) in &varmap {
        let Some(k) = r.rec_key(t) else {
            eprintln!("unknown type {t}");
            std::process::exit(1);
        };
        vars.insert(v.clone(), k);
        let vre = re(&format!(r"\b(?:u8|s8|void|char)\s*\*\s*{}\b", escape(v)));
        body = crate::util::sub(&vre, &body, |_| format!("{t} *{v}"));
    }
    for (o, n) in &renames {
        body = body.replace(o.as_str(), n);
    }
    for m in finditer(&PDECL, &body) {
        if let Some(k) = r.rec_key(g(&m, 1)) {
            vars.entry(g(&m, 2).to_string()).or_insert(k);
        }
    }
    let typer = Typer { r: &r, vars, byte_vars: varmap.iter().map(|v| v.0.clone()).collect() };
    let mut rounds = 0;
    let mut changed = true;
    while changed && rounds < 60 {
        (body, changed) = rewrite(body, &typer);
        rounds += 1;
    }
    let body = tidy(body);
    src = format!("{}{}{}", &src[..a], body, &src[b..]);
    crate::util::write(&path, &src);
    let left: Vec<&str> = body.split('\n').filter(|l| LEFT.is_match(l).unwrap()).collect();
    println!("{fname}: {rounds} rounds, {} lines with offsets left", left.len());
    for l in left.iter().take(60) {
        let t: String = l.trim().chars().take(160).collect();
        println!("    {t}");
    }
    0
}
