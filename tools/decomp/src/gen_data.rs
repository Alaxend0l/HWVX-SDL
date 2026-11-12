//! gen-data: C definitions for game globals that only exist in the original
//! binary's data sections (formerly tools/gen_data.py + tools/gen_data.sh).
//!
//! The decomp keeps much of .data/.bss in split assembly rather than C. This reads
//! the objects from HWVX-GC-Decomp/build/HOTWHE/asm/*.s (produced by the decomp's
//! build) and writes src/game/data_gen.c:
//!
//! - zero-initialised objects (.bss/.sbss) become aligned byte arrays twice the
//!   original size (room for host pointers where the original had 4 bytes);
//! - initialised objects become byte arrays in *host* byte order (W16/W32/W64
//!   macros pick the order at compile time, so the file is endian-portable);
//! - every `.4byte symbol+addend` becomes a 4-byte slot filled at startup with
//!   HND(&symbol + addend), so pointer tables (vtables, string tables) hold
//!   32-bit handles like every other pointer slot in the game.
//!
//! Local objects those tables point at (string literals, nested tables) are
//! pulled in automatically.
//!
//!   hwvx-decomp gen-data                     # the port's set (see DEFAULT below):
//!                                            # names from tools/gen_data_names.txt
//!   hwvx-decomp gen-data NAME...             # or: gen-data @names.txt
//!   hwvx-decomp gen-data --raw rawData NAME...  # rawData's "pointers" are image data
//!   hwvx-decomp gen-data --hostptr g_pX NAME... # g_pX is `T *g_pX` in the C code
//!   hwvx-decomp gen-data --bytes g_tbl NAME...  # g_tbl holds u8/u16 fields: keep the
//!                                               # original byte order (as pack data)
//!   hwvx-decomp gen-data --u16 g_tbl NAME...    # g_tbl is a u16[] the asm spells as
//!                                               # .4byte pairs: emit host-order halves
//!
//! The name list (tools/gen_data_names.txt) is the globals the game's C uses but
//! doesn't define; add names when the link fails.

use crate::util::*;
use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::sync::LazyLock;

/// What `gen-data` with no arguments runs (was tools/gen_data.sh):
///   --raw:     objects whose "pointers" are really image data
///   --hostptr: objects the C code declares as host pointers (T *name)
///   --bytes:   byte tables (u8 fields): keep the original byte order
///   --u16:     u16 arrays the asm writes as .4byte pairs
const DEFAULT: &[&str] = &[
    "--raw", "rawData",
    "--hostptr", "g_pPlayerManager", "--hostptr", "g_rndNumber",
    "--hostptr", "pTriggerFuncArray", "--hostptr", "pActionFuncArray",
    "--bytes", "g_keyBinding", "--bytes", "g_priority",
    "--u16", "g_volTab", "--u16", "g_panTab", "--u16", "g_envelope",
    "@tools/gen_data_names.txt",
];

const BSS: &[&str] = &[".bss", ".sbss", ".sbss2"];

struct Obj {
    name: String,
    scope: String,
    section: Option<String>,
    align: i64,
    lines: Vec<String>,
    file: String,
}

/// Python str.splitlines().
fn splitlines(s: &str) -> Vec<&str> {
    let mut out = Vec::new();
    let mut start = 0;
    let mut it = s.char_indices().peekable();
    while let Some((i, c)) = it.next() {
        let brk = matches!(c, '\n' | '\r' | '\x0b' | '\x0c' | '\x1c' | '\x1d' | '\x1e' | '\u{85}' | '\u{2028}' | '\u{2029}');
        if brk {
            out.push(&s[start..i]);
            let mut next = i + c.len_utf8();
            if c == '\r' {
                if let Some(&(j, '\n')) = it.peek() {
                    it.next();
                    next = j + 1;
                }
            }
            start = next;
        }
    }
    if start < s.len() {
        out.push(&s[start..]);
    }
    out
}

/// Python str.strip() (whitespace as str.isspace()).
fn pystrip(s: &str) -> &str {
    s.trim_matches(|c: char| c.is_whitespace() || ('\x1c'..='\x1f').contains(&c))
}

