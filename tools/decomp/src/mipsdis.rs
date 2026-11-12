//! hwvx-decomp mipsdis ELF FUNC [FUNC ...] | ELF --find SUBSTR | ELF --addr HEX [COUNT]
//!
//! A small MIPS (R5900 subset) disassembler for reading the PS2 prototype's
//! unstripped executable (orig/ps2-beta/SLUS_001.00, its .symtab names 2,081
//! functions), with no toolchain: macOS's objdump has no MIPS target. Reads
//! only; output is for notes (docs-pending/prototype-worlds.md), not matching.
//!
//! Calls and data addresses are named from the symbol table; lui/addiu and
//! lui/lw pairs are folded into the address they build. Unknown encodings show
//! as .word.

use std::collections::{BTreeMap, HashMap};
use std::io::Write;
use std::path::Path;

const R: [&str; 32] = [
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "s0", "s1",
    "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
];

fn u32le(d: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(d[o..o + 4].try_into().unwrap())
}
fn u16le(d: &[u8], o: usize) -> u16 {
    u16::from_le_bytes(d[o..o + 2].try_into().unwrap())
}

struct Elf {
    d: Vec<u8>,
    segs: Vec<(u32, usize, u32)>,
    /// address -> first name (Python dict.setdefault)
    syms: BTreeMap<u32, String>,
    /// functions in first-seen order, value from the last definition (Python dict)
    funcs: Vec<(String, (u32, u32))>,
    func_ix: HashMap<String, usize>,
}

fn load(path: &str) -> Elf {
    let d = std::fs::read(path).unwrap_or_else(|e| panic!("{path}: {e}"));
    let shoff = u32le(&d, 0x20) as usize;
    let (shentsize, shnum) = (u16le(&d, 0x2E) as usize, u16le(&d, 0x30) as usize);
    let secs: Vec<[u32; 10]> = (0..shnum)
        .map(|i| {
            let mut s = [0u32; 10];
            for (k, v) in s.iter_mut().enumerate() {
                *v = u32le(&d, shoff + i * shentsize + k * 4);
            }
            s
        })
        .collect();
    let phoff = u32le(&d, 0x1C) as usize;
    let (phentsize, phnum) = (u16le(&d, 0x2A) as usize, u16le(&d, 0x2C) as usize);
    let mut segs = vec![];
    for i in 0..phnum {
        let b = phoff + i * phentsize;
        if u32le(&d, b) == 1 {
            segs.push((u32le(&d, b + 8), u32le(&d, b + 4) as usize, u32le(&d, b + 16)));
        }
    }
    let mut syms = BTreeMap::new();
    let mut funcs: Vec<(String, (u32, u32))> = vec![];
    let mut func_ix: HashMap<String, usize> = HashMap::new();
    for s in &secs {
        if s[1] != 2 {
            continue;
        }
        let strs = secs[s[6] as usize];
        for k in 0..(s[5] / 16) as usize {
            let o = s[4] as usize + k * 16;
            let (nm, val, size) = (u32le(&d, o) as usize, u32le(&d, o + 4), u32le(&d, o + 8));
            let info = d[o + 12];
            let st = strs[4] as usize + nm;
            let e = st + d[st..].iter().position(|&b| b == 0).expect("unterminated name");
            let name: String = d[st..e].iter().map(|&b| b as char).collect(); // latin1
            if !name.is_empty() && val != 0 && !name.starts_with('.') {
                syms.entry(val).or_insert_with(|| name.clone());
                if info & 0xF == 2 {
                    match func_ix.get(&name) {
                        Some(&i) => funcs[i].1 = (val, size),
                        None => {
                            func_ix.insert(name.clone(), funcs.len());
                            funcs.push((name, (val, size)));
                        }
                    }
                }
            }
        }
    }
    Elf { d, segs, syms, funcs, func_ix }
}

/// Python str.isidentifier() for the symbol names (latin1).
fn is_identifier(s: &str) -> bool {
    let mut c = s.chars();
    match c.next() {
        Some(f) if f == '_' || f.is_alphabetic() => c.all(|x| x == '_' || x.is_alphanumeric()),
        _ => false,
    }
}

/// Python repr() of a printable-ASCII string.
fn py_repr(s: &str) -> String {
    let q = if s.contains('\'') && !s.contains('"') { '"' } else { '\'' };
    let mut o = String::new();
    o.push(q);
    for c in s.chars() {
        if c == '\\' || c == q {
            o.push('\\');
        }
        o.push(c);
    }
    o.push(q);
    o
}

