//! `xdlrs fonts list`: every font page of the releases' archives (plan v0.4.1 part B, stage B1).
//!
//! A font is an interface record (IfaceFont, 0x2C: texture slot 0x00, cell width 0x09, height
//! 0x0A, case mode 0x0B, spacing table 0x20/0x24 of 0xC entries, spacing 0x28) that a text (type
//! 1, 12: sub+4), a box (type 10: sub+0x10) or a briefing character (type 2: dialog entry
//! +0x14 -> char +0x08) points at. Interfaces are found in every pack by Port_IfaceScan.c's
//! header test. Gfx_FontManager_AddFont lays the case mode's letters on a grid of cells
//! (cell w x h, row by row from the page's top-left); the spacing is the advance and the
//! glyph's width; SetCharacterSpacing overrides it per letter.
//!
//! Output: one line per page (release, owner archive:pack:texture, size, format, content hash,
//! the font definitions that draw it, the screens), duplicate groups (same pixels; same glyphs
//! across releases by the mix's GlyphDistance), a class (flat / shaded / colour), and with
//! `--dump DIR` the pages as PNG (never commit those: game data).

use crate::modcheck::hash::h64;
use crate::modcheck::media::{self, Archive, RawPack};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

const DEF_SIZE: u32 = 0x6C;

/// FontManager.c's letter strings (English), by case mode.
pub fn letters_e(case: u8) -> &'static [u8] {
    match case {
        1 => b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?;:+-*/=%\"()&#'",
        2 => b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'",
        3 => b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\";+=()&#",
        _ => b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'",
    }
}

/// The French and German strings (accents), by case mode: (F, G).
pub fn letters_fg(case: u8) -> (&'static [u8], &'static [u8]) {
    match case {
        1 => (
            b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.'!?;:+-*/=%\xC9\xC8,\"()&#'",
            b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.'!?;:+-*/=%\xC4\xD6\xDC\xDF,\"()&#'",
        ),
        2 => (letters_e(2), letters_e(2)),
        3 => (
            b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\"\xC9\xCA\xC7\xC8\xD4\xC2\xC0;+=()&#",
            b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?:-'/%\"\xC4\xD6\xDC\xDF;+=()&#",
        ),
        _ => (
            b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'      \xC9\xE9\xC8\xE8\xCA\xEA\xD4\xF4\xB0",
            b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?abcdefghijklmnopqrstuvwxyz;:+-*/=%\"()&#'      \xC4\xE4\xD6\xF6\xDC\xFC\xDF",
        ),
    }
}

fn latin1(b: &[u8]) -> String {
    b.iter().map(|&c| c as char).collect()
}

/// Gfx_FontManager_AddFont's grid in pixels: the top-left of each letter's cell (f32 as the game).
pub fn cell_origins(n: usize, cw: f32, ch: f32, size: f32) -> Vec<(f32, f32)> {
    let du = cw / size;
    let dv = ch / size;
    let (mut u, mut v) = (0.0f32, 1.0f32);
    let mut out = vec![];
    for _ in 0..n {
        out.push((u * size, (1.0 - v) * size));
        u += du;
        if u + du > 1.0 {
            u = 0.0;
            v -= dv;
        }
    }
    out
}

struct Blob<'a> {
    d: &'a [u8],
    big: bool,
    is_ptr: Vec<bool>,
}