/// Python repr() of a str (the common cases: printable text).
fn pyrepr(s: &str) -> String {
    let q = if s.contains('\'') && !s.contains('"') { '"' } else { '\'' };
    let mut out = String::new();
    out.push(q);
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c == q => {
                out.push('\\');
                out.push(c);
            }
            c if (c as u32) < 0x20 || c as u32 == 0x7f => out.push_str(&format!("\\x{:02x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push(q);
    out
}

/// Python int(s, 0).
fn pyint0(s: &str) -> i128 {
    let t = pystrip(s);
    let (neg, body) = match t.as_bytes().first() {
        Some(b'-') => (true, &t[1..]),
        Some(b'+') => (false, &t[1..]),
        _ => (false, t),
    };
    let lower = body.to_ascii_lowercase();
    let (radix, digits) = if lower.starts_with("0x") {
        (16, &body[2..])
    } else if lower.starts_with("0o") {
        (8, &body[2..])
    } else if lower.starts_with("0b") {
        (2, &body[2..])
    } else {
        (10, body)
    };
    let bad = || -> ! { panic!("invalid literal for int() with base 0: {}", pyrepr(s)) };
    // underscores: single, between digits (after a prefix one may lead)
    let mut clean = String::new();
    let mut prev_us = radix == 10; // a leading '_' is only allowed after a prefix
    for (k, c) in digits.chars().enumerate() {
        if c == '_' {
            if prev_us || k + 1 == digits.chars().count() {
                bad();
            }
            prev_us = true;
            continue;
        }
        prev_us = false;
        clean.push(c);
    }
    if clean.is_empty() {
        bad();
    }
    if radix == 10 && clean.len() > 1 && clean.starts_with('0') && clean.chars().any(|c| c != '0') {
        bad();
    }
    let v = i128::from_str_radix(&clean, radix).unwrap_or_else(|_| bad());
    if neg { -v } else { v }
}

/// Python float(s).
fn pyfloat(s: &str) -> f64 {
    let t = pystrip(s);
    let c = t.replace('_', "");
    c.parse::<f64>().unwrap_or_else(|_| panic!("could not convert string to float: {}", pyrepr(s)))
}

struct Tables {
    addresses: HashMap<String, i64>,
    functions: HashSet<String>,
}

static SECTION: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^\.section\s+(\S+?),?(\s|$)"));
static BALIGN: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^\.balign\s+(\d+)"));
static OBJ: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^\.obj\s+([^,\s]+),\s*(\w+)"));

fn load_objects(asm: &Path) -> HashMap<String, Obj> {
    let mut objs = HashMap::new();
    for f in glob_ext(asm, "s") {
        let fname = f.file_name().unwrap().to_string_lossy().into_owned();
        let mut section: Option<String> = None;
        let mut align = 4i64;
        let mut cur: Option<Obj> = None;
        let text = read_lossy(&f);
        for raw in splitlines(&text) {
            let line = pystrip(raw);
            if let Some(m) = rmatch(&SECTION, line) {
                section = Some(g(&m, 1).trim_end_matches(',').to_string());
                continue;
            }
            if matches!(line, ".data" | ".text" | ".rodata" | ".bss") {
                section = Some(line.to_string());
                continue;
            }
            if let Some(m) = rmatch(&BALIGN, line) {
                if cur.is_none() {
                    align = g(&m, 1).parse::<i64>().unwrap();
                    continue;
                }
            }
            if let Some(m) = rmatch(&OBJ, line) {
                cur = Some(Obj {
                    name: g(&m, 1).to_string(),
                    scope: g(&m, 2).to_string(),
                    section: section.clone(),
                    align,
                    lines: Vec::new(),
                    file: fname.clone(),
                });
                continue;
            }
            if line.starts_with(".endobj") {
                if let Some(c) = cur.take() {
                    let key = if c.scope == "local" { format!("{}:{}", fname, c.name) } else { c.name.clone() };
                    objs.insert(key, c);
                }
                align = 4;
                continue;
            }
            if let Some(c) = cur.as_mut() {
                if line.starts_with('.') {
                    c.lines.push(line.to_string());
                }
            }
        }
    }
    objs
}

fn load_tables(symbols: &Path) -> Tables {
    static ADDR: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^(\S+) = \.\w+:0x(\w+);"));
    static FUNC: LazyLock<fancy_regex::Regex> =
        LazyLock::new(|| re(r"^(\S+) = \.\w+:0x\w+; // type:function"));
    let text = read(symbols);
    let mut addresses = HashMap::new();
    let mut functions = HashSet::new();
    for line in splitlines(&text) {
        if let Some(m) = rmatch(&ADDR, line) {
            let v = i64::from_str_radix(g(&m, 2), 16)
                .unwrap_or_else(|_| panic!("invalid literal for int() with base 16: {}", pyrepr(g(&m, 2))));
            addresses.insert(g(&m, 1).to_string(), v);
        }
        if let Some(m) = rmatch(&FUNC, line) {
            functions.insert(g(&m, 1).to_string());
        }
    }
    Tables { addresses, functions }
}

fn address_of(t: &Tables, sym: &str) -> i64 {
    static ADDR: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"_([0-9A-F]{8})(?=\n?\z)"));
    if let Some(&v) = t.addresses.get(sym) {
        return v;
    }
    // asm built before a rename: Name_ADDR
    if let Some(m) = rsearch(&ADDR, sym) {
        return i64::from_str_radix(g(&m, 1), 16).unwrap();
    }
    panic!("KeyError: {}", pyrepr(sym));
}

