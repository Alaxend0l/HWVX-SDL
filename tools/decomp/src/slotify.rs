//! hwvx-decomp slotify FILES...
//!
//! Rewrite raw pointer-slot accesses in game code to SLOT_LOAD / SLOT_STORE.
//!
//! The decomp reads pointers out of structs by byte offset:
//!
//!     M2C_FIELD(e, T **, off)      *(T **)(addr)      ((T **)base)[i]
//!
//! On 64-bit hosts those slots still hold 4 bytes (a handle, see src/port/port.h),
//! so reads become SLOT_LOAD(T *, addr) and simple assignments become
//! SLOT_STORE(addr, value). Anything else (address-of, ++, +=, T ***) is left
//! unchanged and listed, for the compiler and a human to resolve.
//!
//!   hwvx-decomp slotify src/game/*.c

use crate::util::{at, count_nl, finditer, g, is_match, re, read, rmatch, sub, write, Re};
use std::collections::BTreeMap;
use std::path::Path;
use std::sync::LazyLock;

const TYPE: &str = r"(?:const\s+)?(?:struct\s+|union\s+|unsigned\s+)?\w+";
static M2C: Re = LazyLock::new(|| re(r"M2C_FIELD\s*\("));
static DEREF_CAST: Re = LazyLock::new(|| re(&format!(r"\*\s*\(\s*({TYPE})\s*\*\s*\*\s*\)\s*")));
static PTRARITH: Re = LazyLock::new(|| re(&format!(r"\*\s*\(\s*\(\s*({TYPE})\s*\*\s*\*\s*\)\s*")));
static INDEX_CAST: Re = LazyLock::new(|| re(&format!(r"\(\s*\(\s*({TYPE})\s*\*\s*\*\s*\)\s*")));
static PP_TYPE: Re = LazyLock::new(|| re(&format!(r"^(?:{TYPE}\s*\*\s*\*)\z")));
static PP_TAIL: Re = LazyLock::new(|| re(r"\s*\*\s*\*(?=\n?\z)"));
static IDENT: Re = LazyLock::new(|| re(r"^[A-Za-z_]\w*"));
static MEMBER: Re = LazyLock::new(|| re(r"^(?:->|\.)[A-Za-z_]\w*"));
static ASSIGN: Re = LazyLock::new(|| re(r"^\s*=(?!=)"));
static BAD_OP: Re = LazyLock::new(|| re(r"^\s*(\+\+|--|[-+*/|&^]=|<<=|>>=)"));
static PLUS: Re = LazyLock::new(|| re(r"^\s*\+\s*"));
static FN_DEREF: Re = LazyLock::new(|| re(&format!(r"\*\(\s*({TYPE}[\s\*]*)\(\s*\*\s*\*\s*\)\s*\(")));

/// s[i] == '(' -> index of the matching ')'.
fn match_paren(s: &str, i: usize) -> usize {
    let b = s.as_bytes();
    let mut depth = 0i64;
    let mut j = i;
    while j < b.len() {
        let c = b[j];
        if c == b'(' || c == b'[' {
            depth += 1;
        } else if c == b')' || c == b']' {
            depth -= 1;
            if depth == 0 {
                return j;
            }
        } else if c == b'"' || c == b'\'' {
            let q = c;
            j += 1;
            while b[j] != q {
                j += if b[j] == b'\\' { 2 } else { 1 };
            }
        }
        j += 1;
    }
    panic!("ValueError: unbalanced");
}

fn split_args(s: &str) -> Vec<String> {
    let (mut out, mut depth, mut cur) = (vec![], 0i64, String::new());
    for c in s.chars() {
        if c == '(' || c == '[' {
            depth += 1;
        } else if c == ')' || c == ']' {
            depth -= 1;
        }
        if c == ',' && depth == 0 {
            out.push(cur.trim().to_string());
            cur.clear();
        } else {
            cur.push(c);
        }
    }
    out.push(cur.trim().to_string());
    out
}