impl Elf {
    fn cstr(&self, a: u32) -> Option<String> {
        for &(v, off, fs) in &self.segs {
            if v <= a && (a as u64) < v as u64 + fs as u64 {
                let o = off + (a - v) as usize;
                let lim = (o + 128).min(self.d.len());
                let e = self.d.get(o..lim).and_then(|w| w.iter().position(|&b| b == 0)).map(|p| o + p)?;
                let sym_ok = self.syms.get(&a).map(|n| !is_identifier(n)).unwrap_or(true);
                if e > o + 1 && self.d[o..e].iter().all(|&c| (32..127).contains(&c)) && sym_ok {
                    return Some(String::from_utf8(self.d[o..e].to_vec()).unwrap());
                }
                return None;
            }
        }
        None
    }

    fn word(&self, va: u32) -> Option<u32> {
        for &(v, off, fs) in &self.segs {
            if v <= va && (va as u64) < v as u64 + fs as u64 {
                return Some(u32le(&self.d, off + (va - v) as usize));
            }
        }
        None
    }

    fn name(&self, a: u32) -> String {
        if let Some(st) = self.cstr(a) {
            return py_repr(&st);
        }
        if let Some(n) = self.syms.get(&a) {
            return n.clone();
        }
        if let Some((&k, n)) = self.syms.range(..=a).next_back() {
            if a - k < 0x4000 {
                return format!("{}+0x{:x}", n, a - k);
            }
        }
        format!("0x{a:x}")
    }