/// Make a C identifier out of an assembler symbol.
fn cname(name: &str) -> String {
    static ID: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^[A-Za-z_]\w*\z"));
    static BAD: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"[^A-Za-z0-9_]"));
    if is_match(&ID, name) {
        name.to_string()
    } else {
        sub(&BAD, name.trim_start_matches('.'), |_| "_".to_string())
    }
}

/// dtk emits C-style escapes inside "..."
fn parse_string(arg: &str) -> Vec<u8> {
    let body = pystrip(arg);
    assert!(body.starts_with('"') && body.ends_with('"'), "{}", arg);
    let mut out = Vec::new();
    // body[1:-1] (as chars)
    let s: Vec<char> = {
        let v: Vec<char> = body.chars().collect();
        if v.len() >= 2 { v[1..v.len() - 1].to_vec() } else { Vec::new() }
    };
    let oct = |c: char| ('0'..='7').contains(&c);
    let mut i = 0;
    while i < s.len() {
        let c = s[i];
        if c == '\\' {
            let n = s[i + 1];
            if oct(n) {
                let mut j = i + 1;
                while j < s.len() && j < i + 4 && oct(s[j]) {
                    j += 1;
                }
                let t: String = s[i + 1..j].iter().collect();
                out.push((i64::from_str_radix(&t, 8).unwrap() & 0xFF) as u8);
                i = j;
                continue;
            }
            if n == 'x' {
                let mut j = i + 2;
                while j < s.len() && s[j].is_ascii_hexdigit() {
                    j += 1;
                }
                let t: String = s[i + 2..j].iter().collect();
                let v = i128::from_str_radix(&t, 16)
                    .unwrap_or_else(|_| panic!("invalid literal for int() with base 16: {}", pyrepr(&t)));
                out.push((v & 0xFF) as u8);
                i = j;
                continue;
            }
            let v = match n {
                'n' => 10,
                't' => 9,
                'r' => 13,
                '0' => 0,
                '\\' => 92,
                '"' => 34,
                '\'' => 39,
                'a' => 7,
                'b' => 8,
                'f' => 12,
                'v' => 11,
                _ => panic!("KeyError: {}", pyrepr(&n.to_string())),
            };
            out.push(v);
            i += 2;
            continue;
        }
        let cp = c as u32;
        if cp > 0xFF {
            panic!("'latin-1' codec can't encode character {:?}", c);
        }
        out.push(cp as u8);
        i += 1;
    }
    out
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord)]
enum Kind {
    Obj,
    Sym,
}

/// (offset, kind, target, addend)
type Fix = (i64, Kind, String, i128);

struct Emitter<'a> {
    objs: &'a HashMap<String, Obj>,
    tables: &'a Tables,
    done: Vec<String>,
    done_map: HashMap<String, (String, Vec<Fix>)>,
    queue: Vec<String>,
    exported: HashSet<String>,
    raw: HashSet<String>,
    hostptr: HashSet<String>,
    bytes: HashSet<String>,
    u16: HashSet<String>,
    late: Vec<(String, String, String, Vec<Fix>)>,
}

impl<'a> Emitter<'a> {
    fn resolve(&self, name: &str, file: &str) -> Option<String> {
        let k = format!("{file}:{name}");
        if self.objs.contains_key(&k) {
            return Some(k);
        }
        if self.objs.contains_key(name) {
            return Some(name.to_string());
        }
        None
    }

    fn want(&mut self, key: &str) {
        if !self.done_map.contains_key(key) && !self.queue.iter().any(|q| q == key) {
            self.queue.push(key.to_string());
        }
    }

    fn c_ident(&self, key: &str) -> String {
        let obj = &self.objs[key];
        if obj.scope == "local" && !self.exported.contains(key) {
            format!("s_{}_{}", cname(&obj.file.replace(".s", "")), cname(&obj.name))
        } else {
            cname(&obj.name)
        }
    }