/// End of a unary-cast operand starting at s[i]: (...) or identifier chain.
fn operand_end(s: &str, i: usize) -> Option<usize> {
    if at(s, i) == b'(' {
        return Some(match_paren(s, i) + 1);
    }
    let m = rmatch(&IDENT, &s[i..])?;
    let mut j = i + m.get(0).unwrap().end();
    // allow member access / indexing chains like a->b.c[2]
    loop {
        if let Some(m2) = rmatch(&MEMBER, &s[j..]) {
            j += m2.get(0).unwrap().end();
            continue;
        }
        if j < s.len() && (at(s, j) == b'[' || at(s, j) == b'(') {
            j = match_paren(s, j) + 1;
            continue;
        }
        return Some(j);
    }
}

/// If s[j:] starts with `= rhs` (not ==), return (rhs_start, rhs_end).
fn assignment_rhs(s: &str, j: usize) -> Option<(usize, usize)> {
    let m = rmatch(&ASSIGN, &s[j..])?;
    let k = j + m.get(0).unwrap().end();
    let b = s.as_bytes();
    let mut depth = 0i64;
    let mut e = k;
    while e < b.len() {
        let c = b[e];
        if b"([{".contains(&c) {
            depth += 1;
        } else if b")]}".contains(&c) {
            if depth == 0 {
                break;
            }
            depth -= 1;
        } else if (c == b';' || c == b',') && depth == 0 {
            break;
        } else if c == b'"' || c == b'\'' {
            let q = c;
            e += 1;
            while b[e] != q {
                e += if b[e] == b'\\' { 2 } else { 1 };
            }
        }
        e += 1;
    }
    Some((k, e))
}

fn preceded_by_addr_or_incdec(s: &str, i: usize) -> bool {
    let b = s.as_bytes();
    let mut k = i as isize - 1;
    while k >= 0 && (b[k as usize] == b' ' || b[k as usize] == b'\t') {
        k -= 1;
    }
    if k >= 0 && b[k as usize] == b'&' && (k == 0 || b[k as usize - 1] != b'&') {
        return true;
    }
    if k < 1 {
        return false; // s[max(0, k-1):k+1] is shorter than 2
    }
    let w = &b[k as usize - 1..k as usize + 1];
    w == b"++" || w == b"--"
}

fn followed_by_bad_op(s: &str, j: usize) -> bool {
    is_match(&BAD_OP, &s[j..])
}

struct Cand {
    start: usize,
    end: usize,
    t: String,
    addr: String,
}

