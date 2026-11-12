//! List constant byte offsets added to typed (non-byte) pointers in the game
//! sources: `GETP(x->field) + 0x24` is a byte offset only while `field` points
//! to u8; once the field is given a struct type it steps whole structs. Run it
//! after retyping pointer fields (clang, needs BUILD/compile_commands.json):
//!
//!   hwvx-decomp check-ptr-offsets [BUILD_DIR]
//!
//! Prints file:line, the pointer type and the constant. Deliberate strides
//! (e.g. `matrices + i * 16` on f32 *) show up too; check each new one.
//!
//!   hwvx-decomp check-ptr-offsets --bytes [BUILD_DIR]
//!
//! lists the constant offsets on byte pointers instead: the struct accesses
//! still done by offset (`(u8 *)x + 0x24`), the progress list of the
//! struct refactor.

use crate::util::{re, Re};
use serde_json::Value;
use std::path::Path;
use std::process::Command;

static BYTE: Re = Re::new(|| {
    re(r"^(const )?(volatile )?(unsigned char|signed char|char|void|u8|s8|vu8)( const)? \*( const)?(?=\n?\z)")
});

struct St {
    f: Option<String>,
    l: Option<i64>,
}

fn upd(st: &mut St, loc: Option<&Value>) {
    let Some(loc) = loc.and_then(|l| l.as_object()) else { return };
    if loc.is_empty() {
        return;
    }
    if let Some(e) = loc.get("expansionLoc") {
        upd(st, Some(e));
        return;
    }
    if let Some(f) = loc.get("file") {
        st.f = f.as_str().map(|s| s.to_string());
    }
    if let Some(l) = loc.get("line") {
        st.l = l.as_i64();
    }
}

fn s<'a>(v: &'a Value, k: &str) -> &'a str {
    v.get(k).and_then(|x| x.as_str()).unwrap_or("")
}

fn inner(v: &Value) -> &[Value] {
    v.get("inner").and_then(|x| x.as_array()).map(|a| a.as_slice()).unwrap_or(&[])
}

/// int(n.get('value', '0'))
fn value(v: &Value) -> i128 {
    match v.get("value") {
        None => 0,
        Some(Value::String(s)) => s.trim().parse().unwrap_or(0),
        Some(Value::Number(n)) => n.as_i64().map(|x| x as i128).unwrap_or(0),
        _ => 0,
    }
}

fn peel(mut c: &Value) -> &Value {
    while matches!(s(c, "kind"), "ImplicitCastExpr" | "ParenExpr") && !inner(c).is_empty() {
        c = &inner(c)[0];
    }
    c
}

fn walk(root: &Value, bytes: bool, st: &mut St, out: &mut Vec<(Option<String>, Option<i64>, String, i128)>) {
    // pre-order, iteratively (the AST is deep)
    let mut stack = vec![root];
    while let Some(n) = stack.pop() {
        if !n.is_object() {
            continue;
        }
        upd(st, n.get("loc"));
        if let Some(r) = n.get("range") {
            if r.as_object().is_some_and(|o| !o.is_empty()) {
                upd(st, r.get("begin"));
            }
        }
        let kids = inner(n);
        if s(n, "kind") == "BinaryOperator" && matches!(s(n, "opcode"), "+" | "-") && kids.len() == 2 {
            let ty = kids[0].get("type");
            let q = ty.map(|t| s(t, "qualType")).unwrap_or("");
            let d = ty.and_then(|t| t.get("desugaredQualType")).and_then(|x| x.as_str()).unwrap_or(q);
            let rr = peel(&kids[1]);
            let byte = BYTE.is_match(d).unwrap_or(false) || BYTE.is_match(q).unwrap_or(false);
            if bytes && d.ends_with('*') && byte {
                if s(rr, "kind") == "IntegerLiteral" && value(rr) >= 4 {
                    out.push((st.f.clone(), st.l, d.to_string(), value(rr)));
                }
            } else if !bytes && d.ends_with('*') && !byte {
                if s(rr, "kind") == "IntegerLiteral" && value(rr) >= 4 {
                    out.push((st.f.clone(), st.l, d.to_string(), value(rr)));
                } else if s(rr, "kind") == "BinaryOperator" && s(rr, "opcode") == "*" {
                    for c in inner(rr) {
                        let c = peel(c);
                        if s(c, "kind") == "IntegerLiteral" && value(c) >= 4 {
                            out.push((st.f.clone(), st.l, d.to_string(), value(c)));
                        }
                    }
                }
            }
        }
        for c in kids.iter().rev() {
            stack.push(c);
        }
    }
}

pub fn run(_root: &Path, args: Vec<String>) -> i32 {
    let bytes = args.iter().any(|a| a == "--bytes");
    let rest: Vec<&String> = args.iter().filter(|a| *a != "--bytes").collect();
    let build = rest.first().map(|s| s.as_str()).unwrap_or("build");
    let db: Value = serde_json::from_str(&crate::util::read(format!("{build}/compile_commands.json"))).expect("compile_commands.json");
    for e in db.as_array().expect("array") {
        let f = s(e, "file");
        if !f.contains("/src/game/") {
            continue;
        }
        let mut a = shlex::split(s(e, "command")).expect("command");
        let i = a.iter().position(|x| x == "-o").expect("-o");
        a.drain(i..i + 2);
        a.retain(|x| x != "-c");
        a.extend(["-fsyntax-only", "-w", "-Xclang", "-ast-dump=json"].map(String::from));
        let ast: Option<Value> = Command::new(&a[0])
            .args(&a[1..])
            .current_dir(s(e, "directory"))
            .stderr(std::process::Stdio::piped())
            .output()
            .ok()
            .and_then(|o| String::from_utf8(o.stdout).ok())
            .and_then(|t| serde_json::from_str(&t).ok());
        let Some(ast) = ast else {
            eprintln!("PARSE FAIL {f}");
            continue;
        };
        let mut st = St { f: None, l: None };
        let mut out = Vec::new();
        walk(&ast, bytes, &mut st, &mut out);
        let text = crate::util::read(f);
        let lines: Vec<&str> = text.split('\n').collect();
        let rel = f.rsplit("/src/game/").next().unwrap();
        for (file, line, t, v) in out {
            if file.as_deref() == Some(f) {
                if let Some(line) = line.filter(|&l| l != 0) {
                    let idx = if line >= 1 { (line - 1) as usize } else { (lines.len() as i64 + line - 1) as usize };
                    let l: String = lines[idx].trim().chars().take(150).collect();
                    println!("{rel}:{line}: [{t}] {v:#x}: {l}");
                }
            }
        }
    }
    0
}