    fn mark_done(&mut self, key: &str, ident: String, resolved: Vec<Fix>) {
        if !self.done_map.contains_key(key) {
            self.done.push(key.to_string());
        }
        self.done_map.insert(key.to_string(), (ident, resolved));
    }

    fn emit(&mut self, key: &str) -> Result<String, String> {
        static INT: LazyLock<fancy_regex::Regex> = LazyLock::new(|| re(r"^-?(0x[0-9A-Fa-f]+|\d+)\z"));
        static SYM: LazyLock<fancy_regex::Regex> =
            LazyLock::new(|| re(r"^([^+\-]+)([+\-]0x[0-9A-Fa-f]+|[+\-]\d+)?\z"));
        let objs = self.objs;
        let obj = &objs[key];
        let ident = self.c_ident(key);
        let mut items: Vec<(&str, u64)> = Vec::new(); // ("W8".."W64", value) or ("ZERO", n)
        let mut fixups: Vec<(i64, String, i128)> = Vec::new();
        let mut size: i64 = 0;
        let name = obj.name.as_str();
        for line in &obj.lines {
            let (d, arg) = match line.find(' ') {
                Some(p) => (&line[..p], &line[p + 1..]),
                None => (line.as_str(), ""),
            };
            let mut arg = pystrip(arg);
            if matches!(d, ".4byte" | ".2byte" | ".byte" | ".float" | ".double") {
                // e.g. `.4byte 0x7FFFFFFF # NaN`
                arg = pystrip(arg.split('#').next().unwrap());
            }
            match d {
                ".4byte" => {
                    for a in arg.split(',').map(pystrip) {
                        if is_match(&INT, a) {
                            let v = (pyint0(a) & 0xFFFF_FFFF) as u64;
                            if self.bytes.contains(name) {
                                for sh in [24, 16, 8, 0] {
                                    items.push(("W8", (v >> sh) & 0xFF));
                                }
                            } else if self.u16.contains(name) {
                                items.push(("W16", v >> 16));
                                items.push(("W16", v & 0xFFFF));
                            } else {
                                items.push(("W32", v));
                            }
                        } else {
                            let m = rmatch(&SYM, a)
                                .unwrap_or_else(|| panic!("AttributeError: no match for {}", pyrepr(a)));
                            let sym = g(&m, 1).to_string();
                            let add = if m.get(2).is_some() { pyint0(g(&m, 2)) } else { 0 };
                            if self.raw.contains(name) {
                                // not a pointer: data that happens to look like an address
                                let v = (address_of(self.tables, &sym) as i128 + add) & 0xFFFF_FFFF;
                                items.push(("W32", v as u64));
                            } else {
                                fixups.push((size, sym, add));
                                items.push(("W32", 0));
                            }
                        }
                        size += 4;
                    }
                }
                ".2byte" => {
                    for a in arg.split(',') {
                        let v = (pyint0(pystrip(a)) & 0xFFFF) as u64;
                        if self.bytes.contains(name) {
                            items.push(("W8", v >> 8));
                            items.push(("W8", v & 0xFF));
                        } else {
                            items.push(("W16", v));
                        }
                        size += 2;
                    }
                }
                ".byte" => {
                    for a in arg.split(',') {
                        items.push(("W8", (pyint0(pystrip(a)) & 0xFF) as u64));
                        size += 1;
                    }
                }
                ".float" => {
                    for a in arg.split(',') {
                        items.push(("W32", (pyfloat(a) as f32).to_bits() as u64));
                        size += 4;
                    }
                }
                ".double" => {
                    for a in arg.split(',') {
                        items.push(("W64", pyfloat(a).to_bits()));
                        size += 8;
                    }
                }
                ".string" | ".asciz" | ".ascii" => {
                    let mut b = parse_string(arg);
                    if d != ".ascii" {
                        b.push(0);
                    }
                    for &x in &b {
                        items.push(("W8", x as u64));
                    }
                    size += b.len() as i64;
                }
                ".skip" => {
                    let n = pyint0(arg.split(',').next().unwrap()) as i64;
                    items.push(("ZERO", n as u64));
                    size += n;
                }
                ".balign" | ".hidden" | ".sym" | ".rel" => continue,
                _ => {
                    return Err(format!("{}: unhandled directive {} in {}", obj.file, pyrepr(line), obj.name));
                }
            }
        }

        let mut resolved: Vec<Fix> = Vec::new();
        for (off, sym, add) in fixups {
            let mut k = self.resolve(&sym, &obj.file);
            if let Some(kk) = &k {
                if matches!(objs[kk].section.as_deref(), Some(".text") | Some(".init")) {
                    k = None;
                }
            }
            if let Some(kk) = k {
                self.want(&kk);
                resolved.push((off, Kind::Obj, kk, add));
            } else if sym.starts_with(".L_") || sym.starts_with("jumptable") || sym.starts_with('@') {
                // Code labels (switch jump tables): C never reads these.
                continue;
            } else {
                resolved.push((off, Kind::Sym, sym, add));
            }
        }

        let storage = if obj.scope == "local" && !self.exported.contains(key) { "static " } else { "" };
        if self.hostptr.contains(name) && !resolved.is_empty() && resolved.len() as i64 * 4 == size {
            // The game's C declares this as host pointer(s) (`T *name`), so
            // emit real pointers with constant initialisers; placed after the
            // other objects so the targets are declared (see run()).
            self.late.push((key.to_string(), ident.clone(), storage.to_string(), resolved));
            self.mark_done(key, ident, Vec::new());
            return Ok(String::new());
        }
        let section = obj.section.as_deref().unwrap_or("None");
        let is_bss = obj.section.as_deref().map(|s| BSS.contains(&s)).unwrap_or(false)
            || items.iter().all(|i| i.0 == "ZERO");
        let mut decl = format!(
            "{storage}u8 {ident}[{}] __attribute__((aligned({})))",
            size.max(1),
            obj.align.max(4)
        );
        let mut out = vec![format!("/* {} ({}, {}) */", obj.name, section, obj.file)];
        if is_bss && resolved.is_empty() {
            // Zero-initialised globals are often host pointers (or arrays of
            // them) in the game code, which take 8 bytes where the original
            // had 4; twice the size leaves room for that.
            decl = format!("{storage}u8 {ident}[{}] __attribute__((aligned(16)))", (size * 2).max(8));
            out.push(format!("{decl};  /* original size 0x{size:X} */"));
        } else {
            // Designated indices skip zero runs (.skip) without spelling them
            // out; `[pos] = W32(x)` initialises bytes pos..pos+3.
            let mut text = Vec::new();
            let mut pos: u64 = 0;
            for (kind, v) in &items {
                if *kind == "ZERO" {
                    pos = pos.wrapping_add(*v);
                    continue;
                }
                let n = match *kind {
                    "W8" => 1,
                    "W16" => 2,
                    "W32" => 4,
                    _ => 8,
                };
                text.push(format!("[{pos}] = {kind}(0x{v:X})"));
                pos += n;
            }
            out.push(format!("{decl} = {{"));
            for ch in text.chunks(6) {
                out.push(format!("    {},", ch.join(", ")));
            }
            out.push("};".to_string());
        }
        self.mark_done(key, ident, resolved);
        Ok(out.join("\n"))
    }
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let mut args: Vec<String> = if args.is_empty() {
        DEFAULT.iter().map(|s| s.replace("@tools/", &format!("@{}/tools/", root.display()))).collect()
    } else {
        args
    };
    let asm = root.join("HWVX-GC-Decomp/build/HOTWHE/asm");
    let symbols = root.join("HWVX-GC-Decomp/config/HOTWHE/symbols.txt");
    let out_path = root.join("src/game/data_gen.c");
    let tables = load_tables(&symbols);