impl<'a> Blob<'a> {
    fn inb(&self, off: u32, n: u32) -> bool {
        (off as usize) <= self.d.len() && (n as usize) <= self.d.len() - off as usize
    }
    fn u32(&self, off: u32) -> u32 {
        if !self.inb(off, 4) {
            return 0;
        }
        let o = off as usize;
        let a = [self.d[o], self.d[o + 1], self.d[o + 2], self.d[o + 3]];
        if self.big { u32::from_be_bytes(a) } else { u32::from_le_bytes(a) }
    }
    fn u16(&self, off: u32) -> u16 {
        if !self.inb(off, 2) {
            return 0;
        }
        let a = [self.d[off as usize], self.d[off as usize + 1]];
        if self.big { u16::from_be_bytes(a) } else { u16::from_le_bytes(a) }
    }
    fn u8(&self, off: u32) -> u8 {
        if self.inb(off, 1) { self.d[off as usize] } else { 0 }
    }
    fn f32(&self, off: u32) -> f32 {
        f32::from_bits(self.u32(off))
    }
    fn ptr_slot(&self, off: u32) -> bool {
        off & 3 == 0 && self.inb(off, 4) && self.is_ptr[(off / 4) as usize]
    }
    fn to(&self, off: u32) -> Option<u32> {
        if !self.ptr_slot(off) {
            return None;
        }
        let v = self.u32(off);
        ((v as usize) < self.d.len()).then_some(v)
    }
    fn null_or_ptr(&self, off: u32, n: u32) -> bool {
        if !self.ptr_slot(off) {
            return self.u32(off) == 0;
        }
        self.to(off).is_some_and(|t| self.inb(t, n))
    }
    fn is_def(&self, d: u32) -> bool {
        if !matches!(self.u8(d + 1), 0 | 1 | 2 | 3 | 4 | 6 | 7 | 10 | 11 | 12) {
            return false;
        }
        let (ax, ay) = (self.f32(d + 0x30), self.f32(d + 0x34));
        if !((-1000.0..=1000.0).contains(&ax) && (-1000.0..=1000.0).contains(&ay)) {
            return false;
        }
        let ns = self.u32(d + 0x64);
        if ns > 64 || !self.null_or_ptr(d + 0x60, 0x10) || !self.null_or_ptr(d + 0x14, 4) || !self.null_or_ptr(d + 0x68, ns * 0xC) {
            return false;
        }
        !matches!(self.u8(d + 1), 6 | 7) || self.ptr_slot(d + 0x3C)
    }
    fn is_header(&self, h: u32) -> bool {
        if h & 3 != 0 || !self.inb(h, 0x40) {
            return false;
        }
        let n = self.u32(h + 0xC);
        let ev = self.u32(h + 0x14);
        if n == 0 || n > 200 || ev > 256 || self.u8(h + 0x3C) > 4 || self.u32(h + 0x34) > 64 || !self.ptr_slot(h + 0x10) {
            return false;
        }
        if !self.null_or_ptr(h + 0x18, ev * 0xC) || !self.null_or_ptr(h + 0x1C, 8) || !self.null_or_ptr(h + 0x30, 8) {
            return false;
        }
        let Some(objs) = self.to(h + 0x10) else { return false };
        self.inb(objs, n * DEF_SIZE) && (0..n).all(|i| self.is_def(objs + i * DEF_SIZE))
    }
    fn cstr(&self, off: u32) -> String {
        let mut s = vec![];
        let mut o = off as usize;
        while o < self.d.len() && self.d[o] != 0 && s.len() < 80 {
            s.push(self.d[o]);
            o += 1;
        }
        latin1(&s)
    }
    /// IfaceText.strings[0]
    fn text_of(&self, sub: u32) -> Option<String> {
        let slots = self.to(sub)?;
        let s = self.to(slots)?;
        Some(self.cstr(s))
    }
}

/// One font record as the data holds it.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct FontDef {
    pub cw: u8,
    pub ch: u8,
    pub case: u8,
    /// f32 bits
    pub spacing: u32,
    pub type_delay: i32,
    /// (char, x bits, y bits)
    pub chars: Vec<(u8, u32, u32)>,
}

impl FontDef {
    pub fn spacing(&self) -> f32 {
        f32::from_bits(self.spacing)
    }
    pub fn table_text(&self) -> String {
        self.chars
            .iter()
            .map(|&(c, x, y)| {
                let (x, y) = (f32::from_bits(x), f32::from_bits(y));
                if y == 0.0 { format!("{}:{}", latin1(&[c]), x) } else { format!("{}:{},{}", latin1(&[c]), x, y) }
            })
            .collect::<Vec<_>>()
            .join(" ")
    }
}