fn rewrite(src: &str, fname: &str, report: &mut BTreeMap<(String, usize), String>) -> String {
    let mut out = src.to_string();
    let mut changed = true;
    // Iterate to a fixed point: inner occurrences get rewritten first because
    // we always rewrite the *last* match in the text, which is innermost-or-later.
    while changed {
        changed = false;
        let mut cands: Vec<Cand> = vec![];
        for m in finditer(&M2C, &out) {
            let m0 = m.get(0).unwrap();
            let open_i = m0.end() - 1;
            let close_i = match_paren(&out, open_i);
            let args = split_args(&out[open_i + 1..close_i]);
            if args.len() == 3 && is_match(&PP_TYPE, &args[1]) {
                let t = sub(&PP_TAIL, &args[1], |_| String::new());
                let addr = format!("(u8 *)({}) + {}", args[0], args[2]);
                cands.push(Cand { start: m0.start(), end: close_i + 1, t, addr });
            }
        }
        for m in finditer(&DEREF_CAST, &out) {
            let m0 = m.get(0).unwrap();
            // #define lines are view macros over host objects (view_externs)
            let ls = out[..m0.start()].rfind('\n').map(|p| p + 1).unwrap_or(0);
            if out[ls..m0.start()].trim_start().starts_with("#define") {
                continue;
            }
            let Some(j) = operand_end(&out, m0.end()) else { continue };
            // `*(T **)x` is always a dereference: a T ** can't be multiplied.
            cands.push(Cand { start: m0.start(), end: j, t: g(&m, 1).into(), addr: out[m0.end()..j].into() });
        }
        for m in finditer(&INDEX_CAST, &out) {
            let m0 = m.get(0).unwrap();
            let Some(base_end) = operand_end(&out, m0.end()) else { continue };
            if base_end >= out.len() || at(&out, base_end) != b')' {
                continue;
            }
            let j = base_end + 1;
            if j >= out.len() || at(&out, j) != b'[' {
                continue;
            }
            let idx_end = match_paren(&out, j);
            let base = &out[m0.end()..base_end];
            let idx = &out[j + 1..idx_end];
            cands.push(Cand {
                start: m0.start(),
                end: idx_end + 1,
                t: g(&m, 1).into(),
                addr: format!("(u32 *)({base}) + ({idx})"),
            });
        }
        for m in finditer(&PTRARITH, &out) {
            let m0 = m.get(0).unwrap();
            // *((T **)base + n)
            let Some(base_end) = operand_end(&out, m0.end()) else { continue };
            let Some(pm) = rmatch(&PLUS, &out[base_end..]) else { continue };
            let pm_end = pm.get(0).unwrap().end();
            // find the ')' closing the paren opened right after the '*'
            let open_i = m0.start() + 1 + out[m0.start() + 1..].find('(').expect("ValueError: substring not found");
            let close = match_paren(&out, open_i);
            let n = &out[base_end + pm_end..close];
            let base = &out[m0.end()..base_end];
            cands.push(Cand {
                start: m0.start(),
                end: close + 1,
                t: g(&m, 1).into(),
                addr: format!("(u32 *)({base}) + ({n})"),
            });
        }
        cands.sort_by_key(|c| std::cmp::Reverse(c.start)); // stable, like Python
        for c in &cands {
            let line = count_nl(&out, 0, c.start) + 1;
            if preceded_by_addr_or_incdec(&out, c.start) || followed_by_bad_op(&out, c.end) {
                report.entry((fname.to_string(), line)).or_insert_with(|| out[c.start..c.end].to_string());
                continue;
            }
            let rhs = assignment_rhs(&out, c.end);
            // `**(T **)a = v` assigns through the loaded pointer, not to the slot.
            let b = out.as_bytes();
            let mut k = c.start as isize - 1;
            while k >= 0 && (b[k as usize] == b' ' || b[k as usize] == b'\t') {
                k -= 1;
            }
            let through = k >= 0 && b[k as usize] == b'*' && (k == 0 || b" \t\n(=!&|,;".contains(&b[k as usize - 1]));
            if let (Some((r0, r1)), false) = (rhs, through) {
                let new = format!("SLOT_STORE({}, {})", c.addr, out[r0..r1].trim());
                out = format!("{}{}{}", &out[..c.start], new, &out[r1..]);
            } else {
                out = format!("{}SLOT_LOAD({} *, {}){}", &out[..c.start], c.t, c.addr, &out[c.end..]);
            }
            changed = true;
            break; // positions shifted; rescan
        }
    }
    out
}

/// `*(R (**)(A))(expr)` reads a function pointer from a 4-byte slot (a
/// vtable entry): -> `((R (*)(A))SLOT_LOAD(void *, (expr)))`.
fn rewrite_fn_slots(src: String) -> String {
    let mut out = src;
    'outer: loop {
        for m in finditer(&FN_DEREF, &out) {
            let m0 = m.get(0).unwrap();
            let args_open = m0.end() - 1;
            let args_close = match_paren(&out, args_open);
            let cast_close = args_close + 1;
            if cast_close >= out.len() || at(&out, cast_close) != b')' {
                continue;
            }
            let mut j = cast_close + 1;
            while j < out.len() && (at(&out, j) == b' ' || at(&out, j) == b'\t') {
                j += 1;
            }
            if j >= out.len() || at(&out, j) != b'(' {
                continue;
            }
            let expr_close = match_paren(&out, j);
            let ret = g(&m, 1).trim().to_string();
            let args = &out[args_open..args_close + 1];
            let new = format!("(({} (*){})SLOT_LOAD(void *, {}))", ret, args, &out[j..expr_close + 1]);
            out = format!("{}{}{}", &out[..m0.start()], new, &out[expr_close + 1..]);
            continue 'outer;
        }
        return out;
    }
}

pub fn run(_root: &Path, args: Vec<String>) -> i32 {
    let mut report = BTreeMap::new();
    for path in &args {
        let src = read(path);
        let fname = path.rsplit('/').next().unwrap();
        let out = rewrite_fn_slots(rewrite(&src, fname, &mut report));
        if out != src {
            write(path, &out);
            eprintln!(
                "{}: {} rewrites",
                path,
                out.matches("SLOT_").count() as i64 - src.matches("SLOT_").count() as i64
            );
        }
    }
    for ((f, line), text) in &report {
        println!("{f}:{line}: manual: {text}");
    }
    0
}