    let mut names: Vec<String> = Vec::new();
    let (mut raw, mut hostptr, mut nbytes, mut u16) =
        (HashSet::new(), HashSet::new(), HashSet::new(), HashSet::new());
    while !args.is_empty() && matches!(args[0].as_str(), "--raw" | "--hostptr" | "--bytes" | "--u16") {
        let v = args.get(1).cloned().unwrap_or_else(|| panic!("IndexError: {} needs a name", args[0]));
        match args[0].as_str() {
            "--raw" => raw.insert(v),
            "--hostptr" => hostptr.insert(v),
            "--bytes" => nbytes.insert(v),
            _ => u16.insert(v),
        };
        args.drain(..2);
    }
    for a in &args {
        if let Some(p) = a.strip_prefix('@') {
            names.extend(read(p).split_whitespace().map(String::from));
        } else {
            names.push(a.clone());
        }
    }
    let objs = load_objects(&asm);
    let mut em = Emitter {
        objs: &objs,
        tables: &tables,
        done: Vec::new(),
        done_map: HashMap::new(),
        queue: Vec::new(),
        exported: HashSet::new(),
        raw,
        hostptr,
        bytes: nbytes,
        u16,
        late: Vec::new(),
    };
    let mut not_found = Vec::new();
    for n in &names {
        if objs.contains_key(n) {
            em.want(n);
            continue;
        }
        // file-local in the original, but the port's C names it from elsewhere
        let suffix = format!(":{n}");
        let local: Vec<&String> = objs.keys().filter(|k| k.ends_with(&suffix)).collect();
        if local.len() == 1 {
            let k = local[0].clone();
            em.exported.insert(k.clone());
            em.want(&k);
        } else {
            not_found.push(n.clone());
        }
    }
    let mut chunks = Vec::new();
    while !em.queue.is_empty() {
        let k = em.queue.remove(0);
        match em.emit(&k) {
            Ok(c) => chunks.push(c),
            Err(e) => {
                eprintln!("{e}");
                return 1;
            }
        }
    }