/// Where a page lives: (release, archive, pack, texture index).
pub type PageKey = (String, String, String, u32);

#[derive(Default)]
pub struct Use {
    /// (font def, user pack "archive:pack", header id, header offset, object kind, text)
    pub list: Vec<(FontDef, String, u16, u32, &'static str, String)>,
}

fn kind_name(t: u8) -> &'static str {
    match t {
        1 => "text",
        12 => "debug text",
        10 => "box",
        2 => "briefing",
        _ => "?",
    }
}

/// One spelling of an archive name for every release (the PS2 disc's are upper case).
fn canonical(name: &str) -> String {
    const KNOWN: [&str; 7] = ["HW_share", "HW_Cars", "HW_Items", "HW_IPack", "HW_brief", "HW_music", "mcp"];
    KNOWN.iter().find(|k| k.eq_ignore_ascii_case(name)).map(|k| k.to_string()).unwrap_or_else(|| name.to_lowercase())
}

fn find_archives(root: &Path) -> Vec<PathBuf> {
    let mut out = vec![];
    let mut stack = vec![root.to_path_buf()];
    while let Some(d) = stack.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                stack.push(p);
            } else if let Some(x) = p.extension().and_then(|x| x.to_str()) {
                if matches!(x.to_lowercase().as_str(), "xgc" | "xpc" | "xps") {
                    out.push(p);
                }
            }
        }
    }
    out.sort();
    out
}

fn font_def(b: &Blob, f: u32) -> Option<FontDef> {
    if !b.inb(f, 0x2C) {
        return None;
    }
    let n = b.u32(f + 0x20).min(0x200);
    let mut chars = vec![];
    if let Some(sp) = b.to(f + 0x24) {
        for i in 0..n {
            let e = sp + i * 0xC;
            if b.inb(e, 0xC) {
                chars.push((b.u8(e), b.u32(e + 4), b.u32(e + 8)));
            }
        }
    }
    Some(FontDef { cw: b.u8(f + 9), ch: b.u8(f + 0xA), case: b.u8(f + 0xB), spacing: b.u32(f + 0x28), type_delay: b.u32(f + 0x14) as i32, chars })
}