    fn dis(&self, w: u32, pc: u32, hi: &mut BTreeMap<u32, u32>) -> String {
        let (op, rs, rt, rd, sa, fn_) = (w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63);
        let imm = w & 0xFFFF;
        let simm: i64 = if imm & 0x8000 != 0 { imm as i64 - 0x10000 } else { imm as i64 };
        let r = |i: u32| R[i as usize];
        let tgt = |s: i64| (pc as i64 + 4 + s * 4) as u64;
        if w == 0 {
            return "nop".into();
        }
        if op == 0 {
            let rt3 = match fn_ {
                0 => Some("sll"),
                2 => Some("srl"),
                3 => Some("sra"),
                0x38 => Some("dsll"),
                0x3A => Some("dsrl"),
                0x3B => Some("dsra"),
                0x3C => Some("dsll32"),
                0x3E => Some("dsrl32"),
                0x3F => Some("dsra32"),
                _ => None,
            };
            if let Some(n) = rt3 {
                return format!("{} {}, {}, {}", n, r(rd), r(rt), sa);
            }
            if fn_ == 8 {
                return format!("jr {}", r(rs));
            }
            if fn_ == 9 {
                return format!("jalr {}", r(rs));
            }
            if fn_ == 0x10 || fn_ == 0x12 {
                return format!("{} {}", if fn_ == 0x10 { "mfhi" } else { "mflo" }, r(rd));
            }
            let three = match fn_ {
                4 => Some("sllv"),
                6 => Some("srlv"),
                7 => Some("srav"),
                0x0A => Some("movz"),
                0x0B => Some("movn"),
                0x20 => Some("add"),
                0x21 => Some("addu"),
                0x22 => Some("sub"),
                0x23 => Some("subu"),
                0x24 => Some("and"),
                0x25 => Some("or"),
                0x26 => Some("xor"),
                0x27 => Some("nor"),
                0x2A => Some("slt"),
                0x2B => Some("sltu"),
                0x2D => Some("daddu"),
                0x2F => Some("dsubu"),
                0x14 => Some("dsllv"),
                0x16 => Some("dsrlv"),
                _ => None,
            };
            if let Some(n) = three {
                if (fn_ == 0x25 || fn_ == 0x2D) && rt == 0 {
                    return format!("move {}, {}", r(rd), r(rs));
                }
                return format!("{} {}, {}, {}", n, r(rd), r(rs), r(rt));
            }
            let two = match fn_ {
                0x18 => Some("mult"),
                0x19 => Some("multu"),
                0x1A => Some("div"),
                0x1B => Some("divu"),
                _ => None,
            };
            if let Some(n) = two {
                let extra = if rd != 0 { format!(", {}", r(rd)) } else { String::new() };
                return format!("{} {}, {}{}", n, r(rs), r(rt), extra);
            }
            if fn_ == 0xF {
                return "sync".into();
            }
            return format!(".word 0x{w:08x}");
        }
        if op == 1 {
            let n = match rt {
                0 => Some("bltz"),
                1 => Some("bgez"),
                2 => Some("bltzl"),
                3 => Some("bgezl"),
                0x10 => Some("bltzal"),
                0x11 => Some("bgezal"),
                _ => None,
            };
            if let Some(n) = n {
                return format!("{} {}, 0x{:x}", n, r(rs), tgt(simm));
            }
            return format!(".word 0x{w:08x}");
        }
        if op == 2 || op == 3 {
            let t = (pc.wrapping_add(4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2);
            return format!("{} {}", if op == 2 { "j" } else { "jal" }, self.name(t));
        }
        let br = match op {
            4 => Some("beq"),
            5 => Some("bne"),
            0x14 => Some("beql"),
            0x15 => Some("bnel"),
            _ => None,
        };
        if let Some(n) = br {
            let t = tgt(simm);
            if op == 4 && rs == 0 && rt == 0 {
                return format!("b 0x{t:x}");
            }
            return format!("{} {}, {}, 0x{:x}", n, r(rs), r(rt), t);
        }
        if matches!(op, 6 | 7 | 0x16 | 0x17) {
            let n = match op {
                6 => "blez",
                7 => "bgtz",
                0x16 => "blezl",
                _ => "bgtzl",
            };
            return format!("{} {}, 0x{:x}", n, r(rs), tgt(simm));
        }
        if op == 0xF {
            hi.insert(rt, imm << 16);
            return format!("lui {}, 0x{:x}", r(rt), imm);
        }
        let ari = match op {
            8 => Some("addi"),
            9 => Some("addiu"),
            0xA => Some("slti"),
            0xB => Some("sltiu"),
            0xC => Some("andi"),
            0xD => Some("ori"),
            0xE => Some("xori"),
            0x18 => Some("daddi"),
            0x19 => Some("daddiu"),
            _ => None,
        };
        if let Some(n) = ari {
            let mut note = String::new();
            if (op == 9 || op == 0x19) && hi.contains_key(&rs) {
                let a = (hi[&rs] as i64 + simm) as u32;
                note = if a >= 0x100000 { format!("   # {}", self.name(a)) } else { format!("   # 0x{a:x}") };
            }
            if op == 0xD && hi.contains_key(&rs) {
                note = format!("   # 0x{:x}", hi[&rs] | imm);
            }
            if matches!(op, 0xC | 0xD | 0xE) {
                return format!("{} {}, {}, 0x{:x}{}", n, r(rt), r(rs), imm, note);
            }
            return format!("{} {}, {}, {}{}", n, r(rt), r(rs), simm, note);
        }
        let mem = match op {
            0x20 => Some("lb"),
            0x21 => Some("lh"),
            0x23 => Some("lw"),
            0x24 => Some("lbu"),
            0x25 => Some("lhu"),
            0x27 => Some("lwu"),
            0x28 => Some("sb"),
            0x29 => Some("sh"),
            0x2B => Some("sw"),
            0x37 => Some("ld"),
            0x3F => Some("sd"),
            0x1E => Some("lq"),
            0x1F => Some("sq"),
            0x31 => Some("lwc1"),
            0x39 => Some("swc1"),
            0x22 => Some("lwl"),
            0x26 => Some("lwr"),
            0x2A => Some("swl"),
            0x2E => Some("swr"),
            0x1A => Some("ldl"),
            0x1B => Some("ldr"),
            0x2C => Some("sdl"),
            0x2D => Some("sdr"),
            0x2F => Some("cache"),
            0x33 => Some("pref"),
            _ => None,
        };
        if let Some(n) = mem {
            let dst = if op == 0x31 || op == 0x39 { format!("$f{rt}") } else { r(rt).to_string() };
            let mut note = String::new();
            if hi.contains_key(&rs) {
                note = format!("   # {}", self.name((hi[&rs] as i64 + simm) as u32));
            }
            return format!("{} {}, {}({}){}", n, dst, simm, r(rs), note);
        }
        if op == 0x11 {
            let fmt = rs;
            match fmt {
                0 => return format!("mfc1 {}, $f{}", r(rt), rd),
                4 => return format!("mtc1 {}, $f{}", r(rt), rd),
                2 => return format!("cfc1 {}, ${}", r(rt), rd),
                6 => return format!("ctc1 {}, ${}", r(rt), rd),
                8 => return format!("{} 0x{:x}", ["bc1f", "bc1t", "bc1fl", "bc1tl"][(rt & 3) as usize], tgt(simm)),
                _ => {}
            }
            let (fs, fd, ft) = (rd, sa, rt);
            if fmt == 0x10 {
                let n = match fn_ {
                    0 => Some("add.s"),
                    1 => Some("sub.s"),
                    2 => Some("mul.s"),
                    3 => Some("div.s"),
                    4 => Some("sqrt.s"),
                    5 => Some("abs.s"),
                    6 => Some("mov.s"),
                    7 => Some("neg.s"),
                    0x18 => Some("adda.s"),
                    0x1C => Some("madd.s"),
                    0x1D => Some("msub.s"),
                    0x24 => Some("cvt.w.s"),
                    0x28 => Some("max.s"),
                    0x29 => Some("min.s"),
                    0x30 => Some("c.f.s"),
                    0x32 => Some("c.eq.s"),
                    0x34 => Some("c.lt.s"),
                    0x36 => Some("c.le.s"),
                    0x16 => Some("rsqrt.s"),
                    0x1A => Some("mula.s"),
                    _ => None,
                };
                if let Some(n) = n {
                    return format!("{n} $f{fd}, $f{fs}, $f{ft}");
                }
            }
            if fmt == 0x14 && fn_ == 0x20 {
                return format!("cvt.s.w $f{fd}, $f{fs}");
            }
            return format!(".word 0x{w:08x}  # cop1");
        }
        if op == 0x10 {
            return format!("{} {}, ${}   # cop0", if rs == 0 { "mfc0" } else { "mtc0" }, r(rt), rd);
        }
        if op == 0x1C {
            return format!(".word 0x{w:08x}  # mmi");
        }
        if op == 0x12 {
            return format!(".word 0x{w:08x}  # cop2 (vu0)");
        }
        format!(".word 0x{w:08x}")
    }

    fn disfunc(&self, out: &mut impl Write, start: u32, size: u64) {
        let mut hi: BTreeMap<u32, u32> = BTreeMap::new();
        let mut clear = false;
        let mut pc64 = start as u64;
        while pc64 < start as u64 + size {
            let pc = pc64 as u32;
            pc64 += 4;
            let Some(w) = self.word(pc) else { break };
            if let Some(lab) = self.syms.get(&pc) {
                if pc != start {
                    writeln!(out, "{lab}:").unwrap();
                }
            }
            writeln!(out, "  {:08x}  {}", pc, self.dis(w, pc, &mut hi)).unwrap();
            let op = w >> 26;
            if clear {
                hi.retain(|&k, _| (16..24).contains(&k)); // callee-saved survive a call
                clear = false;
            }
            if op == 2 || op == 3 || (op == 0 && matches!(w & 63, 8 | 9)) {
                clear = true;
            } else if op == 0 {
                hi.remove(&((w >> 11) & 31));
            } else if !matches!(op, 0xF | 0x28 | 0x29 | 0x2B | 0x3F | 0x1F | 0x39 | 0x2A | 0x2E | 0x2C | 0x2D) && op >= 8 {
                hi.remove(&((w >> 16) & 31));
            }
        }
    }
}

pub fn run(_root: &Path, args: Vec<String>) -> i32 {
    if args.len() < 2 {
        eprintln!("usage: hwvx-decomp mipsdis ELF FUNC [FUNC ...] | ELF --find SUBSTR | ELF --addr HEX [COUNT]");
        return 2;
    }
    let elf = load(&args[0]);
    let stdout = std::io::stdout();
    let mut out = std::io::BufWriter::new(stdout.lock());
    if args[1] == "--find" {
        let mut v: Vec<&(String, (u32, u32))> = elf.funcs.iter().collect();
        v.sort_by_key(|x| x.1); // stable, like Python's sorted
        let q = args[2].to_lowercase();
        for (n, (a, s)) in v {
            if n.to_lowercase().contains(&q) {
                writeln!(out, "{a:08x} {s:5x} {n}").unwrap();
            }
        }
        return 0;
    }
    if args[1] == "--addr" {
        let a = u32::from_str_radix(args[2].trim_start_matches("0x").trim_start_matches("0X"), 16).expect("HEX");
        let n: u64 = args.get(3).map(|s| s.parse().expect("COUNT")).unwrap_or(64);
        elf.disfunc(&mut out, a, 4 * n);
        return 0;
    }
    for f in &args[1..] {
        let Some(&i) = elf.func_ix.get(f) else {
            out.flush().unwrap();
            eprintln!("KeyError: '{f}'");
            return 1;
        };
        let (a, s) = elf.funcs[i].1;
        writeln!(out, "{f}:  (0x{a:x}, {s} bytes)").unwrap();
        elf.disfunc(&mut out, a, s as u64);
    }
    0
}