    let target_ident = |em: &Emitter, t: &str| -> String {
        match em.done_map.get(t) {
            Some((id, _)) => id.clone(),
            None => em.c_ident(t),
        }
    };
    let mut externs: Vec<String> = Vec::new();
    let mut fix_lines = Vec::new();
    for key in &em.done {
        let (ident, resolved) = &em.done_map[key];
        for (off, kind, target, add) in resolved {
            let expr = if *kind == Kind::Obj {
                format!("(u8 *){} + {}", target_ident(&em, target), add)
            } else {
                externs.push(target.clone());
                format!("(u8 *)&{} + {}", target, add)
            };
            fix_lines.push(format!("    SLOT_STORE({ident} + 0x{off:X}, {expr});"));
        }
    }

    let mut hdr: Vec<String> = [
        "/* Generated by tools/gen_data.py from the decomp's split data sections.",
        " * Do not edit; regenerate instead. */",
        "#include \"port.h\"",
        "",
        "#if PORT_BIG_ENDIAN",
        "#define W16(v) ((v) >> 8) & 0xFF, (v) & 0xFF",
        "#define W32(v) ((v) >> 24) & 0xFF, ((v) >> 16) & 0xFF, ((v) >> 8) & 0xFF, (v) & 0xFF",
        "#define W64(v) W32((v) >> 32), W32((v) & 0xFFFFFFFFu)",
        "#else",
        "#define W16(v) (v) & 0xFF, ((v) >> 8) & 0xFF",
        "#define W32(v) (v) & 0xFF, ((v) >> 8) & 0xFF, ((v) >> 16) & 0xFF, ((v) >> 24) & 0xFF",
        "#define W64(v) W32((v) & 0xFFFFFFFFu), W32((v) >> 32)",
        "#endif",
        "#define W8(v) (v)",
        "",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    let late = std::mem::take(&mut em.late);
    for (key, ident, storage, mut resolved) in late {
        resolved.sort();
        let mut vals = Vec::new();
        for (_off, kind, target, add) in &resolved {
            if *kind == Kind::Obj {
                vals.push(format!("(u8 *){} + {}", target_ident(&em, target), add));
            } else {
                externs.push(target.clone());
                vals.push(format!("(u8 *)&{} + {}", target, add));
            }
        }
        chunks.push(format!(
            "/* {}: host pointer(s) */\n{storage}void *{ident}[{}] = {{ {} }};",
            objs[&key].name,
            vals.len(),
            vals.join(", ")
        ));
    }
    // after the late (host pointer) objects, which can add externs too
    externs.sort();
    externs.dedup();
    for s in &externs {
        hdr.push(if tables.functions.contains(s) {
            format!("extern void {s}(void);")
        } else {
            format!("extern char {s}[];")
        });
    }
    hdr.push(String::new());
    let chunks: Vec<String> = chunks.into_iter().filter(|c| !c.is_empty()).collect();
    let body = chunks.join("\n\n");
    let mut tail = vec![
        String::new(),
        "/* Fill pointer slots with handles once, before main(). */".to_string(),
        "__attribute__((constructor)) static void DataGen_Fixups(void) {".to_string(),
    ];
    tail.extend(fix_lines.iter().cloned());
    tail.push("}".to_string());
    tail.push(String::new());
    write(&out_path, &format!("{}{}\n{}", hdr.join("\n"), body, tail.join("\n")));
    println!("{} objects, {} pointer fixups -> src/game/data_gen.c", em.done.len(), fix_lines.len());
    if !not_found.is_empty() {
        println!("not in asm: {}", not_found.join(" "));
    }
    0
}