/// Every font use in one release's archives: page -> uses; plus the archives by name.
pub fn scan_release(rel_name: &str, root: &Path, notes: &mut Vec<String>) -> (BTreeMap<PageKey, Use>, HashMap<String, PathBuf>) {
    let mut pages: BTreeMap<PageKey, Use> = BTreeMap::new();
    let mut by_name = HashMap::new();
    for path in find_archives(root) {
        let Ok(bytes) = std::fs::read(&path) else { continue };
        let arc = match Archive::from_bytes(bytes, &path) {
            Ok(a) => a,
            Err(e) => {
                notes.push(format!("{rel_name}: {e}"));
                continue;
            }
        };
        if arc.release != rel_name {
            notes.push(format!("{}: a {} archive under the {rel_name} folder: skipped", path.display(), arc.release));
            continue;
        }
        let aname = canonical(&arc.name);
        by_name.insert(aname.clone(), path.clone());
        for ent in &arc.entries {
            let raw = arc.pack_bytes(ent);
            let Ok(pk) = RawPack::new(arc.release, ent.typ, raw, &ent.name) else { continue };
            let d = pk.section("data");
            if d.is_empty() {
                continue;
            }
            let mut is_ptr = vec![false; d.len() / 4 + 1];
            for p in pk.words("pointers") {
                if p & 3 == 0 && (p as usize) / 4 < is_ptr.len() {
                    is_ptr[(p / 4) as usize] = true;
                }
            }
            let refs = pk.words("ref_relocs");
            for c in refs.chunks_exact(2) {
                if c[1] >> 16 == 0 && c[0] & 3 == 0 && (c[0] as usize) / 4 < is_ptr.len() {
                    is_ptr[(c[0] / 4) as usize] = true;
                }
            }
            let tex: HashMap<u32, u32> = pk.words("texture_relocs").chunks_exact(2).map(|c| (c[0], c[1])).collect();
            let b = Blob { d, big: pk.big, is_ptr };
            let mut seen = BTreeSet::new();
            for w in 0..(d.len() / 4) as u32 {
                if let Some(t) = b.to(w * 4)
                    && !seen.contains(&t)
                    && b.is_header(t)
                {
                    seen.insert(t);
                }
            }
            let user = format!("{aname}:{}", ent.name);
            for &h in &seen {
                let hid = b.u16(h);
                let n = b.u32(h + 0xC);
                let objs = b.to(h + 0x10).unwrap();
                let mut fonts: Vec<(u32, u8, String)> = vec![];
                for i in 0..n {
                    let dd = objs + i * DEF_SIZE;
                    let ty = b.u8(dd + 1);
                    let Some(sub) = b.to(dd + 0x14) else { continue };
                    match ty {
                        1 | 12 => {
                            if let Some(f) = b.to(sub + 4) {
                                fonts.push((f, ty, b.text_of(sub).unwrap_or_default()));
                            }
                        }
                        10 => {
                            if let Some(f) = b.to(sub + 0x10) {
                                fonts.push((f, ty, String::new()));
                            }
                        }
                        2 => {
                            let cnt = b.u32(sub).min(0x400);
                            if let Some(ents) = b.to(sub + 4) {
                                for k in 0..cnt {
                                    let e = ents + k * 0x20;
                                    if let Some(chr) = b.to(e + 0x14)
                                        && let Some(f) = b.to(chr + 8)
                                    {
                                        let txt = b.to(e + 0x10).and_then(|s| b.to(s)).map(|s| b.cstr(s)).unwrap_or_default();
                                        fonts.push((f, ty, txt));
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                }
                for (f, ty, txt) in fonts {
                    let Some(def) = font_def(&b, f) else { continue };
                    let (sid, idx) = match tex.get(&f) {
                        Some(v) => (v >> 16, v & 0xFFFF),
                        None => {
                            notes.push(format!("{rel_name} {user}: font at 0x{f:X} has no texture relocation"));
                            continue;
                        }
                    };
                    let (oa, op) = if sid == 0 {
                        (aname.clone(), ent.name.clone())
                    } else {
                        match crate::pack::share(sid) {
                            Some((a, p)) => (a.to_string(), p.to_string()),
                            None => {
                                notes.push(format!("{rel_name} {user}: font texture of share {sid}"));
                                continue;
                            }
                        }
                    };
                    let key = (rel_name.to_string(), oa, op, idx);
                    pages.entry(key).or_default().list.push((def, user.clone(), hid, h, kind_name(ty), txt));
                }
            }
        }
    }
    (pages, by_name)
}

/// A page's pixels (level 0, RGBA) and format name.
pub struct Page {
    pub w: usize,
    pub h: usize,
    pub fmt: &'static str,
    pub px: Vec<u8>,
}

pub fn load_page(rel: &str, archive: &Path, pack: &str, idx: u32) -> Result<Page, String> {
    let bytes = std::fs::read(archive).map_err(|e| e.to_string())?;
    let arc = Archive::from_bytes(bytes, archive)?;
    let ent = arc.entries.iter().find(|e| e.name == pack).ok_or_else(|| format!("{pack} not in {}", archive.display()))?;
    let pk = RawPack::new(arc.release, ent.typ, arc.pack_bytes(ent), &ent.name)?;
    let texs = media::scan_textures(&pk)?;
    let t = texs.get(idx as usize).ok_or_else(|| format!("{pack}: no texture {idx}"))?;
    let &(o, n, lw, lh) = t.levels.first().ok_or("no levels")?;
    let clut = t.clut.map(|c| media::slice(pk.b, c, (c + 0x400) as i64));
    let px = media::decode_level(rel, t.fmt, media::slice(pk.b, o, (o + n) as i64), lw, lh, clut)?;
    // the pages are stored bottom row first (the font grid starts at t = 1.0): flip them upright
    let px: Vec<u8> = px.chunks_exact(4 * lw).rev().flatten().copied().collect();
    Ok(Page { w: lw, h: lh, fmt: media::tex_format(rel, t.fmt).map(|f| f.0).unwrap_or("?"), px })
}

/// Pixels classified: (class, metrics text). Over the opaque half of the page (alpha >= 128).
pub fn classify(p: &Page) -> (&'static str, String) {
    let mut lum = vec![];
    let mut chroma = vec![];
    let mut dark = 0usize;
    for q in p.px.chunks_exact(4) {
        if q[3] < 128 {
            continue;
        }
        let (r, g, b) = (q[0] as f32, q[1] as f32, q[2] as f32);
        let l = 0.299 * r + 0.587 * g + 0.114 * b;
        lum.push(l);
        chroma.push(r.max(g).max(b) - r.min(g).min(b));
        if l < 80.0 {
            dark += 1;
        }
    }
    if lum.is_empty() {
        return ("empty", String::new());
    }
    let n = lum.len() as f32;
    let mean = lum.iter().sum::<f32>() / n;
    let sd = (lum.iter().map(|l| (l - mean) * (l - mean)).sum::<f32>() / n).sqrt();
    chroma.sort_by(|a, b| a.total_cmp(b));
    let c95 = chroma[(chroma.len() * 95 / 100).min(chroma.len() - 1)];
    let darkf = dark as f32 / n;
    let class = if c95 >= 40.0 {
        "colour"
    } else if darkf > 0.05 {
        "outlined"
    } else if sd > 18.0 || mean < 200.0 {
        "shaded"
    } else {
        "flat"
    };
    (class, format!("lum {mean:.0}±{sd:.0}, chroma95 {c95:.0}, dark {:.0}%", darkf * 100.0))
}

/// Ink per letter cell: (blank letters, spare cells after the letters with ink / spare cells).
pub fn letter_ink(p: &Page, def: &FontDef) -> (String, (usize, usize)) {
    let size = p.w as f32;
    let ink = |x0: f32, y0: f32| -> usize {
        let mut n = 0;
        for y in (y0 as usize)..((y0 + def.ch as f32) as usize).min(p.h) {
            for x in (x0 as usize)..((x0 + def.cw as f32) as usize).min(p.w) {
                if p.px[4 * (y * p.w + x) + 3] > 32 {
                    n += 1;
                }
            }
        }
        n
    };
    let le = letters_e(def.case);
    let cells = cell_origins(le.len(), def.cw as f32, def.ch as f32, size);
    let blank: Vec<u8> = le.iter().zip(&cells).filter(|(_, c)| c.1 + def.ch as f32 > p.h as f32 + 0.5 || ink(c.0, c.1) == 0).map(|(l, _)| *l).collect();
    // the cells after the English letters (the French and German strings of case 0 put their accents
    // there; case 1 and 3 put them in place of English symbols, which this cannot tell)
    let all = cell_origins(4096, def.cw as f32, def.ch as f32, size);
    let mut acc = (0, 0);
    for c in all.iter().skip(le.len()).take_while(|c| c.1 + def.ch as f32 <= p.h as f32 + 0.5) {
        acc.1 += 1;
        if ink(c.0, c.1) > 0 {
            acc.0 += 1;
        }
    }
    (latin1(&blank), acc)
}

/// The mix's GlyphDistance (Port_Mix.c): the larger page box-filtered to the smaller's size, the
/// mean absolute difference of premultiplied RGB and alpha.
pub fn glyph_distance(a: &Page, b: &Page) -> Option<f32> {
    let (s, l) = if a.w <= b.w { (a, b) } else { (b, a) };
    if l.w % s.w != 0 || l.h % s.h != 0 || l.w / s.w != l.h / s.h {
        return None;
    }
    let k = l.w / s.w;
    let mut sum = 0f64;
    for y in 0..s.h {
        for x in 0..s.w {
            let mut acc = [0f64; 4];
            for yy in 0..k {
                for xx in 0..k {
                    let q = &l.px[4 * ((y * k + yy) * l.w + x * k + xx)..][..4];
                    let al = q[3] as f64 / 255.0;
                    for c in 0..3 {
                        acc[c] += q[c] as f64 * al;
                    }
                    acc[3] += q[3] as f64;
                }
            }
            let q = &s.px[4 * (y * s.w + x)..][..4];
            let al = q[3] as f64 / 255.0;
            for c in 0..4 {
                let v = if c < 3 { q[c] as f64 * al } else { q[3] as f64 };
                sum += (acc[c] / (k * k) as f64 - v).abs();
            }
        }
    }
    Some((sum / (s.w * s.h * 4) as f64) as f32)
}

pub fn write_png(path: &Path, p: &Page) -> Result<(), String> {
    if let Some(d) = path.parent() {
        std::fs::create_dir_all(d).map_err(|e| e.to_string())?;
    }
    let f = std::fs::File::create(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), p.w as u32, p.h as u32);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut wr = enc.write_header().map_err(|e| e.to_string())?;
    wr.write_image_data(&p.px).map_err(|e| e.to_string())
}

struct Row {
    key: PageKey,
    page: Page,
    hash: u64,
    class: &'static str,
    metrics: String,
    defs: BTreeMap<FontDef, (usize, String, (usize, usize))>,
    users: BTreeMap<String, BTreeSet<(u16, String)>>,
    texts: Vec<String>,
}

fn find(p: &mut Vec<usize>, x: usize) -> usize {
    let mut r = x;
    while p[r] != r {
        r = p[r];
    }
    let mut y = x;
    while p[y] != r {
        let n = p[y];
        p[y] = r;
        y = n;
    }
    r
}

/// `xdlrs fonts list [--gc DIR] [--pc DIR] [--ps2 DIR] [--dump DIR] [--report FILE.json] [--threshold 12]`
pub fn list(dirs: &[(String, PathBuf)], dump: Option<&Path>, json_out: Option<&Path>, threshold: f32) -> Result<String, String> {
    let mut notes = vec![];
    let mut rows: Vec<Row> = vec![];
    for (rel, dir) in dirs {
        let (pages, arcs) = scan_release(rel, dir, &mut notes);
        for (key, uses) in pages {
            let Some(ap) = arcs.get(&key.1) else {
                notes.push(format!("{rel}: archive {} not found", key.1));
                continue;
            };
            let page = match load_page(rel, ap, &key.2, key.3) {
                Ok(p) => p,
                Err(e) => {
                    notes.push(format!("{rel} {}:{}:{}: {e}", key.1, key.2, key.3));
                    continue;
                }
            };
            let hash = h64(&page.px);
            let (class, metrics) = classify(&page);
            let mut defs = BTreeMap::new();
            let mut users: BTreeMap<String, BTreeSet<(u16, String)>> = BTreeMap::new();
            let mut texts = vec![];
            for (def, user, hid, _h, kind, txt) in uses.list {
                let e = defs.entry(def.clone()).or_insert_with(|| {
                    let (blank, acc) = letter_ink(&page, &def);
                    (0, blank, acc)
                });
                e.0 += 1;
                users.entry(user).or_default().insert((hid, kind.to_string()));
                let t = txt.trim().to_string();
                if !t.is_empty() && !texts.contains(&t) && texts.len() < 6 {
                    texts.push(t);
                }
            }
            rows.push(Row { key, page, hash, class, metrics, defs, users, texts });
        }
    }
    // groups: same pixels, then same glyphs (GlyphDistance under the threshold)
    let mut parent: Vec<usize> = (0..rows.len()).collect();
    let mut by_hash: HashMap<u64, usize> = HashMap::new();
    for i in 0..rows.len() {
        if let Some(&j) = by_hash.get(&rows[i].hash) {
            let (a, b) = (find(&mut parent, i), find(&mut parent, j));
            parent[a] = b;
        } else {
            by_hash.insert(rows[i].hash, i);
        }
    }
    let reps: Vec<usize> = by_hash.values().copied().collect();
    let mut dist_note: HashMap<(usize, usize), f32> = HashMap::new();
    for (ai, &a) in reps.iter().enumerate() {
        for &b in &reps[ai + 1..] {
            if rows[a].page.w != rows[b].page.w || rows[a].page.h != rows[b].page.h {
                continue;
            }
            if let Some(d) = glyph_distance(&rows[a].page, &rows[b].page)
                && d < threshold
            {
                dist_note.insert((a.min(b), a.max(b)), d);
                let (x, y) = (find(&mut parent, a), find(&mut parent, b));
                parent[x] = y;
            }
        }
    }
    let mut groups: BTreeMap<usize, Vec<usize>> = BTreeMap::new();
    for i in 0..rows.len() {
        let r = find(&mut parent, i);
        groups.entry(r).or_default().push(i);
    }
    let mut glist: Vec<Vec<usize>> = groups.into_values().collect();
    glist.sort_by_key(|g| {
        let r = &rows[g[0]];
        (r.page.w, r.key.1.clone(), r.key.2.clone(), r.key.3)
    });
    let mut out = String::new();
    let mut jgroups = vec![];
    out += &format!("{} font pages ({} distinct pixel sets) in {} glyph groups\n", rows.len(), by_hash.len(), glist.len());
    let id_of = |r: &Row| format!("{}:{}:{}:{}", r.key.0, r.key.1, r.key.2, r.key.3);
    for (gi, g) in glist.iter().enumerate() {
        let mut members: Vec<usize> = g.clone();
        members.sort_by_key(|&i| (rows[i].key.0.clone(), rows[i].key.1.clone(), rows[i].key.2.clone(), rows[i].key.3));
        let r0 = &rows[members[0]];
        let mut rels: BTreeMap<&str, usize> = BTreeMap::new();
        for &i in &members {
            *rels.entry(rows[i].key.0.as_str()).or_default() += 1;
        }
        out += &format!(
            "\n== G{gi}: {}x{}, {} page(s) ({})\n",
            r0.page.w,
            r0.page.h,
            members.len(),
            rels.iter().map(|(r, n)| format!("{r} {n}")).collect::<Vec<_>>().join(", ")
        );
        // the group's distinct pixel sets, most used first
        let mut sets: Vec<(u64, Vec<usize>)> = vec![];
        for &i in &members {
            match sets.iter_mut().find(|s| s.0 == rows[i].hash) {
                Some(s) => s.1.push(i),
                None => sets.push((rows[i].hash, vec![i])),
            }
        }
        sets.sort_by_key(|s| std::cmp::Reverse(s.1.len()));
        let mut jm = vec![];
        for (hash, idx) in &sets {
            let r = &rows[idx[0]];
            let ids: Vec<String> = idx.iter().map(|&i| id_of(&rows[i])).collect();
            let at = if ids.len() > 5 { format!("{} ... +{} more", ids[..4].join(" "), ids.len() - 4) } else { ids.join(" ") };
            out += &format!("  #{hash:016x} {} {}x{} {} ({}); {} copies: {at}\n", r.page.fmt, r.page.w, r.page.h, r.class, r.metrics, ids.len());
            let mut defs: BTreeMap<FontDef, (usize, String, (usize, usize))> = BTreeMap::new();
            let mut users: BTreeMap<String, BTreeSet<(u16, String)>> = BTreeMap::new();
            let mut texts: Vec<String> = vec![];
            for &i in idx {
                for (d, v) in &rows[i].defs {
                    defs.entry(d.clone()).or_insert((0, v.1.clone(), v.2)).0 += v.0;
                }
                for (u, hs) in &rows[i].users {
                    users.entry(format!("{}:{u}", rows[i].key.0)).or_default().extend(hs.iter().cloned());
                }
                for t in &rows[i].texts {
                    if !texts.contains(t) && texts.len() < 8 {
                        texts.push(t.clone());
                    }
                }
            }
            let mut jd = vec![];
            for (def, (count, blank, acc)) in &defs {
                out += &format!(
                    "    cell {}x{} case {} spacing {} delay {} ({} records): {} letters{}{}{}\n",
                    def.cw,
                    def.ch,
                    def.case,
                    def.spacing(),
                    def.type_delay,
                    count,
                    letters_e(def.case).len(),
                    if blank.is_empty() { String::new() } else { format!(", blank cells: {blank}") },
                    if acc.1 > 0 { format!(", spare cells inked {}/{}", acc.0, acc.1) } else { String::new() },
                    if def.chars.is_empty() { String::new() } else { format!("\n      spacing table ({}): {}", def.chars.len(), def.table_text()) }
                );
                jd.push(json!({"cell": [def.cw, def.ch], "case": def.case, "letters": latin1(letters_e(def.case)),
                    "spacing": def.spacing(), "typeDelay": def.type_delay, "records": count, "blank": blank,
                    "spareCellsInked": [acc.0, acc.1],
                    "table": def.chars.iter().map(|&(c, x, y)| json!([latin1(&[c]), f32::from_bits(x), f32::from_bits(y)])).collect::<Vec<_>>()}));
            }
            let us: Vec<String> = users
                .iter()
                .map(|(u, hs)| format!("{u}[{}]", hs.iter().map(|(h, k)| format!("{h}{}", if k == "text" { String::new() } else { format!(" {k}") })).collect::<Vec<_>>().join(",")))
                .collect();
            let mut kinds: BTreeMap<String, usize> = BTreeMap::new();
            for hs in users.values() {
                for (_, k) in hs {
                    *kinds.entry(k.clone()).or_default() += 1;
                }
            }
            let shown = if us.len() > 6 { format!("{} ... ({} packs)", us[..6].join(" "), us.len()) } else { us.join(" ") };
            out += &format!(
                "    drawn by {} ({}): {shown}\n",
                us.len(),
                kinds.iter().map(|(k, n)| format!("{k} {n}")).collect::<Vec<_>>().join(", ")
            );
            if !texts.is_empty() {
                out += &format!("    texts: {}\n", texts.iter().map(|t| format!("{t:?}")).collect::<Vec<_>>().join(" "));
            }
            let png = format!("{}_{}__{}__t{}_{hash:016x}.png", r.key.0, r.key.1, r.key.2, r.key.3);
            if let Some(d) = dump {
                write_png(&d.join(&png), &r.page)?;
            }
            jm.push(json!({"hash": format!("{hash:016x}"), "format": r.page.fmt, "w": r.page.w, "h": r.page.h,
                "class": r.class, "metrics": r.metrics, "copies": ids, "defs": jd, "users": us, "texts": texts, "png": png}));
        }
        let mut dd = vec![];
        for (&(a, b), &d) in &dist_note {
            if g.contains(&a) {
                dd.push(format!("#{:016x} ({}) ~ #{:016x} ({}) {:.2}", rows[a].hash, id_of(&rows[a]), rows[b].hash, id_of(&rows[b]), d));
            }
        }
        dd.sort();
        for d in &dd {
            out += &format!("    glyph distance {d}\n");
        }
        jgroups.push(json!({"group": gi, "sets": jm, "distances": dd}));
    }
    if !notes.is_empty() {
        out += &format!("\nnotes ({}):\n", notes.len());
        for n in notes.iter().take(50) {
            out += &format!("  {n}\n");
        }
    }
    if let Some(j) = json_out {
        let v: Value = json!({"groups": jgroups, "notes": notes});
        std::fs::write(j, serde_json::to_string_pretty(&v).unwrap()).map_err(|e| e.to_string())?;
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn grid_wraps_like_addfont() {
        // 128 px page, 16 px cells: 8 per row
        let c = cell_origins(10, 16.0, 16.0, 128.0);
        assert_eq!(c[0], (0.0, 0.0));
        assert_eq!(c[7], (112.0, 0.0));
        assert_eq!(c[8], (0.0, 16.0));
    }

    #[test]
    fn letter_strings() {
        assert_eq!(letters_e(0).len(), 80);
        assert_eq!(letters_e(1).len(), 54);
        assert_eq!(letters_fg(0).0.len(), 95);
    }
}
